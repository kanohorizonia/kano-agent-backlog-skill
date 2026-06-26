#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/integrity/integrity_ops.hpp"

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
    const auto root = std::filesystem::temp_directory_path() / "kano-backlog-integrity-smoke" / suffix.str();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "products");
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << text;
}

std::string body_all_ready() {
    return
        "# Context\n\nFixture context.\n\n"
        "# Goal\n\nFixture goal.\n\n"
        "# Approach\n\nFixture approach.\n\n"
        "# Acceptance Criteria\n\nFixture acceptance.\n\n"
        "# Risks / Dependencies\n\nFixture risks.\n";
}

std::string item_markdown(
    const std::string& id,
    const std::string& uid,
    const std::string& type,
    const std::string& title,
    const std::string& state,
    const std::string& created,
    const std::string& updated,
    const std::string& parent = "",
    const std::string& duplicate_of = "",
    const std::string& body = ""
) {
    std::ostringstream out;
    out << "---\n";
    out << "id: " << id << "\n";
    out << "uid: " << uid << "\n";
    out << "type: " << type << "\n";
    out << "title: \"" << title << "\"\n";
    out << "state: " << state << "\n";
    out << "priority: P2\n";
    out << "parent: " << (parent.empty() ? "null" : parent) << "\n";
    out << "duplicate_of: " << (duplicate_of.empty() ? "null" : duplicate_of) << "\n";
    out << "owner: null\n";
    out << "tags: []\n";
    out << "created: " << created << "\n";
    out << "updated: " << updated << "\n";
    out << "area: general\n";
    out << "iteration: backlog\n";
    out << "external: {}\n";
    out << "links:\n";
    out << "  relates: []\n";
    out << "  blocks: []\n";
    out << "  blocked_by: []\n";
    out << "decisions: []\n";
    out << "---\n\n";
    out << body;
    if (!body.empty() && body.back() != '\n') {
        out << "\n";
    }
    return out.str();
}

std::filesystem::path item_path(
    const std::filesystem::path& backlog_root,
    const std::string& product,
    const std::string& type_dir,
    const std::string& filename
) {
    return backlog_root / "products" / product / "items" / type_dir / "0000" / filename;
}

void write_item(
    const std::filesystem::path& backlog_root,
    const std::string& product,
    const std::string& type_dir,
    const std::string& filename,
    const std::string& markdown
) {
    write_text(item_path(backlog_root, product, type_dir, filename), markdown);
}

std::vector<std::string> finding_rule_ids(const kano::backlog_ops::IntegrityReport& report) {
    std::vector<std::string> ids;
    ids.reserve(report.findings.size());
    for (const auto& finding : report.findings) {
        ids.push_back(finding.rule_id);
    }
    return ids;
}

std::string join_rule_ids(const std::vector<std::string>& ids) {
    std::ostringstream out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << ids[i];
    }
    return out.str();
}

void create_clean_fixture(const std::filesystem::path& root) {
    write_item(
        root,
        "clean-a",
        "feature",
        "CLNA-FTR-0001_clean-parent.md",
        item_markdown(
            "CLNA-FTR-0001",
            "019cdf6a-1000-7000-8000-000000000001",
            "Feature",
            "Clean parent",
            "Planned",
            "2026-06-01",
            "2026-06-01"));
    write_item(
        root,
        "clean-a",
        "task",
        "CLNA-TSK-0001_clean-ready-task.md",
        item_markdown(
            "CLNA-TSK-0001",
            "019cdf6a-1000-7000-8000-000000000002",
            "Task",
            "Clean ready task",
            "Ready",
            "2026-06-01",
            "2026-06-10",
            "CLNA-FTR-0001",
            "",
            body_all_ready()));
    write_item(
        root,
        "clean-b",
        "task",
        "CLNB-TSK-0001_other-clean-task.md",
        item_markdown(
            "CLNB-TSK-0001",
            "019cdf6a-1000-7000-8000-000000000003",
            "Task",
            "Other clean task",
            "Planned",
            "2026-06-01",
            "2026-06-11"));
    write_text(
        item_path(root, "clean-b", "task", "CLNB-TSK-0001_other-clean-task.index.md"),
        "Generated index sidecar, not canonical item Markdown.\n\n- id: CLNB-TSK-0001\n");
}

