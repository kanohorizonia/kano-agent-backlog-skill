#include "KanoBacklog.BacklogWebviewService.hpp"

#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/state/state_machine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

import KanoBacklogWebview.Strings;

namespace kano::backlog::webview {

namespace {

constexpr const char* kDefaultReviewActorAlias = "human-reviewer";

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

std::string HtmlEscape(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        result += "&amp;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      case '"':
        result += "&quot;";
        break;
      case '\'':
        result += "&#39;";
        break;
      default:
        result.push_back(ch);
        break;
    }
  }
  return result;
}

std::string PillClass(const std::string& value);

std::string RenderErrorPartial(const std::string& message) {
  return "<div class=\"card\" role=\"alert\"><strong>Unable to load partial</strong><div class=\"muted\">" +
         HtmlEscape(message) + "</div></div>";
}

std::string ItemMetaText(const Json::Value& item) {
  std::vector<std::string> parts;
  for (const auto& key : {"product", "type", "state", "source_kind"}) {
    if (item.isMember(key) && !item[key].asString().empty()) {
      parts.push_back(item[key].asString());
    }
  }
  parts.push_back(item.get("topic", "").asString().empty()
                      ? "Topic: none"
                      : "Topic: " + item.get("topic", "").asString());
  std::ostringstream out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out << " / ";
    }
    out << parts[i];
  }
  return out.str();
}

std::string RenderItemLink(const Json::Value& item) {
  return "<a href=\"#\" class=\"item-link\" data-item-id=\"" +
         HtmlEscape(item.get("id", "").asString()) +
         "\" data-item-product=\"" +
         HtmlEscape(item.get("product", "").asString()) + "\">" +
         HtmlEscape(item.get("title", "").asString()) + "</a>";
}

std::string GateStateText(const std::string& state) {
  const auto lowered = text::ToLower(Trim(state));
  if (lowered == "not-run") {
    return "not run";
  }
  return lowered.empty() ? "unknown" : lowered;
}

std::string GateStateSymbol(const std::string& state) {
  const auto lowered = text::ToLower(Trim(state));
  if (lowered == "passed") {
    return "✓";
  }
  if (lowered == "failed") {
    return "!";
  }
  if (lowered == "blocked") {
    return "⛔";
  }
  return "?";
}

std::string RenderSelectableItemAttributes(const Json::Value& item) {
  return "data-selectable-item=\"true\" data-item-id=\"" +
         HtmlEscape(item.get("id", "").asString()) +
         "\" data-item-product=\"" +
         HtmlEscape(item.get("product", "").asString()) +
         "\" tabindex=\"-1\" aria-selected=\"false\" role=\"option\"";
}

std::string RenderGateBadge(const std::string& label, const Json::Value& gate) {
  const auto state = gate.get("state", "unknown").asString();
  const auto stateText = GateStateText(state);
  return "<span class=\"pill gate-badge " + PillClass(state) +
         "\" data-gate-state=\"" + HtmlEscape(state.empty() ? "unknown" : state) +
         "\" data-gate-symbol=\"" + HtmlEscape(GateStateSymbol(state)) +
         "\" title=\"" + HtmlEscape(label + " gate: " + stateText) +
         "\" aria-label=\"" + HtmlEscape(label + " gate " + stateText) +
         "\">" + HtmlEscape(label) + "</span>";
}

std::string RenderGateStrip(const Json::Value& gateStatus) {
  return "<div class=\"gate-strip\" role=\"list\" aria-label=\"Gate status\">" +
         RenderGateBadge("Ready", gateStatus["ready"]) +
         RenderGateBadge("Review", gateStatus["review"]) +
         RenderGateBadge("Done", gateStatus["done"]) + "</div>";
}

std::string RenderItemCardSummary(const Json::Value& item) {
  return "<div><code>" + HtmlEscape(item.get("id", "").asString()) +
         "</code></div><div>" + RenderItemLink(item) + "</div>" +
         RenderGateStrip(item["gate_status"]) +
         "<div class=\"muted\">" + HtmlEscape(ItemMetaText(item)) +
         "</div>";
}

std::string RenderItemCardPartial(const Json::Value& item) {
  return "<div class=\"card\" " + RenderSelectableItemAttributes(item) + ">" +
         RenderItemCardSummary(item) + "</div>";
}

std::string RenderTreeNodePartial(const Json::Value& node, int depth) {
  const auto nodeKey =
      node.get("product", "").asString() + "::" + node.get("id", "").asString();
  const auto label =
      "<span class=\"node-line\"><span>[" +
      HtmlEscape(node.get("type", "").asString()) + "]</span><code>" +
      HtmlEscape(node.get("id", "").asString()) + "</code>" +
      RenderItemLink(node) + "<span class=\"muted\">(" +
      HtmlEscape(ItemMetaText(node)) + ")</span></span>";
  const auto& children = node["children"];
  if (children.empty()) {
    return "<li><span class=\"leaf-spacer\"></span>" + label + "</li>";
  }

  std::string html = "<li><details data-node-key=\"" + HtmlEscape(nodeKey) +
                     "\" " + (depth <= 1 ? "open" : "") + "><summary>" +
                     label + "</summary><ul>";
  for (const auto& child : children) {
    html += RenderTreeNodePartial(child, depth + 1);
  }
  html += "</ul></details></li>";
  return html;
}

std::string JsonCompactString(const Json::Value& value);
std::string RenderReviewDecisionHistoryPartial(const Json::Value& history);
std::string RenderPill(const std::string& value);
std::string TruncateForReviewHistory(const std::string& value);

std::string RenderEvidenceSummaryPartial(const Json::Value& evidence) {
  const auto score = evidence.get("score", 0);
  std::ostringstream scoreText;
  if (score.isNumeric()) {
    scoreText << score.asDouble();
  } else {
    scoreText << score.asString();
  }
  std::string html = "<div class=\"muted\">Evidence score: " +
                     HtmlEscape(scoreText.str()) + "</div>";
  if (!evidence["missing"].empty()) {
    html += "<div class=\"muted\">Missing: ";
    for (const auto& missing : evidence["missing"]) {
      html += "<span class=\"pill missing\">" +
              HtmlEscape(missing.asString()) + "</span> ";
    }
    html += "</div>";
  }
  return html;
}

std::string RenderReviewDecisionHistoryPartial(const Json::Value& history) {
  std::string html = "<section class=\"panel detail-section\"><h4>Review decision history</h4>";
  if (history.get("empty", true).asBool() || history["entries"].empty()) {
    html += "<div class=\"muted\">No review decisions recorded.</div></section>";
    return html;
  }

  for (const auto& entry : history["entries"]) {
    const auto humanDecision = entry.get("human_decision", "").asString();
    const auto lane = entry.get("lane", "").asString();
    const auto reasonCode = entry.get("reason_code", "").asString();
    const auto actor = entry.get("actor_alias", "unknown").asString();
    const auto timestamp = entry.get("submitted_at", entry.get("created_at", "unknown")).asString();
    const auto rationale = entry.get("rationale", "").asString();
    const auto suggested = entry.get("suggested_decision", "").asString();
    const auto source = entry.get("source_detector", "").asString();
    const auto outcome = entry["transition"].get("outcome", "skipped").asString();
    html += "<div class=\"card\"><div><strong>" +
            HtmlEscape(humanDecision.empty() ? "Review decision" : humanDecision) +
            "</strong> " + RenderPill(outcome) +
            (entry.get("superseded", false).asBool() ? " <span class=\"pill missing\">superseded</span>" : "") +
            "</div><div class=\"muted\">" + HtmlEscape(lane) +
            (reasonCode.empty() ? "" : " / " + HtmlEscape(reasonCode)) +
            " | " + HtmlEscape(actor) + " | " + HtmlEscape(timestamp) + "</div>";
    if (!suggested.empty() || !source.empty()) {
      html += "<div class=\"muted\">Detector suggestion: " +
              HtmlEscape(suggested.empty() ? "unknown" : suggested) +
              (source.empty() ? "" : " from " + HtmlEscape(source)) + "</div>";
    }
    if (!rationale.empty()) {
      html += "<div>" + HtmlEscape(TruncateForReviewHistory(rationale)) + "</div>";
    }
    const auto supersedes = entry.get("supersedes", "").asString();
    if (!supersedes.empty()) {
      html += "<div class=\"muted\">Supersedes: <code>" + HtmlEscape(supersedes) + "</code></div>";
    }
    html += "<details><summary>Raw review decision metadata</summary><pre>" +
            HtmlEscape(entry.get("raw_metadata", JsonCompactString(entry)).asString()) +
            "</pre></details></div>";
  }
  html += "</section>";
  return html;
}

std::string RenderReviewBundlePartial(const Json::Value& bundle) {
  const auto& item = bundle["item"];
  std::string html = "<div class=\"card\" " + RenderSelectableItemAttributes(item) + ">" +
                     RenderItemCardSummary(item);
  const auto reason = bundle.get("review_reason", "").asString();
  if (!reason.empty()) {
    const auto reasonCode = bundle.get("reason_code", "").asString();
    html += "<div class=\"review-reason muted\"><strong>Why this needs review:</strong> " +
            (reasonCode.empty() ? "" : "<code>" + HtmlEscape(reasonCode) + "</code> ") +
            HtmlEscape(reason);
    const auto decision = bundle.get("suggested_human_decision", "").asString();
    if (!decision.empty()) {
      html += "<div class=\"muted\">Suggested decision: " +
              HtmlEscape(decision) + "</div>";
    }
    html += "</div>";
  }
  html += RenderEvidenceSummaryPartial(bundle["evidence"]);
  html += "</div>";
  return html;
}

std::string RenderCheckbox(const std::string& name, const std::string& value,
                           bool checked) {
  return "<label><input type=\"checkbox\" name=\"" + HtmlEscape(name) +
         "\" value=\"" + HtmlEscape(value) + "\" " +
         (checked ? "checked" : "") + " /> " + HtmlEscape(value) + "</label>";
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

std::string JsonStringField(const Json::Value& value, const std::string& key) {
  if (!value.isObject() || !value.isMember(key) || value[key].isNull()) {
    return "";
  }
  return Trim(value[key].asString());
}

bool JsonBoolField(const Json::Value& value, const std::string& key) {
  return value.isObject() && value.isMember(key) && value[key].asBool();
}

bool ReadJsonFile(const std::filesystem::path& path, Json::Value& value);

Json::Value MakeError(const std::string& code, const std::string& message) {
  Json::Value out(Json::objectValue);
  out["error"] = message;
  out["error_code"] = code;
  return out;
}

std::string SafeFileToken(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
      out.push_back(static_cast<char>(ch));
    } else {
      out.push_back('-');
    }
  }
  while (!out.empty() && out.front() == '-') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  return out.empty() ? "unknown" : out;
}

std::filesystem::path ReviewDecisionDraftPath(const std::filesystem::path& productRoot,
                                             const std::string& itemId,
                                             const std::string& actorAlias) {
  return productRoot / "_meta" / "review-decisions" / "drafts" /
      (SafeFileToken(itemId) + "-" + SafeFileToken(actorAlias) + ".json");
}

Json::Value MissingReviewDecisionDraft(const std::string& product,
                                       const std::string& itemId,
                                       const std::string& actorAlias) {
  Json::Value draft(Json::objectValue);
  draft["status"] = "missing";
  draft["exists"] = false;
  draft["empty"] = true;
  draft["product"] = product;
  draft["item_id"] = itemId;
  draft["actor_alias"] = actorAlias;
  draft["rationale"] = "";
  return draft;
}

Json::Value LoadReviewDecisionDraft(const std::filesystem::path& productRoot,
                                    const std::string& product,
                                    const std::string& itemId,
                                    const std::string& actorAlias) {
  auto draftPath = ReviewDecisionDraftPath(productRoot, itemId, actorAlias);
  if (!std::filesystem::exists(draftPath)) {
    const auto draftDir = productRoot / "_meta" / "review-decisions" / "drafts";
    const auto prefix = SafeFileToken(itemId) + "-";
    std::optional<std::filesystem::path> latestDraft;
    std::filesystem::file_time_type latestTime{};
    if (std::filesystem::exists(draftDir)) {
      for (const auto& entry : std::filesystem::directory_iterator(draftDir)) {
        if (!entry.is_regular_file()) {
          continue;
        }
        const auto filename = entry.path().filename().string();
        if (!StartsWith(filename, prefix) || entry.path().extension() != ".json") {
          continue;
        }
        const auto writeTime = entry.last_write_time();
        if (!latestDraft || writeTime > latestTime) {
          latestDraft = entry.path();
          latestTime = writeTime;
        }
      }
    }
    if (latestDraft) {
      draftPath = *latestDraft;
    }
  }
  Json::Value draft(Json::objectValue);
  if (!ReadJsonFile(draftPath, draft) || !draft.isObject()) {
    return MissingReviewDecisionDraft(product, itemId, actorAlias);
  }
  draft["exists"] = true;
  draft["empty"] = JsonStringField(draft, "rationale").empty();
  draft["path"] = std::filesystem::relative(draftPath, productRoot).generic_string();
  return draft;
}

std::string ReviewTimestampText(std::chrono::system_clock::time_point now) {
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#ifdef _WIN32
  gmtime_s(&tm_buf, &now_time);
#else
  gmtime_r(&now_time, &tm_buf);
#endif
  std::ostringstream out;
  out << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string ReviewTimestampFileToken(std::chrono::system_clock::time_point now) {
  const auto now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#ifdef _WIN32
  gmtime_s(&tm_buf, &now_time);
#else
  gmtime_r(&now_time, &tm_buf);
#endif
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count() % 1000;
  std::ostringstream out;
  out << std::put_time(&tm_buf, "%Y%m%dT%H%M%S") << "-"
      << std::setw(3) << std::setfill('0') << millis << "Z";
  return out.str();
}

bool ReadJsonFile(const std::filesystem::path& path, Json::Value& value) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }
  Json::CharReaderBuilder builder;
  std::string errors;
  return Json::parseFromStream(builder, input, &value, &errors);
}

std::string JsonCompactString(const Json::Value& value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  return Json::writeString(builder, value);
}

std::string TruncateForReviewHistory(const std::string& value) {
  constexpr size_t maxLength = 180;
  if (value.size() <= maxLength) {
    return value;
  }
  return value.substr(0, maxLength) + "...";
}

Json::Value BuildReviewDecisionHistory(const std::filesystem::path& productRoot,
                                       const std::string& product,
                                       const std::string& itemId) {
  Json::Value history(Json::objectValue);
  history["product"] = product;
  history["item_id"] = itemId;
  history["entries"] = Json::arrayValue;
  history["empty"] = true;

  const auto submittedDir = productRoot / "_meta" / "review-decisions" /
      "submitted" / SafeFileToken(itemId);
  if (!std::filesystem::exists(submittedDir)) {
    return history;
  }

  std::vector<Json::Value> entries;
  for (const auto& entry : std::filesystem::directory_iterator(submittedDir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    Json::Value record(Json::objectValue);
    if (!ReadJsonFile(entry.path(), record) || !record.isObject()) {
      continue;
    }
    record["path"] = std::filesystem::relative(entry.path(), productRoot).generic_string();
    record["raw_metadata"] = JsonCompactString(record);
    entries.push_back(record);
  }

  std::sort(entries.begin(), entries.end(), [](const Json::Value& lhs, const Json::Value& rhs) {
    const auto lhsTime = lhs.get("submitted_at", lhs.get("created_at", "")).asString();
    const auto rhsTime = rhs.get("submitted_at", rhs.get("created_at", "")).asString();
    if (lhsTime == rhsTime) {
      return lhs.get("path", "").asString() < rhs.get("path", "").asString();
    }
    return lhsTime < rhsTime;
  });

  std::set<std::string> supersededPaths;
  for (const auto& entry : entries) {
    const auto supersedes = entry.get("supersedes", "").asString();
    if (!supersedes.empty()) {
      supersededPaths.insert(supersedes);
    }
  }

  for (auto entry : entries) {
    entry["superseded"] = supersededPaths.count(entry.get("path", "").asString()) > 0;
    history["entries"].append(entry);
  }
  history["empty"] = history["entries"].empty();
  return history;
}

void WriteJsonFile(const std::filesystem::path& path, const Json::Value& value) {
  std::filesystem::create_directories(path.parent_path());
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open review decision file for write: " + path.generic_string());
  }
  out << Json::writeString(builder, value) << "\n";
}

