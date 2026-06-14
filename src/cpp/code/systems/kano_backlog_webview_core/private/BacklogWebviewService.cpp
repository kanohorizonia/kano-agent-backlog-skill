#include "KanoBacklog.BacklogWebviewService.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

import KanoBacklogWebview.Strings;

namespace kano::backlog::webview {

namespace {

std::string Trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string Unquote(const std::string& value) {
  const auto trimmed = Trim(value);
  if (trimmed.size() >= 2) {
    const char first = trimmed.front();
    const char last = trimmed.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      return trimmed.substr(1, trimmed.size() - 2);
    }
  }
  return trimmed;
}

std::string NormalizeNullToken(std::string value) {
  const auto lowered = text::ToLower(Trim(value));
  if (lowered == "null" || lowered == "none" || lowered == "~") {
    return "";
  }
  return value;
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::stringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::string ReadTextFile(const std::filesystem::path& path, bool& ok,
                         std::string& error) {
  ok = false;
  error.clear();
  std::ifstream input(path);
  if (!input.is_open()) {
    error = "Failed to open file";
    return "";
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  ok = true;
  return buffer.str();
}

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::vector<std::string> SplitCsv(const std::string& value) {
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string token;
  while (std::getline(stream, token, ',')) {
    auto trimmed = Trim(token);
    if (!trimmed.empty()) {
      result.push_back(trimmed);
    }
  }
  return result;
}

std::string JoinStrings(const std::vector<std::string>& values,
                        const std::string& separator) {
  std::ostringstream out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << separator;
    }
    out << values[i];
  }
  return out.str();
}

bool ContainsToken(const std::vector<std::string>& tokens,
                   const std::string& value) {
  if (tokens.empty()) {
    return true;
  }
  const auto lowered = text::ToLower(value);
  for (const auto& token : tokens) {
    if (text::ToLower(token) == lowered) {
      return true;
    }
  }
  return false;
}

void AppendUnique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty()) {
    return;
  }
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

size_t ParseSizeOrDefault(const std::string& value, const size_t fallback,
                          const size_t maximum) {
  if (Trim(value).empty()) {
    return fallback;
  }
  try {
    const auto parsed = static_cast<size_t>(std::stoull(value));
    return std::min(parsed, maximum);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

BacklogWebviewService::BacklogWebviewService(std::filesystem::path productsRootPath)
    : productsRoot(std::move(productsRootPath)) {}

std::filesystem::path BacklogWebviewService::GetProductsRoot() const {
  return productsRoot;
}

bool BacklogWebviewService::IsValidProductName(const std::string& product) const {
  static const std::regex productRegex("^[A-Za-z0-9._-]+$");
  return std::regex_match(product, productRegex);
}

std::filesystem::path BacklogWebviewService::ProductRoot(
    const std::string& product) const {
  return productsRoot / product;
}

std::filesystem::path BacklogWebviewService::ResolveProductsPathFromInput(
    const std::filesystem::path& inputPath) {
  if (inputPath.empty()) {
    return {};
  }

  const auto directProducts = inputPath / "products";
  if (std::filesystem::exists(directProducts) &&
      std::filesystem::is_directory(directProducts)) {
    return directProducts;
  }

  if (inputPath.filename() == "products" && std::filesystem::exists(inputPath) &&
      std::filesystem::is_directory(inputPath)) {
    return inputPath;
  }

  const auto nestedProducts = inputPath / "_kano" / "backlog" / "products";
  if (std::filesystem::exists(nestedProducts) &&
      std::filesystem::is_directory(nestedProducts)) {
    return nestedProducts;
  }

  return {};
}

std::filesystem::file_time_type BacklogWebviewService::ScanLatestMtime(
    const std::filesystem::path& productRoot) const {
  auto latest = std::filesystem::file_time_type::min();
  const auto backlogRoot = productsRoot.parent_path();
  const std::vector<std::filesystem::path> roots = {
      productRoot / "items", productRoot / "decisions", backlogRoot / "topics",
      backlogRoot / "worksets"};

  for (const auto& root : roots) {
    if (!std::filesystem::exists(root)) {
      continue;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto path = entry.path();
      const auto name = path.filename().string();
      const bool tracked = IsMarkdownItemFile(path) || name == "manifest.json";
      if (!tracked) {
        continue;
      }
      if (ShouldSkipPath(path)) {
        continue;
      }
      const auto mtime = entry.last_write_time();
      if (mtime > latest) {
        latest = mtime;
      }
    }
  }
  return latest;
}

bool BacklogWebviewService::ShouldLoad(const std::string& product,
                                       bool forceRefresh) {
  if (forceRefresh) {
    return true;
  }
  const auto it = cacheByProduct.find(product);
  if (it == cacheByProduct.end()) {
    return true;
  }

  const auto latest = ScanLatestMtime(ProductRoot(product));
  return latest > it->second.latestMtime;
}

bool BacklogWebviewService::IsMarkdownItemFile(const std::filesystem::path& path) {
  return path.extension() == ".md";
}

bool BacklogWebviewService::ShouldSkipPath(const std::filesystem::path& path) {
  const auto filename = path.filename().string();
  if (filename == "README.md") {
    return true;
  }
  if (filename.size() >= 9 &&
      filename.substr(filename.size() - 9) == ".index.md") {
    return true;
  }

  for (const auto& part : path) {
    if (part.string() == "_trash") {
      return true;
    }
  }

  return false;
}

std::vector<std::string> BacklogWebviewService::ResolveSelectedProducts(
    const std::vector<std::string>& requestedProducts) const {
  std::vector<std::string> selected;
  bool wantsAll = requestedProducts.empty();
  for (const auto& product : requestedProducts) {
    if (text::ToLower(product) == "all") {
      wantsAll = true;
      break;
    }
  }

  if (wantsAll) {
    if (!std::filesystem::exists(productsRoot)) {
      return selected;
    }
    for (const auto& entry : std::filesystem::directory_iterator(productsRoot)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto candidate = entry.path() / "items";
      if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
        selected.push_back(entry.path().filename().string());
      }
    }
  } else {
    for (const auto& product : requestedProducts) {
      if (!IsValidProductName(product)) {
        continue;
      }
      const auto root = ProductRoot(product);
      if (std::filesystem::exists(root / "items")) {
        selected.push_back(product);
      }
    }
  }

  std::sort(selected.begin(), selected.end());
  selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
  return selected;
}

std::unordered_map<std::string, std::vector<std::string>>
BacklogWebviewService::BuildTopicLookup() const {
  std::unordered_map<std::string, std::vector<std::string>> byRef;
  const auto topicsRoot = productsRoot.parent_path() / "topics";
  if (!std::filesystem::exists(topicsRoot)) {
    return byRef;
  }

  for (const auto& entry : std::filesystem::directory_iterator(topicsRoot)) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto manifestPath = entry.path() / "manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
      continue;
    }
    bool ok = false;
    std::string error;
    const auto manifest = ParseJsonFile(manifestPath, ok, error);
    if (!ok) {
      continue;
    }
    const auto topic = manifest.get("topic", entry.path().filename().string()).asString();
    if (!manifest.isMember("seed_items") || !manifest["seed_items"].isArray()) {
      continue;
    }
    for (const auto& seed : manifest["seed_items"]) {
      AppendUnique(byRef[seed.asString()], topic);
    }
  }
  return byRef;
}