void create_dirty_fixture(const std::filesystem::path& root) {
    write_item(
        root,
        "dirty",
        "feature",
        "DRTY-FTR-0001_shared-title-feature.md",
        item_markdown(
            "DRTY-FTR-0001",
            "019cdf6a-2000-7000-8000-000000000001",
            "Feature",
            "Shared Title",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0001_shared-title-task.md",
        item_markdown(
            "DRTY-TSK-0001",
            "019cdf6a-2000-7000-8000-000000000002",
            "Task",
            "Shared Title",
            "Planned",
            "2026-01-01",
            "2026-01-01",
            "DRTY-FTR-9999"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0002_duplicate-of-missing.md",
        item_markdown(
            "DRTY-TSK-0002",
            "019cdf6a-2000-7000-8000-000000000003",
            "Task",
            "Duplicate of missing",
            "Duplicate",
            "2026-06-01",
            "2026-06-20",
            "",
            "DRTY-TSK-9999"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0003_duplicate-state-drift.md",
        item_markdown(
            "DRTY-TSK-0003",
            "019cdf6a-2000-7000-8000-000000000004",
            "Task",
            "Duplicate state drift",
            "Planned",
            "2026-06-01",
            "2026-06-20",
            "",
            "DRTY-TSK-0001"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0004_ready-missing.md",
        item_markdown(
            "DRTY-TSK-0004",
            "019cdf6a-2000-7000-8000-000000000005",
            "Task",
            "Ready missing",
            "Ready",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0999_wrong-id.md",
        item_markdown(
            "DRTY-TSK-0005",
            "019cdf6a-2000-7000-8000-000000000006",
            "Task",
            "Wrong path id",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "bug",
        "DRTY-TSK-0006_wrong-type.md",
        item_markdown(
            "DRTY-TSK-0006",
            "019cdf6a-2000-7000-8000-000000000007",
            "Task",
            "Wrong path type",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0007_duplicate-id-a.md",
        item_markdown(
            "DRTY-TSK-0007",
            "019cdf6a-2000-7000-8000-000000000008",
            "Task",
            "Duplicate id A",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0007_duplicate-id-b.md",
        item_markdown(
            "DRTY-TSK-0007",
            "019cdf6a-2000-7000-8000-000000000009",
            "Task",
            "Duplicate id B",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0008_duplicate-uid-a.md",
        item_markdown(
            "DRTY-TSK-0008",
            "019cdf6a-2000-7000-8000-000000000010",
            "Task",
            "Duplicate uid A",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
    write_item(
        root,
        "dirty",
        "task",
        "DRTY-TSK-0009_duplicate-uid-b.md",
        item_markdown(
            "DRTY-TSK-0009",
            "019cdf6a-2000-7000-8000-000000000010",
            "Task",
            "Duplicate uid B",
            "Planned",
            "2026-06-01",
            "2026-06-20"));
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    try {
        const auto root = make_temp_root();
        create_clean_fixture(root);
        create_dirty_fixture(root);

        kano::backlog_ops::IntegrityOptions clean_options;
        clean_options.backlog_root = root;
        clean_options.products = {"clean-a", "clean-b"};
        clean_options.as_of = "2026-06-25";
        clean_options.stale_days = 90;
        const auto clean_report = kano::backlog_ops::IntegrityOps::inspect(clean_options);
        expect(clean_report.items_scanned == 3, "clean fixture should scan three items");
        expect(clean_report.findings.empty(), "clean fixture should produce no findings");
        expect(kano::backlog_ops::IntegrityOps::render_markdown(clean_report).find("No integrity findings.") != std::string::npos,
            "clean markdown should say no findings");

        kano::backlog_ops::IntegrityOptions dirty_options;
        dirty_options.backlog_root = root;
        dirty_options.products = {"dirty"};
        dirty_options.as_of = "2026-06-25";
        dirty_options.stale_days = 90;
        const auto dirty_report = kano::backlog_ops::IntegrityOps::inspect(dirty_options);
        const std::vector<std::string> expected_rules = {
            "drift.duplicate_state",
            "drift.path_id",
            "drift.path_type",
            "drift.ready_gate",
            "duplicate.id",
            "duplicate.id",
            "duplicate.title",
            "duplicate.title",
            "duplicate.uid",
            "duplicate.uid",
            "stale.duplicate_of_ref",
            "stale.parent_ref",
            "stale.updated_age"
        };
        const auto actual_rules = finding_rule_ids(dirty_report);
        expect(
            actual_rules == expected_rules,
            "dirty fixture rule ordering should be deterministic; actual: " + join_rule_ids(actual_rules));
        expect(dirty_report.findings.size() == expected_rules.size(), "dirty fixture finding count mismatch");
        const auto dirty_markdown = kano::backlog_ops::IntegrityOps::render_markdown(dirty_report);
        expect(dirty_markdown.find("## duplicate") != std::string::npos, "dirty markdown should group duplicate findings");
        expect(dirty_markdown.find("## drift") != std::string::npos, "dirty markdown should group drift findings");
        expect(dirty_markdown.find("## stale") != std::string::npos, "dirty markdown should group stale findings");
        const auto dirty_json = kano::backlog_ops::IntegrityOps::render_json(dirty_report);
        expect(dirty_json.find("\"groups\"") != std::string::npos, "dirty JSON should include grouped findings");
        expect(dirty_json.find("duplicate.uid") != std::string::npos, "dirty JSON should include duplicate uid rule");

        kano::backlog_ops::IntegrityOptions all_options;
        all_options.backlog_root = root;
        all_options.as_of = "2026-06-25";
        all_options.stale_days = 90;
        const auto all_report = kano::backlog_ops::IntegrityOps::inspect(all_options);
        expect(all_report.items_scanned == 14, "all-product scan should include clean and dirty products");
        expect(all_report.findings.size() == expected_rules.size(), "all-product scan should only report dirty findings");

        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integrity_ops_smoke_test failed: " << ex.what() << "\n";
        return 1;
    }
}
