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

Json::Value parse_json_text(const std::string& text,
                            const std::string& label) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("failed to parse " + label + ": " + errors);
    }
    return value;
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
                     const std::string& extra_frontmatter = "",
                     const std::string& updated = "2026-06-14") {
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
    out << "updated: " << updated << "\n";
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

std::optional<Json::Value> find_finding(const Json::Value& findings,
                                        const std::string& product,
                                        const std::string& item_id,
                                        const std::string& reason_code) {
    for (const auto& finding : findings) {
        if (finding["product"].asString() == product &&
            finding["item_id"].asString() == item_id &&
            finding["reason_code"].asString() == reason_code) {
            return finding;
        }
    }
    return std::nullopt;
}

std::optional<Json::Value> find_quality_row(const Json::Value& rows,
                                            const std::string& product,
                                            const std::string& item_id) {
    for (const auto& row : rows) {
        if (row["product"].asString() == product &&
            row["item_id"].asString() == item_id) {
            return row;
        }
    }
    return std::nullopt;
}

std::optional<Json::Value> find_goal(const Json::Value& goals,
                                     const std::string& goal_id) {
    for (const auto& goal : goals) {
        if (goal["goal_id"].asString() == goal_id) {
            return goal;
        }
    }
    return std::nullopt;
}

std::optional<Json::Value> find_decision_row(const Json::Value& rows,
                                             const std::string& adr_id) {
    for (const auto& row : rows) {
        if (row["adr_id"].asString() == adr_id) {
            return row;
        }
    }
    return std::nullopt;
}

bool has_string_value(const Json::Value& array,
                      const std::string& value) {
    for (const auto& entry : array) {
        if (entry.asString() == value) {
            return true;
        }
    }
    return false;
}

bool has_logical_ref(const Json::Value& refs,
                     const std::string& key,
                     const std::string& value) {
    for (const auto& ref : refs) {
        if (ref.isMember(key) && ref[key].asString() == value) {
            return true;
        }
    }
    return false;
}

bool has_edge(const Json::Value& edges,
              const std::string& from,
              const std::string& to,
              const std::string& kind) {
    for (const auto& edge : edges) {
        const auto edgeKind = edge.isMember("edge_type") ? edge["edge_type"].asString() : edge["kind"].asString();
        if (edge["from"].asString() == from &&
            edge["to"].asString() == to &&
            edgeKind == kind) {
            return true;
        }
    }
    return false;
}

bool has_node(const Json::Value& nodes,
              const std::string& id,
              const std::string& node_type) {
    for (const auto& node : nodes) {
        if (node["id"].asString() == id &&
            node["node_type"].asString() == node_type) {
            return true;
        }
    }
    return false;
}

bool has_diagnostic(const Json::Value& diagnostics,
                    const std::string& code,
                    const std::string& target) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic["code"].asString() == code &&
            diagnostic["target"].asString() == target) {
            return true;
        }
    }
    return false;
}

std::string json_to_string(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::size_t count_regular_files(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root)) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            ++count;
        }
    }
    return count;
}