std::string BacklogWebviewService::NormalizeTypeFromPath(
    const std::filesystem::path& itemPath, const std::string& declaredType) {
  if (!declaredType.empty()) {
    return declaredType;
  }

  const auto parent = itemPath.parent_path().parent_path().filename().string();
  if (parent == "story" || parent == "userstory") {
    return "UserStory";
  }
  if (parent == "epic") {
    return "Epic";
  }
  if (parent == "feature") {
    return "Feature";
  }
  if (parent == "task") {
    return "Task";
  }
  if (parent == "bug") {
    return "Bug";
  }
  return "Unknown";
}

std::unordered_map<std::string, std::string>
BacklogWebviewService::ParseFrontmatterMap(const std::string& content, bool& ok,
                                           std::string& error) {
  std::unordered_map<std::string, std::string> result;
  ok = false;
  error.clear();

  const auto lines = SplitLines(content);
  if (lines.empty() || Trim(lines.front()) != "---") {
    error = "Missing frontmatter start marker";
    return result;
  }

  bool foundEnd = false;
  std::string currentKey;
  for (size_t i = 1; i < lines.size(); ++i) {
    const auto raw = lines[i];
    const auto trimmed = Trim(raw);
    if (trimmed == "---") {
      foundEnd = true;
      break;
    }
    if (trimmed.empty()) {
      continue;
    }

    const auto keyPos = raw.find(':');
    const bool likelyKeyLine = keyPos != std::string::npos &&
                               !raw.empty() && raw[0] != ' ' && raw[0] != '\t';
    if (likelyKeyLine) {
      currentKey = Trim(raw.substr(0, keyPos));
      auto value = Trim(raw.substr(keyPos + 1));
      result[currentKey] = NormalizeNullToken(Unquote(value));
      continue;
    }

    if (!currentKey.empty() && (StartsWith(raw, "  -") || StartsWith(raw, "- "))) {
      auto itemValue = Trim(raw);
      if (StartsWith(itemValue, "- ")) {
        itemValue = Trim(itemValue.substr(2));
      } else if (StartsWith(itemValue, "-")) {
        itemValue = Trim(itemValue.substr(1));
      } else if (StartsWith(itemValue, "  -")) {
        itemValue = Trim(itemValue.substr(3));
      }
      if (!itemValue.empty()) {
        if (!result[currentKey].empty()) {
          result[currentKey] += ",";
        }
        result[currentKey] += NormalizeNullToken(Unquote(itemValue));
      }
    }
  }

  if (!foundEnd) {
    error = "Missing frontmatter end marker";
    return result;
  }

  ok = true;
  return result;
}

