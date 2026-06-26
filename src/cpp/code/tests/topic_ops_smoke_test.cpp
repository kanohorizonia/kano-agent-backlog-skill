#include "kano/backlog_ops/topic/topic_ops.hpp"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path unique_temp_root() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("kob-topic-ops-smoke-" + std::to_string(now));
}

void write_item(
    const std::filesystem::path& backlog_root,
    const std::string& id,
    const std::string& state
) {
    write_text(
        backlog_root / "products" / "demo" / "items" / "task" / "0000" / (id + "_smoke.md"),
        "---\n"
        "id: " + id + "\n"
        "uid: " + id + "\n"
        "title: Smoke " + id + "\n"
        "type: Task\n"
        "state: " + state + "\n"
        "priority: Medium\n"
        "---\n\n"
        "# " + id + "\n"
    );
}

kano::backlog_ops::TopicOps::TopicAuditEntry find_topic(
    const kano::backlog_ops::TopicOps::TopicAuditReport& report,
    const std::string& name
) {
    for (const auto& topic : report.topics) {
        if (topic.topic == name) {
            return topic;
        }
    }
    throw std::runtime_error("missing audit topic " + name);
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace

int main() {
    try {
        const auto root = unique_temp_root();
        const auto backlog_root = root / "_kano" / "backlog";
        std::filesystem::create_directories(backlog_root / "topics");

        write_item(backlog_root, "KOB-TSK-0001", "InProgress");
        write_item(backlog_root, "KOB-TSK-0002", "Done");
        write_item(backlog_root, "KOB-TSK-0003", "Dropped");

        kano::backlog_ops::TopicOps::validate_topic_name("2026-02-01-safe-topic");
        expect(kano::backlog_ops::TopicOps::has_date_prefix("2026-02-01-safe-topic"), "date-prefixed topic not detected");
        expect(!kano::backlog_ops::TopicOps::has_date_prefix("legacy-topic"), "legacy topic incorrectly detected as date-prefixed");
        for (const auto& bad_name : {"", ".", "..", "../escape", "bad/name", "bad name", "bad$name"}) {
            bool rejected = false;
            try {
                kano::backlog_ops::TopicOps::validate_topic_name(bad_name);
            } catch (const std::exception&) {
                rejected = true;
            }
            expect(rejected, std::string("unsafe topic name accepted: ") + bad_name);
        }

        auto active = kano::backlog_ops::TopicOps::create_topic("2026-01-01-active-topic", "tester", backlog_root);
        auto active_manifest = *kano::backlog_ops::TopicOps::load_manifest(active.topic_path);
        active_manifest.created_at = "2026-01-01T00:00:00Z";
        active_manifest.updated_at = "2026-01-01T00:00:00Z";
        active_manifest.seed_items = {"KOB-TSK-0001"};
        kano::backlog_ops::TopicOps::save_manifest(active.topic_path, active_manifest);
        write_text(backlog_root / ".cache" / "worksets" / "topics" / "topic-active.json",
            "{\n"
            "  \"topic_id\": \"topic-active\",\n"
            "  \"name\": \"2026-01-01-active-topic\"\n"
            "}\n");
        write_text(backlog_root / ".cache" / "worksets" / "state.json",
            "{\n"
            "  \"agents\": {\n"
            "    \"tester\": {\"active_topic_id\": \"topic-active\"}\n"
            "  }\n"
            "}\n");

        auto stale_done = kano::backlog_ops::TopicOps::create_topic("2026-01-02-stale-done", "tester", backlog_root);
        auto stale_manifest = *kano::backlog_ops::TopicOps::load_manifest(stale_done.topic_path);
        stale_manifest.created_at = "2026-01-02T00:00:00Z";
        stale_manifest.updated_at = "2026-01-02T00:00:00Z";
        stale_manifest.seed_items = {"KOB-TSK-0002", "KOB-TSK-0003"};
        kano::backlog_ops::TopicOps::save_manifest(stale_done.topic_path, stale_manifest);

        auto closed_materials = kano::backlog_ops::TopicOps::create_topic("2026-01-03-closed-materials", "tester", backlog_root);
        auto materials_manifest = *kano::backlog_ops::TopicOps::load_manifest(closed_materials.topic_path);
        materials_manifest.created_at = "2026-01-03T00:00:00Z";
        materials_manifest.updated_at = "2026-01-03T00:00:00Z";
        materials_manifest.status = "closed";
        materials_manifest.closed_at = "2026-01-03T00:00:00Z";
        kano::backlog_ops::TopicOps::save_manifest(closed_materials.topic_path, materials_manifest);
        write_text(closed_materials.topic_path / "materials" / "clips" / "old.txt", "old material\n");

        auto closed_empty = kano::backlog_ops::TopicOps::create_topic("2026-01-04-closed-empty", "tester", backlog_root);
        auto empty_manifest = *kano::backlog_ops::TopicOps::load_manifest(closed_empty.topic_path);
        empty_manifest.created_at = "2026-01-04T00:00:00Z";
        empty_manifest.updated_at = "2026-01-04T00:00:00Z";
        empty_manifest.status = "closed";
        empty_manifest.closed_at = "2026-01-04T00:00:00Z";
        kano::backlog_ops::TopicOps::save_manifest(closed_empty.topic_path, empty_manifest);

        auto legacy = kano::backlog_ops::TopicOps::create_topic("legacy-topic", "tester", backlog_root);
        const auto legacy_manifest_before = read_text(legacy.topic_path / "manifest.json");

        write_text(backlog_root / "topics" / "invalid-manifest-topic" / "manifest.json", "{ not valid json\n");
        std::filesystem::create_directories(backlog_root / "topics" / "2026-01-05-missing-manifest");

        kano::backlog_ops::TopicOps::TopicAuditOptions options;
        options.ttl_days = 14;
        options.stale_days = 30;
        options.as_of = "2026-03-01";
        const auto report = kano::backlog_ops::TopicOps::audit_topics(backlog_root, options);
        expect(!report.mutated, "audit report must declare mutated=false");
        expect(report.topics.size() == 7, "audit did not scan all topic directories");

        const auto active_audit = find_topic(report, "2026-01-01-active-topic");
        expect(active_audit.recommendation == "keep", "active topic should be kept");
        expect(contains(active_audit.active_agents, "tester"), "active topic missing active agent");
        expect(active_audit.open_item_count == 1, "active topic should count open items");

        const auto stale_audit = find_topic(report, "2026-01-02-stale-done");
        expect(stale_audit.recommendation == "close_candidate", "stale all-done topic should be close candidate");
        expect(stale_audit.done_item_count == 1, "done item count missing");
        expect(stale_audit.dropped_item_count == 1, "dropped item count missing");

        const auto cleanup_audit = find_topic(report, "2026-01-03-closed-materials");
        expect(cleanup_audit.recommendation == "cleanup_materials_candidate", "closed materials topic should be cleanup candidate");
        expect(cleanup_audit.materials_present, "materials presence not detected");
        expect(cleanup_audit.materials_size_bytes > 0, "materials size not detected");

        const auto delete_audit = find_topic(report, "2026-01-04-closed-empty");
        expect(delete_audit.recommendation == "delete_topic_candidate", "closed empty topic should be delete candidate");

        const auto legacy_audit = find_topic(report, "legacy-topic");
        expect(!legacy_audit.has_date_prefix, "legacy topic should not have date prefix");
        expect(contains(legacy_audit.stale_reasons, "missing_date_prefix"), "legacy topic missing prefix reason");

        const auto invalid_audit = find_topic(report, "invalid-manifest-topic");
        expect(invalid_audit.status == "invalid", "invalid manifest topic should report invalid status");
        expect(invalid_audit.recommendation == "manual_review", "invalid manifest topic should require manual review");
        expect(contains(invalid_audit.stale_reasons, "invalid_manifest"), "invalid manifest topic missing reason");

        const auto missing_audit = find_topic(report, "2026-01-05-missing-manifest");
        expect(missing_audit.status == "invalid", "missing manifest topic should report invalid status");
        expect(missing_audit.recommendation == "manual_review", "missing manifest topic should require manual review");

        const auto json = kano::backlog_ops::TopicOps::render_audit_report(report, "json");
        expect(json.find("\"mutated\" : false") != std::string::npos, "json audit output missing mutated=false");
        expect(json.find("cleanup_materials_candidate") != std::string::npos, "json audit output missing cleanup recommendation");
        const auto markdown = kano::backlog_ops::TopicOps::render_audit_report(report, "markdown");
        expect(markdown.find("# Topic Audit") != std::string::npos, "markdown audit output missing heading");
        const auto plain = kano::backlog_ops::TopicOps::render_audit_report(report, "plain");
        expect(plain.find("Topic audit as of 2026-03-01") != std::string::npos, "plain audit output missing heading");

        expect(read_text(legacy.topic_path / "manifest.json") == legacy_manifest_before, "audit mutated topic manifest");
        expect(!std::filesystem::exists(legacy.topic_path / "publish" / "topic-audit.md"), "audit wrote a report file");

        std::filesystem::remove_all(root);
        std::cout << "topic_ops_smoke_test passed\n";
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "topic_ops_smoke_test failed: " << exc.what() << "\n";
        return 1;
    }
}