void WriteTextFile(const std::filesystem::path& path, const std::string& value) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open file for write: " + path.generic_string());
  }
  out << value;
}

std::string UpsertFrontmatterScalar(std::string text,
                                    const std::string& key,
                                    const std::string& value) {
  const auto first = text.find("---");
  if (first != 0) {
    return text;
  }
  const auto end = text.find("\n---", 3);
  if (end == std::string::npos) {
    return text;
  }
  const auto keyPrefix = key + ":";
  auto lineStart = text.find('\n', first + 3);
  while (lineStart != std::string::npos && lineStart < end) {
    const auto contentStart = lineStart + 1;
    const auto lineEnd = text.find('\n', contentStart);
    if (contentStart < end && text.compare(contentStart, keyPrefix.size(), keyPrefix) == 0) {
      const auto replaceEnd = lineEnd == std::string::npos ? text.size() : lineEnd;
      text.replace(contentStart, replaceEnd - contentStart, keyPrefix + " " + value);
      return text;
    }
    lineStart = lineEnd;
  }
  text.insert(end + 1, keyPrefix + " " + value + "\n");
  return text;
}

std::string AppendWorklogLine(std::string text, const std::string& line) {
  const auto heading = text.find("\n# Worklog");
  if (heading == std::string::npos) {
    if (!text.empty() && text.back() != '\n') {
      text.push_back('\n');
    }
    text += "\n# Worklog\n\n" + line + "\n";
    return text;
  }
  const auto nextHeading = text.find("\n# ", heading + 1);
  const auto insertion = nextHeading == std::string::npos ? text.size() : nextHeading;
  const auto prefix = insertion > 0 && text[insertion - 1] == '\n' ? "" : "\n";
  text.insert(insertion, prefix + line + "\n");
  return text;
}

std::optional<kano::backlog_core::StateAction> ReviewActionForTargetState(
    kano::backlog_core::ItemState state) {
  using kano::backlog_core::ItemState;
  using kano::backlog_core::StateAction;
  switch (state) {
    case ItemState::Proposed:
      return StateAction::Propose;
    case ItemState::Planned:
    case ItemState::Ready:
      return StateAction::Ready;
    case ItemState::InProgress:
      return StateAction::Start;
    case ItemState::Review:
      return StateAction::Review;
    case ItemState::Done:
      return StateAction::Done;
    case ItemState::Blocked:
      return StateAction::Block;
    case ItemState::Dropped:
      return StateAction::Drop;
    case ItemState::Duplicate:
    case ItemState::New:
      return std::nullopt;
  }
  return std::nullopt;
}

void ApplyTransitionToMarkdown(
    const std::filesystem::path& productRoot,
    const Json::Value& item,
    const kano::backlog_core::BacklogItem& transitioned) {
  const auto relativePath = item.get("path", "").asString();
  if (relativePath.empty()) {
    throw std::runtime_error("review decision item path is missing");
  }
  const auto itemPath = productRoot / relativePath;
  bool ok = false;
  std::string error;
  auto text = ReadTextFile(itemPath, ok, error);
  if (!ok) {
    throw std::runtime_error("failed to read item for review transition: " +
                             itemPath.generic_string());
  }
  text = UpsertFrontmatterScalar(
      std::move(text), "state", kano::backlog_core::to_string(transitioned.state));
  text = UpsertFrontmatterScalar(std::move(text), "updated", transitioned.updated);
  if (!transitioned.worklog.empty()) {
    text = AppendWorklogLine(std::move(text), transitioned.worklog.back());
  }
  WriteTextFile(itemPath, text);
}