ItemRecord BacklogWebviewService::ParseItem(const std::filesystem::path& itemPath,
                                            const std::filesystem::path& productRoot) {
  ItemRecord item;
  item.valid = false;
  item.sourceKind = "Item";
  item.relativePath =
      std::filesystem::relative(itemPath, productRoot).generic_string();

  bool fileOk = false;
  std::string readError;
  const auto content = ReadTextFile(itemPath, fileOk, readError);
  if (!fileOk) {
    item.parseError = readError;
    return item;
  }
  item.rawContent = content;

  bool ok = false;
  std::string error;
  auto map = ParseFrontmatterMap(content, ok, error);
  if (!ok) {
    item.parseError = error;
    return item;
  }

  item.id = map["id"];
  item.uid = map["uid"];
  item.type = NormalizeTypeFromPath(itemPath, map["type"]);
  item.title = map["title"];
  item.state = map["state"];
  item.parent = map["parent"];
  item.created = map["created"];
  item.updated = map["updated"];

  if (item.id.empty()) {
    item.parseError = "Missing id";
    return item;
  }

  if (text::ToLower(item.id) == "null") {
    item.parseError = "Invalid id";
    return item;
  }

  if (item.title.empty()) {
    item.title = "(untitled)";
  }

  if (item.state.empty()) {
    item.state = "Proposed";
  }

  item.valid = true;
  return item;
}

ItemRecord BacklogWebviewService::ParseDecision(
    const std::filesystem::path& decisionPath,
    const std::filesystem::path& productRoot) {
  ItemRecord item;
  item.valid = false;
  item.sourceKind = "Decision";
  item.type = "ADR";
  item.relativePath =
      std::filesystem::relative(decisionPath, productRoot).generic_string();

  bool fileOk = false;
  std::string readError;
  const auto content = ReadTextFile(decisionPath, fileOk, readError);
  if (!fileOk) {
    item.parseError = readError;
    return item;
  }
  item.rawContent = content;

  bool ok = false;
  std::string error;
  auto map = ParseFrontmatterMap(content, ok, error);
  if (!ok) {
    item.parseError = error;
    return item;
  }

  item.id = map["id"];
  if (item.id.empty()) {
    item.id = decisionPath.stem().string();
  }
  item.title = map["title"];
  if (item.title.empty()) {
    item.title = decisionPath.stem().string();
  }
  item.state = map["status"];
  if (item.state.empty()) {
    item.state = "Proposed";
  }
  item.created = map["date"];
  item.updated = map["date"];
  item.valid = true;
  return item;
}

Json::Value BacklogWebviewService::ParseJsonFile(const std::filesystem::path& jsonPath,
                                                 bool& ok,
                                                 std::string& error) {
  ok = false;
  error.clear();
  bool readOk = false;
  std::string readError;
  const auto text = ReadTextFile(jsonPath, readOk, readError);
  if (!readOk) {
    error = readError;
    return Json::Value(Json::nullValue);
  }

  Json::CharReaderBuilder builder;
  std::string parseErrors;
  std::istringstream input(text);
  Json::Value root;
  if (!Json::parseFromStream(builder, input, &root, &parseErrors)) {
    error = parseErrors;
    return Json::Value(Json::nullValue);
  }
  ok = true;
  return root;
}

ItemRecord BacklogWebviewService::ParseTopicManifest(
    const std::filesystem::path& topicManifestPath,
    const std::filesystem::path& backlogRoot) {
  ItemRecord item;
  item.valid = false;
  item.sourceKind = "Topic";
  item.type = "Topic";
  item.relativePath =
      std::filesystem::relative(topicManifestPath, backlogRoot).generic_string();

  bool ok = false;
  std::string error;
  const auto manifest = ParseJsonFile(topicManifestPath, ok, error);
  if (!ok) {
    item.parseError = error;
    return item;
  }

  const auto slug = manifest.get("topic", topicManifestPath.parent_path().filename().string())
                        .asString();
  item.id = "TOPIC-" + slug;
  item.title = slug;
  item.state = manifest.get("status", "open").asString();
  item.created = manifest.get("created_at", "").asString();
  item.updated = manifest.get("updated_at", "").asString();

  bool readOk = false;
  std::string readError;
  const auto briefPath = topicManifestPath.parent_path() / "brief.md";
  if (std::filesystem::exists(briefPath)) {
    item.rawContent = ReadTextFile(briefPath, readOk, readError);
  }
  if (!readOk) {
    item.rawContent = ReadTextFile(topicManifestPath, readOk, readError);
  }
  item.valid = true;
  return item;
}

