#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "KanoBacklog.BacklogWebviewService.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_root() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned int> dist(0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << dist(gen);

    auto root = std::filesystem::temp_directory_path() /
        "kano-backlog-webview-smoke" /
        suffix.str();
    std::filesystem::create_directories(root / "products");
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write " + path.generic_string());
    }
    out << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read " + path.generic_string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::filesystem::path locate_repo_file(const std::filesystem::path& relative) {
    const std::vector<std::filesystem::path> seeds = {
        std::filesystem::path(__FILE__).parent_path(),
        std::filesystem::current_path(),
    };
    for (auto seed : seeds) {
        while (!seed.empty()) {
            const auto candidate = seed / relative;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
            const auto parent = seed.parent_path();
            if (parent == seed) {
                break;
            }
            seed = parent;
        }
    }
    throw std::runtime_error("failed to locate repo file: " + relative.generic_string());
}

std::string item_doc(const std::string& id,
                     const std::string& uid,
                     const std::string& type,
                     const std::string& title,
                     const std::string& state,
                     const std::string& parent,
                     const std::string& body,
                     const std::string& extra_frontmatter = "") {
    std::ostringstream out;
    out << "---\n";
    out << "id: " << id << "\n";
    out << "uid: " << uid << "\n";
    out << "type: " << type << "\n";
    out << "title: " << title << "\n";
    out << "state: " << state << "\n";
    out << "parent: " << (parent.empty() ? "null" : parent) << "\n";
    out << extra_frontmatter;
    out << "created: 2026-06-14\n";
    out << "updated: 2026-06-14\n";
    out << "---\n\n";
    out << body << "\n";
    return out.str();
}

std::optional<Json::Value> find_item(const Json::Value& items,
                                     const std::string& product,
                                     const std::string& id) {
    for (const auto& item : items) {
        if (item["product"].asString() == product &&
            item["id"].asString() == id) {
            return item;
        }
    }
    return std::nullopt;
}

bool has_edge(const Json::Value& edges,
              const std::string& from,
              const std::string& to,
              const std::string& kind) {
    for (const auto& edge : edges) {
        if (edge["from"].asString() == from &&
            edge["to"].asString() == to &&
            edge["kind"].asString() == kind) {
            return true;
        }
    }
    return false;
}

