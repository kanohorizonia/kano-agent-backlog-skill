#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/drogon.h>

namespace kano::backlog::webview {

struct ItemRecord {
  std::string product;
  std::string id;
  std::string uid;
  std::string type;
  std::string sourceKind;
  std::string title;
  std::string state;
  std::string priority;
  std::string parent;
  std::string owner;
  std::string area;
  std::string iteration;
  std::string topic;
  std::string created;
  std::string updated;
  std::string relativePath;
  std::string rawContent;
  std::vector<std::string> tags;
  std::vector<std::string> decisions;
  std::vector<std::string> relates;
  std::vector<std::string> blocks;
  std::vector<std::string> blockedBy;
  std::map<std::string, std::string> external;
  bool valid;
  std::string parseError;
};

struct ItemQueryOptions {
  std::vector<std::string> products;
  std::vector<std::string> states;
  std::vector<std::string> types;
  std::string text;
  size_t limit = 200;
  size_t offset = 0;
  bool forceRefresh = false;
};

class BacklogWebviewService {
 public:
  explicit BacklogWebviewService(std::filesystem::path productsRoot);

  std::filesystem::path GetProductsRoot() const;

  Json::Value ListProducts();
  Json::Value ListItems(const std::string& product, bool forceRefresh = false);
  Json::Value QueryItems(const ItemQueryOptions& options);
  Json::Value GetItem(const std::string& product, const std::string& id,
                     bool forceRefresh = false);
  Json::Value BuildTree(const std::string& product, bool forceRefresh = false);
  Json::Value BuildTree(const ItemQueryOptions& options);
  Json::Value BuildKanban(const std::string& product,
                          bool forceRefresh = false);
  Json::Value BuildKanban(const ItemQueryOptions& options);
  Json::Value ListSavedViews();
  Json::Value RunSavedView(const std::string& viewId,
                           const ItemQueryOptions& options);
  Json::Value RunKobql(const std::string& query,
                       const ItemQueryOptions& options);
  Json::Value PreviewCommand(const std::string& phrase,
                                const ItemQueryOptions& options);
  Json::Value RecommendCapabilityRoute(const std::string& product,
                                        const std::string& itemId,
                                        bool forceRefresh = false);
  Json::Value BuildDoneCandidateDetector(const ItemQueryOptions& options);
  Json::Value BuildEvidenceQualityView(const ItemQueryOptions& options);
  Json::Value BuildContextRecoverySummary(const std::string& area,
                                          const ItemQueryOptions& options);
  Json::Value BuildReviewInbox(const ItemQueryOptions& options);
  Json::Value SaveReviewDecisionDraft(const Json::Value& request);
  Json::Value DiscardReviewDecisionDraft(const Json::Value& request);
  Json::Value SubmitReviewDecision(const Json::Value& request);
  Json::Value GetEvidenceDetail(const std::string& product,
                                 const std::string& id,
                                 bool forceRefresh = false);
  Json::Value BuildTopicHome(const std::string& topic,
                             const ItemQueryOptions& options);
  Json::Value BuildDependencyGraph(const ItemQueryOptions& options,
                                   const std::string& itemId = "",
                                   const std::string& topic = "");
  Json::Value BuildWorkOrderTimeline(const ItemQueryOptions& options,
                                     const std::string& itemId = "",
                                     const std::string& topic = "");
  Json::Value BuildAgentRunBoard(const ItemQueryOptions& options,
                                 const std::string& agent = "",
                                 const std::string& runState = "",
                                 size_t staleDays = 3);
  std::string RenderTreePartial(const ItemQueryOptions& options);
  std::string RenderKanbanPartial(const ItemQueryOptions& options);
  std::string RenderReviewPartial(const ItemQueryOptions& options);
  std::string RenderContextPartial(const ItemQueryOptions& options);
  std::string RenderFiltersPartial(const ItemQueryOptions& options);
  std::string RenderItemPartial(const std::string& product,
                                const std::string& id);
  Json::Value Refresh(const std::string& product);
  Json::Value GetWorkspaceInfo() const;
  Json::Value SwitchWorkspace(const std::string& inputPath);

 private:
  struct ProductCache {
    std::vector<ItemRecord> allItems;
    std::unordered_map<std::string, std::vector<size_t>> idIndexes;
    std::unordered_map<std::string, size_t> primaryById;
    std::filesystem::file_time_type latestMtime;
    std::vector<std::string> warnings;
  };

  std::filesystem::path productsRoot;
  std::unordered_map<std::string, ProductCache> cacheByProduct;

  static std::filesystem::path ResolveProductsPathFromInput(
      const std::filesystem::path& inputPath);

  bool IsValidProductName(const std::string& product) const;
  std::filesystem::path ProductRoot(const std::string& product) const;
  std::filesystem::file_time_type ScanLatestMtime(
      const std::filesystem::path& productRoot) const;
  bool ShouldLoad(const std::string& product, bool forceRefresh);
  void LoadProduct(const std::string& product, bool forceRefresh);

  static bool IsMarkdownItemFile(const std::filesystem::path& path);
  static bool ShouldSkipPath(const std::filesystem::path& path);
  std::vector<std::string> ResolveSelectedProducts(
      const std::vector<std::string>& requestedProducts) const;
  std::unordered_map<std::string, std::vector<std::string>> BuildTopicLookup() const;
  static std::string NormalizeTypeFromPath(
      const std::filesystem::path& itemPath, const std::string& declaredType);

  static ItemRecord ParseItem(const std::filesystem::path& itemPath,
                              const std::filesystem::path& productRoot);
  static ItemRecord ParseDecision(const std::filesystem::path& decisionPath,
                                  const std::filesystem::path& productRoot);
  static ItemRecord ParseTopicManifest(const std::filesystem::path& topicManifestPath,
                                       const std::filesystem::path& backlogRoot);
  static ItemRecord ParseWorksetManifest(
      const std::filesystem::path& worksetManifestPath,
      const std::filesystem::path& backlogRoot);
  static std::unordered_map<std::string, std::string> ParseFrontmatterMap(
      const std::string& content, bool& ok, std::string& error);
  static Json::Value ParseJsonFile(const std::filesystem::path& jsonPath, bool& ok,
                                   std::string& error);

  static Json::Value ItemToJson(const ItemRecord& item, bool includeContent = false);
  static std::string ToIsoString(const std::filesystem::file_time_type& value);
};

void RegisterBacklogWebviewRoutes(
    BacklogWebviewService& service,
    const std::function<void(const drogon::HttpRequestPtr&, Json::Value&)>&
        appendCommonMeta);

}  // namespace kano::backlog::webview