ItemRecord BacklogWebviewService::ParseWorksetManifest(
    const std::filesystem::path& worksetManifestPath,
    const std::filesystem::path& backlogRoot) {
  ItemRecord item;
  item.valid = false;
  item.sourceKind = "Workset";
  item.type = "Workset";
  item.relativePath =
      std::filesystem::relative(worksetManifestPath, backlogRoot).generic_string();

  bool ok = false;
  std::string error;
  const auto manifest = ParseJsonFile(worksetManifestPath, ok, error);
  if (!ok) {
    item.parseError = error;
    return item;
  }

  const auto name = manifest.get("name", worksetManifestPath.parent_path().filename().string())
                        .asString();
  item.id = "WORKSET-" + name;
  item.title = name;
  item.state = manifest.get("status", "open").asString();
  item.created = manifest.get("created_at", "").asString();
  item.updated = manifest.get("updated_at", "").asString();

  bool readOk = false;
  std::string readError;
  item.rawContent = ReadTextFile(worksetManifestPath, readOk, readError);
  item.valid = true;
  return item;
}

Json::Value BacklogWebviewService::ItemToJson(const ItemRecord& item,
                                              const bool includeContent) {
  Json::Value value(Json::objectValue);
  value["product"] = item.product;
  value["id"] = item.id;
  value["uid"] = item.uid;
  value["type"] = item.type;
  value["source_kind"] = item.sourceKind;
  value["title"] = item.title;
  value["state"] = item.state;
  value["parent"] = item.parent;
  value["topic"] = item.topic.empty() ? Json::Value(Json::nullValue)
                                      : Json::Value(item.topic);
  value["created"] = item.created;
  value["updated"] = item.updated;
  value["path"] = item.relativePath;
  value["valid"] = item.valid;
  if (!item.parseError.empty()) {
    value["parse_error"] = item.parseError;
  }
  if (includeContent) {
    value["content"] = item.rawContent;
  }
  return value;
}

std::string BacklogWebviewService::ToIsoString(
    const std::filesystem::file_time_type& value) {
  if (value == std::filesystem::file_time_type::min()) {
    return "";
  }
  const auto nowFile = std::filesystem::file_time_type::clock::now();
  const auto nowSys = std::chrono::system_clock::now();
  const auto converted = nowSys + (value - nowFile);
  const std::time_t time = std::chrono::system_clock::to_time_t(converted);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  char buffer[32] = {0};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

void BacklogWebviewService::LoadProduct(const std::string& product,
                                        bool forceRefresh) {
  if (!ShouldLoad(product, forceRefresh)) {
    return;
  }

  ProductCache productCache;
  const auto productRoot = ProductRoot(product);
  const auto itemsRoot = productRoot / "items";
  const auto topicLookup = BuildTopicLookup();
  productCache.latestMtime = ScanLatestMtime(productRoot);

  if (!std::filesystem::exists(itemsRoot)) {
    productCache.warnings.push_back("Missing items directory");
    cacheByProduct[product] = std::move(productCache);
    return;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(itemsRoot)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (!IsMarkdownItemFile(entry.path()) || ShouldSkipPath(entry.path())) {
      continue;
    }

    auto item = ParseItem(entry.path(), productRoot);
    item.product = product;
    const auto uidTopicIt = topicLookup.find(item.uid);
    if (uidTopicIt != topicLookup.end()) {
      item.topic = JoinStrings(uidTopicIt->second, ", ");
    } else {
      const auto idTopicIt = topicLookup.find(item.id);
      if (idTopicIt != topicLookup.end()) {
        item.topic = JoinStrings(idTopicIt->second, ", ");
      }
    }
    const auto index = productCache.allItems.size();
    if (!item.valid) {
      productCache.warnings.push_back("Invalid item: " + item.relativePath +
                                      " - " + item.parseError);
    }
    productCache.allItems.push_back(std::move(item));
  }

  const auto decisionsRoot = productRoot / "decisions";
  if (std::filesystem::exists(decisionsRoot)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(decisionsRoot)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      if (!IsMarkdownItemFile(entry.path()) || ShouldSkipPath(entry.path())) {
        continue;
      }
      auto item = ParseDecision(entry.path(), productRoot);
      item.product = product;
      if (!item.valid) {
        productCache.warnings.push_back("Invalid decision: " + item.relativePath +
                                        " - " + item.parseError);
      }
      productCache.allItems.push_back(std::move(item));
    }
  }

  const auto backlogRoot = productsRoot.parent_path();
  const auto topicsRoot = backlogRoot / "topics";
  if (std::filesystem::exists(topicsRoot)) {
    for (const auto& entry : std::filesystem::directory_iterator(topicsRoot)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto manifestPath = entry.path() / "manifest.json";
      if (!std::filesystem::exists(manifestPath)) {
        continue;
      }
      auto item = ParseTopicManifest(manifestPath, backlogRoot);
      item.product = product;
      item.topic = item.title;
      if (!item.valid) {
        productCache.warnings.push_back("Invalid topic: " + item.relativePath +
                                        " - " + item.parseError);
      }
      productCache.allItems.push_back(std::move(item));
    }
  }

  const auto worksetsRoot = backlogRoot / "worksets";
  if (std::filesystem::exists(worksetsRoot)) {
    for (const auto& entry : std::filesystem::directory_iterator(worksetsRoot)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto manifestPath = entry.path() / "manifest.json";
      if (!std::filesystem::exists(manifestPath)) {
        continue;
      }
      auto item = ParseWorksetManifest(manifestPath, backlogRoot);
      item.product = product;
      if (!item.valid) {
        productCache.warnings.push_back("Invalid workset: " + item.relativePath +
                                        " - " + item.parseError);
      }
      productCache.allItems.push_back(std::move(item));
    }
  }

  for (size_t i = 0; i < productCache.allItems.size(); ++i) {
    const auto& item = productCache.allItems[i];
    if (!item.id.empty()) {
      productCache.idIndexes[item.id].push_back(i);
    }
  }

  for (const auto& [id, indexes] : productCache.idIndexes) {
    if (indexes.empty()) {
      continue;
    }
    auto primary = indexes.front();
    for (const auto index : indexes) {
      const auto& candidate = productCache.allItems[index];
      const auto& current = productCache.allItems[primary];
      if (candidate.updated > current.updated) {
        primary = index;
      }
      if (candidate.updated == current.updated && candidate.relativePath < current.relativePath) {
        primary = index;
      }
    }
    productCache.primaryById[id] = primary;
  }

  cacheByProduct[product] = std::move(productCache);
}