void expect_in_order(const std::string& text,
                     const std::vector<std::string>& markers,
                     const std::string& message) {
    size_t previous = 0;
    bool first = true;
    for (const auto& marker : markers) {
        const auto pos = text.find(marker);
        if (pos == std::string::npos) {
            throw std::runtime_error(message + ": missing marker " + marker);
        }
        if (!first && pos <= previous) {
            throw std::runtime_error(message + ": wrong order at " + marker);
        }
        previous = pos;
        first = false;
    }
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    namespace webview = kano::backlog::webview;
    std::filesystem::path root;

    try {
        root = make_temp_root();
        const auto products = root / "products";

        write_text(
            products / "product-alpha" / "items" / "epic" / "0001" / "PRA-EPIC-0001.md",
            item_doc("PRA-EPIC-0001",
                     "019ec100-0000-7000-8000-000000000001",
                     "Epic",
                     "Alpha epic",
                     "Ready",
                     "",
                     "Alpha parent body."));
        write_text(
            products / "product-alpha" / "items" / "task" / "0001" / "PRA-TSK-0001.md",
            item_doc("PRA-TSK-0001",
                     "019ec100-0000-7000-8000-000000000002",
                     "Task",
                     "Alpha native task",
                     "Ready",
                     "PRA-EPIC-0001",
                     "Native migration evidence lives here.\n\n"
                     "## Context\n\n"
                     "The native migration needs a bounded review surface.\n\n"
                     "## Goal\n\n"
                     "Expose backlog state through deterministic native JSON.\n\n"
                     "## Acceptance Criteria\n\n"
                     "- Webview service returns product data.\n\n"
                     "## Risks / Dependencies\n\n"
                     "- Keep the workflow read-only.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 10:00 [agent=codex] Work order dispatched for native migration review.\n"
                     "2026-06-14 10:10 [agent=codex] Artifact attached: [report](../artifacts/PRA-TSK-0001/report.md).\n"
                     "2026-06-14 10:20 [agent=codex] Validation: pixi run quick-test PASS.\n",
                     "links:\n"
                     "  relates:\n"
                     "    - product-beta:PRB-BUG-0001\n"
                     "    - PRA-TSK-0003\n"
                     "    - PRA-TSK-9999\n"
                      "  blocks:\n"
                      "    - PRA-TSK-0002\n"
                      "  blocked_by: []\n"));
        write_text(
            products / "product-alpha" / "items" / "task" / "0004" / "PRA-TSK-0004.md",
            item_doc("PRA-TSK-0004",
                     "019ec100-0000-7000-8000-000000000009",
                     "Task",
                     "Alpha review panel task",
                     "Ready",
                     "",
                     "Review-first detail body.\n\n"
                     "## Context\n\n"
                     "Humans need the useful review sections before the raw file dump.\n\n"
                     "## Goal\n\n"
                     "Open the item detail in a review-first layout by default.\n\n"
                     "## Acceptance Criteria\n\n"
                     "- Context, Goal, Acceptance Criteria, Risks / Dependencies, and Worklog are visible before the raw markdown toggle.\n"
                     "- Empty metadata stays hidden from the default detail panel.\n\n"
                     "## Risks / Dependencies\n\n"
                     "- Do not duplicate the full renderer in browser code.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 13:00 [agent=codex] Work order dispatched for review-first modal layout.\n"
                     "2026-06-14 13:10 [agent=codex] Artifact attached: [report](../artifacts/PRA-TSK-0004/report.md).\n"
                     "2026-06-14 13:20 [agent=codex] Validation: pixi run quick-test PASS.\n",
                     "priority: P1\n"
                     "area: review-ui\n"
                     "iteration: null\n"
                     "owner: ~\n"
                     "external: {}\n"
                     "tags: []\n"
                     "decisions: []\n"
                     "links:\n"
                     "  relates:\n"
                     "    - PRA-TSK-0003\n"
                     "  blocks: []\n"
                     "  blocked_by: []\n"));
        write_text(
            products / "product-alpha" / "items" / "task" / "0002" / "PRA-TSK-0002.md",
            item_doc("PRA-TSK-0002",
                     "019ec100-0000-7000-8000-000000000004",
                     "Task",
                     "Alpha blocked task",
                     "Blocked",
                     "PRA-EPIC-0001",
                     "Blocked by the native task.",
                     "links:\n"
                     "  relates: []\n"
                     "  blocks: []\n"
                     "  blocked_by:\n"
                     "    - PRA-TSK-0001\n"));
        write_text(
            products / "product-alpha" / "items" / "task" / "0003" / "PRA-TSK-0003.md",
            item_doc("PRA-TSK-0003",
                     "019ec100-0000-7000-8000-000000000005",
                     "Task",
                     "Alpha related task",
                     "Ready",
                     "PRA-EPIC-0001",
                     "Related-only cycle coverage.",
                     "links:\n"
                     "  relates:\n"
                     "    - PRA-TSK-0001\n"
                     "  blocks: []\n"
                     "  blocked_by: []\n"));
        write_text(
            products / "product-beta" / "items" / "bug" / "0001" / "PRB-BUG-0001.md",
            item_doc("PRB-BUG-0001",
                     "019ec100-0000-7000-8000-000000000003",
                     "Bug",
                     "Beta live bug",
                     "InProgress",
                     "",
                     "Beta product bug body.",
                     "links:\n"
                     "  relates: [product-alpha:PRA-TSK-0001]\n"
                     "  blocks: []\n"
                     "  blocked_by: []\n"));
        write_text(
            products / "product-beta" / "items" / "bug" / "0002" / "PRB-BUG-0002.md",
            item_doc("PRB-BUG-0002",
                     "019ec100-0000-7000-8000-000000000006",
                     "Bug",
                     "Beta done with evidence",
                     "Done",
                     "",
                     "Done candidate with durable evidence.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 11:00 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0002/report.md).\n"
                     "2026-06-14 11:10 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        write_text(
            products / "product-beta" / "items" / "bug" / "0003" / "PRB-BUG-0003.md",
            item_doc("PRB-BUG-0003",
                     "019ec100-0000-7000-8000-000000000007",
                     "Bug",
                     "Beta done without evidence",
                     "Done",
                     "",
                     "Closed without durable validation evidence."));
        write_text(
            products / "product-beta" / "items" / "bug" / "0004" / "PRB-BUG-0004.md",
            item_doc("PRB-BUG-0004",
                     "019ec100-0000-7000-8000-000000000008",
                     "Bug",
                     "Beta review with evidence",
                     "Review",
                     "",
                     "Review candidate with durable evidence.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 12:00 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0004/report.md).\n"
                     "2026-06-14 12:10 [agent=codex] Validation: pixi run quick-test PASS.\n"));

        write_text(root / "topics" / "native-migration" / "manifest.json",
                   R"json({"topic":"Native Migration","status":"open","seed_items":["019ec100-0000-7000-8000-000000000002","PRA-TSK-0001"]})json");
        write_text(root / "topics" / "native-migration" / "brief.md",
                   "# Native Migration\n\nTopic brief.");

        webview::BacklogWebviewService service(products);

        auto productList = service.ListProducts();
        expect(productList.size() == 2, "service should list both products");
        expect(productList[0].asString() == "product-alpha", "products should be sorted");

        webview::ItemQueryOptions allOptions;
        auto all = service.QueryItems(allOptions);
        expect(!all.isMember("error"), "all-products query should not fail");
        expect(all["products"].size() == 2, "all-products query should include both products");
        expect(all["total"].asUInt64() == 10, "all-products query should include items plus unique topic pseudo-items");
        for (const auto& item : all["items"]) {
            expect(item.isMember("gate_status"), "all-products items should include gate_status");
            expect(item["gate_status"].isMember("ready"), "gate_status should include ready gate");
            expect(item["gate_status"].isMember("review"), "gate_status should include review gate");
            expect(item["gate_status"].isMember("done"), "gate_status should include done gate");
        }

        auto task = find_item(all["items"], "product-alpha", "PRA-TSK-0001");
        expect(task.has_value(), "alpha task should be present");
        expect((*task)["topic"].asString() == "Native Migration", "seeded task should expose topic name");
        expect((*task)["gate_status"]["ready"]["state"].asString() == "passed",
               "ready item with required sections should pass ready gate");
        expect((*task)["gate_status"]["ready"]["native_checks"].size() >= 1,
               "ready gate should expose native checks");
        expect((*task)["gate_status"]["ready"]["source_fields"].size() >= 1,
               "ready gate should expose source fields");

        auto incompleteReady = find_item(all["items"], "product-alpha", "PRA-TSK-0003");
        expect(incompleteReady.has_value(), "incomplete ready task should be present");
        expect((*incompleteReady)["gate_status"]["ready"]["state"].asString() == "failed",
               "ready item missing required sections should fail ready gate");
        expect((*incompleteReady)["gate_status"]["ready"]["blockers"].size() >= 1,
               "failed ready gate should expose blockers");

        auto reviewPanelTask = find_item(all["items"], "product-alpha", "PRA-TSK-0004");
        expect(reviewPanelTask.has_value(), "review panel task should be present");
        expect((*reviewPanelTask)["priority"].asString() == "P1",
               "item json should expose priority for review-first detail rendering");
        expect((*reviewPanelTask)["gate_status"]["ready"]["state"].asString() == "passed",
               "risks / dependencies heading should satisfy ready gate");

        auto betaBug = find_item(all["items"], "product-beta", "PRB-BUG-0001");
        expect(betaBug.has_value(), "beta bug should be present");
        expect((*betaBug)["gate_status"]["ready"]["state"].asString() == "unknown",
               "non-ready item should report unknown ready gate");

        auto doneWithEvidence = find_item(all["items"], "product-beta", "PRB-BUG-0002");
        expect(doneWithEvidence.has_value(), "done item with evidence should be present");
        expect((*doneWithEvidence)["gate_status"]["done"]["state"].asString() == "passed",
               "done item with sufficient evidence should pass done gate");

        auto doneWithoutEvidence = find_item(all["items"], "product-beta", "PRB-BUG-0003");
        expect(doneWithoutEvidence.has_value(), "done item without evidence should be present");
        expect((*doneWithoutEvidence)["gate_status"]["done"]["state"].asString() == "failed",
               "done item without sufficient evidence should fail done gate");
        expect((*doneWithoutEvidence)["gate_status"]["review"]["state"].asString() == "failed",
               "done path lacking evidence should fail review gate");

        auto reviewWithEvidence = find_item(all["items"], "product-beta", "PRB-BUG-0004");
        expect(reviewWithEvidence.has_value(), "review item with evidence should be present");
        expect((*reviewWithEvidence)["gate_status"]["review"]["state"].asString() == "passed",
               "review item with sufficient evidence should pass review gate");

        webview::ItemQueryOptions taskText;
        taskText.types = {"Task"};
        taskText.text = "migration evidence";
        auto taskTextResult = service.QueryItems(taskText);
        expect(taskTextResult["total"].asUInt64() == 1, "type and text filter should narrow to one task");
        expect(taskTextResult["items"][0]["id"].asString() == "PRA-TSK-0001", "filtered task id mismatch");

        webview::ItemQueryOptions betaDoing;
        betaDoing.products = {"product-beta"};
        betaDoing.states = {"InProgress"};
        auto betaResult = service.QueryItems(betaDoing);
        expect(betaResult["total"].asUInt64() == 1, "product and state filter should narrow to beta bug");
        expect(betaResult["items"][0]["product"].asString() == "product-beta", "filtered product mismatch");

        webview::ItemQueryOptions limited;
        limited.limit = 2;
        auto limitedResult = service.QueryItems(limited);
        expect(limitedResult["total"].asUInt64() == all["total"].asUInt64(), "limited query should preserve total");
        expect(limitedResult["items"].size() == 2, "limited query should return requested page size");

        webview::ItemQueryOptions treeOptions;
        treeOptions.types = {"Epic", "Task"};
        auto tree = service.BuildTree(treeOptions);
        expect(!tree.isMember("error"), "tree query should not fail");
        expect(tree["roots"].size() == 2, "filtered tree should include the epic root and standalone review task");
        bool foundEpicRoot = false;
        bool foundStandaloneTaskRoot = false;
        for (const auto& rootNode : tree["roots"]) {
            if (rootNode["id"].asString() == "PRA-EPIC-0001") {
                foundEpicRoot = true;
                expect(rootNode["children"].size() == 3, "tree should attach task children under the epic root");
            }
            if (rootNode["id"].asString() == "PRA-TSK-0004") {
                foundStandaloneTaskRoot = true;
            }
        }
        expect(foundEpicRoot, "filtered tree should keep the epic root");
        expect(foundStandaloneTaskRoot, "filtered tree should keep standalone tasks without parents as roots");

        auto kanban = service.BuildKanban(betaDoing);
        expect(kanban["lanes"]["Doing"].size() == 1, "kanban should place InProgress item in Doing lane");

        auto detail = service.GetItem("all", "PRA-TSK-0001");
        expect(!detail.isMember("error"), "all-product detail lookup should find task");
        expect(detail["item"]["product"].asString() == "product-alpha", "detail lookup product mismatch");
        expect(detail["item"]["content"].asString().find("Native migration evidence") != std::string::npos,
               "detail lookup should include content");
        expect(detail["item"]["gate_status"]["ready"]["state"].asString() == "passed",
               "item detail should include gate_status");

        auto savedViews = service.ListSavedViews();
        expect(savedViews["views"].size() >= 4, "saved views should expose review lanes");

        auto readyView = service.RunSavedView("ready-frontier", allOptions);
        expect(!readyView.isMember("error"), "ready saved view should run");
        expect(readyView["result"]["total"].asUInt64() >= 1, "ready saved view should include ready work");

        auto kobql = service.RunKobql("state:Ready type:Task topic:\"Native Migration\"", allOptions);
        expect(!kobql.isMember("error"), "KOBQL query should run");
        expect(kobql["total"].asUInt64() == 1, "KOBQL query should filter by state, type, and topic");

        auto preview = service.PreviewCommand("show ready tasks", allOptions);
        expect(!preview.isMember("error"), "command preview should parse supported phrase");
        expect(preview["generated_kobql"].asString().find("state:Ready") != std::string::npos,
               "command preview should expose generated KOBQL");
        expect(preview["mutation_allowed"].asBool() == false, "command preview must stay read-only");

        auto inbox = service.BuildReviewInbox(allOptions);
        expect(!inbox["lanes"].isMember("Ready Approval"), "review inbox should not expose legacy Ready Approval lane");
        for (const auto& lane : {"Needs Review", "Done Candidate", "False Done Suspect", "Evidence Gap",
                                 "Blocked/Dirty", "Stale/Drift", "Ready Frontier"}) {
            expect(inbox["lanes"].isMember(lane), std::string("review inbox should expose queue: ") + lane);
        }
        expect(inbox["lane_taxonomy"].size() >= 7, "review inbox should expose lane taxonomy metadata");
        expect(inbox["lanes"]["Ready Frontier"].size() >= 1, "review inbox should expose ready frontier queue");
        expect(!inbox["lanes"]["Ready Frontier"][0]["review_reason"].asString().empty(),
               "review inbox bundles should explain why the item needs review");
        expect(!inbox["lanes"]["Ready Frontier"][0]["reason_code"].asString().empty(),
               "review inbox bundles should expose deterministic reason codes");
        expect(inbox["lanes"]["Ready Frontier"][0]["source_fields"].size() >= 1,
               "review inbox bundles should expose source fields");

        auto evidence = service.GetEvidenceDetail("product-alpha", "PRA-TSK-0001");
        expect(evidence["evidence"]["signals"]["artifact"].asBool(), "evidence detail should detect artifact signal");
        expect(evidence["evidence"]["signals"]["validation"].asBool(), "evidence detail should detect validation signal");
        expect(evidence["worklog_events"].size() >= 3, "evidence detail should expose worklog events");

        auto topicHome = service.BuildTopicHome("Native Migration", allOptions);
        expect(topicHome["items"].size() >= 1, "topic home should include seeded topic item");
        expect(topicHome["missing_topic_metadata"].asBool() == false, "topic home should find manifest metadata");

        auto graph = service.BuildDependencyGraph(allOptions, "PRA-TSK-0001");
        expect(graph["nodes"].size() >= 1, "dependency graph should include selected item node");
        expect(graph["edges"].size() >= 1, "dependency graph should include parent or topic edges");
        expect(graph["visualization"]["kind"].asString() == "first-party-svg",
               "dependency graph should advertise the first-party visualization payload");
        expect(has_edge(graph["edges"], "product-alpha:PRA-EPIC-0001", "product-alpha:PRA-TSK-0001", "parent"),
               "dependency graph should include structural parent edge");
        expect(has_edge(graph["edges"], "topic:Native Migration", "product-alpha:PRA-TSK-0001", "topic-membership"),
               "dependency graph should include grouping topic edge");
        expect(has_edge(graph["edges"], "product-alpha:PRA-TSK-0001", "product-alpha:PRA-TSK-0002", "blocks"),
               "links.blocks should render A -> B dependency direction");
        expect(has_edge(graph["edges"], "product-alpha:PRA-TSK-0001", "product-alpha:PRA-TSK-0002", "blocked_by"),
               "links.blocked_by should render blocker -> blocked dependency direction");
        expect(has_edge(graph["edges"], "product-alpha:PRA-TSK-0001", "product-beta:PRB-BUG-0001", "relates"),
               "dependency graph should include cross-product relates reference");
        expect(has_edge(graph["edges"], "product-alpha:PRA-TSK-0001", "product-alpha:PRA-TSK-0003", "relates"),
               "dependency graph should include non-blocking relates reference");
        expect(graph["missing_nodes"].size() >= 1, "dependency graph should expose unresolved references");
        expect(graph["dependency_cycles"].empty(),
               "related-only cycles should not participate in dependency cycle semantics");

        webview::ItemQueryOptions boundedOptions;
        boundedOptions.limit = 1;
        auto boundedGraph = service.BuildDependencyGraph(boundedOptions);
        expect(boundedGraph["truncated"].asBool(), "dependency graph should report bounded truncated output");

        auto timeline = service.BuildWorkOrderTimeline(allOptions, "PRA-TSK-0001");
        expect(timeline["events"].size() >= 3, "timeline should expose worklog-backed events");

        auto runs = service.BuildAgentRunBoard(allOptions, "codex");
        expect(runs["runs"].size() >= 1, "agent run board should include codex run evidence");

        auto treePartial = service.RenderTreePartial(treeOptions);
        expect(treePartial.find("PRA-EPIC-0001") != std::string::npos,
               "tree partial should render item ids");
        expect(treePartial.find("data-item-id") != std::string::npos,
               "tree partial should expose item link hooks");

        auto kanbanPartial = service.RenderKanbanPartial(betaDoing);
        expect(kanbanPartial.find("Beta live bug") != std::string::npos,
               "kanban partial should render matching cards");
        expect(kanbanPartial.find("data-selectable-item=\"true\"") != std::string::npos,
               "kanban partial should expose selectable card markup");
        expect(kanbanPartial.find("data-item-product=\"product-beta\"") != std::string::npos,
               "kanban partial should expose selectable item product metadata");
        expect(kanbanPartial.find("gate-strip") != std::string::npos,
               "kanban partial should render compact gate badges");
        expect(kanbanPartial.find("aria-selected=\"false\"") != std::string::npos,
               "kanban partial should expose initial aria-selected state");

        auto reviewPartial = service.RenderReviewPartial(allOptions);
        expect(reviewPartial.find("Ready Frontier") != std::string::npos,
               "review partial should render review queues");
        expect(reviewPartial.find("Done Candidate") != std::string::npos,
               "review partial should render canonical done candidate lane");
        expect(reviewPartial.find("Why this needs review") != std::string::npos,
               "review partial should render review reasons");
        expect(reviewPartial.find("data-selectable-item=\"true\"") != std::string::npos,
               "review partial should expose selectable review cards");
        expect(reviewPartial.find("aria-label=\"Ready gate") != std::string::npos,
               "review partial should expose accessible gate badge labels");

        auto contextPartial = service.RenderContextPartial(allOptions);
        expect(contextPartial.find("Native Migration") != std::string::npos,
               "context partial should render topic context");
        expect(contextPartial.find("data-selectable-item=\"true\"") != std::string::npos,
               "context partial should expose selectable context cards");

        auto filterPartial = service.RenderFiltersPartial(allOptions);
        expect(filterPartial.find("product-alpha") != std::string::npos,
               "filters partial should render products");

        auto itemPartial = service.RenderItemPartial("product-alpha", "PRA-TSK-0004");
        expect(itemPartial.find("Alpha review panel task") != std::string::npos,
               "item partial should render the review-first identity header");
        expect(itemPartial.find(">Priority<") != std::string::npos &&
               itemPartial.find("P1") != std::string::npos,
               "item partial should render priority in the header facts");
        expect(itemPartial.find(">Updated<") != std::string::npos &&
               itemPartial.find("2026-06-14") != std::string::npos,
               "item partial should render the updated timestamp");
        expect(itemPartial.find("Gate status") != std::string::npos,
               "item partial should render gate status details");
        expect(itemPartial.find("required_ready_sections") != std::string::npos,
               "item partial should render native gate checks");
        expect(itemPartial.find(">Owner<") == std::string::npos,
               "item partial should hide empty owner metadata");
        expect(itemPartial.find(">External<") == std::string::npos,
               "item partial should hide empty external metadata maps");
        expect(itemPartial.find(">Tags<") == std::string::npos,
               "item partial should hide empty tags");
        expect(itemPartial.find(">Decisions<") == std::string::npos,
               "item partial should hide empty decisions");
        expect(itemPartial.find(">Blocks<") == std::string::npos,
               "item partial should hide empty blocks links");
        expect(itemPartial.find(">Blocked by<") == std::string::npos,
               "item partial should hide empty blocked-by links");
        expect(itemPartial.find(">Area<") != std::string::npos &&
               itemPartial.find("review-ui") != std::string::npos,
               "item partial should keep non-empty metadata visible");
        expect(itemPartial.find(">Relates<") != std::string::npos &&
               itemPartial.find("PRA-TSK-0003") != std::string::npos,
               "item partial should keep non-empty relations visible");
        expect(itemPartial.find("Raw markdown / full file") != std::string::npos,
               "item partial should expose the explicit raw markdown toggle");
        expect_in_order(itemPartial,
                        {">Context<", ">Goal<", ">Acceptance Criteria<",
                         ">Risks / Dependencies<", ">Worklog<",
                         "Raw markdown / full file"},
                        "review-first sections should appear before the raw markdown toggle");

        const auto mainSource = read_text(
            locate_repo_file(std::filesystem::path("src") / "cpp" / "code" /
                             "apps" / "kano_backlog_webview" / "main.cpp"));
        expect(mainSource.find("data-selectable-item") != std::string::npos,
               "main source should embed selectable card markup hooks");
        expect(mainSource.find("Shortcuts ?") != std::string::npos,
               "main source should expose the visible shortcut help affordance");
        expect(mainSource.find("aria-keyshortcuts=\"?\"") != std::string::npos,
               "main source should expose shortcut help aria-keyshortcuts");
        expect(mainSource.find("aria-keyshortcuts=\"/\"") != std::string::npos,
               "main source should expose search aria-keyshortcuts");
        expect(mainSource.find("function isTypingContext") != std::string::npos,
               "main source should keep the typing-context shortcut guard");
        expect(mainSource.find("function selectItemByDelta") != std::string::npos,
               "main source should keep keyboard selection helpers");
        expect(mainSource.find("selectedItemVisibleIndex") != std::string::npos,
               "main source should keep a single roving-tabindex card selection index");
        expect(mainSource.find("card === selectedCard") != std::string::npos,
               "main source should select one visible card instance even when item keys repeat");
        expect(mainSource.find("function openSelectedItem") != std::string::npos,
               "main source should keep keyboard open helper");
        expect(mainSource.find("Focus the backlog search field") != std::string::npos,
               "main source should document slash-search help text");

        const auto smokeScript = read_text(
            locate_repo_file(std::filesystem::path("src") / "shell" / "webview" /
                             "smoke-artifacts.sh"));
        expect(smokeScript.find("root.html") != std::string::npos,
               "smoke artifact script should capture root html");
        expect(smokeScript.find("items-all-limit-10.json") != std::string::npos,
               "smoke artifact script should capture bounded items json");
        expect(smokeScript.find("healthz.txt") != std::string::npos,
               "smoke artifact script should capture healthz output");

        const auto pixiToml = read_text(locate_repo_file("pixi.toml"));
        expect(pixiToml.find("webview-smoke-artifacts") != std::string::npos,
               "pixi manifest should expose the smoke artifact command");

        const auto readme = read_text(locate_repo_file("README.md"));
        expect(readme.find("pixi run webview-smoke-artifacts") != std::string::npos,
               "README should document the smoke artifact command");
        expect(readme.find("_ws/test-output/webview-smoke") != std::string::npos,
               "README should document the deterministic smoke artifact path");

        const auto docsIndex = read_text(
            locate_repo_file(std::filesystem::path("docs") / "index.md"));
        expect(docsIndex.find("pixi run webview-smoke-artifacts") != std::string::npos,
               "docs index should document the smoke artifact command");
        expect(docsIndex.find("_ws/test-output/webview-smoke") != std::string::npos,
               "docs index should document the deterministic smoke artifact path");

        std::cout << "webview_service_smoke_test: PASS\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& ex) {
        if (!root.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove_all(root, cleanupError);
        }
        std::cerr << "webview_service_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
