#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/workitem/workitem_ops.hpp"

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

    std::filesystem::path root = std::filesystem::temp_directory_path() /
        "kano-backlog-create-request-scan-smoke" /
        suffix.str();
    std::filesystem::create_directories(root / "items");
    std::filesystem::create_directories(root / "views");
    std::filesystem::create_directories(root / "_meta");
    return root;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << text;
}

template <typename Fn>
void expect_throws_contains(Fn&& fn, const std::string& needle, const std::string& message) {
    try {
        fn();
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find(needle) != std::string::npos) return;
        throw std::runtime_error(message + " (unexpected diagnostic: " + ex.what() + ")");
    }
    throw std::runtime_error(message + " (operation unexpectedly succeeded)");
}

kano::backlog_ops::DuplicateAdmissionEvidence duplicate_admission(
    std::string query,
    std::vector<std::string> candidates = {},
    std::vector<std::string> read = {},
    bool override_requested = false,
    std::string rationale = ""
) {
    return {std::move(query), "test-product", std::move(candidates), std::move(read), "create", std::move(rationale), override_requested};
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    using kano::backlog_core::ItemType;
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;
    std::filesystem::path root;
    try {
        root = make_temp_root();
        {
            // PR5 must never write metadata that its next canonical request
            // scan cannot verify, even when the item title is short.
            const auto product = root / "generated-metadata-admission";
            BacklogIndex index(product / ".cache" / "index" / "backlog.db");
            index.initialize();
            auto oversized = duplicate_admission("Short metadata request");
            oversized.rationale = std::string(70 * 1024, 'x');
            const auto oversized_create = [&] {
                (void)WorkitemOps::create_item(
                    index, product, "OVR", ItemType::Task, "Short metadata request", "opencode",
                    std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                    "", "", oversized, "oversize-key");
            };
            expect_throws_contains(oversized_create, "generated_metadata_limit_exceeded",
                "oversized generated metadata must be rejected on first create");
            expect_throws_contains(oversized_create, "generated_metadata_limit_exceeded",
                "same-key retry of rejected metadata must remain a pre-allocation failure");
            expect(!index.has_sequence("OVR", "TSK") && CanonicalStore(product).list_items().empty(),
                "oversized first create and retry must leave no reservation or canonical item");
            const auto bystander = WorkitemOps::create_item(
                index, product, "OVR", ItemType::Task, "Bystander", "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                "", "", duplicate_admission("Bystander"), "bystander-key");
            expect(bystander.id == "OVR-TSK-0001" && bystander.read_after_write,
                "rejected metadata must not poison a bystander or consume its first allocation");
            const auto bystander_retry = WorkitemOps::create_item(
                index, product, "OVR", ItemType::Task, "Bystander", "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                "", "", duplicate_admission("Bystander"), "bystander-key");
            expect(bystander_retry.idempotent_replay && bystander_retry.uid == bystander.uid,
                "bystander must remain replayable after rejecting oversized metadata");

            // YAML escaping counts toward the limit, not just raw input bytes.
            auto expanded = duplicate_admission("Escaped metadata");
            expanded.rationale = "x" + std::string(35 * 1024, '\t') + "x";
            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    index, product, "ESC", ItemType::Task, "Escaped metadata", "opencode",
                    std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                    "", "", expanded, "escaped-key");
            }, "generated_metadata_limit_exceeded", "admission must measure serialized metadata bytes");
            expect(!index.has_sequence("ESC", "TSK"), "escaped metadata failure must not reserve an ID");

            // Plain creates must not poison subsequent idempotent admission.
            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    index, product, "PLN", ItemType::Task, "Plain metadata", "opencode",
                    std::nullopt, "P2", {std::string(70 * 1024, 'x')}, "general", "backlog",
                    std::nullopt, std::nullopt, "", "", duplicate_admission("Plain metadata"));
            }, "generated_metadata_limit_exceeded", "non-idempotent oversized metadata must also fail before allocation");
            expect(!index.has_sequence("PLN", "TSK"), "plain metadata failure must not reserve an ID");

            auto admitted = duplicate_admission("Admitted metadata");
            admitted.rationale = std::string(60 * 1024, 'x');
            const auto valid = WorkitemOps::create_item(
                index, product, "VAL", ItemType::Task, "Admitted metadata", "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                "", "", admitted, "valid-key");
            expect(CanonicalStore(product).read(valid.path).external.at("create_request_payload")
                .find(admitted.rationale) != std::string::npos,
                "admitted metadata must preserve the complete rationale without truncation");
            const auto valid_retry = WorkitemOps::create_item(
                index, product, "VAL", ItemType::Task, "Admitted metadata", "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                "", "", admitted, "valid-key");
            expect(valid_retry.idempotent_replay && valid_retry.uid == valid.uid,
                "large admitted metadata must remain replayable");
            const auto after_valid = WorkitemOps::create_item(
                index, product, "OVR", ItemType::Task, "After valid metadata", "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                "", "", duplicate_admission("After valid metadata"), "after-valid-key");
            expect(after_valid.id == "OVR-TSK-0002" && after_valid.read_after_write,
                "valid large metadata must not block an unrelated idempotent create");
        }
        {
            // KOB-BUG-0098: idempotency admission needs canonical metadata,
            // not the bodies/worklogs of every unrelated item in a large store.
            const auto scale_root = root / "request-scan-scale";
            std::filesystem::create_directories(scale_root / "items");
            BacklogIndex scale_index(scale_root / ".cache" / "index" / "backlog.db");
            scale_index.initialize();
            const std::string large_body = "\n# Worklog\n\n" + std::string(65536, 'x') + "\n";
            for (int number = 1; number <= 512; ++number) {
                std::ostringstream id;
                id << "SCL-TSK-" << std::setw(4) << std::setfill('0') << number;
                write_text(scale_root / "items" / (id.str() + "_unrelated.md"),
                    "---\nid: " + id.str() + "\nuid: " + CanonicalStore::generate_uuid_v7() +
                    "\ntype: Task\ntitle: Unrelated scale fixture\nstate: Proposed\n"
                    "created: 2026-10-01\nupdated: 2026-10-01\nexternal: {}\n---\n" + large_body);
            }
            std::ostringstream scan_timing;
            auto* previous_stderr = std::cerr.rdbuf(scan_timing.rdbuf());
            const bool previous_timing = kano::backlog_core::diagnostics::mutation_timing_forced();
            kano::backlog_core::diagnostics::mutation_timing_forced() = true;
            kano::backlog_core::CreateItemResult scale_created;
            try {
                scale_created = WorkitemOps::create_item(
                    scale_index, scale_root, "NEW", ItemType::Task,
                    "Large-store request scan", "opencode", std::nullopt, "P2", {},
                    "general", "backlog", std::nullopt, std::nullopt, "", "",
                    duplicate_admission("Large-store request scan"), "scale-key");
            } catch (...) {
                std::cerr.rdbuf(previous_stderr);
                kano::backlog_core::diagnostics::mutation_timing_forced() = previous_timing;
                throw;
            }
            std::cerr.rdbuf(previous_stderr);
            kano::backlog_core::diagnostics::mutation_timing_forced() = previous_timing;
            expect(scan_timing.str().find("\"span\":\"canonical_store.read\",\"duration_ms\":") !=
                std::string::npos, "create must retain full canonical allocation readback evidence");
            bool unrelated_body_read = false;
            std::istringstream scan_lines(scan_timing.str());
            for (std::string line; std::getline(scan_lines, line);) {
                unrelated_body_read = unrelated_body_read ||
                    (line.find("\"span\":\"canonical_store.read\"") != std::string::npos &&
                     line.find("\"detail\":\"SCL-") != std::string::npos);
            }
            expect(!unrelated_body_read, "request scans must not fully parse unrelated large bodies");
            expect(scan_timing.str().find("canonical_store.read_metadata_bounded") != std::string::npos,
                "request admission must use bounded canonical metadata reads");
            expect(scale_created.read_after_write &&
                CanonicalStore(scale_root).read(scale_created.path).uid == scale_created.uid,
                "large-store create must verify the real canonical UID");
            const auto scale_receipt = scale_root / "_meta" / "duplicate-admission" /
                (scale_created.id + ".json");
            const auto scale_receipt_bytes = read_text(scale_receipt);
            const auto scale_replayed = WorkitemOps::create_item(
                scale_index, scale_root, "NEW", ItemType::Task,
                "Large-store request scan", "opencode", std::nullopt, "P2", {},
                "general", "backlog", std::nullopt, std::nullopt, "", "",
                duplicate_admission("Large-store request scan"), "scale-key");
            expect(scale_replayed.idempotent_replay && scale_replayed.id == scale_created.id &&
                scale_replayed.uid == scale_created.uid && read_text(scale_receipt) == scale_receipt_bytes,
                "large-store retry must preserve canonical identity and original receipt bytes");
            write_text(scale_receipt,
                "{\"item_id\":\"" + scale_created.id +
                "\",\"item_uid\":\"conflicting-uid\",\"idempotency_key\":\"scale-key\"}");
            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    scale_index, scale_root, "NEW", ItemType::Task,
                    "Large-store request scan", "opencode", std::nullopt, "P2", {},
                    "general", "backlog", std::nullopt, std::nullopt, "", "",
                    duplicate_admission("Large-store request scan"), "scale-key");
            }, "request_receipt_identity_mismatch",
                "metadata scan optimization must not bypass a conflicting partial-write receipt");
            expect(CanonicalStore(scale_root).read(scale_created.path).uid == scale_created.uid &&
                CanonicalStore(scale_root).find_item_paths_by_id("NEW-TSK-0002").empty(),
                "receipt conflict must preserve the original canonical UID without replaying creation");
            std::filesystem::remove(scale_receipt);
            const auto scale_recovered = WorkitemOps::create_item(
                scale_index, scale_root, "NEW", ItemType::Task,
                "Large-store request scan", "opencode", std::nullopt, "P2", {},
                "general", "backlog", std::nullopt, std::nullopt, "", "",
                duplicate_admission("Large-store request scan"), "scale-key");
            expect(scale_recovered.idempotent_replay && scale_recovered.uid == scale_created.uid &&
                std::filesystem::exists(scale_receipt),
                "partial canonical write must recover a missing receipt without allocating a replacement UID");

            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    scale_index, scale_root, "NEW", ItemType::Task, "Changed request",
                    "opencode", std::nullopt, "P2", {}, "general", "backlog",
                    std::nullopt, std::nullopt, "", "", duplicate_admission("Changed request"),
                    "scale-key");
            }, "idempotency_conflict", "changed requests must not reuse a canonical marker");

            // A pre-write crash keeps its reservation; both admission passes
            // must fit the same bounded budget in a large store.
            const std::string crash_title = "Large-store reserved request";
            std::string crash_payload;
            for (const auto& field : {"CRH", "TSK", crash_title.c_str(), "opencode", "", "P2",
                     "general", "backlog", "", "", "", "", crash_title.c_str(),
                     "test-product", "create", "", "false", "0", "0", "0"}) {
                const std::string value = field;
                crash_payload += std::to_string(value.size()) + ":" + value;
            }
            const auto reservation = scale_index.reserve_next_number_for_request(
                "CRH", "TSK", "opencode", "crash-key", crash_payload);
            const auto recovered_reservation = WorkitemOps::create_item(
                scale_index, scale_root, "CRH", ItemType::Task, crash_title, "opencode",
                std::nullopt, "P2", {}, "general", "backlog", std::nullopt,
                std::nullopt, "", "", duplicate_admission(crash_title), "crash-key");
            expect(reservation.number == 1 && recovered_reservation.id == "CRH-TSK-0001" &&
                recovered_reservation.read_after_write &&
                CanonicalStore(scale_root).read(recovered_reservation.path).uid == recovered_reservation.uid,
                "pre-write crash must reuse its durable allocation and verify canonical identity");

            // Oversized frontmatter must stop admission before reserving a new ID.
            const auto oversized_path = scale_root / "items" / "SCL-TSK-9999_oversized.md";
            write_text(oversized_path,
                "---\nid: SCL-TSK-9999\nuid: " + CanonicalStore::generate_uuid_v7() +
                "\ntype: Task\ntitle: " + std::string(131072, 'y') + "\nstate: Proposed\n---\n");
            const auto existing_oversized_bytes = read_text(oversized_path);
            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    scale_index, scale_root, "BND", ItemType::Task, "Bounded request scan",
                    "opencode", std::nullopt, "P2", {}, "general", "backlog",
                    std::nullopt, std::nullopt, "", "", duplicate_admission("Bounded request scan"),
                    "bounded-scan-key");
            }, "request_scan_unreadable", "oversized metadata must fail closed before canonical mutation");
            expect(!scale_index.has_sequence("BND", "TSK"),
                "bounded scan failure must not advance allocation sequence");
            expect(CanonicalStore(scale_root).find_item_paths_by_id("BND-TSK-0001").empty(),
                "bounded scan failure must not write a canonical item");
            expect(read_text(oversized_path) == existing_oversized_bytes,
                "oversized existing metadata must remain untouched, not truncated or repaired implicitly");

        }
        std::filesystem::remove_all(root);
        std::cout << "create_request_scan_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        if (!root.empty()) {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        std::cerr << "create_request_scan_smoke_test: FAIL: " << ex.what() << '\n';
        return 1;
    }
}