Json::Value BacklogWebviewService::ListProducts() {
  Json::Value data(Json::arrayValue);
  if (!std::filesystem::exists(productsRoot)) {
    return data;
  }

  std::vector<std::string> products;
  for (const auto& entry : std::filesystem::directory_iterator(productsRoot)) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto candidate = entry.path() / "items";
    if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
      products.push_back(entry.path().filename().string());
    }
  }
  std::sort(products.begin(), products.end());
  for (const auto& product : products) {
    data.append(product);
  }
  return data;
}

Json::Value BacklogWebviewService::ListItems(const std::string& product,
                                             bool forceRefresh) {
  if (product.empty() || text::ToLower(product) == "all" ||
      product.find(',') != std::string::npos) {
    ItemQueryOptions options;
    options.products = SplitCsv(product);
    options.forceRefresh = forceRefresh;
    return QueryItems(options);
  }

  Json::Value response(Json::objectValue);
  response["items"] = Json::arrayValue;
  response["warnings"] = Json::arrayValue;
  if (!IsValidProductName(product)) {
    response["error"] = "Invalid product name";
    return response;
  }

  LoadProduct(product, forceRefresh);
  const auto cacheIt = cacheByProduct.find(product);
  if (cacheIt == cacheByProduct.end()) {
    response["error"] = "Product not found";
    return response;
  }

  const auto& productCache = cacheIt->second;
  for (const auto& warning : productCache.warnings) {
    response["warnings"].append(warning);
  }

  for (const auto& [id, primaryIndex] : productCache.primaryById) {
    const auto& item = productCache.allItems[primaryIndex];
    auto value = ItemToJson(item);
    const auto duplicateIt = productCache.idIndexes.find(id);
    if (duplicateIt != productCache.idIndexes.end()) {
      value["duplicate_count"] =
          static_cast<Json::UInt64>(duplicateIt->second.size());
    }
    response["items"].append(value);
  }

  response["cached_at"] = ToIsoString(productCache.latestMtime);
  response["total"] = static_cast<Json::UInt64>(response["items"].size());
  response["limit"] = static_cast<Json::UInt64>(response["items"].size());
  response["offset"] = 0U;
  return response;
}