std::filesystem::path UniqueJsonPath(std::filesystem::path path) {
  if (!std::filesystem::exists(path)) {
    return path;
  }
  const auto parent = path.parent_path();
  const auto stem = path.stem().string();
  const auto ext = path.extension().string();
  for (int i = 2; i < 1000; ++i) {
    auto candidate = parent / (stem + "-" + std::to_string(i) + ext);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  throw std::runtime_error("failed to allocate unique review decision path: " + path.generic_string());
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

std::string StripInlineComment(const std::string& value) {
  bool quoted = false;
  char quote = '\0';
  for (size_t i = 0; i < value.size(); ++i) {
    const char ch = value[i];
    if (quoted) {
      if (ch == quote) {
        quoted = false;
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quoted = true;
      quote = ch;
      continue;
    }
    if (ch == '#') {
      return Trim(value.substr(0, i));
    }
  }
  return Trim(value);
}

std::string CleanListToken(std::string value) {
  value = StripInlineComment(Trim(value));
  while (!value.empty() && value.back() == ',') {
    value.pop_back();
    value = Trim(value);
  }
  value = NormalizeNullToken(Unquote(value));
  if (value == "[]" || value == "{}") {
    return "";
  }
  return value;
}

std::string NormalizeItemTypeName(const std::string& value) {
  const auto trimmed = Trim(value);
  const auto lowered = text::ToLower(trimmed);
  if (lowered == "initiative") return "Initiative";
  if (lowered == "epic") return "Epic";
  if (lowered == "feature") return "Feature";
  if (lowered == "userstory" || lowered == "user story" ||
      lowered == "user-story" || lowered == "story") {
    return "UserStory";
  }
  if (lowered == "task") return "Task";
  if (lowered == "bug") return "Bug";
  if (lowered == "issue") return "Issue";
  return trimmed;
}

std::vector<std::string> ParseListValue(std::string value) {
  std::vector<std::string> result;
  value = StripInlineComment(Trim(value));
  if (value.empty() || value == "[]") {
    return result;
  }
  if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
    const auto inner = value.substr(1, value.size() - 2);
    std::stringstream stream(inner);
    std::string token;
    while (std::getline(stream, token, ',')) {
      AppendUnique(result, CleanListToken(token));
    }
    return result;
  }
  AppendUnique(result, CleanListToken(value));
  return result;
}

std::vector<std::string> ExtractFrontmatterList(const std::string& content,
                                                const std::string& key) {
  std::vector<std::string> result;
  const auto lines = SplitLines(content);
  if (lines.empty() || Trim(lines.front()) != "---") {
    return result;
  }

  bool inLinks = false;
  std::string activeList;
  for (size_t i = 1; i < lines.size(); ++i) {
    const auto raw = lines[i];
    const auto trimmed = Trim(raw);
    if (trimmed == "---") {
      break;
    }
    if (trimmed.empty()) {
      continue;
    }

    const auto firstNonSpace = raw.find_first_not_of(" \t");
    const size_t indent =
        firstNonSpace == std::string::npos ? 0 : firstNonSpace;
    const auto colon = trimmed.find(':');
    if (indent == 0 && colon != std::string::npos) {
      const auto currentKey = Trim(trimmed.substr(0, colon));
      inLinks = currentKey == "links";
      activeList.clear();
      if (currentKey == key) {
        activeList = key;
        for (const auto& value : ParseListValue(trimmed.substr(colon + 1))) {
          AppendUnique(result, value);
        }
      }
      continue;
    }

    if (colon != std::string::npos) {
      const auto currentKey = Trim(trimmed.substr(0, colon));
      if ((inLinks || currentKey == key) && currentKey == key) {
        activeList = key;
        for (const auto& value : ParseListValue(trimmed.substr(colon + 1))) {
          AppendUnique(result, value);
        }
        continue;
      }
      if (indent <= 2) {
        activeList.clear();
      }
    }

    if (activeList == key && StartsWith(trimmed, "-")) {
      auto value = Trim(trimmed.substr(1));
      for (const auto& parsed : ParseListValue(value)) {
        AppendUnique(result, parsed);
      }
    }
  }
  return result;
}

std::map<std::string, std::string> ExtractFrontmatterMap(
    const std::string& content,
    const std::string& key) {
  std::map<std::string, std::string> result;
  const auto lines = SplitLines(content);
  if (lines.empty() || Trim(lines.front()) != "---") {
    return result;
  }

  bool inMap = false;
  for (size_t i = 1; i < lines.size(); ++i) {
    const auto& raw = lines[i];
    const auto trimmed = Trim(raw);
    if (trimmed == "---") {
      break;
    }
    if (trimmed.empty()) {
      continue;
    }

    const auto firstNonSpace = raw.find_first_not_of(" \t");
    const size_t indent =
        firstNonSpace == std::string::npos ? 0 : firstNonSpace;
    const auto colon = trimmed.find(':');
    if (indent == 0 && colon != std::string::npos) {
      const auto currentKey = Trim(trimmed.substr(0, colon));
      inMap = currentKey == key;
      if (!inMap) {
        continue;
      }

      auto value = Trim(trimmed.substr(colon + 1));
      if (value.empty() || value == "{}") {
        continue;
      }
      if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
        std::stringstream stream(value.substr(1, value.size() - 2));
        std::string token;
        while (std::getline(stream, token, ',')) {
          const auto tokenTrimmed = Trim(token);
          const auto tokenColon = tokenTrimmed.find(':');
          if (tokenColon == std::string::npos) {
            continue;
          }
          const auto nestedKey =
              Trim(tokenTrimmed.substr(0, tokenColon));
          const auto nestedValue = NormalizeNullToken(
              Unquote(Trim(tokenTrimmed.substr(tokenColon + 1))));
          if (!nestedKey.empty() && !nestedValue.empty()) {
            result[nestedKey] = nestedValue;
          }
        }
      }
      continue;
    }

    if (!inMap || indent == 0 || colon == std::string::npos) {
      continue;
    }

    const auto nestedKey = Trim(trimmed.substr(0, colon));
    const auto nestedValue =
        NormalizeNullToken(Unquote(Trim(trimmed.substr(colon + 1))));
    if (!nestedKey.empty() && !nestedValue.empty()) {
      result[nestedKey] = nestedValue;
    }
  }

  return result;
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

struct SavedViewDefinition {
  std::string id;
  std::string title;
  std::string description;
  std::string kobql;
};

const std::vector<SavedViewDefinition>& SavedViews() {
  static const std::vector<SavedViewDefinition> views = {
      {"ready-frontier", "Ready Frontier",
       "Ready items waiting for a human to approve, dispatch, or defer.",
       "state:Ready type:Initiative type:Feature type:UserStory type:Task type:Bug"},
      {"needs-review", "Needs Review",
       "Items waiting for a human to review delivered results.",
       "state:Review"},
      {"blocked-dirty", "Blocked/Dirty",
       "Items that cannot move forward without an explicit blocker decision.",
       "state:Blocked"},
      {"stale-drift", "Stale/Drift",
       "Active items that need an execution freshness check.",
       "state:InProgress"},
      {"evidence-gap", "Evidence Gap",
       "Reviewable items whose worklog or detail text lacks concrete evidence markers.",
       "state:Ready state:Review state:Done"}};
  return views;
}

Json::Value SavedViewToJson(const SavedViewDefinition& view) {
  Json::Value value(Json::objectValue);
  value["id"] = view.id;
  value["title"] = view.title;
  value["description"] = view.description;
  value["kobql"] = view.kobql;
  value["read_only"] = true;
  return value;
}

std::vector<std::string> TokenizeQuery(const std::string& query) {
  std::vector<std::string> tokens;
  std::string current;
  bool quoted = false;
  char quote = '\0';
  for (const char ch : query) {
    if (quoted) {
      if (ch == quote) {
        quoted = false;
      } else {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quoted = true;
      quote = ch;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

bool JsonStringContains(const Json::Value& value, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return text::ContainsCaseInsensitive(value.asString(), needle);
}

bool ContentContainsAny(const std::string& content,
                        const std::vector<std::string>& needles) {
  for (const auto& needle : needles) {
    if (text::ContainsCaseInsensitive(content, needle)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ExtractWorklogLines(const std::string& content) {
  std::vector<std::string> lines;
  bool inWorklog = false;
  for (const auto& line : SplitLines(content)) {
    const auto trimmed = Trim(line);
    if (trimmed == "## Worklog") {
      inWorklog = true;
      continue;
    }
    if (inWorklog && StartsWith(trimmed, "## ")) {
      break;
    }
    if (inWorklog && !trimmed.empty()) {
      lines.push_back(trimmed);
    }
  }
  return lines;
}

std::string ExtractAgent(const std::string& line) {
  static const std::regex agentPattern(R"(\[agent=([^\]]+)\])");
  std::smatch match;
  if (std::regex_search(line, match, agentPattern) && match.size() > 1) {
    return match[1].str();
  }
  return "unknown";
}

std::string ExtractTimestamp(const std::string& line) {
  static const std::regex timestampPattern(R"(^([0-9]{4}-[0-9]{2}-[0-9]{2}(?: [0-9]{2}:[0-9]{2})?))");
  std::smatch match;
  if (std::regex_search(line, match, timestampPattern) && match.size() > 1) {
    return match[1].str();
  }
  return "";
}

std::string InferEventKind(const std::string& line) {
  if (text::ContainsCaseInsensitive(line, "State:")) {
    return "state-transition";
  }
  if (text::ContainsCaseInsensitive(line, "Artifact attached")) {
    return "artifact";
  }
  if (ContentContainsAny(line, {"test", "validation", "PASS", "FAIL", "quick-test", "build"})) {
    return "validation";
  }
  if (ContentContainsAny(line, {"blocked", "blocker"})) {
    return "blocked";
  }
  if (ContentContainsAny(line, {"work order", "handoff", "dispatch"})) {
    return "work-order";
  }
  return "worklog";
}

std::string InferRunState(const Json::Value& item, const std::string& lastEventKind) {
  const auto state = text::ToLower(item["state"].asString());
  if (state == "inprogress" || state == "active") {
    return lastEventKind == "blocked" ? "blocked" : "active";
  }
  if (state == "blocked") {
    return "blocked";
  }
  if (state == "review") {
    return "review";
  }
  if (state == "done" || state == "dropped" || state == "closed") {
    return "complete";
  }
  if (state == "ready") {
    return "queued";
  }
  return "unknown";
}

Json::Value MakeValidationCheck(const std::string& name,
                                const std::string& status,
                                const std::string& evidence) {
  Json::Value check(Json::objectValue);
  check["name"] = name;
  check["status"] = status;
  check["evidence"] = evidence;
  return check;
}

Json::Value BuildEvidenceSummary(const Json::Value& item) {
  const auto content = item.get("content", "").asString();
  const auto combined = item["id"].asString() + "\n" + item["title"].asString() + "\n" + content;

  const bool hasArtifact = ContentContainsAny(combined, {"Artifact attached", "artifacts/", "artifact"});
  const bool hasValidation = ContentContainsAny(combined, {"validation", "test", "quick-test", "build-dev", "build-release", "PASS", "FAIL"});
  const bool hasCommit = ContentContainsAny(combined, {"commit", "git hash", "revision"});
  const bool hasDeploy = ContentContainsAny(combined, {"deploy", "deployment", "Jenkins", "GitHub Release", "release asset", "Pages"});
  const bool hasWorkOrder = ContentContainsAny(combined, {"work order", "handoff", "agent run", "dispatch"});

  Json::Value summary(Json::objectValue);
  summary["score"] = static_cast<Json::UInt64>(
      (hasArtifact ? 1 : 0) + (hasValidation ? 1 : 0) + (hasCommit ? 1 : 0) +
      (hasDeploy ? 1 : 0) + (hasWorkOrder ? 1 : 0));
  summary["complete"] = summary["score"].asUInt64() >= 2;
  summary["signals"]["artifact"] = hasArtifact;
  summary["signals"]["validation"] = hasValidation;
  summary["signals"]["commit"] = hasCommit;
  summary["signals"]["deployment"] = hasDeploy;
  summary["signals"]["work_order"] = hasWorkOrder;

  summary["missing"] = Json::arrayValue;
  if (!hasArtifact) summary["missing"].append("artifact");
  if (!hasValidation) summary["missing"].append("validation");
  if (!hasCommit) summary["missing"].append("commit");
  if (!hasWorkOrder) summary["missing"].append("work_order");

  Json::Value matrix(Json::arrayValue);
  matrix.append(MakeValidationCheck(
      "build",
      ContentContainsAny(combined, {"build failed", "build: FAIL"}) ? "failed" :
          (ContentContainsAny(combined, {"build", "build-dev", "build-release"}) ? "passed" : "not-run"),
      "Matched build terms in item body/worklog"));
  matrix.append(MakeValidationCheck(
      "test",
      ContentContainsAny(combined, {"test failed", "quick-test failed", "FAIL"}) ? "failed" :
          (ContentContainsAny(combined, {"test", "quick-test", "PASS"}) ? "passed" : "not-run"),
      "Matched test terms in item body/worklog"));
  matrix.append(MakeValidationCheck(
      "deployment",
      ContentContainsAny(combined, {"deploy failed", "deployment failed"}) ? "failed" :
          (hasDeploy ? "passed" : "not-run"),
      "Matched deployment/release terms in item body/worklog"));
  summary["validation_matrix"] = matrix;

  return summary;
}

Json::Value MakeArray(const std::vector<std::string>& values) {
  Json::Value out(Json::arrayValue);
  for (const auto& value : values) {
    out.append(value);
  }
  return out;
}

Json::Value MakeEvidenceItem(const ItemRecord& item) {
  Json::Value value(Json::objectValue);
  value["id"] = item.id;
  value["title"] = item.title;
  value["state"] = item.state;
  value["content"] = item.rawContent;
  return value;
}

std::string NormalizeSectionName(const std::string& value) {
  auto name = text::ToLower(Trim(value));
  while (!name.empty() && name.back() == ':') {
    name.pop_back();
    name = Trim(name);
  }
  if (name == "risks / dependencies") {
    return "risks";
  }
  return name;
}

std::string ExtractMarkdownBody(const std::string& content) {
  const auto lines = SplitLines(content);
  if (lines.empty() || Trim(lines.front()) != "---") {
    return content;
  }

  size_t bodyStart = 0;
  bool foundEnd = false;
  for (size_t i = 1; i < lines.size(); ++i) {
    if (Trim(lines[i]) == "---") {
      bodyStart = i + 1;
      foundEnd = true;
      break;
    }
  }
  if (!foundEnd || bodyStart >= lines.size()) {
    return "";
  }

  std::ostringstream out;
  for (size_t i = bodyStart; i < lines.size(); ++i) {
    if (i > bodyStart) {
      out << "\n";
    }
    out << lines[i];
  }
  return out.str();
}

struct ReviewSection {
  std::string key;
  std::string title;
  std::string body;
};

std::vector<ReviewSection> ExtractReviewSections(const std::string& content) {
  const std::set<std::string> wanted = {
      "context", "goal", "acceptance criteria", "risks", "worklog"};

  std::vector<ReviewSection> sections;
  ReviewSection current;
  bool capturing = false;
  auto flush = [&]() {
    if (!capturing) {
      return;
    }
    current.body = Trim(current.body);
    if (!current.body.empty()) {
      sections.push_back(current);
    }
    current = ReviewSection{};
    capturing = false;
  };

  for (const auto& line : SplitLines(ExtractMarkdownBody(content))) {
    const auto trimmed = Trim(line);
    if (StartsWith(trimmed, "#")) {
      flush();
      const auto titleStart = trimmed.find_first_not_of('#');
      if (titleStart == std::string::npos) {
        continue;
      }
      const auto title = Trim(trimmed.substr(titleStart));
      const auto key = NormalizeSectionName(title);
      if (wanted.count(key) > 0) {
        current.key = key;
        current.title = title;
        current.body.clear();
        capturing = true;
      }
      continue;
    }
    if (!capturing) {
      continue;
    }
    if (!current.body.empty()) {
      current.body += "\n";
    }
    current.body += line;
  }
  flush();
  return sections;
}

std::string PillClass(const std::string& value) {
  const auto lowered = text::ToLower(Trim(value));
  if (lowered == "passed") {
    return "passed";
  }
  if (lowered == "failed") {
    return "failed";
  }
  if (lowered == "blocked") {
    return "blocked";
  }
  if (lowered == "not-run" || lowered == "unknown") {
    return "missing";
  }
  return "";
}

std::string RenderPill(const std::string& value) {
  return "<span class=\"pill " + PillClass(value) + "\">" +
         HtmlEscape(value.empty() ? "unknown" : value) + "</span>";
}

bool IsEmptyLikeString(const std::string& value) {
  const auto lowered = text::ToLower(Trim(value));
  return lowered.empty() || lowered == "~" || lowered == "null" ||
         lowered == "none" || lowered == "{}" || lowered == "[]";
}

bool JsonHasMeaningfulContent(const Json::Value& value) {
  if (value.isNull()) {
    return false;
  }
  if (value.isString()) {
    return !IsEmptyLikeString(value.asString());
  }
  if (value.isArray()) {
    for (const auto& entry : value) {
      if (JsonHasMeaningfulContent(entry)) {
        return true;
      }
    }
    return false;
  }
  if (value.isObject()) {
    for (const auto& member : value.getMemberNames()) {
      if (JsonHasMeaningfulContent(value[member])) {
        return true;
      }
    }
    return false;
  }
  return true;
}

std::string RenderDetailFact(const std::string& label,
                             const std::string& value) {
  return "<div class=\"detail-fact\"><span class=\"detail-label\">" +
         HtmlEscape(label) +
         "</span><div class=\"detail-value\">" + HtmlEscape(value) +
         "</div></div>";
}

std::string RenderMarkdownRegion(const std::string& markdown) {
  return "<div class=\"md-view\" data-kob-markdown=\"1\"><textarea class=\"kob-md-source\" hidden>" +
         HtmlEscape(markdown) + "</textarea></div>";
}

std::string RenderSectionPanel(const ReviewSection& section) {
  return "<section class=\"panel detail-section\"><h4>" +
         HtmlEscape(section.title) + "</h4>" +
         RenderMarkdownRegion(section.body) + "</section>";
}

std::pair<std::string, std::string> SplitReferenceProduct(
    const std::string& rawRef,
    const std::string& defaultProduct) {
  const auto cleaned = CleanListToken(rawRef);
  const auto colon = cleaned.find(':');
  if (colon == std::string::npos) {
    return {defaultProduct, cleaned};
  }
  return {Trim(cleaned.substr(0, colon)), Trim(cleaned.substr(colon + 1))};
}

std::string RenderReferenceChip(const std::string& rawRef,
                                const std::string& defaultProduct) {
  const auto cleaned = CleanListToken(rawRef);
  if (cleaned.empty()) {
    return "";
  }
  const auto [product, itemId] = SplitReferenceProduct(cleaned, defaultProduct);
  if (itemId.empty()) {
    return "";
  }
  return "<a href=\"#\" class=\"pill item-link\" data-item-id=\"" +
         HtmlEscape(itemId) + "\" data-item-product=\"" +
         HtmlEscape(product) + "\">" + HtmlEscape(cleaned) + "</a>";
}

std::string RenderTextChip(const std::string& value) {
  const auto cleaned = CleanListToken(value);
  if (cleaned.empty()) {
    return "";
  }
  return "<span class=\"pill\">" + HtmlEscape(cleaned) + "</span>";
}

std::string RenderPillRow(const std::vector<std::string>& values,
                          const std::function<std::string(const std::string&)>& renderer) {
  std::string chips;
  for (const auto& value : values) {
    const auto chip = renderer(value);
    if (!chip.empty()) {
      chips += chip;
    }
  }
  if (chips.empty()) {
    return "";
  }
  return "<div class=\"detail-links\">" + chips + "</div>";
}

std::string RenderKeyValueRows(
    const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string html;
  for (const auto& [label, value] : rows) {
    if (IsEmptyLikeString(value)) {
      continue;
    }
    html += "<div class=\"detail-kv\"><div class=\"detail-label\">" +
            HtmlEscape(label) + "</div><div class=\"detail-value\">" +
            HtmlEscape(value) + "</div></div>";
  }
  if (html.empty()) {
    return "";
  }
  return "<div class=\"detail-kv-list\">" + html + "</div>";
}

std::string RenderGateStatusCard(const Json::Value& gate) {
  std::string html = "<div class=\"review-lane detail-gate\"><div class=\"detail-title-row\"><strong>" +
                     HtmlEscape(gate.get("label", gate.get("name", "Gate")).asString()) +
                     " gate</strong> " + RenderPill(gate.get("state", "unknown").asString()) +
                     "</div>";
  if (JsonHasMeaningfulContent(gate["blockers"])) {
    html += "<div class=\"detail-stack\"><div class=\"detail-label\">Blockers</div><ul>";
    for (const auto& blocker : gate["blockers"]) {
      if (!IsEmptyLikeString(blocker.asString())) {
        html += "<li>" + HtmlEscape(blocker.asString()) + "</li>";
      }
    }
    html += "</ul></div>";
  } else {
    html += "<div class=\"muted\">No blockers.</div>";
  }

  html += "<div class=\"detail-stack\"><div class=\"detail-label\">Native checks</div>";
  if (!JsonHasMeaningfulContent(gate["native_checks"])) {
    html += "<div class=\"muted\">No native checks.</div>";
  } else {
    for (const auto& check : gate["native_checks"]) {
      html += "<div class=\"evidence-row\"><strong>" +
              HtmlEscape(check.get("name", "check").asString()) + "</strong>" +
              RenderPill(check.get("status", "unknown").asString()) +
              "<span class=\"muted\">" +
              HtmlEscape(check.get("evidence", "").asString()) +
              "</span></div>";
    }
  }

  if (JsonHasMeaningfulContent(gate["source_fields"])) {
    std::vector<std::string> fields;
    for (const auto& field : gate["source_fields"]) {
      if (!IsEmptyLikeString(field.asString())) {
        fields.push_back(field.asString());
      }
    }
    const auto fieldRow = RenderPillRow(fields, [](const std::string& value) {
      return RenderTextChip(value);
    });
    if (!fieldRow.empty()) {
      html += "<div class=\"detail-stack\"><div class=\"detail-label\">Source fields</div>" +
              fieldRow + "</div>";
    }
  }

  html += "</div>";
  return html;
}

std::map<std::string, bool> ExtractSectionContentPresence(
    const std::string& content,
    const std::vector<std::string>& sectionNames) {
  std::set<std::string> required;
  for (const auto& sectionName : sectionNames) {
    required.insert(NormalizeSectionName(sectionName));
  }

  std::map<std::string, bool> present;
  for (const auto& sectionName : required) {
    present[sectionName] = false;
  }

  std::string activeSection;
  bool activeHasContent = false;
  auto closeActiveSection = [&]() {
    if (!activeSection.empty()) {
      present[activeSection] = activeHasContent;
      activeSection.clear();
      activeHasContent = false;
    }
  };

  for (const auto& line : SplitLines(content)) {
    const auto trimmed = Trim(line);
    if (StartsWith(trimmed, "#")) {
      closeActiveSection();
      const auto titleStart = trimmed.find_first_not_of('#');
      if (titleStart == std::string::npos) {
        continue;
      }
      const auto sectionName = NormalizeSectionName(trimmed.substr(titleStart));
      if (required.count(sectionName) > 0) {
        activeSection = sectionName;
      }
      continue;
    }
    if (!activeSection.empty() && !trimmed.empty()) {
      activeHasContent = true;
    }
  }
  closeActiveSection();
  return present;
}

Json::Value MakeGateStatus(const std::string& name,
                           const std::string& label,
                           const std::string& state,
                           const std::vector<std::string>& blockers,
                           const Json::Value& nativeChecks,
                           const std::vector<std::string>& sourceFields) {
  Json::Value gate(Json::objectValue);
  gate["name"] = name;
  gate["label"] = label;
  gate["state"] = state;
  gate["blockers"] = MakeArray(blockers);
  gate["native_checks"] = nativeChecks;
  gate["source_fields"] = MakeArray(sourceFields);
  return gate;
}

Json::Value BuildReadyGateStatus(const ItemRecord& item) {
  Json::Value checks(Json::arrayValue);
  const bool structuralRoot = item.type == "Initiative" || item.type == "Epic";
  const std::vector<std::string> requiredSections = structuralRoot
      ? std::vector<std::string>{"Context", "Goal"}
      : std::vector<std::string>{"Context", "Goal", "Acceptance Criteria", "Risks / Dependencies"};
  std::vector<std::string> sourceFields = {"source_kind", "state"};
  for (const auto& sectionName : requiredSections) {
    sourceFields.push_back("content.sections." + sectionName);
  }
  if (item.sourceKind != "Item") {
    checks.append(MakeValidationCheck("applicable_source_kind", "unknown",
                                      "Ready gate applies only to backlog item Markdown."));
    return MakeGateStatus("ready", "Ready", "unknown", {}, checks,
                          {"source_kind"});
  }

  const auto state = text::ToLower(item.state);
  if (state != "ready") {
    checks.append(MakeValidationCheck("ready_state", "unknown",
                                      "Item state is not Ready."));
    return MakeGateStatus("ready", "Ready", "unknown", {}, checks,
                          {"state"});
  }

  const auto sectionPresence =
      ExtractSectionContentPresence(item.rawContent, requiredSections);
  std::vector<std::string> missing;
  for (const auto& sectionName : requiredSections) {
    const auto normalized = NormalizeSectionName(sectionName);
    const auto found = sectionPresence.find(normalized);
    if (found == sectionPresence.end() || !found->second) {
      missing.push_back("missing section: " + sectionName);
    }
  }

  const std::string requirementMessage = structuralRoot
      ? "Context and Goal sections must contain content."
      : "Context, Goal, Acceptance Criteria, and Risks / Dependencies sections must contain content.";
  checks.append(MakeValidationCheck(
      "required_ready_sections", missing.empty() ? "passed" : "failed",
      requirementMessage));
  return MakeGateStatus("ready", "Ready", missing.empty() ? "passed" : "failed",
                        missing, checks, sourceFields);
}

std::vector<std::string> EvidenceMissingBlockers(const Json::Value& evidence) {
  std::vector<std::string> blockers;
  for (const auto& missing : evidence["missing"]) {
    blockers.push_back("missing evidence: " + missing.asString());
  }
  if (blockers.empty()) {
    blockers.push_back("missing sufficient evidence");
  }
  return blockers;
}

Json::Value BuildReviewGateStatus(const ItemRecord& item,
                                  const Json::Value& evidence) {
  Json::Value checks(Json::arrayValue);
  if (item.sourceKind != "Item") {
    checks.append(MakeValidationCheck("applicable_source_kind", "unknown",
                                      "Review gate applies only to backlog item Markdown."));
    return MakeGateStatus("review", "Review", "unknown", {}, checks,
                          {"source_kind"});
  }

  const auto state = text::ToLower(item.state);
  const bool evidenceComplete = evidence["complete"].asBool();
  if (state == "review") {
    checks.append(MakeValidationCheck("review_state", "passed",
                                      "Item is in Review."));
    checks.append(MakeValidationCheck("review_evidence", evidenceComplete ? "passed" : "failed",
                                      "Review path requires sufficient evidence."));
    return MakeGateStatus("review", "Review", evidenceComplete ? "passed" : "failed",
                          evidenceComplete ? std::vector<std::string>{}
                                           : EvidenceMissingBlockers(evidence),
                          checks, {"state", "evidence.complete", "evidence.missing"});
  }

  if (state == "done" && !evidenceComplete) {
    checks.append(MakeValidationCheck("review_done_path_evidence", "failed",
                                      "Done path lacks sufficient review evidence."));
    return MakeGateStatus("review", "Review", "failed",
                          EvidenceMissingBlockers(evidence), checks,
                          {"state", "evidence.complete", "evidence.missing"});
  }

  checks.append(MakeValidationCheck("review_applicability", "unknown",
                                    "Item is not in Review and has no failed Done review evidence path."));
  return MakeGateStatus("review", "Review", "unknown", {}, checks,
                        {"state", "evidence.complete"});
}

Json::Value BuildDoneGateStatus(const ItemRecord& item,
                                const Json::Value& evidence) {
  Json::Value checks(Json::arrayValue);
  if (item.sourceKind != "Item") {
    checks.append(MakeValidationCheck("applicable_source_kind", "unknown",
                                      "Done gate applies only to backlog item Markdown."));
    return MakeGateStatus("done", "Done", "unknown", {}, checks,
                          {"source_kind"});
  }

  const auto state = text::ToLower(item.state);
  if (state != "done") {
    checks.append(MakeValidationCheck("done_state", "unknown",
                                      "Item state is not Done."));
    return MakeGateStatus("done", "Done", "unknown", {}, checks,
                          {"state"});
  }

  const bool evidenceComplete = evidence["complete"].asBool();
  checks.append(MakeValidationCheck("done_state", "passed", "Item is in Done."));
  checks.append(MakeValidationCheck("done_evidence", evidenceComplete ? "passed" : "failed",
                                    "Done requires sufficient durable evidence."));
  return MakeGateStatus("done", "Done", evidenceComplete ? "passed" : "failed",
                        evidenceComplete ? std::vector<std::string>{}
                                         : EvidenceMissingBlockers(evidence),
                        checks, {"state", "evidence.complete", "evidence.missing"});
}

Json::Value BuildGateStatus(const ItemRecord& item) {
  const auto evidence = BuildEvidenceSummary(MakeEvidenceItem(item));
  Json::Value status(Json::objectValue);
  status["ready"] = BuildReadyGateStatus(item);
  status["review"] = BuildReviewGateStatus(item, evidence);
  status["done"] = BuildDoneGateStatus(item, evidence);
  return status;
}

Json::Value MakeReviewBundle(const Json::Value& item, const Json::Value& evidence) {
  Json::Value bundle(Json::objectValue);
  bundle["item"] = item;
  bundle["decision"] = "review";
  bundle["evidence"] = evidence;
  bundle["missing_evidence"] = evidence["missing"];
  bundle["recommended_action"] =
      evidence["complete"].asBool() ? "review_result" : "request_evidence";
  return bundle;
}

Json::Value ReviewSourceFields(std::initializer_list<const char*> fields) {
  Json::Value value(Json::arrayValue);
  for (const auto* field : fields) {
    value.append(field);
  }
  return value;
}

Json::Value ReviewLaneDefinition(const std::string& id,
                                 const std::string& title,
                                 const std::string& purpose,
                                 const std::string& inclusion,
                                 const std::string& exclusion,
                                 const std::string& emptyState,
                                 int priority) {
  Json::Value lane(Json::objectValue);
  lane["id"] = id;
  lane["title"] = title;
  lane["purpose"] = purpose;
  lane["inclusion_criteria"] = inclusion;
  lane["exclusion_criteria"] = exclusion;
  lane["empty_state"] = emptyState;
  lane["priority"] = priority;
  lane["read_only"] = true;
  return lane;
}

Json::Value ReviewAction(const std::string& id,
                         const std::string& label,
                         const std::string& humanDecision,
                         const std::string& targetState,
                         bool requiresConfirmation) {
  Json::Value action(Json::objectValue);
  action["id"] = id;
  action["label"] = label;
  action["human_decision"] = humanDecision;
  action["target_state"] = targetState;
  action["requires_confirmation"] = requiresConfirmation;
  action["starts_agent"] = false;
  action["dispatches_work"] = false;
  if (requiresConfirmation) {
    action["confirmation_label"] = "Explicit human confirmation required";
  }
  return action;
}

Json::Value ReviewActionsForLane(const std::string& lane) {
  Json::Value actions(Json::arrayValue);
  if (lane == "Ready Frontier") {
    actions.append(ReviewAction("approve_ready_boundary", "Approve Ready Boundary",
                                "approve_ready_boundary", "", false));
    actions.append(ReviewAction("defer_ready_item", "Defer Ready Item",
                                "defer_ready_item", "", false));
  } else if (lane == "Done Candidate") {
    actions.append(ReviewAction("mark_done", "Mark Done",
                                "mark_done", "Done", true));
    actions.append(ReviewAction("move_to_review", "Move to Review",
                                "move_to_review", "Review", false));
    actions.append(ReviewAction("reject_completion", "Reject Completion",
                                "reject_completion", "Review", false));
  } else if (lane == "Needs Review") {
    actions.append(ReviewAction("request_evidence", "Request Evidence",
                                "request_evidence", "", false));
    actions.append(ReviewAction("approve_for_review", "Approve For Review",
                                "approve_for_review", "", false));
    actions.append(ReviewAction("defer_review", "Defer",
                                "defer_review", "", false));
  } else if (lane == "False Done Suspect") {
    actions.append(ReviewAction("reopen_from_done", "Reopen from Done",
                                "reopen_from_done", "Review", true));
    actions.append(ReviewAction("dismiss", "Dismiss",
                                "dismiss", "", false));
    actions.append(ReviewAction("request_evidence", "Request Evidence",
                                "request_evidence", "", false));
  } else if (lane == "Evidence Gap") {
    actions.append(ReviewAction("request_evidence", "Request Evidence",
                                "request_evidence", "", false));
    actions.append(ReviewAction("accept_limitation", "Accept Limitation",
                                "accept_limitation", "", false));
    actions.append(ReviewAction("defer_evidence_gap", "Defer",
                                "defer_evidence_gap", "", false));
  } else if (lane == "Blocked/Dirty") {
    actions.append(ReviewAction("mark_blocked", "Mark Blocked",
                                "mark_blocked", "Blocked", false));
    actions.append(ReviewAction("request_cleanup", "Request Cleanup",
                                "request_cleanup", "", false));
    actions.append(ReviewAction("accept_risk", "Accept Risk",
                                "accept_risk", "", true));
  } else if (lane == "Stale/Drift") {
    actions.append(ReviewAction("revalidate_intent", "Revalidate Intent",
                                "revalidate_intent", "", false));
    actions.append(ReviewAction("drop", "Drop",
                                "drop", "Dropped", true));
    actions.append(ReviewAction("defer_stale_drift", "Defer",
                                "defer_stale_drift", "", false));
  }
  return actions;
}

struct CapabilityRouteRule {
  std::string id;
  std::string capabilityProduct;
  std::string skill;
  std::string command;
  std::string reasonCode;
  std::string reason;
  std::vector<std::string> types;
  std::vector<std::string> keywords;
};

const std::vector<CapabilityRouteRule>& CapabilityRouteRules() {
  static const std::vector<CapabilityRouteRule> rules = {
      {"native-cpp", "kano-native-cpp", "kano-cpp-expert-skill",
       "pixi run build && pixi run quick-test",
       "native_cpp_signal",
       "Native C++ or Drogon work should route to the Kano C++ skill path.",
       {"Feature", "UserStory", "Task", "Bug", "Issue"},
       {"c++", "cpp", "native", "cmake", "drogon", "msvc", "webview", "pixi"}},
      {"docs", "kano-docs", "kano-agent-backlog-skill",
       "review docs and update markdown references",
       "documentation_signal",
       "Documentation-heavy work should route to the Kano backlog/docs authoring path.",
       {"Feature", "UserStory", "Task", "Bug", "Issue"},
       {"doc", "docs", "documentation", "readme", "reference", "design note"}},
      {"git", "kano-git", "kano-git-master-skill",
       "GIT_MASTER=1 git status",
       "git_workflow_signal",
       "Git workflow work should route through the Git master path.",
       {"Feature", "UserStory", "Task", "Bug", "Issue"},
       {"git", "commit", "push", "branch", "worktree", "merge"}},
  };
  return rules;
}

bool RouteTypeMatches(const CapabilityRouteRule& rule, const std::string& type) {
  return rule.types.empty() ||
         std::find(rule.types.begin(), rule.types.end(), type) != rule.types.end();
}

std::string RouteDeclarationSourceField(const Json::Value& item,
                                        const std::string& token) {
  if (!item["external"].isObject()) {
    return "external.capability_route";
  }
  for (const auto& key : {"capability_route", "capability_routes", "agent_skill",
                          "skill", "route"}) {
    if (!item["external"].isMember(key)) {
      continue;
    }
    for (const auto& declared : SplitCsv(item["external"][key].asString())) {
      if (text::ToLower(Trim(declared)) == text::ToLower(Trim(token))) {
        return std::string("external.") + key;
      }
    }
  }
  return "external.capability_route";
}

std::string CapabilitySearchText(const Json::Value& item) {
  std::vector<std::string> parts = {
      item.get("id", "").asString(), item.get("type", "").asString(),
      item.get("title", "").asString(), item.get("state", "").asString(),
      item.get("area", "").asString(), item.get("topic", "").asString(),
      item.get("content", "").asString()};
  for (const auto& tag : item["tags"]) {
    parts.push_back(tag.asString());
  }
  if (item["external"].isObject()) {
    for (const auto& key : item["external"].getMemberNames()) {
      parts.push_back(key);
      parts.push_back(item["external"][key].asString());
    }
  }
  return JoinStrings(parts, "\n");
}

std::vector<std::string> CapabilityRouteDeclarations(const Json::Value& item) {
  std::vector<std::string> declarations;
  if (!item["external"].isObject()) {
    return declarations;
  }
  for (const auto& key : {"capability_route", "capability_routes", "agent_skill",
                          "skill", "route"}) {
    if (!item["external"].isMember(key)) {
      continue;
    }
    for (const auto& token : SplitCsv(item["external"][key].asString())) {
      AppendUnique(declarations, token);
    }
  }
  return declarations;
}

std::optional<CapabilityRouteRule> CapabilityRuleForToken(const std::string& token) {
  const auto normalized = text::ToLower(Trim(token));
  if (normalized.empty()) {
    return std::nullopt;
  }
  for (const auto& rule : CapabilityRouteRules()) {
    if (text::ToLower(rule.id) == normalized ||
        text::ToLower(rule.skill) == normalized ||
        text::ToLower(rule.capabilityProduct) == normalized) {
      return rule;
    }
  }
  return std::nullopt;
}

bool IsCommonRoutableType(const std::string& type) {
  static const std::vector<std::string> types = {
      "Initiative", "Epic", "Feature", "UserStory", "Task", "Bug", "Issue"};
  return std::find(types.begin(), types.end(), type) != types.end();
}

Json::Value CapabilityRouteToJson(const CapabilityRouteRule& rule,
                                  const Json::Value& item,
                                  const std::string& confidence,
                                  const std::string& reason,
                                  const std::vector<std::string>& sourceFields) {
  Json::Value route(Json::objectValue);
  route["id"] = rule.id;
  route["product"] = item.get("product", "").asString();
  route["item_id"] = item.get("id", "").asString();
  route["capability_product"] = rule.capabilityProduct;
  route["skill"] = rule.skill;
  route["command"] = rule.command;
  route["confidence"] = confidence;
  route["reason"] = reason.empty() ? rule.reason : reason;
  route["reason_code"] = rule.reasonCode;
  route["source_fields"] = MakeArray(sourceFields);
  route["read_only"] = true;
  route["mutation_allowed"] = false;
  route["starts_agent"] = false;
  route["dispatches_work"] = false;
  return route;
}

Json::Value FallbackCapabilityRoute(const Json::Value& item) {
  CapabilityRouteRule fallback{
      "kob-workitem", "kano-agent-backlog-skill", "kano-agent-backlog-skill",
      "scripts/kob item read --product " + item.get("product", "").asString() +
          " --item " + item.get("id", "").asString(),
      "common_item_type_fallback",
      "No explicit capability metadata matched; use the native KOB work-item review path.",
      {}, {}};
  return CapabilityRouteToJson(
      fallback, item, "medium", fallback.reason,
      {"type", "product", "item_id", "external.capability_route"});
}

Json::Value CapabilityWarning(const std::string& code,
                              const std::string& message,
                              const std::vector<std::string>& sourceFields) {
  Json::Value warning(Json::objectValue);
  warning["code"] = code;
  warning["message"] = message;
  warning["source_fields"] = MakeArray(sourceFields);
  return warning;
}

std::optional<Json::Value> FindReviewAction(const std::string& lane,
                                            const std::string& humanDecision) {
  for (const auto& action : ReviewActionsForLane(lane)) {
    if (action.get("human_decision", "").asString() == humanDecision) {
      return action;
    }
  }
  return std::nullopt;
}

const std::vector<std::string>& ReviewLaneOrder() {
  static const std::vector<std::string> order = {
      "Needs Review", "Done Candidate", "False Done Suspect",
      "Evidence Gap", "Blocked/Dirty", "Stale/Drift", "Ready Frontier"};
  return order;
}

Json::Value ReviewLaneTaxonomy() {
  Json::Value lanes(Json::arrayValue);
  lanes.append(ReviewLaneDefinition(
      "needs-review", "Needs Review",
      "Items that require a human result or evidence decision.",
      "state=Review, rejected/needs-fix marker, or explicit review reason.",
      "Items already closed with sufficient evidence unless a false-done signal exists.",
      "No items currently need human review.", 10));
  lanes.append(ReviewLaneDefinition(
      "done-candidate", "Done Candidate",
      "In-progress items with validation evidence that may be ready for review.",
      "state=InProgress and validation evidence signal is present.",
      "Items without validation evidence or already in Review/Done.",
      "No in-progress items look ready for completion review.", 20));
  lanes.append(ReviewLaneDefinition(
      "false-done-suspect", "False Done Suspect",
      "Done items whose evidence chain is incomplete.",
      "state=Done and evidence summary is incomplete.",
      "Done items with enough durable evidence signals.",
      "No Done items look under-evidenced.", 30));
  lanes.append(ReviewLaneDefinition(
      "evidence-gap", "Evidence Gap",
      "Reviewable items missing durable validation, artifact, or worklog evidence.",
      "state in Ready/Review/Done and evidence summary is incomplete.",
      "Items outside reviewable states unless another lane explains them.",
      "No reviewable items have obvious evidence gaps.", 40));
  lanes.append(ReviewLaneDefinition(
      "blocked-dirty", "Blocked/Dirty",
      "Items blocked by humans or by dirty/uncommitted work signals.",
      "state=Blocked or item text mentions dirty/uncommitted work.",
      "Ordinary Ready/Review items without blocker or dirty-work evidence.",
      "No blocker or dirty-work items are visible.", 50));
  lanes.append(ReviewLaneDefinition(
      "stale-drift", "Stale/Drift",
      "Items whose text signals stale intent, drift, timeout, or freshness risk.",
      "Item id, title, or body mentions stale, drift, outdated, timeout, or timed out.",
      "Fresh items with no drift/freshness terms.",
      "No stale or drift candidates are visible.", 60));
  lanes.append(ReviewLaneDefinition(
      "ready-frontier", "Ready Frontier",
      "Ready items waiting for human approval, dispatch, or prioritization.",
      "state=Ready.",
      "Items already in execution, review, blocked, done, or dropped.",
      "No Ready items are waiting at the frontier.", 70));
  return lanes;
}

Json::Value MakeReviewQueueBundle(
    const Json::Value& bundle,
    const std::string& queue,
    const std::string& reasonCode,
    const std::string& reason,
    const Json::Value& sourceFields,
    const std::string& suggestedDecision,
    const std::string& diagnosticStatus = "deterministic",
    const std::string& confidence = "high") {
  Json::Value out = bundle;
  out["review_queue"] = queue;
  out["reason_code"] = reasonCode;
  out["review_reason"] = reason;
  out["reason_text"] = reason;
  out["source_fields"] = sourceFields;
  out["confidence"] = confidence;
  out["diagnostic_status"] = diagnosticStatus;
  out["suggested_decision"] = suggestedDecision;
  out["suggested_human_decision"] = suggestedDecision;
  out["actions"] = ReviewActionsForLane(queue);
  return out;
}

bool ItemMatchesTopic(const Json::Value& item, const std::string& topic) {
  if (topic.empty()) {
    return true;
  }
  return JsonStringContains(item["topic"], topic) ||
         JsonStringContains(item["id"], topic) ||
         JsonStringContains(item["title"], topic);
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
    return NormalizeItemTypeName(declaredType);
  }

  const auto parent = itemPath.parent_path().parent_path().filename().string();
  if (parent == "initiative") {
    return "Initiative";
  }
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
  if (parent == "issue") {
    return "Issue";
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
  item.priority = map["priority"];
  item.parent = map["parent"];
  item.owner = map["owner"];
  item.area = map["area"];
  item.iteration = map["iteration"];
  item.created = map["created"];
  item.updated = map["updated"];
  item.tags = ExtractFrontmatterList(content, "tags");
  item.decisions = ExtractFrontmatterList(content, "decisions");
  item.relates = ExtractFrontmatterList(content, "relates");
  item.blocks = ExtractFrontmatterList(content, "blocks");
  item.blockedBy = ExtractFrontmatterList(content, "blocked_by");
  item.external = ExtractFrontmatterMap(content, "external");

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
  value["priority"] = item.priority;
  value["parent"] = item.parent;
  value["owner"] = item.owner;
  value["area"] = item.area;
  value["iteration"] = item.iteration;
  value["topic"] = item.topic.empty() ? Json::Value(Json::nullValue)
                                      : Json::Value(item.topic);
  value["created"] = item.created;
  value["updated"] = item.updated;
  value["path"] = item.relativePath;
  value["valid"] = item.valid;
  value["gate_status"] = BuildGateStatus(item);
  value["tags"] = MakeArray(item.tags);
  value["decisions"] = MakeArray(item.decisions);
  value["external"] = Json::objectValue;
  for (const auto& [key, entry] : item.external) {
    value["external"][key] = entry;
  }
  value["links"]["relates"] = Json::arrayValue;
  value["links"]["blocks"] = Json::arrayValue;
  value["links"]["blocked_by"] = Json::arrayValue;
  for (const auto& ref : item.relates) {
    value["links"]["relates"].append(ref);
  }
  for (const auto& ref : item.blocks) {
    value["links"]["blocks"].append(ref);
  }
  for (const auto& ref : item.blockedBy) {
    value["links"]["blocked_by"].append(ref);
  }
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

Json::Value BacklogWebviewService::RecommendCapabilityRoute(
    const std::string& product,
    const std::string& itemId,
    bool forceRefresh) {
  if (Trim(itemId).empty()) {
    return MakeError("capability_route.item_required",
                     "pass item or item_id for capability route lookup");
  }

  auto detail = GetItem(product.empty() ? "all" : product, itemId, forceRefresh);
  if (detail.isMember("error")) {
    return detail;
  }

  const auto item = detail["item"];
  const auto itemType = item.get("type", "").asString();
  const auto declarations = CapabilityRouteDeclarations(item);
  Json::Value response(Json::objectValue);
  response["product"] = item.get("product", product).asString();
  response["item_id"] = item.get("id", itemId).asString();
  response["item_type"] = itemType;
  response["item_title"] = item.get("title", "").asString();
  response["status"] = "no_route";
  response["route"] = Json::Value(Json::nullValue);
  response["alternatives"] = Json::arrayValue;
  response["warnings"] = Json::arrayValue;
  response["capability_declarations"] = MakeArray(declarations);
  response["missing_capability_metadata"] = declarations.empty();
  response["ambiguous_capability_data"] = false;
  response["read_only"] = true;
  response["mutation_allowed"] = false;
  response["starts_agent"] = false;
  response["dispatches_work"] = false;

  for (const auto& declaration : declarations) {
    const auto rule = CapabilityRuleForToken(declaration);
    const auto sourceField = RouteDeclarationSourceField(item, declaration);
    if (!rule.has_value()) {
      response["warnings"].append(CapabilityWarning(
          "capability_route.unknown_declaration",
          "Capability declaration does not match a known deterministic route: " + declaration,
          {sourceField}));
      continue;
    }
    if (!RouteTypeMatches(*rule, itemType)) {
      response["warnings"].append(CapabilityWarning(
          "capability_route.type_mismatch",
          "Capability declaration " + declaration + " does not support item type " + itemType,
          {sourceField, "type"}));
      continue;
    }
    response["alternatives"].append(CapabilityRouteToJson(
        *rule, item, "high",
        "Explicit capability metadata selects " + rule->skill + ".",
        {sourceField, "type"}));
  }

  if (response["alternatives"].empty() && declarations.empty()) {
    const auto searchable = CapabilitySearchText(item);
    for (const auto& rule : CapabilityRouteRules()) {
      if (!RouteTypeMatches(rule, itemType)) {
        continue;
      }
      if (ContentContainsAny(searchable, rule.keywords)) {
        response["alternatives"].append(CapabilityRouteToJson(
            rule, item, "medium",
            "Deterministic keyword signals match " + rule.skill + ".",
            {"title", "content", "tags", "external", "type"}));
      }
    }
  }

  if (response["alternatives"].size() == 1) {
    response["route"] = response["alternatives"][0];
    response["status"] = "routed";
    return response;
  }

  if (response["alternatives"].size() > 1) {
    response["status"] = "ambiguous";
    response["ambiguous_capability_data"] = true;
    response["warnings"].append(CapabilityWarning(
        "capability_route.ambiguous",
        "Multiple deterministic capability routes match; choose one before starting work.",
        declarations.empty() ? std::vector<std::string>{"title", "content", "tags", "external", "type"}
                             : std::vector<std::string>{"external.capability_route", "type"}));
    return response;
  }

  if (IsCommonRoutableType(itemType)) {
    response["status"] = "fallback";
    response["route"] = FallbackCapabilityRoute(item);
    response["warnings"].append(CapabilityWarning(
        "capability_route.missing_metadata",
        "No capability metadata matched this common item type; using the native KOB work-item fallback.",
        {"type", "product", "item_id", "external.capability_route"}));
    return response;
  }

  response["warnings"].append(CapabilityWarning(
      "capability_route.no_route",
      "No deterministic capability route matched this item type or metadata.",
      {"type", "external.capability_route"}));
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
    if (type != "Initiative" && type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Issue" && type != "Theme") {
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
    if (type != "Initiative" && type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Issue" && type != "Theme") {
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
    if (type != "Initiative" && type != "Epic" && type != "Feature" && type != "UserStory" &&
        type != "Task" && type != "Bug" && type != "Issue" && type != "Theme") {
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

Json::Value BacklogWebviewService::ListSavedViews() {
  Json::Value response(Json::objectValue);
  response["views"] = Json::arrayValue;
  for (const auto& view : SavedViews()) {
    response["views"].append(SavedViewToJson(view));
  }
  response["read_only"] = false;
  response["review_decisions_enabled"] = true;
  return response;
}

Json::Value BacklogWebviewService::RunKobql(const std::string& query,
                                            const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["kobql"] = Trim(query);
  response["read_only"] = true;
  response["unsupported_tokens"] = Json::arrayValue;

  ItemQueryOptions parsed = options;
  if (parsed.limit == 0) {
    parsed.limit = 200;
  }
  std::vector<std::string> topicFilters;
  std::vector<std::string> textFilters;

  for (const auto& token : TokenizeQuery(query)) {
    const auto separator = token.find(':');
    if (separator == std::string::npos) {
      response["unsupported_tokens"].append(token);
      continue;
    }
    const auto key = text::ToLower(Trim(token.substr(0, separator)));
    const auto value = Trim(token.substr(separator + 1));
    if (value.empty()) {
      response["unsupported_tokens"].append(token);
      continue;
    }
    if (key == "product") {
      parsed.products.push_back(value);
    } else if (key == "state") {
      parsed.states.push_back(value);
    } else if (key == "type") {
      parsed.types.push_back(value);
    } else if (key == "text" || key == "q") {
      textFilters.push_back(value);
    } else if (key == "topic") {
      topicFilters.push_back(value);
    } else {
      response["unsupported_tokens"].append(token);
    }
  }

  if (!response["unsupported_tokens"].empty()) {
    response["error"] = "Unsupported KOBQL token";
    return response;
  }

  if (!textFilters.empty()) {
    parsed.text = JoinStrings(textFilters, " ");
  }

  auto items = QueryItems(parsed);
  if (items.isMember("error")) {
    return items;
  }

  if (!topicFilters.empty()) {
    Json::Value filtered(Json::arrayValue);
    for (const auto& item : items["items"]) {
      bool matches = true;
      for (const auto& topic : topicFilters) {
        if (!ItemMatchesTopic(item, topic)) {
          matches = false;
          break;
        }
      }
      if (matches) {
        filtered.append(item);
      }
    }
    items["items"] = filtered;
    items["total"] = static_cast<Json::UInt64>(filtered.size());
  }

  response["query"] = items;
  response["total"] = items["total"];
  response["items"] = items["items"];
  return response;
}

Json::Value BacklogWebviewService::RunSavedView(const std::string& viewId,
                                                const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  auto normalized = text::ToLower(Trim(viewId));
  if (normalized == "ready-approval") {
    normalized = "ready-frontier";
  } else if (normalized == "result-review") {
    normalized = "needs-review";
  } else if (normalized == "blocked") {
    normalized = "blocked-dirty";
  } else if (normalized == "stale-inprogress") {
    normalized = "stale-drift";
  } else if (normalized == "missing-evidence") {
    normalized = "evidence-gap";
  }
  const auto* selected = static_cast<const SavedViewDefinition*>(nullptr);
  for (const auto& view : SavedViews()) {
    if (view.id == normalized) {
      selected = &view;
      break;
    }
  }
  if (selected == nullptr) {
    response["error"] = "Saved view not found";
    return response;
  }

  response["view"] = SavedViewToJson(*selected);
  auto result = RunKobql(selected->kobql, options);
  if (selected->id == "evidence-gap" && !result.isMember("error")) {
    Json::Value filtered(Json::arrayValue);
    for (const auto& item : result["items"]) {
      auto detail = GetEvidenceDetail(item["product"].asString(), item["id"].asString());
      if (!detail["evidence"]["complete"].asBool()) {
        filtered.append(item);
      }
    }
    result["items"] = filtered;
    result["total"] = static_cast<Json::UInt64>(filtered.size());
  }
  response["result"] = result;
  return response;
}

Json::Value BacklogWebviewService::PreviewCommand(const std::string& phrase,
                                                  const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  const auto input = Trim(phrase);
  const auto lowered = text::ToLower(input);
  response["phrase"] = input;
  response["read_only"] = true;
  response["mutation_allowed"] = false;

  if (input.empty()) {
    response["error"] = "Missing command phrase";
    return response;
  }

  std::string kobql;
  if (ContentContainsAny(lowered, {"ready"})) {
    kobql += "state:Ready ";
  }
  if (ContentContainsAny(lowered, {"review"})) {
    kobql += "state:Review ";
  }
  if (ContentContainsAny(lowered, {"blocked", "blocker"})) {
    kobql += "state:Blocked ";
  }
  if (ContentContainsAny(lowered, {"in progress", "active", "running"})) {
    kobql += "state:InProgress ";
  }
  if (ContentContainsAny(lowered, {"done", "closed", "complete"})) {
    kobql += "state:Done ";
  }
  if (ContentContainsAny(lowered, {"task", "tasks"})) {
    kobql += "type:Task ";
  }
  if (ContentContainsAny(lowered, {"bug", "bugs"})) {
    kobql += "type:Bug ";
  }
  if (ContentContainsAny(lowered, {"issue", "issues", "triage"})) {
    kobql += "type:Issue ";
  }
  if (ContentContainsAny(lowered, {"feature", "features"})) {
    kobql += "type:Feature ";
  }
  if (ContentContainsAny(lowered, {"topic", "topics"})) {
    kobql += "type:Topic ";
  }

  if (kobql.empty()) {
    response["error"] = "Unsupported command phrase";
    return response;
  }

  response["generated_kobql"] = Trim(kobql);
  response["preview"] = RunKobql(kobql, options);
  return response;
}

Json::Value BacklogWebviewService::BuildReviewInbox(const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["lanes"] = Json::objectValue;
  response["lane_taxonomy"] = ReviewLaneTaxonomy();
  response["lane_order"] = Json::arrayValue;
  for (const auto& lane : ReviewLaneOrder()) {
    response["lane_order"].append(lane);
  }
  const auto& lanes = ReviewLaneOrder();
  for (const auto& lane : lanes) {
    response["lanes"][lane] = Json::arrayValue;
  }
  response["read_only"] = true;

  auto items = QueryItems(options);
  if (items.isMember("error")) {
    return items;
  }

  for (const auto& item : items["items"]) {
    const auto detail = GetEvidenceDetail(item["product"].asString(), item["id"].asString());
    const auto evidence = detail["evidence"];
    const auto state = text::ToLower(item["state"].asString());
    auto bundle = MakeReviewBundle(item, evidence);
    const auto searchable = item["id"].asString() + "\n" + item["title"].asString() + "\n" +
                            item["content"].asString();

    auto appendQueue = [&](const std::string& lane,
                           const std::string& reasonCode,
                           const std::string& reason,
                           const Json::Value& sourceFields,
                           const std::string& suggestedDecision,
                           const std::string& diagnosticStatus = "deterministic",
                           const std::string& confidence = "high") {
      auto queued = MakeReviewQueueBundle(
          bundle, lane, reasonCode, reason, sourceFields, suggestedDecision,
          diagnosticStatus, confidence);
      const auto itemProduct = item["product"].asString();
      queued["review_draft"] = LoadReviewDecisionDraft(
          ProductRoot(itemProduct), itemProduct, item["id"].asString(),
          kDefaultReviewActorAlias);
      response["lanes"][lane].append(queued);
    };

    if (state == "ready") {
      appendQueue("Ready Frontier", "ready_frontier_candidate",
                  "Ready item is waiting for human approval, dispatch, or prioritization.",
                  ReviewSourceFields({"state"}), "approve_dispatch_or_defer");
    }
    if (state == "review") {
      appendQueue("Needs Review", "review_state",
                  "Item is in Review and needs a human result or evidence decision.",
                  ReviewSourceFields({"state"}), "accept_reject_or_request_evidence");
    }
    if (state == "inprogress" && evidence["signals"]["validation"].asBool()) {
      appendQueue("Done Candidate", "validation_seen_in_progress",
                  "In-progress item has validation evidence and may be ready to move to Review.",
                  ReviewSourceFields({"state", "evidence.signals.validation"}),
                  "review_for_completion");
    }
    if (state == "blocked") {
      appendQueue("Blocked/Dirty", "blocked_state",
                  "Blocked item needs a human unblock decision or cleanup follow-up.",
                  ReviewSourceFields({"state"}), "unblock_defer_or_split_followup");
    }
    if (!evidence["complete"].asBool() &&
        (state == "ready" || state == "review" || state == "done")) {
      appendQueue("Evidence Gap", "evidence_missing",
                  "Reviewable item is missing durable validation, artifact, or worklog evidence.",
                  ReviewSourceFields({"state", "evidence.complete", "evidence.missing"}),
                  "request_evidence_or_reopen_scope");
    }
    if (state == "done" && !evidence["complete"].asBool()) {
      appendQueue("False Done Suspect", "false_done_evidence_gap",
                  "Done item is missing enough evidence to trust the completed state.",
                  ReviewSourceFields({"state", "evidence.complete", "evidence.missing"}),
                  "reopen_or_accept_risk");
    }
    if (ContentContainsAny(searchable, {"stale", "drift", "outdated", "timeout", "timed out"})) {
      appendQueue("Stale/Drift", "stale_or_drift_signal",
                  "Item text signals stale state, drift, timeout, or freshness risk.",
                  ReviewSourceFields({"id", "title", "content"}), "refresh_intent_or_split_followup",
                  "keyword-signal", "medium");
    }
    if (state != "blocked" &&
        ContentContainsAny(searchable, {"dirty", "uncommitted", "git status", "worktree"})) {
      appendQueue("Blocked/Dirty", "dirty_work_signal",
                  "Item text mentions dirty or uncommitted work that needs reconciliation.",
                  ReviewSourceFields({"title", "content"}), "reconcile_worktree_before_done",
                  "keyword-signal", "medium");
    }
    if (ContentContainsAny(item["title"].asString() + "\n" + item["id"].asString(),
                           {"rejected", "needs fix"})) {
      if (state != "review") {
        appendQueue("Needs Review", "rejected_or_needs_fix",
                    "Rejected or needs-fix item requires a human correction decision.",
                    ReviewSourceFields({"id", "title"}), "decide_fix_reopen_or_drop",
                    "keyword-signal", "medium");
      }
    }
  }

  response["counts"] = Json::objectValue;
  for (const auto& lane : lanes) {
    response["counts"][lane] =
        static_cast<Json::UInt64>(response["lanes"][lane].size());
  }
  return response;
}

Json::Value BacklogWebviewService::SaveReviewDecisionDraft(
    const Json::Value& request) {
  try {
    const auto product = JsonStringField(request, "product");
    const auto itemId = JsonStringField(request, "item_id");
    const auto actorAlias = JsonStringField(request, "actor_alias");
    const auto lane = JsonStringField(request, "lane");
    const auto reasonCode = JsonStringField(request, "reason_code");
    if (product.empty() || product == "all" || !IsValidProductName(product)) {
      return MakeError("review_decision.product_required", "pass a single valid product for review decision drafts");
    }
    if (itemId.empty()) {
      return MakeError("review_decision.item_required", "pass item_id for review decision drafts");
    }
    if (actorAlias.empty()) {
      return MakeError("review_decision.actor_required", "pass actor_alias for review decision drafts");
    }
    if (lane.empty() || reasonCode.empty()) {
      return MakeError("review_decision.context_required", "pass lane and reason_code for review decision drafts");
    }

    auto item = GetItem(product, itemId, true);
    if (item.isMember("error")) {
      return item;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowText = ReviewTimestampText(now);
    const auto productRoot = ProductRoot(product);
    const auto draftPath = ReviewDecisionDraftPath(productRoot, itemId, actorAlias);

    Json::Value existing(Json::objectValue);
    const bool hadExisting = ReadJsonFile(draftPath, existing) && existing.isObject();

    Json::Value record(Json::objectValue);
    record["status"] = "draft";
    record["product"] = product;
    record["item_id"] = itemId;
    record["lane"] = lane;
    record["reason_code"] = reasonCode;
    auto suggestedDecision = JsonStringField(request, "suggested_decision");
    if (suggestedDecision.empty()) {
      suggestedDecision = JsonStringField(request, "suggested_human_decision");
    }
    record["suggested_decision"] = suggestedDecision;
    record["human_decision"] = JsonStringField(request, "human_decision");
    record["rationale"] = JsonStringField(request, "rationale");
    record["actor_alias"] = actorAlias;
    record["target_state"] = JsonStringField(request, "target_state");
    record["source_detector"] = JsonStringField(request, "source_detector").empty()
        ? "backboard-review-inbox"
        : JsonStringField(request, "source_detector");
    record["supersedes"] = JsonStringField(request, "supersedes");
    record["empty"] = record["rationale"].asString().empty();
    record["draft_created_at"] = hadExisting && existing.isMember("draft_created_at")
        ? existing["draft_created_at"].asString()
        : nowText;
    record["draft_updated_at"] = nowText;
    record["agent_started"] = false;
    record["dispatch_started"] = false;

    WriteJsonFile(draftPath, record);

    Json::Value out(Json::objectValue);
    out["status"] = "draft";
    out["record"] = record;
    out["path"] = std::filesystem::relative(draftPath, productRoot).generic_string();
    out["updated_existing"] = hadExisting;
    out["empty"] = record["empty"].asBool();
    out["agent_started"] = false;
    out["dispatch_started"] = false;
    return out;
  } catch (const std::exception& e) {
    return MakeError("review_decision.draft_failed", e.what());
  }
}

Json::Value BacklogWebviewService::DiscardReviewDecisionDraft(
    const Json::Value& request) {
  try {
    const auto product = JsonStringField(request, "product");
    const auto itemId = JsonStringField(request, "item_id");
    const auto actorAlias = JsonStringField(request, "actor_alias");
    if (product.empty() || product == "all" || !IsValidProductName(product)) {
      return MakeError("review_decision.product_required", "pass a single valid product for discarded review decision drafts");
    }
    if (itemId.empty()) {
      return MakeError("review_decision.item_required", "pass item_id for discarded review decision drafts");
    }
    if (actorAlias.empty()) {
      return MakeError("review_decision.actor_required", "pass actor_alias for discarded review decision drafts");
    }

    auto item = GetItem(product, itemId, true);
    if (item.isMember("error")) {
      return item;
    }

    const auto productRoot = ProductRoot(product);
    const auto draftPath = ReviewDecisionDraftPath(productRoot, itemId, actorAlias);
    const bool hadExisting = std::filesystem::exists(draftPath);
    if (hadExisting) {
      std::filesystem::remove(draftPath);
    }

    Json::Value out(Json::objectValue);
    out["status"] = "discarded";
    out["discarded_existing"] = hadExisting;
    out["path"] = std::filesystem::relative(draftPath, productRoot).generic_string();
    out["record"] = MissingReviewDecisionDraft(product, itemId, actorAlias);
    out["agent_started"] = false;
    out["dispatch_started"] = false;
    return out;
  } catch (const std::exception& e) {
    return MakeError("review_decision.discard_failed", e.what());
  }
}

Json::Value BacklogWebviewService::SubmitReviewDecision(
    const Json::Value& request) {
  try {
    const auto product = JsonStringField(request, "product");
    const auto itemId = JsonStringField(request, "item_id");
    const auto actorAlias = JsonStringField(request, "actor_alias");
    const auto lane = JsonStringField(request, "lane");
    const auto reasonCode = JsonStringField(request, "reason_code");
    const auto humanDecision = JsonStringField(request, "human_decision");
    if (product.empty() || product == "all" || !IsValidProductName(product)) {
      return MakeError("review_decision.product_required", "pass a single valid product for submitted review decisions");
    }
    if (itemId.empty()) {
      return MakeError("review_decision.item_required", "pass item_id for submitted review decisions");
    }
    if (actorAlias.empty()) {
      return MakeError("review_decision.actor_required", "pass actor_alias for submitted review decisions");
    }
    if (lane.empty() || reasonCode.empty() || humanDecision.empty()) {
      return MakeError("review_decision.record_required", "pass lane, reason_code, and human_decision before submitting review decisions");
    }

    auto itemDetail = GetItem(product, itemId, true);
    if (itemDetail.isMember("error")) {
      return itemDetail;
    }
    const auto item = itemDetail["item"];
    const auto requestedAction = FindReviewAction(lane, humanDecision);
    if (!requestedAction) {
      return MakeError("review_decision.action_invalid", "human_decision is not valid for the requested review lane");
    }
    const auto actionTargetState = JsonStringField(*requestedAction, "target_state");
    const auto requestedTargetState = JsonStringField(request, "target_state");
    if (!requestedTargetState.empty() && requestedTargetState != actionTargetState) {
      return MakeError("review_decision.target_state_mismatch", "target_state does not match the selected review lane action");
    }
    const auto targetState = actionTargetState;
    const auto humanDecisionLower = text::ToLower(humanDecision);
    const auto targetStateLower = text::ToLower(targetState);
    const auto currentStateLower = text::ToLower(item.get("state", "").asString());
    const bool highRisk = JsonBoolField(*requestedAction, "requires_confirmation") ||
        targetStateLower == "done" || targetStateLower == "dropped" ||
        humanDecisionLower.find("accept_evidence_risk") != std::string::npos ||
        humanDecisionLower.find("accept_risk") != std::string::npos ||
        humanDecisionLower.find("accept risk") != std::string::npos ||
        humanDecisionLower.find("reopen") != std::string::npos ||
        (currentStateLower == "done" && !targetState.empty() && targetStateLower != "done");
    if (highRisk && !JsonBoolField(request, "confirmed")) {
      auto error = MakeError("review_decision.confirmation_required", "explicit confirmation is required before submitting this high-risk review action");
      Json::Value transition(Json::objectValue);
      transition["attempted"] = false;
      transition["applied"] = false;
      transition["parent_synced"] = false;
      transition["dashboards_refreshed"] = false;
      transition["outcome"] = "pending_confirmation";
      transition["diagnostics"] = Json::arrayValue;
      transition["diagnostics"].append("explicit confirmation is required before transition policy can run");
      error["transition"] = transition;
      return error;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowText = ReviewTimestampText(now);
    const auto productRoot = ProductRoot(product);
    const auto submittedDir = productRoot / "_meta" / "review-decisions" /
        "submitted" / SafeFileToken(itemId);
    const auto recordPath = UniqueJsonPath(submittedDir /
        (ReviewTimestampFileToken(now) + "-" + SafeFileToken(humanDecision) + ".json"));

    Json::Value transition(Json::objectValue);
    transition["attempted"] = false;
    transition["applied"] = false;
    transition["parent_synced"] = false;
    transition["dashboards_refreshed"] = false;
    transition["outcome"] = "skipped";
    transition["diagnostics"] = Json::arrayValue;
    std::optional<kano::backlog_core::BacklogItem> transitionItem;
    if (!targetState.empty()) {
      auto parsedState = kano::backlog_core::parse_item_state(targetState);
      if (!parsedState) {
        return MakeError("review_decision.target_state_invalid", "target_state is not a recognized KOB item state: " + targetState);
      }
      auto action = ReviewActionForTargetState(*parsedState);
      if (!action) {
        return MakeError("review_decision.target_state_invalid", "target_state cannot be requested by a review decision: " + targetState);
      }
      transition["attempted"] = true;
      transition["outcome"] = "blocked";
      auto currentState = kano::backlog_core::parse_item_state(item.get("state", "").asString());
      if (!currentState) {
        return MakeError("review_decision.current_state_invalid", "current item state is not a recognized KOB item state");
      }
      transitionItem = kano::backlog_core::BacklogItem{};
      transitionItem->id = itemId;
      transitionItem->state = *currentState;
      transitionItem->updated = item.get("updated", "").asString();
      const auto oldState = transitionItem->state;
      transition["old_state"] = kano::backlog_core::to_string(oldState);
      const auto message = "Human review decision: " + humanDecision +
          (JsonStringField(request, "rationale").empty() ? "" : "; " + JsonStringField(request, "rationale"));
      try {
        kano::backlog_core::StateMachine::transition(
            *transitionItem, *action, std::optional<std::string>(actorAlias),
            std::optional<std::string>(message));
        transition["policy_status"] = "accepted";
        transition["new_state"] = kano::backlog_core::to_string(transitionItem->state);
      } catch (const std::exception& e) {
        transitionItem = std::nullopt;
        transition["policy_status"] = "rejected";
        transition["new_state"] = targetState;
        transition["error"] = e.what();
        transition["diagnostics"].append(e.what());
      }
    }

    Json::Value record(Json::objectValue);
    record["status"] = "submitted";
    record["product"] = product;
    record["item_id"] = itemId;
    record["lane"] = lane;
    record["reason_code"] = reasonCode;
    auto suggestedDecision = JsonStringField(request, "suggested_decision");
    if (suggestedDecision.empty()) {
      suggestedDecision = JsonStringField(request, "suggested_human_decision");
    }
    record["suggested_decision"] = suggestedDecision;
    record["human_decision"] = humanDecision;
    record["rationale"] = JsonStringField(request, "rationale");
    record["actor_alias"] = actorAlias;
    record["target_state"] = targetState;
    record["created_at"] = nowText;
    record["submitted_at"] = nowText;
    record["source_detector"] = JsonStringField(request, "source_detector").empty()
        ? "backboard-review-inbox"
        : JsonStringField(request, "source_detector");
    record["supersedes"] = JsonStringField(request, "supersedes");
    record["confirmed"] = JsonBoolField(request, "confirmed");
    record["high_risk"] = highRisk;
    record["transition"] = transition;
    record["agent_started"] = false;
    record["dispatch_started"] = false;

    WriteJsonFile(recordPath, record);

    if (transitionItem) {
      ApplyTransitionToMarkdown(productRoot, item, *transitionItem);
      transition["applied"] = true;
      transition["outcome"] = "applied";
      record["transition"] = transition;
      WriteJsonFile(recordPath, record);
      cacheByProduct.erase(product);
    }

    Json::Value out(Json::objectValue);
    out["status"] = "submitted";
    out["record"] = record;
    out["path"] = std::filesystem::relative(recordPath, productRoot).generic_string();
    out["agent_started"] = false;
    out["dispatch_started"] = false;
    return out;
  } catch (const std::exception& e) {
    return MakeError("review_decision.submit_failed", e.what());
  }
}

Json::Value BacklogWebviewService::GetEvidenceDetail(const std::string& product,
                                                      const std::string& id,
                                                     bool forceRefresh) {
  Json::Value response(Json::objectValue);
  auto detail = GetItem(product, id, forceRefresh);
  if (detail.isMember("error")) {
    return detail;
  }
  const auto item = detail["item"];
  response["item"] = item;
  response["evidence"] = BuildEvidenceSummary(item);
  response["review_decision_history"] = BuildReviewDecisionHistory(
      ProductRoot(item.get("product", product).asString()),
      item.get("product", product).asString(), item.get("id", id).asString());
  response["worklog_events"] = Json::arrayValue;
  for (const auto& line : ExtractWorklogLines(item.get("content", "").asString())) {
    Json::Value event(Json::objectValue);
    event["timestamp"] = ExtractTimestamp(line);
    event["agent"] = ExtractAgent(line);
    event["kind"] = InferEventKind(line);
    event["text"] = line;
    response["worklog_events"].append(event);
  }
  return response;
}

Json::Value BacklogWebviewService::BuildTopicHome(const std::string& topic,
                                                  const ItemQueryOptions& options) {
  Json::Value response(Json::objectValue);
  response["topic"] = Trim(topic);
  response["topics"] = Json::arrayValue;
  response["items"] = Json::arrayValue;
  response["counts_by_state"] = Json::objectValue;
  response["read_only"] = true;

  auto items = QueryItems(options);
  if (items.isMember("error")) {
    return items;
  }
  const auto selectedTopic = Trim(topic);
  for (const auto& item : items["items"]) {
    if (item["type"].asString() == "Topic") {
      if (selectedTopic.empty() || ItemMatchesTopic(item, selectedTopic)) {
        response["topics"].append(item);
      }
      continue;
    }
    if (!selectedTopic.empty() && !ItemMatchesTopic(item, selectedTopic)) {
      continue;
    }
    response["items"].append(item);
    const auto state = item["state"].asString().empty() ? "Unknown" : item["state"].asString();
    response["counts_by_state"][state] =
        response["counts_by_state"].get(state, 0).asUInt64() + 1U;
  }
  response["missing_topic_metadata"] =
      !selectedTopic.empty() && response["topics"].empty();
  return response;
}

Json::Value BacklogWebviewService::BuildDependencyGraph(const ItemQueryOptions& options,
                                                        const std::string& itemId,
                                                        const std::string& topic) {
  Json::Value response(Json::objectValue);
  response["nodes"] = Json::arrayValue;
  response["edges"] = Json::arrayValue;
  response["missing_nodes"] = Json::arrayValue;
  response["invalid_refs"] = Json::arrayValue;
  response["dependency_cycles"] = Json::arrayValue;
  response["read_only"] = true;
  response["edge_list_debug"] = true;
  response["visualization"]["kind"] = "first-party-svg";
  response["visualization"]["layout"] = "deterministic-layered";

  auto items = QueryItems(options);
  if (items.isMember("error")) {
    return items;
  }

  const size_t maxNodes = 1000;
  const size_t maxEdges = 1000;
  response["node_limit"] = static_cast<Json::UInt64>(maxNodes);
  response["edge_limit"] = static_cast<Json::UInt64>(maxEdges);
  response["query_limit"] = items["limit"];
  response["query_offset"] = items["offset"];
  response["query_total"] = items["total"];
  response["truncated"] =
      items["total"].asUInt64() >
      (items["offset"].asUInt64() + items["items"].size());

  auto itemKey = [](const Json::Value& item) {
    return item["product"].asString() + ":" + item["id"].asString();
  };

  std::map<std::string, Json::Value> allByKey;
  std::map<std::string, std::vector<std::string>> keysByBareId;
  for (const auto& item : items["items"]) {
    const auto key = itemKey(item);
    allByKey[key] = item;
    keysByBareId[item["id"].asString()].push_back(key);
  }

  std::set<std::string> activeKeys;
  for (const auto& [key, item] : allByKey) {
    const bool matchesItem = itemId.empty() || item["id"].asString() == itemId ||
                             item["parent"].asString() == itemId;
    const bool matchesTopic = topic.empty() || ItemMatchesTopic(item, topic);
    if (matchesItem && matchesTopic) {
      activeKeys.insert(key);
    }
  }

  std::map<std::string, Json::Value> nodesByKey;
  Json::Value edges(Json::arrayValue);
  std::set<std::string> edgeKeys;
  std::map<std::string, std::vector<std::string>> dependencyAdjacency;
  bool graphTruncated = response["truncated"].asBool();

  auto appendNode = [&](const Json::Value& node) {
    const auto key = node["id"].asString();
    if (key.empty() || nodesByKey.count(key) > 0) {
      return;
    }
    if (nodesByKey.size() >= maxNodes) {
      graphTruncated = true;
      return;
    }
    nodesByKey[key] = node;
  };

  auto appendItemNode = [&](const Json::Value& item) {
    Json::Value node(Json::objectValue);
    node["id"] = itemKey(item);
    node["item_id"] = item["id"].asString();
    node["product"] = item["product"].asString();
    node["label"] = item["title"].asString();
    node["kind"] = item["type"].asString();
    node["state"] = item["state"].asString();
    node["missing"] = false;
    appendNode(node);
  };

  auto appendTopicNode = [&](const std::string& itemTopic) {
    Json::Value node(Json::objectValue);
    node["id"] = "topic:" + itemTopic;
    node["label"] = itemTopic;
    node["kind"] = "Topic";
    node["state"] = "open";
    node["missing"] = false;
    appendNode(node);
  };

  auto appendInvalidRef = [&](const std::string& sourceKey,
                              const std::string& kind,
                              const std::string& ref) {
    Json::Value invalid(Json::objectValue);
    invalid["source"] = sourceKey;
    invalid["kind"] = kind;
    invalid["ref"] = ref;
    response["invalid_refs"].append(invalid);
  };

  auto appendMissingNode = [&](const std::string& sourceKey,
                               const std::string& kind,
                               const std::string& ref,
                               const std::string& targetKey) {
    Json::Value missing(Json::objectValue);
    missing["id"] = targetKey;
    missing["ref"] = ref;
    missing["source"] = sourceKey;
    missing["kind"] = kind;
    response["missing_nodes"].append(missing);

    Json::Value node(Json::objectValue);
    node["id"] = targetKey;
    node["item_id"] = ref;
    node["product"] = targetKey.find(':') == std::string::npos
                          ? ""
                          : targetKey.substr(0, targetKey.find(':'));
    node["label"] = ref;
    node["kind"] = "Missing";
    node["state"] = "unresolved";
    node["missing"] = true;
    appendNode(node);
  };

  struct RefTarget {
    std::string product;
    std::string itemId;
    std::string key;
    std::string display;
    bool valid = false;
  };

  auto resolveRef = [&](const std::string& rawRef,
                        const std::string& defaultProduct) {
    RefTarget target;
    target.display = CleanListToken(rawRef);
    if (target.display.empty()) {
      return target;
    }
    const auto colon = target.display.find(':');
    if (colon != std::string::npos) {
      target.product = Trim(target.display.substr(0, colon));
      target.itemId = Trim(target.display.substr(colon + 1));
    } else {
      target.product = defaultProduct;
      target.itemId = Trim(target.display);
    }

    static const std::regex productRegex("^[A-Za-z0-9._-]+$");
    static const std::regex itemIdRegex("^[A-Z][A-Z0-9]*-[A-Z]+-[0-9]+$");
    target.valid = std::regex_match(target.product, productRegex) &&
                   std::regex_match(target.itemId, itemIdRegex);
    if (!target.valid) {
      return target;
    }

    target.key = target.product + ":" + target.itemId;
    if (colon == std::string::npos && allByKey.count(target.key) == 0) {
      const auto bareIt = keysByBareId.find(target.itemId);
      if (bareIt != keysByBareId.end() && bareIt->second.size() == 1) {
        target.key = bareIt->second.front();
        target.product = target.key.substr(0, target.key.find(':'));
      }
    }
    return target;
  };

  auto ensureRefNode = [&](const std::string& sourceKey,
                           const std::string& kind,
                           const RefTarget& target) {
    if (!target.valid) {
      appendInvalidRef(sourceKey, kind, target.display);
      return;
    }
    activeKeys.insert(target.key);
    const auto targetIt = allByKey.find(target.key);
    if (targetIt != allByKey.end()) {
      appendItemNode(targetIt->second);
    } else {
      appendMissingNode(sourceKey, kind, target.display, target.key);
    }
  };

  auto appendEdge = [&](const std::string& from,
                        const std::string& to,
                        const std::string& kind,
                        const std::string& semantic,
                        const std::string& source) {
    if (from.empty() || to.empty()) {
      return;
    }
    const auto edgeKey = from + "\n" + to + "\n" + kind;
    if (!edgeKeys.insert(edgeKey).second) {
      return;
    }
    if (edges.size() >= maxEdges) {
      graphTruncated = true;
      return;
    }
    Json::Value edge(Json::objectValue);
    edge["from"] = from;
    edge["to"] = to;
    edge["kind"] = kind;
    edge["semantic"] = semantic;
    edge["source"] = source;
    edge["dependency"] = semantic == "dependency";
    edges.append(edge);
    if (semantic == "dependency") {
      dependencyAdjacency[from].push_back(to);
    }
  };

  for (const auto& key : activeKeys) {
    const auto itemIt = allByKey.find(key);
    if (itemIt != allByKey.end()) {
      appendItemNode(itemIt->second);
    }
  }

  for (const auto& [key, item] : allByKey) {
    if (activeKeys.count(key) == 0) {
      continue;
    }

    for (const auto& itemTopic : SplitCsv(item["topic"].asString())) {
      const auto topicKey = "topic:" + itemTopic;
      appendTopicNode(itemTopic);
      appendEdge(topicKey, key, "topic-membership", "grouping", key);
    }

    const auto parent = item["parent"].asString();
    if (!parent.empty()) {
      const auto target = resolveRef(parent, item["product"].asString());
      ensureRefNode(key, "parent", target);
      if (target.valid) {
        appendEdge(target.key, key, "parent", "structural", key);
      }
    }

    for (const auto& ref : item["links"]["blocks"]) {
      const auto target = resolveRef(ref.asString(), item["product"].asString());
      ensureRefNode(key, "blocks", target);
      if (target.valid) {
        appendEdge(key, target.key, "blocks", "dependency", key);
      }
    }

    for (const auto& ref : item["links"]["blocked_by"]) {
      const auto target = resolveRef(ref.asString(), item["product"].asString());
      ensureRefNode(key, "blocked_by", target);
      if (target.valid) {
        appendEdge(target.key, key, "blocked_by", "dependency", key);
      }
    }

    for (const auto& ref : item["links"]["relates"]) {
      const auto target = resolveRef(ref.asString(), item["product"].asString());
      ensureRefNode(key, "relates", target);
      if (target.valid) {
        appendEdge(key, target.key, "relates", "reference", key);
      }
    }
  }

  std::map<std::string, size_t> visualLayers;
  for (const auto& [key, node] : nodesByKey) {
    if (StartsWith(key, "topic:")) {
      visualLayers[key] = 0;
    } else if (node.get("missing", false).asBool()) {
      visualLayers[key] = 3;
    } else {
      visualLayers[key] = 1;
    }
  }
  for (const auto& edge : edges) {
    const auto kind = edge["kind"].asString();
    const auto from = edge["from"].asString();
    const auto to = edge["to"].asString();
    if (kind == "blocks" || kind == "blocked_by") {
      visualLayers[from] = std::min(visualLayers[from], size_t{0});
      visualLayers[to] = std::max(visualLayers[to], size_t{2});
    } else if (kind == "parent" || kind == "topic-membership") {
      visualLayers[from] = std::min(visualLayers[from], size_t{0});
      visualLayers[to] = std::max(visualLayers[to], size_t{1});
    } else if (kind == "relates") {
      visualLayers[to] = std::max(visualLayers[to], size_t{2});
    }
  }

  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::set<std::string> emittedCycles;
  std::vector<std::string> path;
  std::function<void(const std::string&)> visit = [&](const std::string& node) {
    if (response["dependency_cycles"].size() >= 10) {
      return;
    }
    if (visiting.count(node) > 0 || visited.count(node) > 0) {
      return;
    }
    visiting.insert(node);
    path.push_back(node);
    const auto edgeIt = dependencyAdjacency.find(node);
    if (edgeIt != dependencyAdjacency.end()) {
      for (const auto& next : edgeIt->second) {
        const auto stackIt = std::find(path.begin(), path.end(), next);
        if (stackIt != path.end()) {
          Json::Value cycle(Json::arrayValue);
          std::ostringstream keyStream;
          for (auto it = stackIt; it != path.end(); ++it) {
            cycle.append(*it);
            keyStream << *it << ">";
          }
          cycle.append(next);
          keyStream << next;
          if (emittedCycles.insert(keyStream.str()).second) {
            response["dependency_cycles"].append(cycle);
          }
          continue;
        }
        visit(next);
      }
    }
    path.pop_back();
    visiting.erase(node);
    visited.insert(node);
  };

  for (const auto& [key, _] : dependencyAdjacency) {
    visit(key);
  }

  for (auto& [key, node] : nodesByKey) {
    node["visual_layer"] = static_cast<Json::UInt64>(visualLayers[key]);
    response["nodes"].append(node);
  }
  response["edges"] = edges;
  response["node_count"] = static_cast<Json::UInt64>(response["nodes"].size());
  response["edge_count"] = static_cast<Json::UInt64>(response["edges"].size());
  response["missing_node_count"] =
      static_cast<Json::UInt64>(response["missing_nodes"].size());
  response["truncated"] = graphTruncated;
  return response;
}

Json::Value BacklogWebviewService::BuildWorkOrderTimeline(const ItemQueryOptions& options,
                                                          const std::string& itemId,
                                                          const std::string& topic) {
  Json::Value response(Json::objectValue);
  response["events"] = Json::arrayValue;
  response["read_only"] = true;

  auto items = QueryItems(options);
  if (items.isMember("error")) {
    return items;
  }

  for (const auto& item : items["items"]) {
    if (!itemId.empty() && item["id"].asString() != itemId) {
      continue;
    }
    if (!topic.empty() && !ItemMatchesTopic(item, topic)) {
      continue;
    }
    auto detail = GetItem(item["product"].asString(), item["id"].asString());
    if (detail.isMember("error")) {
      continue;
    }
    const auto fullItem = detail["item"];
    auto lines = ExtractWorklogLines(fullItem.get("content", "").asString());
    if (lines.empty()) {
      Json::Value event(Json::objectValue);
      event["timestamp"] = item["updated"].asString();
      event["agent"] = "unknown";
      event["kind"] = "item-snapshot";
      event["item_id"] = item["id"].asString();
      event["product"] = item["product"].asString();
      event["state"] = item["state"].asString();
      event["text"] = "No worklog events recorded";
      response["events"].append(event);
      continue;
    }
    for (const auto& line : lines) {
      Json::Value event(Json::objectValue);
      event["timestamp"] = ExtractTimestamp(line);
      event["agent"] = ExtractAgent(line);
      event["kind"] = InferEventKind(line);
      event["item_id"] = item["id"].asString();
      event["product"] = item["product"].asString();
      event["state"] = item["state"].asString();
      event["text"] = line;
      response["events"].append(event);
    }
  }
  return response;
}

Json::Value BacklogWebviewService::BuildAgentRunBoard(const ItemQueryOptions& options,
                                                      const std::string& agent,
                                                      const std::string& runState,
                                                      size_t staleDays) {
  Json::Value response(Json::objectValue);
  response["runs"] = Json::arrayValue;
  response["groups"] = Json::objectValue;
  response["read_only"] = true;
  response["stale_days"] = static_cast<Json::UInt64>(staleDays);

  auto timeline = BuildWorkOrderTimeline(options);
  if (timeline.isMember("error")) {
    return timeline;
  }

  std::unordered_map<std::string, Json::Value> latestByRun;
  for (const auto& event : timeline["events"]) {
    const auto eventAgent = event["agent"].asString();
    if (!agent.empty() && !JsonStringContains(event["agent"], agent)) {
      continue;
    }
    const auto key = event["product"].asString() + ":" + event["item_id"].asString() + ":" + eventAgent;
    latestByRun[key] = event;
  }

  for (const auto& [key, event] : latestByRun) {
    Json::Value run(Json::objectValue);
    run["run_id"] = key;
    run["product"] = event["product"].asString();
    run["item_id"] = event["item_id"].asString();
    run["agent"] = event["agent"].asString();
    run["latest_event"] = event;

    Json::Value item(Json::objectValue);
    item["state"] = event["state"].asString();
    const auto inferred = InferRunState(item, event["kind"].asString());
    if (!runState.empty() && !JsonStringContains(Json::Value(inferred), runState)) {
      continue;
    }
    run["state"] = inferred;
    run["evidence_links"] = Json::arrayValue;
    if (event["kind"].asString() == "artifact") {
      run["evidence_links"].append(event["text"].asString());
    }
    response["runs"].append(run);
    response["groups"][inferred].append(run);
  }

  response["total"] = static_cast<Json::UInt64>(response["runs"].size());
  return response;
}

std::string BacklogWebviewService::RenderTreePartial(
    const ItemQueryOptions& options) {
  const auto tree = BuildTree(options);
  if (tree.isMember("error")) {
    return RenderErrorPartial(tree["error"].asString());
  }
  if (tree["roots"].empty()) {
    return "<div class=\"muted\">No items for the current filters.</div>";
  }

  std::string html = "<ul>";
  for (const auto& root : tree["roots"]) {
    html += RenderTreeNodePartial(root, 0);
  }
  html += "</ul>";
  return html;
}

std::string BacklogWebviewService::RenderKanbanPartial(
    const ItemQueryOptions& options) {
  const auto kanban = BuildKanban(options);
  if (kanban.isMember("error")) {
    return RenderErrorPartial(kanban["error"].asString());
  }

  const std::vector<std::string> lanes = {"Backlog", "Doing", "Blocked",
                                          "Review", "Done"};
  std::string html;
  for (const auto& lane : lanes) {
    html += "<div class=\"lane\"><strong>" + HtmlEscape(lane) +
            "</strong><div class=\"lane-items\" role=\"listbox\" aria-label=\"" +
            HtmlEscape(lane + " items") + "\">";
    const auto& items = kanban["lanes"][lane];
    if (items.empty()) {
      html += "<div class=\"muted\">No items</div>";
    } else {
      for (const auto& item : items) {
        html += RenderItemCardPartial(item);
      }
    }
    html += "</div></div>";
  }
  return html;
}

std::string BacklogWebviewService::RenderReviewPartial(
    const ItemQueryOptions& options) {
  const auto inbox = BuildReviewInbox(options);
  if (inbox.isMember("error")) {
    return RenderErrorPartial(inbox["error"].asString());
  }

  const std::vector<std::string> lanes = {
      "Needs Review", "Done Candidate", "False Done Suspect", "Evidence Gap",
      "Blocked/Dirty", "Stale/Drift", "Ready Frontier"};
  std::string html;
  for (const auto& lane : lanes) {
    const auto& bundles = inbox["lanes"][lane];
    html += "<div class=\"review-lane\"><strong>" + HtmlEscape(lane) +
            "</strong><div class=\"muted\">" +
            std::to_string(bundles.size()) +
            " item(s)</div><div class=\"review-lane-items\" role=\"listbox\" aria-label=\"" +
            HtmlEscape(lane + " items") + "\">";
    if (bundles.empty()) {
      html += "<div class=\"muted\">No items</div>";
    } else {
      for (const auto& bundle : bundles) {
        html += RenderReviewBundlePartial(bundle);
      }
    }
    html += "</div></div>";
  }
  return html.empty() ? "<div class=\"muted\">No review queues for the current filters.</div>"
                      : html;
}

std::string BacklogWebviewService::RenderContextPartial(
    const ItemQueryOptions& options) {
  const auto items = QueryItems(options);
  if (items.isMember("error")) {
    return RenderErrorPartial(items["error"].asString());
  }

  std::map<std::string, size_t> counts;
  std::string cards;
  for (const auto& item : items["items"]) {
    const auto type = item.get("type", "").asString();
    if (type != "ADR" && type != "Topic" && type != "Workset") {
      continue;
    }
    ++counts[type];
    cards += RenderItemCardPartial(item);
  }

  std::string html = "<div id=\"context-summary\" class=\"muted\" style=\"margin-bottom:8px;\">ADR: " +
                     std::to_string(counts["ADR"]) + " | Topic: " +
                     std::to_string(counts["Topic"]) + " | Workset: " +
                     std::to_string(counts["Workset"]) + "</div>";
  html += cards.empty()
              ? "<div class=\"muted\">No context items</div>"
              : "<div class=\"context-items\" role=\"listbox\" aria-label=\"Context items\">" + cards + "</div>";
  return html;
}

std::string BacklogWebviewService::RenderFiltersPartial(
    const ItemQueryOptions& options) {
  const auto products = ListProducts();
  const std::vector<std::string> states = {"Proposed", "Ready", "InProgress",
                                           "Blocked", "Review", "Done",
                                           "Dropped"};
  const std::vector<std::string> types = {"Theme", "Initiative", "Epic", "Feature",
                                          "UserStory", "Task", "Bug",
                                          "Issue", "ADR", "Topic",
                                          "Workset"};
  const bool allProducts =
      options.products.empty() || ContainsToken(options.products, "all");

  std::string html =
      "<div class=\"filter-group\"><div class=\"filter-group-title\">Products</div><div class=\"filters\">";
  html += RenderCheckbox("product", "all", allProducts);
  for (const auto& product : products) {
    const auto value = product.asString();
    html += RenderCheckbox("product", value,
                           !allProducts && ContainsToken(options.products, value));
  }
  html += "</div></div><div class=\"filter-group\"><div class=\"filter-group-title\">States</div><div class=\"filters\">";
  for (const auto& state : states) {
    html += RenderCheckbox("state", state,
                           options.states.empty() ||
                               ContainsToken(options.states, state));
  }
  html += "</div></div><div class=\"filter-group\"><div class=\"filter-group-title\">Types</div><div class=\"filters\">";
  for (const auto& type : types) {
    html += RenderCheckbox("type", type,
                           options.types.empty() ||
                               ContainsToken(options.types, type));
  }
  html += "</div></div>";
  return html;
}

std::string BacklogWebviewService::RenderItemPartial(const std::string& product,
                                                     const std::string& id) {
  const auto detail = GetEvidenceDetail(product.empty() ? "all" : product, id);
  if (detail.isMember("error")) {
    return RenderErrorPartial(detail["error"].asString());
  }

  const auto& item = detail["item"];
  const auto reviewSections = ExtractReviewSections(item.get("content", "").asString());

  std::string html = "<section class=\"detail-shell\" data-item-title=\"" +
                     HtmlEscape(item.get("title", item.get("id", "")).asString()) + "\">";
  html += "<section class=\"panel detail-header\"><div class=\"detail-title-row\"><h3 style=\"margin:0;\">" +
          HtmlEscape(item.get("title", item.get("id", "")).asString()) +
          "</h3><span class=\"pill\"><code>" +
          HtmlEscape(item.get("id", "").asString()) +
          "</code></span></div><div class=\"muted\">" +
          HtmlEscape(ItemMetaText(item)) + "</div><div class=\"detail-facts\">" +
          RenderDetailFact("Product", item.get("product", "").asString().empty()
                                          ? "all"
                                          : item.get("product", "").asString()) +
          RenderDetailFact("Type", item.get("type", "").asString()) +
          RenderDetailFact("State", item.get("state", "").asString()) +
          RenderDetailFact("Priority", item.get("priority", "").asString().empty()
                                           ? "Unspecified"
                                           : item.get("priority", "").asString()) +
          RenderDetailFact("Updated", item.get("updated", "").asString().empty()
                                          ? "Unknown"
                                          : item.get("updated", "").asString()) +
          RenderDetailFact("Path", item.get("path", "").asString().empty()
                                       ? "Unknown"
                                       : item.get("path", "").asString()) +
          "</div></section>";

  html += "<section class=\"panel detail-section\"><h4>Gate status</h4><div class=\"review-grid\">" +
          RenderGateStatusCard(item["gate_status"]["ready"]) +
          RenderGateStatusCard(item["gate_status"]["review"]) +
          RenderGateStatusCard(item["gate_status"]["done"]) +
          "</div></section>";

  html += "<section class=\"panel detail-section\"><h4>Evidence</h4>" +
          RenderEvidenceSummaryPartial(detail["evidence"]) + "</section>";

  html += RenderReviewDecisionHistoryPartial(detail["review_decision_history"]);

  if (!reviewSections.empty()) {
    html += "<div class=\"detail-sections\">";
    for (const auto& section : reviewSections) {
      html += RenderSectionPanel(section);
    }
    html += "</div>";
  }

  html += "<section class=\"panel detail-section\"><h4>Timeline</h4>";
  if (detail["worklog_events"].empty()) {
    html += "<div class=\"muted\">No worklog events</div>";
  } else {
    for (const auto& event : detail["worklog_events"]) {
      html += "<div class=\"card\"><div><code>" +
              HtmlEscape(event.get("timestamp", "").asString()) +
              "</code> " + RenderPill(event.get("kind", "worklog").asString()) +
              "</div><div>" + HtmlEscape(event.get("text", "").asString()) +
              "</div><div class=\"muted\">" +
              HtmlEscape(event.get("agent", "unknown").asString()) +
              "</div></div>";
    }
  }
  html += "</section>";

  std::vector<std::pair<std::string, std::string>> metadataRows = {
      {"Owner", item.get("owner", "").asString()},
      {"Area", item.get("area", "").asString()},
      {"Iteration", item.get("iteration", "").asString()},
      {"Parent", item.get("parent", "").asString()},
      {"Topic", item.get("topic", "").asString()},
      {"Created", item.get("created", "").asString()},
      {"Source kind", item.get("source_kind", "").asString()}};
  const auto metadataHtml = RenderKeyValueRows(metadataRows);

  std::vector<std::pair<std::string, std::string>> externalRows;
  for (const auto& key : item["external"].getMemberNames()) {
    const auto value = item["external"][key].asString();
    if (!IsEmptyLikeString(value)) {
      externalRows.emplace_back(key, value);
    }
  }
  const auto externalHtml = RenderKeyValueRows(externalRows);

  const auto tagsHtml = RenderPillRow(
      [&]() {
        std::vector<std::string> values;
        for (const auto& tag : item["tags"]) {
          values.push_back(tag.asString());
        }
        return values;
      }(),
      [](const std::string& value) { return RenderTextChip(value); });
  const auto decisionsHtml = RenderPillRow(
      [&]() {
        std::vector<std::string> values;
        for (const auto& decision : item["decisions"]) {
          values.push_back(decision.asString());
        }
        return values;
      }(),
      [](const std::string& value) { return RenderTextChip(value); });
  const auto relatesHtml = RenderPillRow(
      [&]() {
        std::vector<std::string> values;
        for (const auto& ref : item["links"]["relates"]) {
          values.push_back(ref.asString());
        }
        return values;
      }(),
      [&](const std::string& value) {
        return RenderReferenceChip(value, item.get("product", "").asString());
      });
  const auto blocksHtml = RenderPillRow(
      [&]() {
        std::vector<std::string> values;
        for (const auto& ref : item["links"]["blocks"]) {
          values.push_back(ref.asString());
        }
        return values;
      }(),
      [&](const std::string& value) {
        return RenderReferenceChip(value, item.get("product", "").asString());
      });
  const auto blockedByHtml = RenderPillRow(
      [&]() {
        std::vector<std::string> values;
        for (const auto& ref : item["links"]["blocked_by"]) {
          values.push_back(ref.asString());
        }
        return values;
      }(),
      [&](const std::string& value) {
        return RenderReferenceChip(value, item.get("product", "").asString());
      });

  if (!metadataHtml.empty() || !externalHtml.empty() || !tagsHtml.empty() ||
      !decisionsHtml.empty() || !relatesHtml.empty() || !blocksHtml.empty() ||
      !blockedByHtml.empty()) {
    html += "<section class=\"panel detail-section\"><h4>Metadata &amp; relations</h4><div class=\"detail-stack\">";
    if (!metadataHtml.empty()) {
      html += "<div><div class=\"detail-label\">Metadata</div>" + metadataHtml + "</div>";
    }
    if (!externalHtml.empty()) {
      html += "<div><div class=\"detail-label\">External</div>" + externalHtml + "</div>";
    }
    if (!tagsHtml.empty()) {
      html += "<div><div class=\"detail-label\">Tags</div>" + tagsHtml + "</div>";
    }
    if (!decisionsHtml.empty()) {
      html += "<div><div class=\"detail-label\">Decisions</div>" + decisionsHtml + "</div>";
    }
    if (!relatesHtml.empty()) {
      html += "<div><div class=\"detail-label\">Relates</div>" + relatesHtml + "</div>";
    }
    if (!blocksHtml.empty()) {
      html += "<div><div class=\"detail-label\">Blocks</div>" + blocksHtml + "</div>";
    }
    if (!blockedByHtml.empty()) {
      html += "<div><div class=\"detail-label\">Blocked by</div>" + blockedByHtml + "</div>";
    }
    html += "</div></section>";
  }

  html += "<details class=\"panel detail-toggle\"><summary class=\"detail-toggle-summary\"><strong>Raw markdown / full file</strong></summary><pre>" +
          HtmlEscape(item.get("content", "").asString()) + "</pre></details>";
  html += "</section>";
  return html;
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
    for (auto& type : options.types) {
      type = NormalizeItemTypeName(type);
    }
    options.limit =
        ParseSizeOrDefault(request->getParameter("limit"), 200, 1000);
    options.offset =
        ParseSizeOrDefault(request->getParameter("offset"), 0, 1000000);
    return options;
  };
  auto htmlResponse = [](const std::string& html) {
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(k200OK);
    response->setContentTypeCode(CT_TEXT_HTML);
    response->setBody(html);
    return response;
  };

  app().registerHandler(
      "/partials/tree",
      [&service, queryOptionsFromRequest, htmlResponse](
          const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        callback(htmlResponse(
            service.RenderTreePartial(queryOptionsFromRequest(request))));
      },
      {Get});

  app().registerHandler(
      "/partials/kanban",
      [&service, queryOptionsFromRequest, htmlResponse](
          const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        callback(htmlResponse(
            service.RenderKanbanPartial(queryOptionsFromRequest(request))));
      },
      {Get});

  app().registerHandler(
      "/partials/review",
      [&service, queryOptionsFromRequest, htmlResponse](
          const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        callback(htmlResponse(
            service.RenderReviewPartial(queryOptionsFromRequest(request))));
      },
      {Get});

  app().registerHandler(
      "/partials/context",
      [&service, queryOptionsFromRequest, htmlResponse](
          const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        callback(htmlResponse(
            service.RenderContextPartial(queryOptionsFromRequest(request))));
      },
      {Get});

  app().registerHandler(
      "/partials/filters",
      [&service, queryOptionsFromRequest, htmlResponse](
          const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        callback(htmlResponse(
            service.RenderFiltersPartial(queryOptionsFromRequest(request))));
      },
      {Get});

  app().registerHandler(
      "/partials/item/{1}",
      [&service, htmlResponse](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback,
          const std::string& itemId) {
        auto product = request->getParameter("product");
        if (product.empty()) {
          product = request->getParameter("products");
        }
        callback(htmlResponse(service.RenderItemPartial(product, itemId)));
      },
      {Get});

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

  app().registerHandler(
      "/api/review/saved-views",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        Json::Value body(Json::objectValue);
        body["ok"] = true;
        body["data"] = service.ListSavedViews();
        metaAppender(request, body);
        callback(HttpResponse::newHttpJsonResponse(body));
      },
      {Get});

  app().registerHandler(
      "/api/review/saved-views/{1}",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback,
          const std::string& viewId) {
        auto data = service.RunSavedView(viewId, queryOptionsFromRequest(request));
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
      "/api/review/kobql",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.RunKobql(request->getParameter("q"),
                                     queryOptionsFromRequest(request));
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
      "/api/review/command-preview",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.PreviewCommand(request->getParameter("q"),
                                           queryOptionsFromRequest(request));
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
      "/api/review/capability-route",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto itemId = request->getParameter("item");
        if (itemId.empty()) {
          itemId = request->getParameter("item_id");
        }
        auto product = request->getParameter("product");
        if (product.empty()) {
          product = request->getParameter("products");
        }
        auto data = service.RecommendCapabilityRoute(product, itemId);
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
      "/api/review/inbox",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildReviewInbox(queryOptionsFromRequest(request));
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

  auto reviewDecisionResponse = [metaAppender](
      const HttpRequestPtr& request,
      std::function<void(const HttpResponsePtr&)>&& callback,
      Json::Value data) {
    Json::Value body(Json::objectValue);
    body["ok"] = !data.isMember("error");
    body["data"] = data;
    metaAppender(request, body);
    auto response = HttpResponse::newHttpJsonResponse(body);
    if (!body["ok"].asBool()) {
      response->setStatusCode(k400BadRequest);
    }
    callback(response);
  };

  app().registerHandler(
      "/api/review/decision/draft",
      [reviewDecisionResponse, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto payload = request->getJsonObject();
        if (!payload) {
          reviewDecisionResponse(request, std::move(callback),
                                 MakeError("review_decision.invalid_json", "POST a JSON review decision draft payload"));
          return;
        }
        reviewDecisionResponse(request, std::move(callback),
                               service.SaveReviewDecisionDraft(*payload));
      },
      {Post});

  app().registerHandler(
      "/api/review/decision/draft/discard",
      [reviewDecisionResponse, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto payload = request->getJsonObject();
        if (!payload) {
          reviewDecisionResponse(request, std::move(callback),
                                 MakeError("review_decision.invalid_json", "POST a JSON review decision draft discard payload"));
          return;
        }
        reviewDecisionResponse(request, std::move(callback),
                               service.DiscardReviewDecisionDraft(*payload));
      },
      {Post});

  app().registerHandler(
      "/api/review/decision/submit",
      [reviewDecisionResponse, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto payload = request->getJsonObject();
        if (!payload) {
          reviewDecisionResponse(request, std::move(callback),
                                 MakeError("review_decision.invalid_json", "POST a JSON submitted review decision payload"));
          return;
        }
        reviewDecisionResponse(request, std::move(callback),
                               service.SubmitReviewDecision(*payload));
      },
      {Post});

  app().registerHandler(
      "/api/review/evidence/{1}",
      [metaAppender, &service](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback,
          const std::string& itemId) {
        auto product = request->getParameter("product");
        if (product.empty()) {
          product = "all";
        }
        auto data = service.GetEvidenceDetail(product, itemId);
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
      "/api/review/topics",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildTopicHome(request->getParameter("topic"),
                                           queryOptionsFromRequest(request));
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
      "/api/review/graph",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildDependencyGraph(
            queryOptionsFromRequest(request), request->getParameter("item"),
            request->getParameter("topic"));
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
      "/api/review/timeline",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        auto data = service.BuildWorkOrderTimeline(
            queryOptionsFromRequest(request), request->getParameter("item"),
            request->getParameter("topic"));
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
      "/api/review/runs",
      [metaAppender, &service, queryOptionsFromRequest](const HttpRequestPtr& request,
          std::function<void(const HttpResponsePtr&)>&& callback) {
        const auto staleDays =
            ParseSizeOrDefault(request->getParameter("stale_days"), 3, 365);
        auto data = service.BuildAgentRunBoard(
            queryOptionsFromRequest(request), request->getParameter("agent"),
            request->getParameter("run_state"), staleDays);
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
