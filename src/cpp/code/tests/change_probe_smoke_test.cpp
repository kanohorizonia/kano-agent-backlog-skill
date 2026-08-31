#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../systems/kano_backlog_ops/index/private/change_probe.hpp"

namespace {

namespace probe = kano::backlog_ops::change_probe;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path()
        / ("kano-backlog-change-probe-" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("change probe fixture open failed");
    }
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.flush();
    if (!output.good()) {
        throw std::runtime_error("change probe fixture write failed");
    }
}

}

int main() {
    std::filesystem::path root;
    try {
        root = make_temp_root();
        const auto watched_path = root / "items" / "task" / "probe.md";
        write_text(watched_path, "probe-A\n");

#ifdef _WIN32
        const auto baseline = probe::capture_checkpoint(root);
        expect(baseline.status == probe::Status::Clean && baseline.checkpoint,
            "NTFS checkpoint capture must succeed without elevation");
        const auto watch = probe::capture_watch_set(
            root, {{"items/task/probe.md", watched_path, false}});
        expect(watch.status == probe::Status::Clean && !watch.entries.empty(),
            "canonical watch capture must persist file and directory identities");

        write_text(root / "_meta" / "unrelated.txt", "ignored\n");
        const auto unrelated = probe::verify_window(
            root, *baseline.checkpoint, watch.entries);
        expect(unrelated.status == probe::Status::Clean && unrelated.endpoint,
            "writes outside the canonical items tree must be ignored");

        auto malformed = *unrelated.endpoint;
        malformed.proof_kind = "invalid-proof";
        expect(probe::verify_window(root, malformed, watch.entries).status
                == probe::Status::Malformed,
            "malformed proof metadata must fail closed");

        auto changed_root = *unrelated.endpoint;
        changed_root.root.file_id ^= 1U;
        expect(probe::verify_window(root, changed_root, watch.entries).status
                == probe::Status::RootIdentityChanged,
            "root identity changes must fail closed");

        auto reset_journal = *unrelated.endpoint;
        reset_journal.journal_id ^= ~std::uint64_t{0};
        expect(probe::verify_window(root, reset_journal, watch.entries).status
                == probe::Status::JournalReset,
            "journal identity changes must fail closed");

        auto wrapped_journal = *unrelated.endpoint;
        wrapped_journal.usn = 0;
        expect(probe::verify_window(root, wrapped_journal, watch.entries).status
                == probe::Status::JournalWrapped,
            "a checkpoint older than the journal floor must fail closed");

        const auto before_raw = probe::capture_checkpoint(root);
        expect(before_raw.status == probe::Status::Clean && before_raw.checkpoint,
            "pre-edit checkpoint capture must succeed");
        write_text(watched_path, "probe-B\n");
        const auto relevant = probe::verify_window(
            root, *before_raw.checkpoint, watch.entries);
        expect(relevant.status == probe::Status::RelevantChange,
            "raw canonical writes must be classified as relevant");

        probe::VerifyBudget exhausted_budget;
        exhausted_budget.max_records = 0;
        exhausted_budget.max_bytes = 0;
        exhausted_budget.max_elapsed = std::chrono::milliseconds(0);
        const auto exhausted = probe::verify_window(
            root, *before_raw.checkpoint, watch.entries, {}, exhausted_budget);
        expect(exhausted.status == probe::Status::BudgetExhausted,
            "exhausted journal budgets must fail closed");

        probe::VerifyBudget expired_deadline;
        expired_deadline.deadline =
            std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
        const auto expired = probe::verify_window(
            root, *before_raw.checkpoint, watch.entries, {}, expired_deadline);
        expect(expired.status == probe::Status::BudgetExhausted &&
                   !expired.endpoint && expired.records_read == 0 &&
                   expired.bytes_read == 0,
            "an expired absolute deadline must fail before USN proof work");

        expect(probe::capture_checkpoint(root / "missing").status
                == probe::Status::Unavailable,
            "unavailable roots must not produce a proof");
#else
        const auto checkpoint = probe::capture_checkpoint(root);
        expect(checkpoint.status == probe::Status::Unsupported &&
                   !checkpoint.checkpoint,
            "non-Windows checkpoint capture must be unsupported without an identity");
        const auto watch = probe::capture_watch_set(
            root, {{"items/task/probe.md", watched_path, false}});
        expect(watch.status == probe::Status::Unsupported && watch.entries.empty(),
            "non-Windows watch capture must be unsupported without file identities");
        const probe::Checkpoint absent_proof{"none", {}, 0, 0};
        const auto verification = probe::verify_window(
            root, absent_proof, watch.entries);
        expect(verification.status == probe::Status::Unsupported &&
                   !verification.endpoint && verification.records_read == 0 &&
                   verification.bytes_read == 0 && verification.usn_span == 0,
            "non-Windows verification must be unsupported without proof work");
#endif

        std::filesystem::remove_all(root);
        std::cout << "change_probe_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "change_probe_smoke_test: FAIL: " << error.what() << '\n';
        if (!root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(root, cleanup_error);
        }
        return 1;
    }
}