Json::Value BacklogWebviewService::QueryItems(const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["items"] = Json::arrayValue;
  response["products"] = Json::arrayValue;
  response["warnings"] = Json::arrayValue;

  const auto selectedProducts = ResolveSelectedProducts(options.products);
  for (const auto& product : selectedProducts) {
    response["products"].append(product);
  }

  const size_t maxLimit = 1000;
  const size_t limit = std::min(options.limit == 0 ? size_t{200} : options.limit, maxLimit);
  const size_t offset = options.offset;
  const auto textFilter = Trim(options.text);

  std::vector<Json::Value> matches;
  std::unordered_set<std::string> emittedSharedItems;
  for (const auto& product : selectedProducts) {
    LoadProduct(product, options.forceRefresh);
    const auto cacheIt = cacheByProduct.find(product);
    if (cacheIt == cacheByProduct.end()) {
      response["warnings"].append("Product not found: " + product);
      continue;
    }

    const auto& productCache = cacheIt->second;
    for (const auto& warning : productCache.warnings) {
      response["warnings"].append(product + ": " + warning);
    }

    for (const auto& [id, primaryIndex] : productCache.primaryById) {
      const auto& item = productCache.allItems[primaryIndex];
      if (!ContainsToken(options.types, item.type)) {
        continue;
      }
      if (!ContainsToken(options.states, item.state)) {
        continue;
      }
      if (item.sourceKind == "Topic" || item.sourceKind == "Workset") {
        const auto key = item.sourceKind + "\n" + item.id;
        if (!emittedSharedItems.insert(key).second) {
          continue;
        }
      }
      if (!textFilter.empty() &&
          !text::ContainsCaseInsensitive(item.id, textFilter) &&
          !text::ContainsCaseInsensitive(item.title, textFilter) &&
          !text::ContainsCaseInsensitive(item.rawContent, textFilter) &&
          !text::ContainsCaseInsensitive(item.product, textFilter) &&
          !text::ContainsCaseInsensitive(item.topic, textFilter)) {
        continue;
      }

      auto value = ItemToJson(item);
      const auto duplicateIt = productCache.idIndexes.find(id);
      if (duplicateIt != productCache.idIndexes.end()) {
        value["duplicate_count"] =
            static_cast<Json::UInt64>(duplicateIt->second.size());
      }
      matches.push_back(value);
    }
  }

  std::sort(matches.begin(), matches.end(), [](const Json::Value& left,
                                               const Json::Value& right) {
    const auto lp = left["product"].asString();
    const auto rp = right["product"].asString();
    if (lp != rp) {
      return lp < rp;
    }
    return left["id"].asString() < right["id"].asString();
  });

  response["total"] = static_cast<Json::UInt64>(matches.size());
  response["limit"] = static_cast<Json::UInt64>(limit);
  response["offset"] = static_cast<Json::UInt64>(offset);
  const auto end = std::min(matches.size(), offset + limit);
  if (offset < matches.size()) {
    for (size_t i = offset; i < end; ++i) {
      response["items"].append(matches[i]);
    }
  }
  return response;
}

Json::Value BacklogWebviewService::GetItem(const std::string& product,
                                           const std::string& id,
                                           bool forceRefresh) {
  if (product.empty() || text::ToLower(product) == "all" ||
      product.find(',') != std::string::npos) {
    const auto products = ResolveSelectedProducts(SplitCsv(product));
    for (const auto& selectedProduct : products) {
      auto data = GetItem(selectedProduct, id, forceRefresh);
      if (!data.isMember("error")) {
        return data;
      }
    }
    Json::Value response(Json::objectValue);
    response["error"] = "Item not found";
    return response;
  }

  Json::Value response(Json::objectValue);
  if (!IsValidProductName(product)) {
    response["error"] = "Invalid product name";
    return response;
  }

  LoadProduct(product, forceRefresh);
  const auto cacheIt = cacheByProduct.find(product);
  if (cacheIt == cacheByProduct.end()) {
    response["error"] = "Product not found";
    return response;
  }

  const auto& productCache = cacheIt->second;
  const auto primaryIt = productCache.primaryById.find(id);
  if (primaryIt == productCache.primaryById.end()) {
    response["error"] = "Item not found";
    return response;
  }

  response["item"] = ItemToJson(productCache.allItems[primaryIt->second], true);
  response["duplicates"] = Json::arrayValue;
  const auto allIt = productCache.idIndexes.find(id);
  if (allIt != productCache.idIndexes.end()) {
    for (const auto index : allIt->second) {
      response["duplicates"].append(ItemToJson(productCache.allItems[index]));
    }
  }
  return response;
}

Json::Value BacklogWebviewService::BuildTree(const std::string& product,
                                             bool forceRefresh) {
  ItemQueryOptions options;
  options.products = SplitCsv(product);
  options.forceRefresh = forceRefresh;
  return BuildTree(options);
}