bool directory_tree_contains_text(const std::filesystem::path& root,
                                  const std::string& needle) {
    if (!std::filesystem::exists(root)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && read_text(entry.path()).find(needle) != std::string::npos) {
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

void expect_context_section(const Json::Value& summary,
                            const std::string& key) {
    expect(summary[key].isObject(), "context recovery should include section " + key);
    expect(!summary[key]["summary"].asString().empty(), key + " should include summary");
    expect(summary[key]["refs"].isArray(), key + " should include refs");
    expect(!summary[key]["confidence"].asString().empty(), key + " should include confidence");
    expect(summary[key]["notes"].isArray(), key + " should include notes");
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
            products / "product-alpha" / "items" / "initiative" / "0001" / "PRA-INIT-0001.md",
            item_doc("PRA-INIT-0001",
                     "019ec100-0000-7000-8000-000000000010",
                     "Initiative",
                     "Alpha platform initiative",
                     "Ready",
                     "",
                     "Initiative component narrative.\n\n"
                     "## Context\n\n"
                     "Alpha platform is an independently releasable component.\n\n"
                     "## Goal\n\n"
                     "Keep component ownership visible above release stories.\n"));
        write_text(
            products / "product-alpha" / "items" / "epic" / "0001" / "PRA-EPIC-0001.md",
            item_doc("PRA-EPIC-0001",
                     "019ec100-0000-7000-8000-000000000001",
                     "Epic",
                     "Alpha epic",
                     "Ready",
                     "PRA-INIT-0001",
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
            products / "product-alpha" / "items" / "task" / "0005" / "PRA-TSK-0005.md",
            item_doc("PRA-TSK-0005",
                     "019ec100-0000-7000-8000-000000000018",
                     "Task",
                     "Alpha roadmap evidence-backed task",
                     "Done",
                     "PRA-EPIC-0001",
                     "Evidence-backed roadmap task.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 15:00 [agent=codex] Work order dispatched for roadmap evidence coverage.\n"
                     "2026-06-14 15:10 [agent=codex] Artifact attached: [report](../artifacts/PRA-TSK-0005/report.md).\n"
                     "2026-06-14 15:20 [agent=codex] Commit evidence: implementation_commit=aaa1111 revision aaa1111.\n"
                     "2026-06-14 15:30 [agent=codex] Branch convergence: target=main implementation_commit=aaa1111 reachable_from_target=true remote_publication=origin/main.\n"
                     "2026-06-14 15:40 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        write_text(
            products / "product-alpha" / "items" / "task" / "0006" / "PRA-TSK-0006.md",
            item_doc("PRA-TSK-0006",
                     "019ec100-0000-7000-8000-000000000019",
                     "Task",
                     "Alpha roadmap closed without evidence",
                     "Done",
                     "PRA-EPIC-0001",
                     "Closed roadmap task without durable evidence."));
        write_text(
            products / "product-alpha" / "items" / "task" / "0007" / "PRA-TSK-0007.md",
            item_doc("PRA-TSK-0007",
                     "019ec100-0000-7000-8000-000000000020",
                     "Task",
                     "Alpha roadmap partial task",
                     "InProgress",
                     "PRA-EPIC-0001",
                     "Active roadmap task.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 16:00 [agent=codex] Validation: pixi run quick-test PASS.\n"));
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
                     "Alpha stale drift related task",
                     "Ready",
                     "PRA-EPIC-0001",
                     "Related-only cycle coverage with stale drift signal.",
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
                     "Beta product bug body.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 11:30 [agent=codex] Validation: pixi run quick-test PASS.\n",
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
                      "2026-06-14 10:50 [agent=codex] Work order dispatched for done evidence review.\n"
                      "2026-06-14 11:00 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0002/report.md).\n"
                      "2026-06-14 11:05 [agent=codex] Commit evidence: implementation_commit=abc1234 revision abc1234.\n"
                      "2026-06-14 11:10 [agent=codex] Branch convergence: target=main implementation_commit=abc1234 reachable_from_target=true remote_publication=origin/main.\n"
                      "2026-06-14 11:20 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        write_text(
            products / "product-beta" / "items" / "bug" / "0003" / "PRB-BUG-0003.md",
            item_doc("PRB-BUG-0003",
                     "019ec100-0000-7000-8000-000000000007",
                     "Bug",
                     "Beta done without evidence",
                     "Done",
                     "",
                     "Closed without durable proof."));
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
        const auto missingCommitPath = products / "product-beta" / "items" / "bug" / "0005" / "PRB-BUG-0005.md";
        write_text(
            missingCommitPath,
            item_doc("PRB-BUG-0005",
                     "019ec100-0000-7000-8000-000000000015",
                     "Bug",
                     "Beta done missing source-control evidence",
                     "Done",
                     "",
                     "Done item with validation and artifact but no source-control proof.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 12:30 [agent=codex] Work order dispatched for source-control detector coverage.\n"
                     "2026-06-14 12:40 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0005/report.md).\n"
                     "2026-06-14 12:50 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        const auto staleDonePath = products / "product-beta" / "items" / "bug" / "0006" / "PRB-BUG-0006.md";
        write_text(
            staleDonePath,
            item_doc("PRB-BUG-0006",
                     "019ec100-0000-7000-8000-000000000016",
                     "Bug",
                     "Beta done with stale worklog",
                     "Done",
                     "",
                     "Done item whose markdown updated after the latest evidence worklog.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 13:00 [agent=codex] Work order dispatched for stale worklog detector coverage.\n"
                     "2026-06-14 13:10 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0006/report.md).\n"
                     "2026-06-14 13:20 [agent=codex] Commit evidence: implementation_commit=def5678 revision def5678.\n"
                     "2026-06-14 13:30 [agent=codex] Branch convergence: target=main implementation_commit=def5678 reachable_from_target=true remote_publication=origin/main.\n"
                     "2026-06-14 13:40 [agent=codex] Validation: pixi run quick-test PASS.\n",
                     "",
                     "2026-06-20"));
        const auto branchUnknownPath = products / "product-beta" / "items" / "bug" / "0007" / "PRB-BUG-0007.md";
        write_text(
            branchUnknownPath,
            item_doc("PRB-BUG-0007",
                     "019ec100-0000-7000-8000-000000000017",
                     "Bug",
                     "Beta done missing target reachability",
                     "Done",
                     "",
                     "Done item with commit and push evidence but no explicit target reachability note.\n\n"
                     "## Worklog\n\n"
                     "2026-06-14 14:00 [agent=codex] Work order dispatched for target reachability detector coverage.\n"
                     "2026-06-14 14:10 [agent=codex] Artifact attached: [report](../artifacts/PRB-BUG-0007/report.md).\n"
                     "2026-06-14 14:20 [agent=codex] Commit evidence: commit abc9999 pushed to origin/main.\n"
                     "2026-06-14 14:30 [agent=codex] Validation: pixi run quick-test PASS.\n"));
        write_text(
            products / "product-alpha" / "items" / "feature" / "0002" / "PRA-FTR-0002.md",
            item_doc("PRA-FTR-0002",
                     "019ec100-0000-7000-8000-000000000011",
                     "Feature",
                     "Alpha explicit docs capability",
                     "Ready",
                     "",
                      "Route this feature through explicit capability metadata.",
                      "external:\n"
                      "  capability_route: docs\n"));
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0001_product-map-navigation.md",
            "---\n"
            "id: PRA-ADR-0001\n"
            "title: Product Map navigation model\n"
            "decision_status: accepted\n"
            "feature_refs:\n"
            "  - PRA-FTR-0002\n"
            "accepted_option: Read-only Product Map projection over durable refs\n"
            "rejected_options:\n"
            "  - Canvas-first mutation surface\n"
            "evidence_refs:\n"
            "  - PRA-TSK-0001\n"
            "superseded_by: []\n"
            "revisit_condition: Product Map needs write behavior\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# Product Map navigation model\n\n"
            "Use Backboard navigation to connect features, ADRs, and evidence.\n");
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0002_missing-evidence-gap.md",
            "---\n"
            "id: PRA-ADR-0002\n"
            "title: ADR without evidence refs\n"
            "decision_status: stale\n"
            "feature_refs:\n"
            "  - PRA-FTR-0002\n"
            "evidence_refs: []\n"
            "superseded_by:\n"
            "  - PRA-ADR-0001\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# ADR without evidence refs\n\n"
            "This legacy ADR intentionally lacks evidence refs for diagnostics coverage.\n");
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0003_missing-stale-refs.md",
            "---\n"
            "id: PRA-ADR-0003\n"
            "title: ADR with missing stale refs\n"
            "decision_status: stale\n"
            "feature_refs:\n"
            "  - PRA-FTR-9999\n"
            "evidence_refs:\n"
            "  - PRA-TSK-9999\n"
            "superseded_by:\n"
            "  - PRA-ADR-9999\n"
            "revisit_condition: Missing Product Map refs must be reconciled\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# ADR with missing stale refs\n\n"
            "This ADR intentionally points at missing refs for Product Map diagnostics.\n");
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0004_superseded-decision.md",
            "---\n"
            "id: PRA-ADR-0004\n"
            "title: Superseded ADR radar row\n"
            "decision_status: superseded\n"
            "feature_refs:\n"
            "  - PRA-FTR-0002\n"
            "evidence_refs:\n"
            "  - PRA-TSK-0001\n"
            "superseded_by:\n"
            "  - PRA-ADR-0001\n"
            "revisit_condition: Supersession must remain linked\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# Superseded ADR radar row\n\n"
            "This ADR is intentionally superseded for Decision Radar coverage.\n");
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0005_revisit-needed.md",
            "---\n"
            "id: PRA-ADR-0005\n"
            "title: ADR revisit needed row\n"
            "decision_status: revisit_needed\n"
            "feature_refs:\n"
            "  - PRA-FTR-0002\n"
            "evidence_refs:\n"
            "  - PRA-TSK-0001\n"
            "superseded_by: []\n"
            "revisit_condition: Product Map navigation begins linking decision debt\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# ADR revisit needed row\n\n"
            "This ADR intentionally requires human revisit without mutating item state.\n");
        write_text(
            products / "product-alpha" / "decisions" / "PRA-ADR-0006_evidence-challenged.md",
            "---\n"
            "id: PRA-ADR-0006\n"
            "title: ADR evidence challenged row\n"
            "decision_status: accepted\n"
            "feature_refs:\n"
            "  - PRA-FTR-0002\n"
            "evidence_refs:\n"
            "  - PRA-TSK-0006\n"
            "superseded_by: []\n"
            "revisit_condition: Evidence chain becomes incomplete\n"
            "date: 2026-06-14\n"
            "---\n\n"
            "# ADR evidence challenged row\n\n"
            "This ADR points at incomplete evidence for Decision Radar coverage.\n");
        write_text(
            products / "product-alpha" / "roadmap" / "version-goal-ledger-0.1.0.json",
            R"json({
  "schema": "kob.roadmap.version_goal_ledger.v1",
  "product": "product-alpha",
  "target_version": "0.1.0",
  "goals": [
    {
      "goal_id": "goal-done-evidence-backed",
      "summary": "Done goal has closed work and durable evidence.",
      "status": "Done",
      "evidence_quality": "strong",
      "linked_refs": [
        { "product": "product-alpha", "item_id": "PRA-TSK-0005" }
      ],
      "dogfood_coverage": "webview smoke",
      "gap_state": "none",
      "rationale": "Done requires item-level validation, artifact, commit, and branch convergence evidence."
    },
    {
      "goal_id": "goal-closed-unverified",
      "summary": "Closed work remains implemented but unverified.",
      "status": "Done",
      "evidence_quality": "missing",
      "linked_refs": [
        { "product": "product-alpha", "item_id": "PRA-TSK-0006" }
      ],
      "dogfood_coverage": "webview smoke",
      "gap_state": "closed ticket lacks evidence",
      "rationale": "Closed tickets alone must not become completed roadmap goals."
    },
    {
      "goal_id": "goal-cut-scope",
      "summary": "Cut scope remains visible as a decision.",
      "status": "Cut",
      "evidence_quality": "unclear",
      "linked_refs": [
        { "product": "product-alpha", "item_id": "PRA-FTR-0002" }
      ],
      "dogfood_coverage": "not applicable",
      "gap_state": "scope cut before release",
      "rationale": "Keep cut scope visible instead of hiding it under hierarchy state."
    }
  ]
})json");
        write_text(
            products / "product-alpha" / "roadmap" / "version-goal-ledger-0.2.0.json",
            R"json({
  "schema": "kob.roadmap.version_goal_ledger.v1",
  "product": "product-alpha",
  "target_version": "0.2.0",
  "goals": [
    {
      "goal_id": "goal-partial-active",
      "summary": "Partial goal has active linked work.",
      "status": "Partial",
      "evidence_quality": "weak",
      "linked_refs": [
        { "product": "product-alpha", "item_id": "PRA-TSK-0007" }
      ],
      "dogfood_coverage": "webview smoke",
      "gap_state": "remaining work active",
      "rationale": "Partial is distinct from closed or evidence-backed Done."
    },
    {
      "goal_id": "goal-deferred-scope",
      "summary": "Deferred scope remains visible for the next slice.",
      "status": "Deferred",
      "evidence_quality": "unclear",
      "linked_refs": [
        { "product": "product-alpha", "adr_id": "PRA-ADR-0001" }
      ],
      "dogfood_coverage": "not applicable",
      "gap_state": "deferred after ADR review",
      "rationale": "Deferred roadmap scope should not disappear from review."
    }
  ]
})json");
        write_text(
            products / "product-alpha" / "roadmap" / "version-goal-ledger-future.json",
            R"json({
  "schema": "kob.roadmap.version_goal_ledger.v1",
  "product": "product-alpha",
  "target_version": "future",
  "goals": [
    {
      "goal_id": "goal-stale-missing-refs",
      "summary": "Missing and stale links are shown as gaps.",
      "status": "Unknown",
      "evidence_quality": "stale",
      "linked_refs": [
        { "product": "product-alpha", "item_id": "PRA-TSK-9999" },
        { "product": "product-alpha", "adr_id": "PRA-ADR-9999" }
      ],
      "dogfood_coverage": "webview smoke",
      "gap_state": "links require reconciliation",
      "rationale": "Do not invent refs when the roadmap ledger is stale."
    },
    {
      "goal_id": "goal-unknown-no-refs",
      "summary": "Unsupported future goal remains unknown.",
      "status": "Unknown",
      "evidence_quality": "unclear",
      "linked_refs": [],
      "dogfood_coverage": "none",
      "gap_state": "no evidence chain",
      "rationale": "Unknown is safer than inventing roadmap history."
    }
  ]
})json");
        write_text(
            products / "product-alpha" / "items" / "feature" / "0003" / "PRA-FTR-0003.md",
            item_doc("PRA-FTR-0003",
                     "019ec100-0000-7000-8000-000000000012",
                     "Feature",
                     "Alpha neutral capability fallback",
                     "Ready",
                     "",
                     "Routine backlog review body for fallback routing."));
        write_text(
            products / "product-alpha" / "items" / "feature" / "0004" / "PRA-FTR-0004.md",
            item_doc("PRA-FTR-0004",
                     "019ec100-0000-7000-8000-000000000013",
                     "Feature",
                     "Alpha ambiguous capability metadata",
                     "Ready",
                     "",
                     "Route this feature only after humans choose the deterministic capability.",
                     "external:\n"
                     "  capability_routes: native-cpp, docs\n"));
        write_text(
            products / "product-alpha" / "items" / "experiment" / "0001" / "PRA-EXP-0001.md",
            item_doc("PRA-EXP-0001",
                     "019ec100-0000-7000-8000-000000000014",
                     "Experiment",
                     "Alpha unknown capability type",
                     "Ready",
                     "",
                     "Unclassified item body without route metadata."));

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
        expect(all["total"].asUInt64() == 27, "all-products query should include items, ADRs, plus unique topic pseudo-items");
        for (const auto& item : all["items"]) {
            expect(item.isMember("gate_status"), "all-products items should include gate_status");
            expect(item["gate_status"].isMember("ready"), "gate_status should include ready gate");
            expect(item["gate_status"].isMember("review"), "gate_status should include review gate");
            expect(item["gate_status"].isMember("done"), "gate_status should include done gate");
        }

        auto initiative = find_item(all["items"], "product-alpha", "PRA-INIT-0001");
        expect(initiative.has_value(), "alpha initiative should be present");
        expect((*initiative)["type"].asString() == "Initiative", "initiative type should round-trip through service json");
        expect((*initiative)["gate_status"]["ready"]["state"].asString() == "passed",
               "initiative with Context and Goal should pass ready gate");

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

        const auto detectorFileCountBefore = count_regular_files(products);
        const auto detectorStateBefore = read_text(products / "product-beta" / "items" / "bug" / "0001" / "PRB-BUG-0001.md");
        auto doneDetector = service.BuildDoneCandidateDetector(allOptions);
        expect(!doneDetector.isMember("error"), "done detector query should not fail");
        expect(doneDetector["read_only"].asBool(), "done detector must be read-only");
        expect(!doneDetector["mutation_allowed"].asBool(), "done detector must not allow mutations");
        expect(!doneDetector["starts_agent"].asBool(), "done detector must not start agents");
        expect(!doneDetector["dispatches_work"].asBool(), "done detector must not dispatch work");
        expect(doneDetector["advisory_only"].asBool(), "done detector must be advisory only");
        expect(doneDetector["finding_count"].asUInt64() == doneDetector["findings"].size(),
               "done detector should expose bounded finding_count metadata");
        expect(doneDetector["pagination_ignored_for_full_scan"].asBool(),
               "done detector should scan the selected set instead of one query page");
        expect(doneDetector["scanned"].asUInt64() == doneDetector["query_total"].asUInt64(),
               "done detector full scan should visit every selected query match");
        expect(!doneDetector["truncated"].asBool(), "unbounded smoke detector query should not be truncated");
        expect(count_regular_files(products) == detectorFileCountBefore,
               "done detector must not create or delete files");
        expect(read_text(products / "product-beta" / "items" / "bug" / "0001" / "PRB-BUG-0001.md") == detectorStateBefore,
               "done detector must not mutate item markdown state");
        for (const auto& finding : doneDetector["findings"]) {
            expect(!finding["product"].asString().empty(), "detector finding should include product");
            expect(!finding["item_id"].asString().empty(), "detector finding should include item id");
            expect(!finding["title"].asString().empty(), "detector finding should include title");
            expect(!finding["state"].asString().empty(), "detector finding should include state");
            expect(!finding["reason_code"].asString().empty(), "detector finding should include reason code");
            expect(!finding["severity"].asString().empty(), "detector finding should include severity");
            expect(finding["last_relevant_worklog"].isObject(),
                   "detector finding should include last relevant worklog summary/ref");
            expect(finding["available_evidence_refs"].isArray(),
                   "detector finding should include evidence refs");
            expect(!finding["suggested_human_action"].asString().empty(),
                   "detector finding should include suggested human action");
            expect(finding["advisory"].asBool(), "detector findings should be advisory");
            expect(!finding["blocks_done"].asBool(), "detector findings should not block Done directly");
            expect(!finding["mutation_allowed"].asBool(), "detector findings should not allow mutation");
            expect(!finding["starts_agent"].asBool(), "detector findings should not start agents");
            expect(!finding["dispatches_work"].asBool(), "detector findings should not dispatch work");
        }
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0001", "done_candidate").has_value(),
               "in-progress item with validation should be reported as a done candidate");
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0002", "false_done_suspect") == std::nullopt,
               "strong done evidence item should not be reported as false done");
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0003", "false_done_suspect").has_value(),
               "done item without evidence should be reported as false done suspect");
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0003", "missing_validation_evidence").has_value(),
               "done item without validation should report missing validation evidence");
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0005", "missing_commit_or_push_evidence").has_value(),
               "done item without source-control proof should report missing commit or push evidence");
        expect(find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0006", "stale_worklog_after_done").has_value(),
               "done item updated after worklog evidence should report stale worklog");
        auto branchFinding = find_finding(doneDetector["findings"], "product-beta", "PRB-BUG-0007", "branch_convergence_missing_or_unknown");
        expect(branchFinding.has_value(), "done item without branch convergence proof should report unknown convergence");
        expect((*branchFinding)["diagnostic_status"].asString() == "unknown",
               "unknown branch convergence evidence should be marked unknown");
        expect(!(*branchFinding)["branch_convergence_evidence"]["complete"].asBool(),
               "missing branch convergence evidence should expose incomplete field-level status");
        expect((*branchFinding)["branch_convergence_evidence"]["missing"].size() >= 3,
               "missing branch convergence evidence should name missing target/reachability/publication fields");
        webview::ItemQueryOptions pagedDetectorOptions = allOptions;
        pagedDetectorOptions.limit = 1;
        auto pagedDetector = service.BuildDoneCandidateDetector(pagedDetectorOptions);
        expect(find_finding(pagedDetector["findings"], "product-beta", "PRB-BUG-0007", "branch_convergence_missing_or_unknown").has_value(),
               "done detector should not miss findings beyond the first requested page");
        expect(!pagedDetector["truncated"].asBool(),
               "done detector should report no truncation after internal pagination completes");
        expect(doneDetector["counts_by_reason"]["done_candidate"].asUInt64() >= 1,
               "detector should count done candidate findings by reason");
        expect(doneDetector["counts_by_reason"]["false_done_suspect"].asUInt64() >= 1,
               "detector should count false done findings by reason");
        expect(doneDetector["counts_by_reason"]["weak_done_evidence"].asUInt64() >= 1,
               "detector should count weak done evidence findings by reason");
        expect(doneDetector["counts_by_reason"]["missing_validation_evidence"].asUInt64() >= 1,
               "detector should count missing validation findings by reason");
        expect(doneDetector["counts_by_reason"]["missing_commit_or_push_evidence"].asUInt64() >= 1,
               "detector should count missing commit or push findings by reason");
        expect(doneDetector["counts_by_reason"]["missing_artifact_or_worklog_evidence"].asUInt64() >= 1,
               "detector should count missing artifact or worklog findings by reason");
        expect(doneDetector["counts_by_reason"]["stale_worklog_after_done"].asUInt64() >= 1,
               "detector should count stale worklog findings by reason");
        expect(doneDetector["counts_by_reason"]["branch_convergence_missing_or_unknown"].asUInt64() >= 1,
               "detector should count branch convergence findings by reason");

        const auto qualityFileCountBefore = count_regular_files(products);
        const auto qualityStateBefore = read_text(products / "product-beta" / "items" / "bug" / "0002" / "PRB-BUG-0002.md");
        auto qualityView = service.BuildEvidenceQualityView(allOptions);
        expect(!qualityView.isMember("error"), "evidence quality view query should not fail");
        expect(qualityView["schema"].asString() == "kob.evidence.quality_view.v1",
               "evidence quality view should expose its view schema");
        expect(qualityView["read_only"].asBool(), "evidence quality view must be read-only");
        expect(!qualityView["mutation_allowed"].asBool(), "evidence quality view must not allow mutations");
        expect(!qualityView["starts_agent"].asBool(), "evidence quality view must not start agents");
        expect(!qualityView["dispatches_work"].asBool(), "evidence quality view must not dispatch work");
        expect(qualityView["advisory_only"].asBool(), "evidence quality view must be advisory only");
        expect(qualityView["row_count"].asUInt64() == qualityView["rows"].size(),
               "evidence quality view should expose bounded row_count metadata");
        expect(qualityView["pagination_ignored_for_full_scan"].asBool(),
               "evidence quality view should scan the selected set instead of one query page");
        expect(qualityView["scanned"].asUInt64() == qualityView["query_total"].asUInt64(),
               "evidence quality full scan should visit every selected query match");
        expect(!qualityView["truncated"].asBool(), "unbounded evidence quality query should not be truncated");
        expect(count_regular_files(products) == qualityFileCountBefore,
               "evidence quality view must not create or delete files");
        expect(read_text(products / "product-beta" / "items" / "bug" / "0002" / "PRB-BUG-0002.md") == qualityStateBefore,
               "evidence quality view must not mutate item markdown state");
        for (const auto& row : qualityView["rows"]) {
            expect(row["schema"].asString() == "kob.evidence.quality_classification.v1",
                   "evidence quality rows should match the classification schema id");
            expect(!row["claim"].asString().empty(), "evidence quality row should include claim");
            expect(!row["falsifier"].asString().empty(), "evidence quality row should include falsifier");
            expect(row["evidence"].isArray(), "evidence quality row should include evidence refs");
            expect(!row["verdict"].asString().empty(), "evidence quality row should include verdict");
            expect(!row["gap"].asString().empty(), "evidence quality row should include gap");
            expect(!row["suggested_action"].asString().empty(), "evidence quality row should include suggested action");
            expect(row["claim_ref"].isObject(), "evidence quality row should include claim_ref");
            expect(!row["quality_state"].asString().empty(), "evidence quality row should include quality_state");
            expect(row["inputs"].isObject(), "evidence quality row should include inputs");
            expect(!row["human_wording"].asString().empty(), "evidence quality row should include human wording");
            expect(!row["fallback_behavior"].asString().empty(), "evidence quality row should include fallback behavior");
            expect(row["diagnostics"].isArray(), "evidence quality row should include diagnostics");
            expect(row["read_only"].asBool(), "evidence quality rows should be read-only");
            expect(!row["mutation_allowed"].asBool(), "evidence quality rows should not allow mutation");
            expect(!row["starts_agent"].asBool(), "evidence quality rows should not start agents");
            expect(!row["dispatches_work"].asBool(), "evidence quality rows should not dispatch work");
        }
        auto strongQuality = find_quality_row(qualityView["rows"], "product-beta", "PRB-BUG-0002");
        expect(strongQuality.has_value(), "done item with strong evidence should have a quality row");
        expect((*strongQuality)["quality_state"].asString() == "strong",
               "done item with validation, artifact, commit, and branch convergence should be strong");
        auto missingQuality = find_quality_row(qualityView["rows"], "product-beta", "PRB-BUG-0003");
        expect(missingQuality.has_value(), "closed item without evidence should have a quality row");
        expect((*missingQuality)["quality_state"].asString() == "missing",
               "closed item without durable evidence should be missing");
        auto weakQuality = find_quality_row(qualityView["rows"], "product-beta", "PRB-BUG-0005");
        expect(weakQuality.has_value(), "done item missing source-control proof should have a quality row");
        expect((*weakQuality)["quality_state"].asString() == "weak",
               "partial evidence without source-control proof should be weak");
        auto staleQuality = find_quality_row(qualityView["rows"], "product-beta", "PRB-BUG-0006");
        expect(staleQuality.has_value(), "done item with stale worklog should have a quality row");
        expect((*staleQuality)["quality_state"].asString() == "stale",
               "done item updated after worklog evidence should be stale");
        auto unclearQuality = find_quality_row(qualityView["rows"], "product-beta", "PRB-BUG-0007");
        expect(unclearQuality.has_value(), "done item with unclear branch convergence should have a quality row");
        expect((*unclearQuality)["quality_state"].asString() == "unclear",
               "done item with commit evidence but unknown branch convergence should be unclear");
        expect(qualityView["counts_by_quality_state"]["strong"].asUInt64() >= 1,
               "evidence quality view should count strong rows");
        expect(qualityView["counts_by_quality_state"]["weak"].asUInt64() >= 1,
               "evidence quality view should count weak rows");
        expect(qualityView["counts_by_quality_state"]["missing"].asUInt64() >= 1,
               "evidence quality view should count missing rows");
        expect(qualityView["counts_by_quality_state"]["stale"].asUInt64() >= 1,
               "evidence quality view should count stale rows");
        expect(qualityView["counts_by_quality_state"]["unclear"].asUInt64() >= 1,
               "evidence quality view should count unclear rows");
        webview::ItemQueryOptions pagedQualityOptions = allOptions;
        pagedQualityOptions.limit = 1;
        auto pagedQuality = service.BuildEvidenceQualityView(pagedQualityOptions);
        expect(find_quality_row(pagedQuality["rows"], "product-beta", "PRB-BUG-0007").has_value(),
               "evidence quality view should not miss rows beyond the first requested page");
        expect(!pagedQuality["truncated"].asBool(),
               "evidence quality view should report no truncation after internal pagination completes");

        const auto recoveryFileCountBefore = count_regular_files(products);
        const auto recoveryStateBefore = read_text(products / "product-alpha" / "items" / "task" / "0001" / "PRA-TSK-0001.md");
        auto recovery = service.BuildContextRecoverySummary("Native Migration", allOptions);
        expect(!recovery.isMember("error"), "context recovery query should not fail");
        expect(recovery["schema"].asString() == "kob.context.recovery_summary.v1",
               "context recovery should expose its schema id");
        expect(recovery["area"].asString() == "Native Migration",
               "context recovery should preserve requested area label");
        expect(recovery["read_only"].asBool(), "context recovery must be read-only");
        expect(!recovery["mutation_allowed"].asBool(), "context recovery must not allow mutations");
        expect(!recovery["starts_agent"].asBool(), "context recovery must not start agents");
        expect(!recovery["dispatches_work"].asBool(), "context recovery must not dispatch work");
        expect(recovery["advisory_only"].asBool(), "context recovery must be advisory only");
        expect(recovery["pagination_ignored_for_full_scan"].asBool(),
               "context recovery should scan the selected set instead of one query page");
        expect(recovery["scanned"].asUInt64() == recovery["query_total"].asUInt64(),
               "context recovery full scan should visit every selected query match");
        expect(!recovery["truncated"].asBool(), "unbounded context recovery query should not be truncated");
        expect(recovery["counts"]["items"].asUInt64() >= 1,
               "context recovery should count selected item records");
        expect(recovery["counts"]["evidence_signals"].asUInt64() >= 1,
               "context recovery should count evidence signals");
        expect_context_section(recovery, "area_summary");
        expect_context_section(recovery, "current_state");
        expect_context_section(recovery, "key_decisions");
        expect_context_section(recovery, "active_risks");
        expect_context_section(recovery, "evidence");
        expect_context_section(recovery, "next_actions");
        expect_context_section(recovery, "do_not_touch");
        expect(recovery["key_decisions"]["confidence"].asString() == "missing",
               "context recovery should mark unsupported decision history as missing");
        expect(recovery["do_not_touch"]["confidence"].asString() == "strong",
               "context recovery should keep do-not-touch boundaries strong");
        expect(recovery["do_not_touch"]["summary"].asString().find("private paths") != std::string::npos,
               "context recovery should warn against private path exposure");
        expect(count_regular_files(products) == recoveryFileCountBefore,
               "context recovery must not create or delete files");
        expect(read_text(products / "product-alpha" / "items" / "task" / "0001" / "PRA-TSK-0001.md") == recoveryStateBefore,
               "context recovery must not mutate item markdown state");
        webview::ItemQueryOptions pagedRecoveryOptions = allOptions;
        pagedRecoveryOptions.limit = 1;
        auto pagedRecovery = service.BuildContextRecoverySummary("Native Migration", pagedRecoveryOptions);
        expect(pagedRecovery["counts"]["items"].asUInt64() == recovery["counts"]["items"].asUInt64(),
               "context recovery should not miss rows beyond the first requested page");
        expect(!pagedRecovery["truncated"].asBool(),
               "context recovery should report no truncation after internal pagination completes");

        webview::ItemQueryOptions taskText;
        taskText.types = {"Task"};
        taskText.text = "migration evidence";
        auto taskTextResult = service.QueryItems(taskText);
        expect(taskTextResult["total"].asUInt64() == 1, "type and text filter should narrow to one task");
        expect(taskTextResult["items"][0]["id"].asString() == "PRA-TSK-0001", "filtered task id mismatch");

        webview::ItemQueryOptions initiativeText;
        initiativeText.types = {"Initiative"};
        initiativeText.text = "component narrative";
        auto initiativeTextResult = service.QueryItems(initiativeText);
        expect(initiativeTextResult["total"].asUInt64() == 1, "initiative type filter should find the initiative");
        expect(initiativeTextResult["items"][0]["id"].asString() == "PRA-INIT-0001", "filtered initiative id mismatch");

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
                expect(rootNode["children"].size() == 6, "tree should attach task children under the epic root");
            }
            if (rootNode["id"].asString() == "PRA-TSK-0004") {
                foundStandaloneTaskRoot = true;
            }
        }
        expect(foundEpicRoot, "filtered tree should keep the epic root");
        expect(foundStandaloneTaskRoot, "filtered tree should keep standalone tasks without parents as roots");

        webview::ItemQueryOptions initiativeTreeOptions;
        initiativeTreeOptions.types = {"Initiative", "Epic", "Task"};
        auto initiativeTree = service.BuildTree(initiativeTreeOptions);
        expect(!initiativeTree.isMember("error"), "initiative tree query should not fail");
        bool foundInitiativeRoot = false;
        for (const auto& rootNode : initiativeTree["roots"]) {
            if (rootNode["id"].asString() == "PRA-INIT-0001") {
                foundInitiativeRoot = true;
                expect(rootNode["children"].size() == 1, "initiative root should attach the epic child");
            }
        }
        expect(foundInitiativeRoot, "tree should expose initiative as structural root");

        auto kanban = service.BuildKanban(betaDoing);
        expect(kanban["lanes"]["Doing"].size() == 1, "kanban should place InProgress item in Doing lane");

        auto detail = service.GetItem("all", "PRA-TSK-0001");
        expect(!detail.isMember("error"), "all-product detail lookup should find task");
        expect(detail["item"]["product"].asString() == "product-alpha", "detail lookup product mismatch");
        expect(detail["item"]["content"].asString().find("Native migration evidence") != std::string::npos,
               "detail lookup should include content");
        expect(detail["item"]["gate_status"]["ready"]["state"].asString() == "passed",
               "item detail should include gate_status");
        auto globalRefresh = service.Refresh("all");
        expect(globalRefresh["refreshed"].asString() == "all",
               "global refresh should invalidate all product caches");
        auto exactDetailAfterGlobalRefresh = service.GetItem("product-alpha", "PRA-TSK-0001");
        expect(!exactDetailAfterGlobalRefresh.isMember("error"),
               "exact product detail lookup should remain available after global refresh invalidation");
        expect(exactDetailAfterGlobalRefresh["item"]["content"].asString().find("Native migration evidence") != std::string::npos,
               "exact product detail lookup after global refresh should include content");
        auto exactDetailPartialAfterGlobalRefresh = service.RenderItemPartial("product-alpha", "PRA-TSK-0001");
        expect(exactDetailPartialAfterGlobalRefresh.find("Native migration evidence") != std::string::npos,
               "item detail partial should render after simulated slow/global refresh invalidation");

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

        auto exactRoute = service.RecommendCapabilityRoute("product-alpha", "PRA-FTR-0002");
        expect(!exactRoute.isMember("error"), "exact capability route should not fail");
        expect(exactRoute["status"].asString() == "routed", "exact capability route should be routed");
        expect(exactRoute["route"]["product"].asString() == "product-alpha",
               "exact route should show selected product");
        expect(exactRoute["route"]["skill"].asString() == "kano-agent-backlog-skill",
               "exact route should show selected skill");
        expect(exactRoute["route"]["confidence"].asString() == "high",
               "exact route should show high confidence");
        expect(!exactRoute["route"]["reason"].asString().empty(),
               "exact route should show reason text");
        expect(exactRoute["route"]["source_fields"].size() >= 1,
               "exact route should expose source fields");
        expect(exactRoute["route"]["read_only"].asBool(),
               "exact route should be marked read-only");
        expect(!exactRoute["route"]["mutation_allowed"].asBool(),
               "exact route must not allow mutations");
        expect(!exactRoute["route"]["starts_agent"].asBool(),
               "exact route must not start agents");
        expect(!exactRoute["route"]["dispatches_work"].asBool(),
               "exact route must not dispatch work");
        expect(!exactRoute["mutation_allowed"].asBool(),
               "capability route response must not allow mutations");
        expect(!exactRoute["starts_agent"].asBool(),
               "capability route response must not start agents");
        expect(!exactRoute["dispatches_work"].asBool(),
               "capability route response must not dispatch work");

        auto productMapNavigation = service.BuildProductMapNavigation(allOptions);
        expect(!productMapNavigation.isMember("error"), "product map navigation query should not fail");
        expect(productMapNavigation["schema"].asString() == "kob.backboard.product_map_navigation.v1",
               "product map navigation should expose a stable schema marker");
        expect(productMapNavigation["read_only"].asBool(), "product map navigation must be read-only");
        expect(!productMapNavigation["mutation_allowed"].asBool(), "product map navigation must not allow mutations");
        expect(!productMapNavigation["starts_agent"].asBool(), "product map navigation must not start agents");
        expect(!productMapNavigation["dispatches_work"].asBool(), "product map navigation must not dispatch work");
        expect(productMapNavigation["node_count"].asUInt64() >= 4,
               "product map navigation should expose feature and ADR nodes");
        expect(has_node(productMapNavigation["nodes"], "work_order:PRA-TSK-0001", "work_order"),
               "product map navigation should expose work-order nodes");
        expect(has_node(productMapNavigation["nodes"], "evidence:PRA-TSK-0001", "evidence"),
               "product map navigation should expose linked evidence nodes");
        expect(has_edge(productMapNavigation["edges"], "feature:PRA-FTR-0002", "adr:PRA-ADR-0001", "decided_by"),
               "product map navigation should link feature to ADR decisions");
        expect(has_edge(productMapNavigation["edges"], "adr:PRA-ADR-0001", "feature:PRA-FTR-0002", "impacts_feature"),
               "product map navigation should link ADRs back to impacted features");
        expect(has_edge(productMapNavigation["edges"], "adr:PRA-ADR-0001", "evidence:PRA-TSK-0001", "supported_by"),
               "product map navigation should link ADRs to evidence");
        expect(has_edge(productMapNavigation["edges"], "feature:PRA-FTR-0002", "evidence:PRA-TSK-0001", "has_evidence"),
               "product map navigation should link Product Map nodes to evidence through ADR refs");
        expect(has_edge(productMapNavigation["edges"], "adr:PRA-ADR-0002", "adr:PRA-ADR-0001", "superseded_by"),
               "product map navigation should expose ADR supersession edges");
        expect(has_diagnostic(productMapNavigation["diagnostics"], "evidence_gap", "adr:PRA-ADR-0002"),
               "product map navigation should report ADR evidence gaps without inferring support");
        expect(has_diagnostic(productMapNavigation["diagnostics"], "stale_ref", "adr:PRA-ADR-0002"),
               "product map navigation should report stale ADR lifecycle refs");
        expect(has_diagnostic(productMapNavigation["diagnostics"], "missing_ref", "feature:PRA-FTR-9999"),
               "product map navigation should report missing impacted feature refs");
        expect(has_diagnostic(productMapNavigation["diagnostics"], "missing_ref", "evidence:PRA-TSK-9999"),
               "product map navigation should report missing evidence refs");
        expect(has_diagnostic(productMapNavigation["diagnostics"], "missing_ref", "adr:PRA-ADR-9999"),
               "product map navigation should report missing ADR supersession refs");
        const auto productMapSerialized = json_to_string(productMapNavigation);
        expect(productMapSerialized.find(products.generic_string()) == std::string::npos,
               "product map navigation should not expose absolute filesystem paths");
        expect(productMapSerialized.find("items/") == std::string::npos &&
                   productMapSerialized.find("decisions/") == std::string::npos,
               "product map navigation refs should not expose raw repo paths");

        auto roadmap = service.BuildVersionGoalLedger(allOptions);
        expect(!roadmap.isMember("error"), "version goal ledger projection should not fail");
        expect(roadmap["schema"].asString() == "kob.backboard.version_goal_ledger_projection.v1",
               "roadmap projection should expose a stable schema marker");
        expect(roadmap["read_only"].asBool(), "roadmap projection must be read-only");
        expect(!roadmap["mutation_allowed"].asBool(), "roadmap projection must not allow mutations");
        expect(!roadmap["starts_agent"].asBool(), "roadmap projection must not start agents");
        expect(!roadmap["dispatches_work"].asBool(), "roadmap projection must not dispatch work");
        expect(roadmap["filters_ignored_for_ref_resolution"].asBool(),
               "roadmap projection should not mark refs missing because of active UI filters");
        expect(roadmap["goal_count"].asUInt64() == 7,
               "roadmap projection should load all fixture goals");
        expect(roadmap["slices"]["current"].size() == 3,
               "roadmap projection should expose current goals");
        expect(roadmap["slices"]["next"].size() == 2,
               "roadmap projection should expose next goals");
        expect(roadmap["slices"]["future"].size() == 2,
               "roadmap projection should expose future goals");

        auto doneGoal = find_goal(roadmap["goals"], "goal-done-evidence-backed");
        expect(doneGoal.has_value(), "roadmap should include evidence-backed done goal");
        expect((*doneGoal)["status"].asString() == "Done",
               "evidence-backed done goal should project as Done");
        expect((*doneGoal)["evidence_chain_complete"].asBool(),
               "Done roadmap goals should require a complete evidence chain");
        expect((*doneGoal)["evidence_backed_count"].asUInt64() == 1,
               "Done roadmap goal should count evidence-backed refs");

        auto closedUnverified = find_goal(roadmap["goals"], "goal-closed-unverified");
        expect(closedUnverified.has_value(), "roadmap should include closed-unverified goal");
        expect((*closedUnverified)["declared_status"].asString() == "Done",
               "fixture should declare the unverified goal as Done");
        expect((*closedUnverified)["status"].asString() == "Implemented/Unverified",
               "closed ticket without evidence should project as implemented/unverified");
        expect((*closedUnverified)["closed_ticket_count"].asUInt64() == 1,
               "implemented/unverified goal should distinguish closed ticket count");
        expect((*closedUnverified)["evidence_backed_count"].asUInt64() == 0,
               "implemented/unverified goal should not count as evidence-backed");
        expect(has_diagnostic((*closedUnverified)["diagnostics"], "done_requires_evidence", "goal-closed-unverified"),
               "Done without evidence should produce a done_requires_evidence diagnostic");

        auto cutGoal = find_goal(roadmap["goals"], "goal-cut-scope");
        expect(cutGoal.has_value() && (*cutGoal)["status"].asString() == "Cut",
               "cut goal should remain Cut");
        expect((*cutGoal)["cut_defer_decision"].asBool(),
               "cut goal should expose explicit cut/defer decision metadata");
        auto deferredGoal = find_goal(roadmap["goals"], "goal-deferred-scope");
        expect(deferredGoal.has_value() && (*deferredGoal)["status"].asString() == "Deferred",
               "deferred goal should remain Deferred");
        expect((*deferredGoal)["cut_defer_decision"].asBool(),
               "deferred goal should expose explicit cut/defer decision metadata");

        auto partialGoal = find_goal(roadmap["goals"], "goal-partial-active");
        expect(partialGoal.has_value() && (*partialGoal)["status"].asString() == "Partial",
               "active partial roadmap goal should project as Partial");
        auto staleMissingGoal = find_goal(roadmap["goals"], "goal-stale-missing-refs");
        expect(staleMissingGoal.has_value() && (*staleMissingGoal)["status"].asString() == "At Risk",
               "stale/missing roadmap links should project as At Risk");
        expect(has_diagnostic(roadmap["diagnostics"], "missing_ref", "product-alpha:PRA-TSK-9999"),
               "roadmap projection should report missing item refs");
        expect(has_diagnostic(roadmap["diagnostics"], "missing_ref", "product-alpha:PRA-ADR-9999"),
               "roadmap projection should report missing ADR refs");
        expect(has_diagnostic(roadmap["diagnostics"], "stale_ref", "goal-stale-missing-refs"),
               "roadmap projection should report stale goal evidence quality");
        auto unknownGoal = find_goal(roadmap["goals"], "goal-unknown-no-refs");
        expect(unknownGoal.has_value() && (*unknownGoal)["status"].asString() == "Unknown",
               "unsupported future goal should remain Unknown");
        expect(roadmap["status_counts"].isMember("Done") &&
                   roadmap["status_counts"].isMember("Implemented/Unverified") &&
                   roadmap["status_counts"].isMember("Partial") &&
                   roadmap["status_counts"].isMember("Deferred") &&
                   roadmap["status_counts"].isMember("Cut") &&
                   roadmap["status_counts"].isMember("Blocked") &&
                   roadmap["status_counts"].isMember("At Risk") &&
                   roadmap["status_counts"].isMember("Unknown"),
               "roadmap projection should expose the complete status taxonomy");
        const auto roadmapSerialized = json_to_string(roadmap);
        expect(roadmapSerialized.find(products.generic_string()) == std::string::npos,
               "roadmap projection should not expose absolute filesystem paths");
        expect(roadmapSerialized.find("roadmap/") == std::string::npos &&
                   roadmapSerialized.find("version-goals/") == std::string::npos,
               "roadmap projection should not expose raw ledger file paths");

        auto decisionRadar = service.BuildDecisionDebtRadar(allOptions);
        expect(!decisionRadar.isMember("error"), "decision debt radar projection should not fail");
        expect(decisionRadar["schema"].asString() == "kob.backboard.decision_debt_radar.v1",
               "decision debt radar should expose a stable schema marker");
        expect(decisionRadar["read_only"].asBool(), "decision debt radar must be read-only");
        expect(!decisionRadar["mutation_allowed"].asBool(), "decision debt radar must not allow mutations");
        expect(!decisionRadar["starts_agent"].asBool(), "decision debt radar must not start agents");
        expect(!decisionRadar["dispatches_work"].asBool(), "decision debt radar must not dispatch work");
        expect(decisionRadar["advisory_only"].asBool(), "decision debt radar findings should be advisory");
        expect(decisionRadar["filters_ignored_for_ref_resolution"].asBool(),
               "decision debt radar should not mark refs missing because of active UI filters");
        expect(decisionRadar["row_count"].asUInt64() >= 6,
               "decision debt radar should list ADR lifecycle rows");

        auto activeDecision = find_decision_row(decisionRadar["rows"], "PRA-ADR-0001");
        expect(activeDecision.has_value(), "decision radar should include active ADR row");
        expect(has_string_value((*activeDecision)["categories"], "active"),
               "accepted ADR with evidence should be categorized as active");
        expect((*activeDecision)["radar_status"].asString() == "active",
               "active ADR should keep active radar status");
        expect(has_logical_ref((*activeDecision)["affected_refs"], "item_id", "PRA-FTR-0002"),
               "decision radar active row should link affected feature refs");
        expect(has_logical_ref((*activeDecision)["evidence_refs"], "evidence_id", "PRA-TSK-0001"),
               "decision radar active row should link evidence refs");

        auto supersededDecision = find_decision_row(decisionRadar["rows"], "PRA-ADR-0004");
        expect(supersededDecision.has_value(), "decision radar should include superseded ADR row");
        expect(has_string_value((*supersededDecision)["categories"], "superseded") &&
                   has_logical_ref((*supersededDecision)["superseded_by"], "adr_id", "PRA-ADR-0001"),
               "decision radar should show superseded ADRs with supersession refs");

        auto staleDecision = find_decision_row(decisionRadar["rows"], "PRA-ADR-0002");
        expect(staleDecision.has_value() &&
                   has_string_value((*staleDecision)["categories"], "stale"),
               "decision radar should include stale ADR row");

        auto revisitDecision = find_decision_row(decisionRadar["rows"], "PRA-ADR-0005");
        expect(revisitDecision.has_value(), "decision radar should include revisit-needed ADR row");
        expect(has_string_value((*revisitDecision)["categories"], "revisit_needed"),
               "decision radar should categorize revisit-needed ADRs");
        expect((*revisitDecision)["revisit_condition"].asString().find("Product Map navigation") != std::string::npos,
               "decision radar should show revisit condition text");
        expect(!(*revisitDecision)["mutation_allowed"].asBool() &&
                   !(*revisitDecision)["starts_agent"].asBool(),
               "revisit-needed radar rows must not mutate state or start work");

        auto challengedDecision = find_decision_row(decisionRadar["rows"], "PRA-ADR-0006");
        expect(challengedDecision.has_value(), "decision radar should include evidence-challenged ADR row");
        expect(has_string_value((*challengedDecision)["categories"], "evidence_challenged"),
               "decision radar should categorize evidence-challenged ADRs");
        expect(has_diagnostic((*challengedDecision)["diagnostics"], "evidence_incomplete", "product-alpha:PRA-TSK-0006"),
               "decision radar should diagnose incomplete linked evidence");
        expect(has_diagnostic(decisionRadar["diagnostics"], "missing_ref", "product-alpha:PRA-FTR-9999") &&
                   has_diagnostic(decisionRadar["diagnostics"], "missing_ref", "product-alpha:PRA-TSK-9999"),
               "decision radar should report stale or missing refs as gaps");
        const auto decisionRadarSerialized = json_to_string(decisionRadar);
        expect(decisionRadarSerialized.find(products.generic_string()) == std::string::npos,
               "decision radar projection should not expose absolute filesystem paths");
        expect(decisionRadarSerialized.find("decisions/") == std::string::npos &&
                   decisionRadarSerialized.find("items/") == std::string::npos,
               "decision radar projection should not expose raw repo paths");

        auto fallbackRoute = service.RecommendCapabilityRoute("product-alpha", "PRA-FTR-0003");
        expect(!fallbackRoute.isMember("error"), "fallback capability route should not fail");
        expect(fallbackRoute["status"].asString() == "fallback",
               "common item without metadata should use fallback route");
        expect(fallbackRoute["missing_capability_metadata"].asBool(),
               "fallback route should flag missing capability metadata");
        expect(fallbackRoute["warnings"].size() >= 1,
               "fallback route should expose actionable warning");
        expect(fallbackRoute["warnings"][0]["code"].asString() == "capability_route.missing_metadata",
               "fallback warning should explain missing metadata");
        expect(fallbackRoute["route"]["skill"].asString() == "kano-agent-backlog-skill",
               "fallback route should point to native KOB review path");
        expect(!fallbackRoute["route"]["starts_agent"].asBool(),
               "fallback route must not start agents");

        auto ambiguousRoute = service.RecommendCapabilityRoute("product-alpha", "PRA-FTR-0004");
        expect(!ambiguousRoute.isMember("error"), "ambiguous capability route should not fail");
        expect(ambiguousRoute["status"].asString() == "ambiguous",
               "multiple explicit capability routes should be ambiguous");
        expect(ambiguousRoute["route"].isNull(), "ambiguous route should not select a default route");
        expect(ambiguousRoute["alternatives"].size() == 2,
               "ambiguous route should expose alternatives");
        expect(ambiguousRoute["ambiguous_capability_data"].asBool(),
               "ambiguous route should flag ambiguous capability data");
        expect(ambiguousRoute["warnings"].size() >= 1,
               "ambiguous route should expose actionable warning");
        expect(!ambiguousRoute["mutation_allowed"].asBool(),
               "ambiguous route response must not allow mutations");
        expect(!ambiguousRoute["starts_agent"].asBool(),
               "ambiguous route response must not start agents");

        auto noRoute = service.RecommendCapabilityRoute("product-alpha", "PRA-EXP-0001");
        expect(!noRoute.isMember("error"), "no-route capability lookup should not fail");
        expect(noRoute["status"].asString() == "no_route",
               "unknown item type without metadata should report no route");
        expect(noRoute["route"].isNull(), "no-route response should not select a route");
        expect(noRoute["warnings"].size() >= 1, "no-route response should expose warning");
        expect(noRoute["warnings"][0]["code"].asString() == "capability_route.no_route",
               "no-route warning should use deterministic code");
        expect(!noRoute["mutation_allowed"].asBool(), "no-route response must not allow mutations");
        expect(!noRoute["starts_agent"].asBool(), "no-route response must not start agents");
        expect(!noRoute["dispatches_work"].asBool(), "no-route response must not dispatch work");

        auto inbox = service.BuildReviewInbox(allOptions);
        expect(!inbox["lanes"].isMember("Ready Approval"), "review inbox should not expose legacy Ready Approval lane");
        for (const auto& lane : {"Needs Review", "Done Candidate", "False Done Suspect", "Evidence Gap",
                                 "Blocked/Dirty", "Stale/Drift", "Ready Frontier"}) {
            expect(inbox["lanes"].isMember(lane), std::string("review inbox should expose queue: ") + lane);
        }
        auto lane_has_policy = [&](const std::string& lane,
                                   const std::string& label,
                                   const std::string& humanDecision,
                                   const std::string& targetState,
                                   bool requiresConfirmation) {
            for (const auto& bundle : inbox["lanes"][lane]) {
                for (const auto& action : bundle["actions"]) {
                    if (action["label"].asString() == label &&
                        action["human_decision"].asString() == humanDecision &&
                        action["target_state"].asString() == targetState &&
                        action["requires_confirmation"].asBool() == requiresConfirmation) {
                        return true;
                    }
                }
            }
            return false;
        };
        expect(inbox["lane_taxonomy"].size() >= 7, "review inbox should expose lane taxonomy metadata");
        expect(inbox["lanes"]["Ready Frontier"].size() >= 1, "review inbox should expose ready frontier queue");
        expect(!inbox["lanes"]["Ready Frontier"][0]["review_reason"].asString().empty(),
               "review inbox bundles should explain why the item needs review");
        expect(!inbox["lanes"]["Ready Frontier"][0]["reason_code"].asString().empty(),
               "review inbox bundles should expose deterministic reason codes");
        expect(inbox["lanes"]["Ready Frontier"][0]["source_fields"].size() >= 1,
               "review inbox bundles should expose source fields");
        expect(inbox["lanes"]["Ready Frontier"][0]["suggested_decision"].asString() ==
                   inbox["lanes"]["Ready Frontier"][0]["suggested_human_decision"].asString(),
               "review inbox should expose detector output as suggested_decision only");
        expect(inbox["lanes"]["Ready Frontier"][0]["actions"].size() >= 1,
               "review inbox bundles should expose lane-specific human actions");
        for (const auto& lane : inbox["lane_order"]) {
            for (const auto& bundle : inbox["lanes"][lane.asString()]) {
                for (const auto& action : bundle["actions"]) {
                    expect(action["label"].asString() != "Accept" && action["label"].asString() != "Reject",
                           "review action labels should avoid generic Accept/Reject labels");
                    expect(!action["starts_agent"].asBool(), "review actions must not start agents");
                    expect(!action["dispatches_work"].asBool(), "review actions must not dispatch work");
                }
            }
        }
        expect(inbox["lanes"]["Ready Frontier"][0]["actions"][0]["label"].asString() == "Approve Ready Boundary",
               "ready frontier action should use lane-specific Approve Ready Boundary label");
        expect(lane_has_policy("Needs Review", "Request Evidence", "request_evidence", "", false),
               "needs review action should use single-click Request Evidence policy");
        expect(lane_has_policy("Done Candidate", "Mark Done", "mark_done", "Done", true),
               "done candidate action should use confirm-gated Mark Done policy");
        expect(lane_has_policy("Done Candidate", "Move to Review", "move_to_review", "Review", false),
               "done candidate action should expose single-click Move to Review policy");
        expect(lane_has_policy("Done Candidate", "Reject Completion", "reject_completion", "Review", false),
               "done candidate action should expose single-click Reject Completion policy");
        expect(lane_has_policy("False Done Suspect", "Reopen from Done", "reopen_from_done", "Review", true),
               "false done suspect action should use confirm-gated Reopen from Done policy");
        expect(lane_has_policy("False Done Suspect", "Dismiss", "dismiss", "", false),
               "false done suspect action should expose single-click Dismiss policy");
        expect(lane_has_policy("False Done Suspect", "Request Evidence", "request_evidence", "", false),
               "false done suspect action should expose single-click Request Evidence policy");
        expect(lane_has_policy("Evidence Gap", "Request Evidence", "request_evidence", "", false),
               "evidence gap action should expose single-click Request Evidence policy");
        expect(lane_has_policy("Blocked/Dirty", "Accept Risk", "accept_risk", "", true),
               "blocked dirty action should expose confirm-gated Accept Risk policy");
        expect(lane_has_policy("Stale/Drift", "Drop", "drop", "Dropped", true),
               "stale drift action should expose confirm-gated Drop policy");

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
        auto productMapTreePartial = service.RenderTreePartial(allOptions);
        expect(productMapTreePartial.find("Product Map refs") != std::string::npos,
               "tree partial should expose DOM-readable Product Map navigation refs");
        expect(productMapTreePartial.find("PRA-ADR-0001") != std::string::npos,
               "tree partial should render feature to ADR refs");
        expect(productMapTreePartial.find("PRA-TSK-0001") != std::string::npos,
               "tree partial should render Product Map evidence refs");
        expect(productMapTreePartial.find(products.generic_string()) == std::string::npos,
               "tree partial should not expose absolute filesystem paths");

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

        auto roadmapPartial = service.RenderRoadmapPartial(allOptions);
        expect(roadmapPartial.find("data-navigation-model=\"version-goal-ledger\"") != std::string::npos,
               "roadmap partial should expose DOM-readable Version Goal Ledger markup");
        expect(roadmapPartial.find("data-roadmap-slice=\"current\"") != std::string::npos &&
                   roadmapPartial.find("data-roadmap-slice=\"next\"") != std::string::npos &&
                   roadmapPartial.find("data-roadmap-slice=\"future\"") != std::string::npos,
               "roadmap partial should expose current, next, and future slices");
        expect(roadmapPartial.find("goal-done-evidence-backed") != std::string::npos &&
                   roadmapPartial.find("Implemented/Unverified") != std::string::npos,
               "roadmap partial should distinguish evidence-backed and unverified goals");
        expect(roadmapPartial.find("goal-cut-scope") != std::string::npos &&
                   roadmapPartial.find("goal-deferred-scope") != std::string::npos,
               "roadmap partial should keep cut and deferred scope visible");
        expect(roadmapPartial.find("missing_ref") != std::string::npos &&
                   roadmapPartial.find("stale_ref") != std::string::npos,
               "roadmap partial should render missing and stale ref diagnostics");
        expect(roadmapPartial.find(products.generic_string()) == std::string::npos &&
                   roadmapPartial.find("roadmap/") == std::string::npos,
               "roadmap partial should not expose raw filesystem paths");

        auto decisionRadarPartial = service.RenderDecisionDebtPartial(allOptions);
        expect(decisionRadarPartial.find("data-navigation-model=\"decision-debt-radar\"") != std::string::npos,
               "decision radar partial should expose DOM-readable Decision Debt markup");
        expect(decisionRadarPartial.find("PRA-ADR-0001") != std::string::npos &&
                   decisionRadarPartial.find("PRA-ADR-0004") != std::string::npos &&
                   decisionRadarPartial.find("PRA-ADR-0005") != std::string::npos &&
                   decisionRadarPartial.find("PRA-ADR-0006") != std::string::npos,
               "decision radar partial should render active, superseded, revisit-needed, and evidence-challenged ADR rows");
        expect(decisionRadarPartial.find("Affected feature or Product Map node") != std::string::npos &&
                   decisionRadarPartial.find("PRA-FTR-0002") != std::string::npos,
               "decision radar partial should render affected feature links");
        expect(decisionRadarPartial.find("Recommended human review action") != std::string::npos &&
                   decisionRadarPartial.find("missing_ref") != std::string::npos,
               "decision radar partial should render advisory action and gap diagnostics");
        expect(decisionRadarPartial.find(products.generic_string()) == std::string::npos &&
                   decisionRadarPartial.find("decisions/") == std::string::npos,
               "decision radar partial should not expose raw filesystem paths");

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
        expect(itemPartial.find(">Path<") == std::string::npos,
               "item partial should not expose raw file paths as primary navigation");
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

        auto featurePartial = service.RenderItemPartial("product-alpha", "PRA-FTR-0002");
        expect(featurePartial.find("Product Map navigation") != std::string::npos,
               "feature detail should expose Product Map navigation");
        expect(featurePartial.find("PRA-ADR-0001") != std::string::npos,
               "feature detail should link to ADR refs");
        expect(featurePartial.find("PRA-TSK-0001") != std::string::npos,
               "feature detail should link to evidence refs");
        expect(featurePartial.find("Decision debt") != std::string::npos &&
                   featurePartial.find("PRA-ADR-0005") != std::string::npos,
               "feature detail should link bounded Decision Radar refs");

        auto adrPartial = service.RenderItemPartial("product-alpha", "PRA-ADR-0001");
        expect(adrPartial.find("ADR decision navigation") != std::string::npos,
               "ADR detail should expose DOM-readable decision navigation");
        expect(adrPartial.find("Decision status") != std::string::npos &&
               adrPartial.find("accepted") != std::string::npos,
               "ADR detail should render decision status");
        expect(adrPartial.find("Impacted features / follow-up work") != std::string::npos &&
               adrPartial.find("PRA-FTR-0002") != std::string::npos,
               "ADR detail should link impacted feature refs");
        expect(adrPartial.find("Accepted option") != std::string::npos &&
               adrPartial.find("Read-only Product Map projection over durable refs") != std::string::npos,
               "ADR detail should render accepted option");
        expect(adrPartial.find("Rejected options") != std::string::npos &&
               adrPartial.find("Canvas-first mutation surface") != std::string::npos,
               "ADR detail should render rejected options without enabling canvas mode");
        expect(adrPartial.find("Linked evidence") != std::string::npos &&
               adrPartial.find("PRA-TSK-0001") != std::string::npos,
               "ADR detail should render linked evidence refs");
        expect(adrPartial.find("Product Map needs write behavior") != std::string::npos,
               "ADR detail should render revisit conditions");
        expect(adrPartial.find(products.generic_string()) == std::string::npos &&
                   adrPartial.find("decisions/") == std::string::npos,
               "ADR detail navigation should not expose raw filesystem paths");

        const auto webviewAppRoot = locate_repo_file(
            std::filesystem::path("src") / "cpp" / "code" /
            "apps" / "kano_backlog_webview");
        const auto indexHtmlSource = read_text(
            webviewAppRoot / "assets" / "index_html.hpp");
        const auto indexCssSource = read_text(
            webviewAppRoot / "assets" / "backboard_css.hpp");
        const auto indexAppJsSource = read_text(
            webviewAppRoot / "assets" / "backboard_app_js.hpp");
        const auto kobUiJsSource = read_text(
            webviewAppRoot / "assets" / "kob_ui_js.hpp");
        const auto assetSource =
            indexHtmlSource + "\n" + indexCssSource + "\n" +
            indexAppJsSource + "\n" + kobUiJsSource;

        expect(indexHtmlSource.find("<script src=\"/assets/kob-ui.js\"></script>") != std::string::npos,
               "index html asset should keep the first-party kob-ui runtime script tag");
        expect(indexHtmlSource.find("BackboardAppJs()") != std::string::npos,
               "index html asset should compose the dedicated page app javascript module");
        expect(indexHtmlSource.find("BackboardCss()") != std::string::npos,
               "index html asset should compose the dedicated css module");
        expect(assetSource.find("data-selectable-item") != std::string::npos,
               "embedded webview assets should expose selectable card markup hooks");
        expect(assetSource.find("Shortcuts ?") != std::string::npos,
               "embedded webview assets should expose the visible shortcut help affordance");
        expect(assetSource.find("aria-keyshortcuts=\"?\"") != std::string::npos,
               "embedded webview assets should expose shortcut help aria-keyshortcuts");
        expect(assetSource.find("aria-keyshortcuts=\"/\"") != std::string::npos,
               "embedded webview assets should expose search aria-keyshortcuts");
        expect(assetSource.find("function isTypingContext") != std::string::npos,
               "embedded webview assets should keep the typing-context shortcut guard");
        expect(assetSource.find("function selectItemByDelta") != std::string::npos,
               "embedded webview assets should keep keyboard selection helpers");
        expect(assetSource.find("function renderTreeNavigation") != std::string::npos &&
               assetSource.find("Product Map refs") != std::string::npos,
               "embedded webview assets should render DOM-readable Product Map navigation refs");
        expect(indexHtmlSource.find("tab-roadmap") != std::string::npos &&
                   indexHtmlSource.find("page-roadmap") != std::string::npos,
               "embedded webview assets should expose the Roadmap tab shell");
        expect(assetSource.find("function loadRoadmap") != std::string::npos &&
                   assetSource.find("/api/review/roadmap") != std::string::npos &&
                   assetSource.find("roadmap.version_goals") != std::string::npos,
               "embedded webview assets should lazy-load the Version Goal Ledger roadmap tab");
        expect(indexHtmlSource.find("tab-decision-radar") != std::string::npos &&
                   indexHtmlSource.find("page-decision-radar") != std::string::npos,
               "embedded webview assets should expose the Decision Radar tab shell");
        expect(assetSource.find("function loadDecisionRadar") != std::string::npos &&
                   assetSource.find("/api/review/decision-radar") != std::string::npos &&
                   assetSource.find("decision_radar.adrs") != std::string::npos,
               "embedded webview assets should lazy-load the Decision Debt radar tab");
        expect(assetSource.find("selectedItemVisibleIndex") != std::string::npos,
               "embedded webview assets should keep a single roving-tabindex card selection index");
        expect(assetSource.find("card === selectedCard") != std::string::npos,
               "embedded webview assets should select one visible card instance even when item keys repeat");
        expect(assetSource.find("function openSelectedItem") != std::string::npos,
               "embedded webview assets should keep keyboard open helper");
        expect(assetSource.find("Focus the backlog search field") != std::string::npos,
               "embedded webview assets should document slash-search help text");
        expect(assetSource.find("function refreshActiveTab") != std::string::npos,
               "embedded webview assets should refresh only the active tab by default");
        expect(assetSource.find("function ensureActiveTabLoaded") != std::string::npos,
               "embedded webview assets should lazy-load inactive tabs when selected");
        expect(assetSource.find("loadedTabs") != std::string::npos &&
                   assetSource.find("staleTabs") != std::string::npos,
               "embedded webview assets should track loaded and stale tab state");
        expect(assetSource.find("Promise.allSettled(steps.map") == std::string::npos,
               "embedded webview assets should not run initial refresh as a blocking all-tabs fanout");
        expect(assetSource.find("Refresh replaced by a newer request") == std::string::npos,
               "embedded webview assets should not report item detail failures as refresh replacement");
        expect(assetSource.find("function fetchItemDetailPartial") != std::string::npos,
               "embedded webview assets should use an item-detail request path independent from refresh");
        expect(assetSource.find("const timeoutMs = 60000") != std::string::npos,
               "item detail lookup should use a bounded long timeout independent from tab refresh");
        expect(assetSource.find("detailSeq") != std::string::npos,
               "embedded webview assets should sequence item detail requests separately");
        expect(assetSource.find("stage_timings") != std::string::npos &&
                   assetSource.find("request_id") != std::string::npos &&
                   assetSource.find("abort_reason") != std::string::npos &&
                   assetSource.find("cache_status") != std::string::npos &&
                   assetSource.find("active_endpoint") != std::string::npos,
               "refresh diagnostics should include request, timing, abort, cache, and endpoint metadata");
        expect(indexCssSource.find(".page.is-stale::before") != std::string::npos &&
                   indexCssSource.find(".page.is-refreshing::before") != std::string::npos,
               "embedded webview css should mark stale or refreshing visible data without clearing it");

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

        const auto webviewReadme = read_text(webviewAppRoot / "README.md");
        expect(webviewReadme.find("/api/review/done-detector") != std::string::npos,
               "webview README should list the done detector API route");
        expect(webviewReadme.find("/api/review/evidence-quality") != std::string::npos,
               "webview README should list the evidence quality API route");
        expect(webviewReadme.find("/api/review/context-recovery") != std::string::npos,
               "webview README should list the context recovery API route");
        expect(webviewReadme.find("/api/review/roadmap") != std::string::npos,
               "webview README should list the roadmap API route");
        expect(webviewReadme.find("/partials/roadmap") != std::string::npos,
               "webview README should list the roadmap partial route");
        expect(webviewReadme.find("/api/review/decision-radar") != std::string::npos,
               "webview README should list the decision radar API route");
        expect(webviewReadme.find("/partials/decision-radar") != std::string::npos,
               "webview README should list the decision radar partial route");

        const auto actorAliasDoc = read_text(
            locate_repo_file(std::filesystem::path("docs") / "design" /
                             "actor-alias-and-assignment-policy.md"));
        expect(actorAliasDoc.find("Actor Type Semantics") != std::string::npos &&
                   actorAliasDoc.find("`runner`") != std::string::npos &&
                   actorAliasDoc.find("runner-local") != std::string::npos,
               "actor alias doc should explicitly list runner actor semantics");
        expect(actorAliasDoc.find("not an execution permission grant") != std::string::npos &&
                   actorAliasDoc.find("not an authentication or authorization model") != std::string::npos,
               "actor alias doc should keep runner aliases non-enforcing");

        const auto actorAliasSchemaText = read_text(
            locate_repo_file(std::filesystem::path("references") /
                             "actor-alias-and-assignment-policy.schema.json"));
        const auto actorAliasSchema =
            parse_json_text(actorAliasSchemaText, "actor alias policy schema");
        expect(actorAliasSchema["properties"]["schema"]["const"].asString() ==
                   "kob.actor_alias_policy_examples.v1",
               "actor alias policy schema should expose a stable schema marker");
        expect(has_string_value(actorAliasSchema["$defs"]["actor_type"]["enum"], "human") &&
                   has_string_value(actorAliasSchema["$defs"]["actor_type"]["enum"], "agent") &&
                   has_string_value(actorAliasSchema["$defs"]["actor_type"]["enum"], "service") &&
                   has_string_value(actorAliasSchema["$defs"]["actor_type"]["enum"], "runner") &&
                   has_string_value(actorAliasSchema["$defs"]["actor_type"]["enum"], "role"),
               "actor alias policy schema should enumerate human, agent, service, runner, and role actor types");
        expect(actorAliasSchemaText.find("\"path\"") == std::string::npos &&
                   actorAliasSchemaText.find("auth_subject") == std::string::npos &&
                   actorAliasSchemaText.find("tenant_id") == std::string::npos &&
                   actorAliasSchemaText.find("permission_matrix") == std::string::npos,
               "actor alias policy schema should not expose raw path, auth subject, tenant, or permission matrix fields");

        const auto actorAliasFixture = parse_json_text(
            read_text(locate_repo_file(std::filesystem::path("references") /
                                       "actor-alias-and-assignment-policy.fixture.json")),
            "actor alias policy fixture");
        expect(actorAliasFixture["schema"].asString() ==
                   "kob.actor_alias_policy_examples.v1",
               "actor alias policy fixture should match schema marker");
        bool foundRunnerAlias = false;
        bool foundHumanAlias = false;
        bool foundAgentAlias = false;
        bool foundServiceAlias = false;
        bool foundRoleAlias = false;
        for (const auto& alias : actorAliasFixture["aliases"]) {
            const auto actorType = alias["actor_type"].asString();
            foundHumanAlias = foundHumanAlias || actorType == "human";
            foundAgentAlias = foundAgentAlias || actorType == "agent";
            foundServiceAlias = foundServiceAlias || actorType == "service";
            foundRoleAlias = foundRoleAlias || actorType == "role";
            if (alias["alias"].asString() == "runner-local") {
                foundRunnerAlias = actorType == "runner" &&
                    alias["repo_visible"].asBool() &&
                    !alias["private_identity_data"].asBool() &&
                    !alias["permission_grant"].asBool() &&
                    has_string_value(alias["distinct_from"], "human") &&
                    has_string_value(alias["distinct_from"], "agent") &&
                    has_string_value(alias["distinct_from"], "service") &&
                    has_string_value(alias["distinct_from"], "role");
            }
        }
        expect(foundHumanAlias && foundAgentAlias && foundServiceAlias &&
                   foundRunnerAlias && foundRoleAlias,
               "actor alias policy fixture should include distinct human, agent, service, runner, and role aliases");
        expect(actorAliasFixture["local_first_defaults"]["auth_provider_required"].asBool() == false &&
                   actorAliasFixture["local_first_defaults"]["private_identity_mapping_required"].asBool() == false &&
                   actorAliasFixture["local_first_defaults"]["omitted_actor_type_blocks_local_operation"].asBool() == false,
               "actor alias policy fixture should keep local-first operation valid without enterprise identity providers");
        expect(has_string_value(actorAliasFixture["non_goals"], "RBAC or permission enforcement") &&
                   has_string_value(actorAliasFixture["non_goals"], "enterprise identity mapping") &&
                   has_string_value(actorAliasFixture["non_goals"], "runner execution permission grant"),
               "actor alias policy fixture should keep enterprise auth and runner execution grants out of scope");
        const auto actorAliasFixtureSerialized = json_to_string(actorAliasFixture);
        expect(actorAliasFixtureSerialized.find("\"path\"") == std::string::npos &&
                   actorAliasFixtureSerialized.find("items/") == std::string::npos &&
                   actorAliasFixtureSerialized.find("decisions/") == std::string::npos &&
                   actorAliasFixtureSerialized.find("@") == std::string::npos,
               "actor alias policy fixture should not expose raw paths, personal emails, or repo file paths");

        const auto enterpriseSeamDoc = read_text(
            locate_repo_file(std::filesystem::path("docs") / "design" /
                             "backboard-enterprise-envelope-seams.md"));
        expect(enterpriseSeamDoc.find("Claim Records") != std::string::npos &&
                   enterpriseSeamDoc.find("Lease Lifecycle") != std::string::npos &&
                   enterpriseSeamDoc.find("Review Decision Envelope") != std::string::npos &&
                   enterpriseSeamDoc.find("Audit Event Envelope") != std::string::npos,
               "enterprise seam doc should cover claim, lease, review decision, and audit event contracts");
        expect(enterpriseSeamDoc.find("No authentication provider") != std::string::npos &&
                   enterpriseSeamDoc.find("No RBAC or permission checks") != std::string::npos &&
                   enterpriseSeamDoc.find("No approval quorum") != std::string::npos &&
                   enterpriseSeamDoc.find("No runtime lock enforcement") != std::string::npos,
               "enterprise seam doc should keep auth, RBAC, quorum, and lock enforcement out of scope");

        const auto enterpriseSchemaText = read_text(
            locate_repo_file(std::filesystem::path("references") /
                             "backboard-enterprise-envelope-seams.schema.json"));
        const auto enterpriseSchema =
            parse_json_text(enterpriseSchemaText, "enterprise seam schema");
        expect(enterpriseSchema["properties"]["schema"]["const"].asString() ==
                   "kob.backboard.enterprise_envelope_seams.v1",
               "enterprise seam schema should expose a stable schema marker");
        expect(enterpriseSchemaText.find("\"owner_actor_alias\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"claimed_subject_ref\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"decision_status\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"policy_context_ref\"") != std::string::npos,
               "enterprise seam schema should include claim, review decision, and audit envelope fields");
        expect(enterpriseSchemaText.find("\"active\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"released\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"expired\"") != std::string::npos &&
                   enterpriseSchemaText.find("\"superseded\"") != std::string::npos,
               "enterprise seam schema should cover lease lifecycle statuses");
        expect(enterpriseSchemaText.find("\"path\"") == std::string::npos &&
                   enterpriseSchemaText.find("auth_subject") == std::string::npos &&
                   enterpriseSchemaText.find("tenant_id") == std::string::npos &&
                   enterpriseSchemaText.find("quorum") == std::string::npos,
               "enterprise seam schema should not expose path, auth subject, tenant, or quorum fields");

        const auto enterpriseFixture = parse_json_text(
            read_text(locate_repo_file(std::filesystem::path("references") /
                                       "backboard-enterprise-envelope-seams.fixture.json")),
            "enterprise seam fixture");
        expect(enterpriseFixture["schema"].asString() ==
                   "kob.backboard.enterprise_envelope_seams.v1",
               "enterprise seam fixture should match schema marker");
        expect(enterpriseFixture["claims"].size() == 1 &&
                   enterpriseFixture["claims"][0]["owner_actor_alias"].asString() == "koa" &&
                   enterpriseFixture["claims"][0]["claimed_subject_ref"]["item_id"].asString() == "KOB-TSK-0106",
               "enterprise seam fixture should represent a claim owner and claimed subject");
        expect(enterpriseFixture["claims"][0]["lease"]["status"].asString() == "active" &&
                   enterpriseFixture["claims"][0]["lease"]["expires_at"].asString().find("2026-07-05") != std::string::npos,
               "enterprise seam fixture should represent an advisory active lease with expiry");
        expect(enterpriseFixture["review_decisions"][0]["actor_alias"].asString() == "maintainer" &&
                   enterpriseFixture["review_decisions"][0]["decision_status"].asString() == "approved" &&
                   enterpriseFixture["review_decisions"][0]["related_refs"][0]["item_id"].asString() == "KOB-TSK-0106",
               "enterprise seam fixture should represent review decision metadata");
        expect(enterpriseFixture["audit_events"][0]["actor_alias"].asString() == "koa" &&
                   enterpriseFixture["audit_events"][0]["action_kind"].asString() == "claim.created" &&
                   enterpriseFixture["audit_events"][0]["policy_context_ref"]["policy_context_id"].asString() == "policy-local-first-alias-only",
               "enterprise seam fixture should represent audit event policy context refs");
        expect(has_string_value(enterpriseFixture["non_goals"], "RBAC or permission enforcement") &&
                   has_string_value(enterpriseFixture["non_goals"], "multi-tenant membership") &&
                   has_string_value(enterpriseFixture["non_goals"], "approval quorum"),
               "enterprise seam fixture should make enterprise enforcement non-goals explicit");
        const auto enterpriseFixtureSerialized = json_to_string(enterpriseFixture);
        expect(enterpriseFixtureSerialized.find("\"path\"") == std::string::npos &&
                   enterpriseFixtureSerialized.find("items/") == std::string::npos &&
                   enterpriseFixtureSerialized.find("decisions/") == std::string::npos &&
                   enterpriseFixtureSerialized.find("@") == std::string::npos,
               "enterprise seam fixture should not expose raw paths, personal emails, or repo file paths");

        const auto policySeamDoc = read_text(
            locate_repo_file(std::filesystem::path("docs") / "design" /
                             "backboard-policy-context-extension-seam.md"));
        expect(policySeamDoc.find("Policy Context Refs") != std::string::npos &&
                   policySeamDoc.find("Capability Requirement Metadata") != std::string::npos &&
                   policySeamDoc.find("Local-First Omitted Fields") != std::string::npos,
               "policy context seam doc should cover refs, capability requirements, and omitted local-first fields");
        expect(policySeamDoc.find("No authentication provider") != std::string::npos &&
                   policySeamDoc.find("No RBAC enforcement") != std::string::npos &&
                   policySeamDoc.find("No approval workflows") != std::string::npos &&
                   policySeamDoc.find("No permission matrix behavior") != std::string::npos,
               "policy context seam doc should keep auth, RBAC, approval, and permission behavior out of scope");

        const auto policySchemaText = read_text(
            locate_repo_file(std::filesystem::path("references") /
                             "backboard-policy-context-extension-seam.schema.json"));
        const auto policySchema =
            parse_json_text(policySchemaText, "policy context seam schema");
        expect(policySchema["properties"]["schema"]["const"].asString() ==
                   "kob.backboard.policy_context_extension_seam.v1",
               "policy context seam schema should expose a stable schema marker");
        expect(policySchemaText.find("\"policy_context_id\"") != std::string::npos &&
                   policySchemaText.find("\"required_capabilities\"") != std::string::npos &&
                   policySchemaText.find("\"affected_action\"") != std::string::npos &&
                   policySchemaText.find("\"affected_surface\"") != std::string::npos &&
                   policySchemaText.find("\"actor_alias\"") != std::string::npos &&
                   policySchemaText.find("\"evidence_refs\"") != std::string::npos &&
                   policySchemaText.find("\"rationale\"") != std::string::npos,
               "policy context seam schema should include policy and capability requirement fields");
        expect(policySchemaText.find("\"path\"") == std::string::npos &&
                   policySchemaText.find("auth_subject") == std::string::npos &&
                   policySchemaText.find("tenant_id") == std::string::npos &&
                   policySchemaText.find("permission_matrix") == std::string::npos &&
                   policySchemaText.find("quorum") == std::string::npos,
               "policy context seam schema should not expose path, auth subject, tenant, permission matrix, or quorum fields");

        const auto policyFixture = parse_json_text(
            read_text(locate_repo_file(std::filesystem::path("references") /
                                       "backboard-policy-context-extension-seam.fixture.json")),
            "policy context seam fixture");
        expect(policyFixture["schema"].asString() ==
                   "kob.backboard.policy_context_extension_seam.v1",
               "policy context seam fixture should match schema marker");
        expect(policyFixture["policy_contexts"].size() == 1 &&
                   policyFixture["policy_contexts"][0]["policy_context_id"].asString() == "policy-backboard-local-review" &&
                   policyFixture["policy_contexts"][0]["scope"]["scope_kind"].asString() == "review_queue",
               "policy context seam fixture should include one bounded policy context");
        expect(policyFixture["capability_requirements"].size() == 1 &&
                   policyFixture["capability_requirements"][0]["required_capabilities"][0].asString() ==
                       "backboard.review_decision.submit" &&
                   policyFixture["capability_requirements"][0]["affected_action"].asString() ==
                       "review_decision.submit" &&
                   policyFixture["capability_requirements"][0]["affected_surface"].asString() ==
                       "backboard.review_inbox" &&
                   policyFixture["capability_requirements"][0]["actor_alias"].asString() == "maintainer" &&
                   policyFixture["capability_requirements"][0]["policy_context_ref"]["policy_context_id"].asString() ==
                       "policy-backboard-local-review",
               "policy context seam fixture should include one capability requirement with optional actor and policy context refs");
        expect(policyFixture["local_first_defaults"]["policy_context_required"].asBool() == false &&
                   policyFixture["local_first_defaults"]["capability_requirement_required"].asBool() == false &&
                   policyFixture["local_first_defaults"]["missing_policy_provider_blocks_local_operation"].asBool() == false &&
                   policyFixture["local_first_defaults"]["omitted_fields_remain_valid"].asBool(),
               "policy context seam fixture should preserve single-user local-first defaults");
        expect(policyFixture["local_first_omitted_field_cases"].size() == 1 &&
                   policyFixture["local_first_omitted_field_cases"][0]["local_operation_allowed"].asBool() &&
                   !policyFixture["local_first_omitted_field_cases"][0].isMember("policy_context_ref") &&
                   !policyFixture["local_first_omitted_field_cases"][0].isMember("capability_requirement_ref"),
               "policy context seam fixture should cover omitted policy and capability fields");
        expect(has_string_value(policyFixture["non_goals"], "RBAC enforcement") &&
                   has_string_value(policyFixture["non_goals"], "approval workflows") &&
                   has_string_value(policyFixture["non_goals"], "permission matrix behavior") &&
                   has_string_value(policyFixture["non_goals"], "enterprise UI"),
               "policy context seam fixture should make enterprise policy enforcement non-goals explicit");
        const auto policyFixtureSerialized = json_to_string(policyFixture);
        expect(policyFixtureSerialized.find("\"path\"") == std::string::npos &&
                   policyFixtureSerialized.find("items/") == std::string::npos &&
                   policyFixtureSerialized.find("decisions/") == std::string::npos &&
                   policyFixtureSerialized.find("@") == std::string::npos,
               "policy context seam fixture should not expose raw paths, personal emails, or repo file paths");

        const auto readme = read_text(locate_repo_file("README.md"));
        expect(readme.find("pixi run webview-smoke-artifacts") != std::string::npos,
               "README should document the smoke artifact command");
        expect(readme.find("_ws/test-output/webview-smoke") != std::string::npos,
               "README should document the deterministic smoke artifact path");
        expect(readme.find("Backboard enterprise envelope seams") != std::string::npos,
               "README should link the Backboard enterprise envelope seam contract");
        expect(readme.find("Backboard policy context extension seam") != std::string::npos,
               "README should link the Backboard policy context extension seam contract");

        const auto docsIndex = read_text(
            locate_repo_file(std::filesystem::path("docs") / "index.md"));
        expect(docsIndex.find("pixi run webview-smoke-artifacts") != std::string::npos,
               "docs index should document the smoke artifact command");
        expect(docsIndex.find("_ws/test-output/webview-smoke") != std::string::npos,
               "docs index should document the deterministic smoke artifact path");

        const auto docsReadme = read_text(
            locate_repo_file(std::filesystem::path("docs") / "README.md"));
        expect(docsReadme.find("actor-alias-and-assignment-policy.schema.json") != std::string::npos &&
                   docsReadme.find("actor-alias-and-assignment-policy.fixture.json") != std::string::npos,
               "docs README should link the actor alias policy schema and fixture");
        expect(docsReadme.find("backboard-enterprise-envelope-seams.schema.json") != std::string::npos &&
                   docsReadme.find("backboard-enterprise-envelope-seams.fixture.json") != std::string::npos,
               "docs README should link the enterprise seam schema and fixture");
        expect(docsReadme.find("backboard-policy-context-extension-seam.schema.json") != std::string::npos &&
                   docsReadme.find("backboard-policy-context-extension-seam.fixture.json") != std::string::npos,
               "docs README should link the policy context seam schema and fixture");

        const auto schemaReference = read_text(
            locate_repo_file(std::filesystem::path("references") / "schema.md"));
        expect(schemaReference.find("Actor alias policy examples") != std::string::npos &&
                   schemaReference.find("`runner` aliases") != std::string::npos &&
                   schemaReference.find("grant execution permission") != std::string::npos,
               "schema reference should document runner actor alias semantics and non-enforcement boundary");
        expect(schemaReference.find("Backboard enterprise envelope seams") != std::string::npos &&
                   schemaReference.find("owner_actor_alias") != std::string::npos &&
                   schemaReference.find("permission enforcement") != std::string::npos,
               "schema reference should document enterprise seam fields and non-enforcement boundary");
        expect(schemaReference.find("Backboard policy context extension seam") != std::string::npos &&
                   schemaReference.find("required_capabilities") != std::string::npos &&
                   schemaReference.find("missing policy context or capability") != std::string::npos,
               "schema reference should document policy context and capability requirement fields");

        Json::Value draft(Json::objectValue);
        draft["product"] = "product-alpha";
        draft["item_id"] = "PRA-TSK-0001";
        draft["lane"] = "Ready Frontier";
        draft["reason_code"] = "ready_frontier_candidate";
        draft["suggested_decision"] = "approve_ready_boundary";
        draft["actor_alias"] = "reviewer-alias";
        draft["rationale"] = "First editable draft note.";
        auto draftOne = service.SaveReviewDecisionDraft(draft);
        expect(!draftOne.isMember("error"), "review decision draft save should succeed");
        const auto draftPath = products / "product-alpha" / draftOne["path"].asString();
        expect(std::filesystem::exists(draftPath), "review decision draft should persist to _meta");
        expect(!draftOne["empty"].asBool(), "non-empty draft should report explicit non-empty state");
        draft["rationale"] = "Edited draft note before submit.";
        auto draftTwo = service.SaveReviewDecisionDraft(draft);
        expect(!draftTwo.isMember("error"), "review decision draft edit should succeed");
        expect(draftTwo["path"].asString() == draftOne["path"].asString(),
               "editing a draft should update the same draft file before submit");
        expect(draftTwo["updated_existing"].asBool(), "second draft save should report existing draft update");
        const auto editedDraftText = read_text(draftPath);
        expect(editedDraftText.find("Edited draft note before submit.") != std::string::npos,
               "draft edit should overwrite draft rationale before submit");
        expect(editedDraftText.find("First editable draft note.") == std::string::npos,
               "draft edit should not append stale draft text");
        const auto submittedDraftOnlyDir = products / "product-alpha" / "_meta" /
            "review-decisions" / "submitted" / "PRA-TSK-0001";
        expect(count_regular_files(submittedDraftOnlyDir) == 0,
               "saving and editing drafts must not create submitted decision records");
        expect(read_text(products / "product-alpha" / "items" / "task" / "0001" / "PRA-TSK-0001.md").find("Edited draft note before submit.") == std::string::npos,
               "draft notes must not be written to item worklog before submit");

        auto reviewInboxWithDraft = service.BuildReviewInbox({});
        bool foundDraftInInbox = false;
        for (const auto& lane : reviewInboxWithDraft["lane_order"]) {
            for (const auto& bundle : reviewInboxWithDraft["lanes"][lane.asString()]) {
                if (bundle["item"]["id"].asString() == "PRA-TSK-0001") {
                    foundDraftInInbox = foundDraftInInbox ||
                        (bundle["review_draft"]["exists"].asBool() &&
                         bundle["review_draft"]["rationale"].asString() == "Edited draft note before submit.");
                }
            }
        }
        expect(foundDraftInInbox, "review inbox bundle should preserve saved draft across refresh");

        auto discardDraft = service.DiscardReviewDecisionDraft(draft);
        expect(!discardDraft.isMember("error"), "review decision draft discard should succeed");
        expect(discardDraft["discarded_existing"].asBool(), "discard should report removed existing draft");
        expect(!std::filesystem::exists(draftPath), "discard should remove draft file");
        auto discardMissingDraft = service.DiscardReviewDecisionDraft(draft);
        expect(!discardMissingDraft.isMember("error"), "discarding missing draft should be non-fatal");
        expect(!discardMissingDraft["discarded_existing"].asBool(), "missing draft discard should be explicit");
        draft["rationale"] = "";
        auto emptyDraft = service.SaveReviewDecisionDraft(draft);
        expect(!emptyDraft.isMember("error"), "empty review draft save should be non-fatal");
        expect(emptyDraft["empty"].asBool(), "empty review draft state should be explicit");
        draft["rationale"] = "Edited draft note before submit.";
        auto draftAfterDiscard = service.SaveReviewDecisionDraft(draft);
        expect(!draftAfterDiscard.isMember("error"), "draft should be saveable again after discard");

        Json::Value submit = draft;
        submit["human_decision"] = "approve_ready_boundary";
        submit["rationale"] = "Need validation evidence before closure.";
        auto submittedOne = service.SubmitReviewDecision(submit);
        expect(!submittedOne.isMember("error"), "submitted review decision should succeed");
        const auto submittedOnePath = products / "product-alpha" / submittedOne["path"].asString();
        expect(std::filesystem::exists(submittedOnePath), "submitted review decision should persist append-only record");
        expect(submittedOne["record"]["transition"]["outcome"].asString() == "skipped",
               "submitted decision without target state should record skipped transition outcome");
        expect(!submittedOne["record"]["agent_started"].asBool(), "review decision submit should not start agents");
        expect(!submittedOne["record"]["dispatch_started"].asBool(), "review decision submit should not dispatch work");

        Json::Value superseding = submit;
        superseding["rationale"] = "Superseding instruction after human correction.";
        superseding["supersedes"] = submittedOne["path"].asString();
        auto submittedTwo = service.SubmitReviewDecision(superseding);
        expect(!submittedTwo.isMember("error"), "superseding review decision should succeed");
        const auto submittedTwoPath = products / "product-alpha" / submittedTwo["path"].asString();
        expect(std::filesystem::exists(submittedTwoPath), "superseding review decision should persist a new record");
        expect(submittedTwo["path"].asString() != submittedOne["path"].asString(),
               "superseding a submitted decision must create a new append-only record");
        expect(std::filesystem::exists(submittedOnePath), "superseding must not rewrite or remove prior submitted decision");
        expect(read_text(submittedTwoPath).find(submittedOne["path"].asString()) != std::string::npos,
               "superseding record should reference the superseded decision");

        auto historyDetail = service.GetEvidenceDetail("product-alpha", "PRA-TSK-0001");
        expect(!historyDetail["review_decision_history"]["empty"].asBool(),
               "review decision history should not be empty after submitted decisions");
        expect(historyDetail["review_decision_history"]["entries"].size() >= 2,
               "review decision history should include multiple submitted entries");
        expect(historyDetail["review_decision_history"]["entries"][0]["superseded"].asBool(),
               "review decision history should mark superseded decisions");
        expect(historyDetail["review_decision_history"]["entries"][1]["supersedes"].asString() == submittedOne["path"].asString(),
               "review decision history should expose supersede chains");
        auto historyPartial = service.RenderItemPartial("product-alpha", "PRA-TSK-0001");
        expect(historyPartial.find("Review decision history") != std::string::npos,
               "item detail should render review decision history panel");
        expect(historyPartial.find("Superseding instruction after human correction.") != std::string::npos,
               "history panel should show rationale text");
        expect(historyPartial.find("reviewer-alias") != std::string::npos,
               "history panel should show actor alias");
        expect(historyPartial.find("skipped") != std::string::npos,
               "history panel should show transition outcome");
        expect(historyPartial.find("Raw review decision metadata") != std::string::npos,
               "history panel should keep raw metadata behind details toggle");

        Json::Value longRationaleSubmit = submit;
        longRationaleSubmit["rationale"] = std::string(240, 'x');
        auto longRationaleRecord = service.SubmitReviewDecision(longRationaleSubmit);
        expect(!longRationaleRecord.isMember("error"), "long rationale review decision should submit");
        auto compactHistoryPartial = service.RenderItemPartial("product-alpha", "PRA-TSK-0001");
        expect(compactHistoryPartial.find(std::string(180, 'x') + "...") != std::string::npos,
               "history panel should truncate long rationale in compact display");

        auto emptyHistoryPartial = service.RenderItemPartial("product-beta", "PRB-BUG-0002");
        expect(emptyHistoryPartial.find("No review decisions recorded.") != std::string::npos,
               "history panel should show explicit empty state");

        Json::Value highRisk(Json::objectValue);
        highRisk["product"] = "product-alpha";
        highRisk["item_id"] = "PRA-TSK-0004";
        highRisk["lane"] = "Done Candidate";
        highRisk["reason_code"] = "review_state";
        highRisk["suggested_decision"] = "mark_done";
        highRisk["human_decision"] = "mark_done";
        highRisk["rationale"] = "Human accepted evidence chain.";
        highRisk["actor_alias"] = "reviewer-alias";
        highRisk["target_state"] = "Done";
        auto highRiskBlocked = service.SubmitReviewDecision(highRisk);
        expect(highRiskBlocked["error_code"].asString() == "review_decision.confirmation_required",
               "high-risk Done action should require explicit confirmation before transition");
        expect(highRiskBlocked["transition"]["outcome"].asString() == "pending_confirmation",
               "unconfirmed high-risk action should expose pending confirmation transition outcome");
        expect(read_text(products / "product-alpha" / "items" / "task" / "0004" / "PRA-TSK-0004.md").find("state: Ready") != std::string::npos,
               "unconfirmed high-risk action must not mutate state");
        highRisk["confirmed"] = true;
        auto highRiskSubmitted = service.SubmitReviewDecision(highRisk);
        expect(!highRiskSubmitted.isMember("error"), "confirmed high-risk review decision should submit");
        expect(highRiskSubmitted["record"]["high_risk"].asBool(), "confirmed Done action should be marked high risk");
        expect(highRiskSubmitted["record"]["transition"]["attempted"].asBool(),
               "confirmed target-state action should call KOB state transition policy");
        expect(highRiskSubmitted["record"]["transition"]["outcome"].asString() == "applied",
               "confirmed target-state action should record applied transition outcome");
        expect(!highRiskSubmitted["record"]["agent_started"].asBool(), "confirmed review action should not start agents");
        expect(!highRiskSubmitted["record"]["dispatch_started"].asBool(), "confirmed review action should not dispatch work");
        expect(read_text(products / "product-alpha" / "items" / "task" / "0004" / "PRA-TSK-0004.md").find("state: Done") != std::string::npos,
               "confirmed high-risk action should use existing KOB state transition policy");

        Json::Value acceptRisk(Json::objectValue);
        acceptRisk["product"] = "product-alpha";
        acceptRisk["item_id"] = "PRA-TSK-0002";
        acceptRisk["lane"] = "Blocked/Dirty";
        acceptRisk["reason_code"] = "blocked_state";
        acceptRisk["suggested_decision"] = "accept_risk";
        acceptRisk["human_decision"] = "accept_risk";
        acceptRisk["rationale"] = "Human accepts the evidence risk for now.";
        acceptRisk["actor_alias"] = "reviewer-alias";
        auto acceptRiskBlocked = service.SubmitReviewDecision(acceptRisk);
        expect(acceptRiskBlocked["error_code"].asString() == "review_decision.confirmation_required",
               "Accept Evidence Risk should require explicit confirmation before submit");
        acceptRisk["confirmed"] = true;
        auto acceptRiskSubmitted = service.SubmitReviewDecision(acceptRisk);
        expect(!acceptRiskSubmitted.isMember("error"), "confirmed Accept Evidence Risk should submit");
        expect(acceptRiskSubmitted["record"]["high_risk"].asBool(),
               "Accept Evidence Risk should be marked high risk");
        expect(!acceptRiskSubmitted["record"]["transition"]["attempted"].asBool(),
               "Accept Evidence Risk should not mutate state without a target state");
        expect(acceptRiskSubmitted["record"]["transition"]["outcome"].asString() == "skipped",
               "accepted evidence risk without target state should record skipped transition outcome");

        Json::Value reopenDone(Json::objectValue);
        reopenDone["product"] = "product-beta";
        reopenDone["item_id"] = "PRB-BUG-0003";
        reopenDone["lane"] = "False Done Suspect";
        reopenDone["reason_code"] = "done_without_evidence";
        reopenDone["suggested_decision"] = "reopen_from_done";
        reopenDone["human_decision"] = "reopen_from_done";
        reopenDone["rationale"] = "Human wants Done reopened for review.";
        reopenDone["actor_alias"] = "reviewer-alias";
        reopenDone["target_state"] = "Review";
        auto reopenBlocked = service.SubmitReviewDecision(reopenDone);
        expect(reopenBlocked["error_code"].asString() == "review_decision.confirmation_required",
               "reopening Done should require explicit confirmation before policy check");
        reopenDone["confirmed"] = true;
        auto reopenSubmitted = service.SubmitReviewDecision(reopenDone);
        expect(!reopenSubmitted.isMember("error"),
               "confirmed reopen decision should preserve an audit record even if policy rejects transition");
        expect(reopenSubmitted["record"]["transition"]["attempted"].asBool(),
               "confirmed reopen decision should call transition policy");
        expect(reopenSubmitted["record"]["transition"]["policy_status"].asString() == "rejected",
               "unsupported Done to Review transition should be reported as policy rejection");
        expect(reopenSubmitted["record"]["transition"]["outcome"].asString() == "blocked",
               "policy-rejected transition should record blocked transition outcome");
        expect(!reopenSubmitted["record"]["transition"]["applied"].asBool(),
               "policy-rejected reopen must not mutate markdown state");
        expect(std::filesystem::exists(products / "product-beta" / reopenSubmitted["path"].asString()),
               "policy-rejected reopen should still persist submitted decision record");
        expect(read_text(products / "product-beta" / "items" / "bug" / "0003" / "PRB-BUG-0003.md").find("state: Done") != std::string::npos,
               "policy-rejected reopen must leave Done item unchanged");

        const auto transitionSubmittedDir = products / "product-beta" / "_meta" /
            "review-decisions" / "submitted" / "PRB-BUG-0001";
        const auto transitionRecordCountBefore = count_regular_files(transitionSubmittedDir);
        const auto transitionItemPath = products / "product-beta" / "items" / "bug" / "0001" / "PRB-BUG-0001.md";
        std::filesystem::permissions(
            transitionItemPath,
            std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
                std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace);
        Json::Value writeFailureTransition(Json::objectValue);
        writeFailureTransition["product"] = "product-beta";
        writeFailureTransition["item_id"] = "PRB-BUG-0001";
        writeFailureTransition["lane"] = "Done Candidate";
        writeFailureTransition["reason_code"] = "validation_seen_in_progress";
        writeFailureTransition["suggested_decision"] = "move_to_review";
        writeFailureTransition["human_decision"] = "move_to_review";
        writeFailureTransition["rationale"] = "Human wants review before Done.";
        writeFailureTransition["actor_alias"] = "reviewer-alias";
        writeFailureTransition["target_state"] = "Review";
        auto writeFailureResult = service.SubmitReviewDecision(writeFailureTransition);
        std::filesystem::permissions(
            transitionItemPath,
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                std::filesystem::perms::others_read,
            std::filesystem::perm_options::replace);
        expect(writeFailureResult["error_code"].asString() == "review_decision.submit_failed",
               "markdown apply failure after accepted policy should report submit failure");
        expect(count_regular_files(transitionSubmittedDir) == transitionRecordCountBefore + 1,
               "submitted audit record should survive markdown apply failure");
        expect(directory_tree_contains_text(transitionSubmittedDir, "blocked"),
               "markdown apply failure should leave a recoverable blocked transition outcome in submitted record");
        expect(read_text(transitionItemPath).find("state: InProgress") != std::string::npos,
               "markdown apply failure should leave original item state unchanged");

        Json::Value reviewTransition(Json::objectValue);
        reviewTransition["product"] = "product-beta";
        reviewTransition["item_id"] = "PRB-BUG-0001";
        reviewTransition["lane"] = "Done Candidate";
        reviewTransition["reason_code"] = "validation_seen_in_progress";
        reviewTransition["suggested_decision"] = "reject_completion";
        reviewTransition["human_decision"] = "reject_completion";
        reviewTransition["rationale"] = "Human wants review before Done.";
        reviewTransition["actor_alias"] = "reviewer-alias";
        reviewTransition["target_state"] = "Review";
        auto reviewTransitionResult = service.SubmitReviewDecision(reviewTransition);
        expect(!reviewTransitionResult.isMember("error"), "non-high-risk review action should submit");
        expect(reviewTransitionResult["record"]["transition"]["attempted"].asBool(),
               "target-state review action should call transition policy");
        expect(reviewTransitionResult["record"]["transition"]["new_state"].asString() == "Review",
               "review action should transition through KOB policy to Review");
        expect(reviewTransitionResult["record"]["transition"]["outcome"].asString() == "applied",
               "non-high-risk target-state action should record applied transition outcome");
        expect(!reviewTransitionResult["record"]["agent_started"].asBool(), "review transition should not start agents");
        expect(!reviewTransitionResult["record"]["dispatch_started"].asBool(), "review transition should not dispatch work");

        expect(assetSource.find("data-review-draft-note") != std::string::npos,
               "embedded webview assets should expose editable review draft note controls");
        expect(assetSource.find("data-review-action") != std::string::npos,
               "embedded webview assets should expose review action controls");
        expect(assetSource.find("Resulting state:") != std::string::npos,
               "embedded webview assets should show resulting state before submit");

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
