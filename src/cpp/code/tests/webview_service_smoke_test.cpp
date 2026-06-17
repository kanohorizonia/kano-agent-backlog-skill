#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

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

std::string item_doc(const std::string& id,
                     const std::string& uid,
                     const std::string& type,
                     const std::string& title,
                     const std::string& state,
                     const std::string& parent,
                     const std::string& body) {
    std::ostringstream out;
    out << "---\n";
    out << "id: " << id << "\n";
    out << "uid: " << uid << "\n";
    out << "type: " << type << "\n";
    out << "title: " << title << "\n";
    out << "state: " << state << "\n";
    out << "parent: " << (parent.empty() ? "null" : parent) << "\n";
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
                     "## Worklog\n\n"
                     "2026-06-14 10:00 [agent=codex] Work order dispatched for native migration review.\n"
                     "2026-06-14 10:10 [agent=codex] Artifact attached: [report](../artifacts/PRA-TSK-0001/report.md).\n"
                     "2026-06-14 10:20 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        write_text(
            products / "product-beta" / "items" / "bug" / "0001" / "PRB-BUG-0001.md",
            item_doc("PRB-BUG-0001",
                     "019ec100-0000-7000-8000-000000000003",
                     "Bug",
                     "Beta live bug",
                     "InProgress",
                     "",
                     "Beta product bug body."));

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
        expect(all["total"].asUInt64() == 4, "all-products query should include items plus unique topic pseudo-items");

        auto task = find_item(all["items"], "product-alpha", "PRA-TSK-0001");
        expect(task.has_value(), "alpha task should be present");
        expect((*task)["topic"].asString() == "Native Migration", "seeded task should expose topic name");

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
        expect(limitedResult["total"].asUInt64() == 4, "limited query should preserve total");
        expect(limitedResult["items"].size() == 2, "limited query should return requested page size");

        webview::ItemQueryOptions treeOptions;
        treeOptions.types = {"Epic", "Task"};
        auto tree = service.BuildTree(treeOptions);
        expect(!tree.isMember("error"), "tree query should not fail");
        expect(tree["roots"].size() == 1, "filtered tree should have one root");
        expect(tree["roots"][0]["id"].asString() == "PRA-EPIC-0001", "tree root id mismatch");
        expect(tree["roots"][0]["children"].size() == 1, "tree should attach task child");

        auto kanban = service.BuildKanban(betaDoing);
        expect(kanban["lanes"]["Doing"].size() == 1, "kanban should place InProgress item in Doing lane");

        auto detail = service.GetItem("all", "PRA-TSK-0001");
        expect(!detail.isMember("error"), "all-product detail lookup should find task");
        expect(detail["item"]["product"].asString() == "product-alpha", "detail lookup product mismatch");
        expect(detail["item"]["content"].asString().find("Native migration evidence") != std::string::npos,
               "detail lookup should include content");

        auto savedViews = service.ListSavedViews();
        expect(savedViews["views"].size() >= 4, "saved views should expose review lanes");

        auto readyView = service.RunSavedView("ready-approval", allOptions);
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
        expect(inbox["lanes"]["Ready Approval"].size() >= 1, "review inbox should classify ready work");

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

        auto timeline = service.BuildWorkOrderTimeline(allOptions, "PRA-TSK-0001");
        expect(timeline["events"].size() >= 3, "timeline should expose worklog-backed events");

        auto runs = service.BuildAgentRunBoard(allOptions, "codex");
        expect(runs["runs"].size() >= 1, "agent run board should include codex run evidence");

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