Json::Value BacklogWebviewService::BuildTree(const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["roots"] = Json::arrayValue;
  response["warnings"] = Json::arrayValue;

  auto itemsResponse = QueryItems(options);
  if (itemsResponse.isMember("error")) {
    response["error"] = itemsResponse["error"];
    return response;
  }

  auto makeKey = [](const Json::Value& item) {
    return item["product"].asString() + "\n" + item["id"].asString();
  };
  auto makeParentKey = [](const Json::Value& item) {
    return item["product"].asString() + "\n" + item["parent"].asString();
  };

  std::unordered_map<std::string, Json::Value> byId;
  std::unordered_map<std::string, std::vector<std::string>> childIds;
  std::set<std::string> allIds;

  for (const auto& item : itemsResponse["items"]) {
    const auto type = item["type"].asString();
    if (type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Theme") {
      continue;
    }
    const auto id = item["id"].asString();
    if (id.empty()) {
      continue;
    }
    const auto key = makeKey(item);
    allIds.insert(key);
    Json::Value node(Json::objectValue);
    node["id"] = id;
    node["product"] = item["product"].asString();
    node["title"] = item["title"].asString();
    node["type"] = item["type"].asString();
    node["state"] = item["state"].asString();
    node["parent"] = item["parent"].asString();
    node["topic"] = item["topic"];
    node["children"] = Json::arrayValue;
    byId[key] = node;
  }

  for (const auto& item : itemsResponse["items"]) {
    const auto type = item["type"].asString();
    if (type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Theme") {
      continue;
    }
    const auto id = item["id"].asString();
    if (id.empty()) {
      continue;
    }
    const auto parent = item["parent"].asString();
    if (!parent.empty()) {
      childIds[makeParentKey(item)].push_back(makeKey(item));
      if (!allIds.count(makeParentKey(item))) {
        response["warnings"].append("Orphan parent missing for item " + id +
                                     ": " + parent);
      }
    }
  }

  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::function<void(Json::Value&, const std::string&)> attachChildren;
  attachChildren = [&](Json::Value& node, const std::string& nodeKey) {
    visiting.insert(nodeKey);
    visited.insert(nodeKey);

    for (const auto& childKey : childIds[nodeKey]) {
      if (!byId.count(childKey)) {
        continue;
      }
      if (visiting.count(childKey)) {
        response["warnings"].append("Cycle detected at " + childKey);
        continue;
      }
      auto child = byId[childKey];
      attachChildren(child, childKey);
      node["children"].append(child);
    }

    visiting.erase(nodeKey);
  };

  for (const auto& item : itemsResponse["items"]) {
    const auto type = item["type"].asString();
    if (type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Theme") {
      continue;
    }
    const auto id = item["id"].asString();
    if (id.empty()) {
      continue;
    }
    const auto parent = item["parent"].asString();
    const auto key = makeKey(item);
    const bool isRoot = parent.empty() || !allIds.count(makeParentKey(item));
    if (!isRoot || visited.count(key)) {
      continue;
    }
    auto root = byId[key];
    attachChildren(root, key);
    response["roots"].append(root);
  }

  for (const auto& warning : itemsResponse["warnings"]) {
    response["warnings"].append(warning.asString());
  }

  return response;
}

Json::Value BacklogWebviewService::BuildKanban(const std::string& product,
                                               bool forceRefresh) {
  ItemQueryOptions options;
  options.products = SplitCsv(product);
  options.forceRefresh = forceRefresh;
  return BuildKanban(options);
}

Json::Value BacklogWebviewService::BuildKanban(const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["lanes"] = Json::objectValue;
  response["lanes"]["Backlog"] = Json::arrayValue;
  response["lanes"]["Doing"] = Json::arrayValue;
  response["lanes"]["Blocked"] = Json::arrayValue;
  response["lanes"]["Review"] = Json::arrayValue;
  response["lanes"]["Done"] = Json::arrayValue;
  response["warnings"] = Json::arrayValue;

  auto itemsResponse = QueryItems(options);
  if (itemsResponse.isMember("error")) {
    response["error"] = itemsResponse["error"];
    return response;
  }

  for (const auto& item : itemsResponse["items"]) {
    const auto state = item["state"].asString();
    std::string lane = "Backlog";
    if (state == "InProgress") {
      lane = "Doing";
    } else if (state == "Blocked" || text::ToLower(state) == "blocked") {
      lane = "Blocked";
    } else if (state == "Review" || text::ToLower(state) == "review") {
      lane = "Review";
    } else if (state == "Done" || text::ToLower(state) == "done" ||
               text::ToLower(state) == "closed") {
      lane = "Done";
    } else if (text::ToLower(state) == "inprogress" ||
               text::ToLower(state) == "active") {
      lane = "Doing";
    }

    response["lanes"][lane].append(item);
  }

  for (const auto& warning : itemsResponse["warnings"]) {
    response["warnings"].append(warning.asString());
  }
  return response;
}

Json::Value BacklogWebviewService::Refresh(const std::string& product) {
  Json::Value response(Json::objectValue);
  if (product.empty() || text::ToLower(product) == "all") {
    cacheByProduct.clear();
    response["refreshed"] = "all";
    return response;
  }
  if (!IsValidProductName(product)) {
    response["error"] = "Invalid product name";
    return response;
  }
  cacheByProduct.erase(product);
  response["refreshed"] = product;
  return response;
}

Json::Value BacklogWebviewService::GetWorkspaceInfo() const {
  Json::Value response(Json::objectValue);
  response["products_root"] = productsRoot.generic_string();
  response["workspace_root"] = productsRoot.parent_path().generic_string();
  return response;
}

Json::Value BacklogWebviewService::SwitchWorkspace(const std::string& inputPath) {
  Json::Value response(Json::objectValue);
  const auto trimmed = Trim(inputPath);
  if (trimmed.empty()) {
    response["error"] = "Missing workspace path";
    return response;
  }

  std::error_code ec;
  std::filesystem::path requested(trimmed);
  auto resolved = ResolveProductsPathFromInput(requested);
  if (resolved.empty()) {
    response["error"] =
        "Path does not contain a backlog products directory (expected products/ or _kano/backlog/products/)";
    return response;
  }

  const auto canonical = std::filesystem::weakly_canonical(resolved, ec);
  if (!ec && !canonical.empty()) {
    resolved = canonical;
  }

  productsRoot = resolved;
  cacheByProduct.clear();
  response["products_root"] = productsRoot.generic_string();
  response["workspace_root"] = productsRoot.parent_path().generic_string();
  response["switched"] = true;
  return response;
}

void RegisterBacklogWebviewRoutes(
    BacklogWebviewService& service,
    const std::function<void(const drogon::HttpRequestPtr&, Json::Value&)>&
        appendCommonMeta) {
  using namespace drogon;
  const auto metaAppender = appendCommonMeta;
  auto queryOptionsFromRequest = [](const HttpRequestPtr& request) {
    ItemQueryOptions options;
    const auto products = request->getParameter("products");
    const auto product = request->getParameter("product");
    if (!products.empty()) {
      options.products = SplitCsv(products);
    } else if (!product.empty()) {
      options.products = SplitCsv(product);
    }
    options.text = request->getParameter("q");
    options.states = SplitCsv(request->getParameter("state"));
    options.types = SplitCsv(request->getParameter("type"));
    options.limit =
        ParseSizeOrDefault(request->getParameter("limit"), 200, 1000);
    options.offset =
        ParseSizeOrDefault(request->getParameter("offset"), 0, 1000000);
    return options;
  };

  app().registerHandler(
      "/healthz",
      [metaAppender](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value body(Json::objectValue);
        body["ok"] = true;
        body["status"] = "healthy";
        metaAppender(request, body);
        callback(HttpResponse::newHttpJsonResponse(body));
      },
      {Get});

  app().registerHandler(
      "/api/workspace/info",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.GetWorkspaceInfo();
        Json::Value body(Json::objectValue);
        body["ok"] = true;
        body["data"] = data;
        metaAppender(request, body);
        callback(HttpResponse::newHttpJsonResponse(body));
      },
      {Get});

  app().registerHandler(
      "/api/workspace/switch",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto path = request->getParameter("path");
        auto data = service.SwitchWorkspace(path);
        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);
        auto response = HttpResponse::newHttpJsonResponse(body);
        if (!body["ok"].asBool()) {
          response->setStatusCode(k400BadRequest);
        }
        callback(response);
      },
      {Get});

  app().registerHandler(
      "/api/products",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value body(Json::objectValue);
        body["ok"] = true;
        body["data"] = service.ListProducts();
        metaAppender(request, body);
        callback(HttpResponse::newHttpJsonResponse(body));
      },
      {Get});

  app().registerHandler(
      "/api/refresh",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto product = request->getParameter("product");
        Json::Value data = service.Refresh(product);
        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);
        callback(HttpResponse::newHttpJsonResponse(body));
      },
      {Get});

  app().registerHandler(
      "/api/items",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.QueryItems(queryOptionsFromRequest(request));

        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);

        auto response = HttpResponse::newHttpJsonResponse(body);
        if (!body["ok"].asBool()) {
          response->setStatusCode(k400BadRequest);
        }
        callback(response);
      },
      {Get});

  app().registerHandler(
      "/api/items/{1}",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback,
          const std::string& itemId) {
        auto product = request->getParameter("product");
        if (product.empty()) {
          product = request->getParameter("products");
        }
        if (product.empty()) {
          product = "all";
        }
        auto data = service.GetItem(product, itemId);
        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);

        auto response = HttpResponse::newHttpJsonResponse(body);
        if (!body["ok"].asBool()) {
          response->setStatusCode(k404NotFound);
        }
        callback(response);
      },
      {Get});

  app().registerHandler(
      "/api/tree",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildTree(queryOptionsFromRequest(request));
        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);

        auto response = HttpResponse::newHttpJsonResponse(body);
        if (!body["ok"].asBool()) {
          response->setStatusCode(k400BadRequest);
        }
        callback(response);
      },
      {Get});

  app().registerHandler(
      "/api/kanban",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildKanban(queryOptionsFromRequest(request));
        Json::Value body(Json::objectValue);
        body["ok"] = !data.isMember("error");
        body["data"] = data;
        metaAppender(request, body);

        auto response = HttpResponse::newHttpJsonResponse(body);
        if (!body["ok"].asBool()) {
          response->setStatusCode(k400BadRequest);
        }
        callback(response);
      },
      {Get});
}

}  // namespace kano::backlog::webview
