#include <exception>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
#include <kano_unattended.hpp>
#include <kano_process.h>
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/templates/template_ops.hpp"
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

class MutationTimingCapture : private std::stringbuf {
public:
    explicit MutationTimingCapture(std::function<void()> before_parent_recheck = {},
        std::string callback_span = "workitem.create_item.render_template")
        : previous_buffer_(std::cerr.rdbuf(this)),
          previous_timing_(kano::backlog_core::diagnostics::mutation_timing_forced()),
          before_parent_recheck_(std::move(before_parent_recheck)),
          callback_span_(std::move(callback_span)) {
        kano::backlog_core::diagnostics::mutation_timing_forced() = true;
    }

    ~MutationTimingCapture() {
        std::cerr.rdbuf(previous_buffer_);
        kano::backlog_core::diagnostics::mutation_timing_forced() = previous_timing_;
    }

    std::size_t full_reads(const std::string& filename) const {
        return reads("canonical_store.read", filename);
    }

    std::size_t metadata_reads(const std::string& filename) const {
        return reads("canonical_store.read_metadata_bounded", filename);
    }

    std::size_t reads(const std::string& span, const std::string& filename) const {
        std::size_t count = 0;
        std::istringstream lines(str());
        for (std::string line; std::getline(lines, line);) {
            if (line.find("\"span\":\"" + span + "\"") != std::string::npos &&
                line.find("\"detail\":\"" + filename + "\"") != std::string::npos) {
                ++count;
            }
        }
        return count;
    }

    void verify_callback() const {
        expect(callback_invoked_, "the deterministic pre-write parent mutation must run");
        if (callback_error_) std::rethrow_exception(callback_error_);
    }

private:
    int sync() override {
        if (before_parent_recheck_ && !callback_invoked_ &&
            str().find("\"span\":\"" + callback_span_ + "\"") != std::string::npos) {
            callback_invoked_ = true;
            try {
                before_parent_recheck_();
            } catch (...) {
                callback_error_ = std::current_exception();
            }
        }
        return 0;
    }

    std::streambuf* previous_buffer_;
    bool previous_timing_;
    std::function<void()> before_parent_recheck_;
    std::string callback_span_;
    bool callback_invoked_ = false;
    std::exception_ptr callback_error_;
};

constexpr int injected_crash_exit = 77;

kano::backlog_core::CreateItemResult create_crash_fixture(const std::filesystem::path& root) {
    kano::backlog_ops::BacklogIndex index(root / ".cache" / "index" / "backlog.db");
    return kano::backlog_ops::WorkitemOps::create_item(
        index, root, "CRS", kano::backlog_core::ItemType::Task, "Crash boundary", "codex",
        std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
        "", "", duplicate_admission("Crash boundary"), "boundary-key");
}

void run_crash_fixture(
    const std::filesystem::path& binary,
    const std::filesystem::path& root,
    const std::string& span
) {
    const auto executable = std::filesystem::absolute(binary).string();
    const auto path = root.string();
    const char* args[] = {
#ifdef _WIN32
        executable.c_str(),
#endif
        "--crash-fixture", path.c_str(), span.c_str()
    };
    KanoProcessOptions options{};
    options.executable = executable.c_str();
    options.argv = args;
    options.argv_count = sizeof(args) / sizeof(args[0]);
    options.mode = KANO_PROCESS_MODE_CAPTURE;
    options.timeout_ms = 10000;
    KanoProcessResult result{};
    expect(kano_process_run_ex(&options, &result), "crash fixture must launch as a native child");
    const bool exited_at_boundary = !result.timed_out && result.exit_code == injected_crash_exit;
    const std::string error = result.stderr_data ? result.stderr_data : "";
    kano_process_free_result(&result);
    expect(exited_at_boundary, "child must terminate at " + span + ": " + error);
}

void test_registered_sibling_projection(const std::filesystem::path& root) {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;
    const auto primary = root / "primary";
    const auto linked = root / "linked # 100%";
    const auto linked_git = primary / ".git" / "worktrees" / "side";
    const auto relative_product = std::filesystem::path("products") / "sibling-product";
    const auto active = primary / relative_product;
    const auto sibling = linked / relative_product;
    const auto db_relative = std::filesystem::path(".cache") / "index" / "backlog.db";
    const auto sibling_db = linked / db_relative;
    write_text(linked_git / "gitdir", "../../../../linked # 100%/.git\n");
    write_text(linked_git / "commondir", "../..\n");
    write_text(linked / ".git", "gitdir: ../primary/.git/worktrees/side\n");
    const auto create = [](BacklogIndex& index, const std::filesystem::path& product,
        const std::string& title, const std::string& key) {
        return WorkitemOps::create_item(index, product, "SIB", ItemType::Task, title, "codex",
            std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
            "", "", duplicate_admission(title), key);
    };

    CanonicalStore sibling_store(sibling);
    auto original = sibling_store.create("SIB", ItemType::Task, "Sibling seed", 280);
    sibling_store.write(original);
    BacklogIndex active_index(primary / db_relative, "sibling-product", active);
    {
        MutationTimingCapture timing;
        const auto item = create(active_index, active, "First local item", "primary-first");
        expect(item.id == "SIB-TSK-0281" &&
            timing.metadata_reads(original.file_path->filename().string()) == 1 &&
            !std::filesystem::exists(sibling_db.parent_path()),
            "missing sibling index must scan canonical data without creating a cache");
    }

    BacklogIndex sibling_writer(sibling_db, "sibling-product", sibling);
    const auto committed = create(sibling_writer, sibling, "Sibling request", "sibling-request");
    auto higher = sibling_store.create("SIB", ItemType::Task, "Higher sibling allocation", 375);
    sibling_store.write(higher);
    sibling_writer.rebuild_metadata(sibling, "sibling-product");
    const auto cached_bytes = read_text(sibling_db);
    const auto probe = active_index.open_sibling_mutation_index(sibling);
#ifdef _WIN32
    expect(probe != nullptr, "Windows must open the existing fixed sibling projection read-only");
#endif
    const auto candidates = probe
        ? probe->verified_mutation_paths(sibling, "sibling-request", true) : std::nullopt;
    expect(candidates == sibling_writer.verified_mutation_paths(sibling, "sibling-request", true),
        "read-only sibling lookup must accept a witness verified by the existing index reader");
    const bool verified = candidates.has_value();
    if (verified) {
        expect(*candidates == std::vector<std::filesystem::path>{committed.path},
            "sibling proof must resolve the matching request in that checkout");
        expect(probe->verified_allocation_maximum(sibling, "SIB", "TSK") == 375,
            "sibling proof must resolve the sibling allocation high-water mark");
        expect(probe->verified_mutation_paths(sibling, higher.id) ==
            std::vector<std::filesystem::path>{*higher.file_path},
            "sibling proof must find occupied sibling filenames");
        expect(!probe->verified_mutation_paths(active, "sibling-request", true) &&
            !probe->verified_allocation_maximum(active, "SIB", "TSK"),
            "sibling proof must refuse any other checkout root");
    }
    {
        MutationTimingCapture timing;
        const auto replay = create(active_index, active, "Sibling request", "sibling-request");
        expect(replay.idempotent_replay && replay.id == committed.id &&
            replay.uid == committed.uid && replay.path == committed.path,
            "sibling replay must preserve its canonical ID, UID and checkout");
        expect(timing.metadata_reads(committed.path.filename().string()) == 1 &&
            timing.metadata_reads(higher.file_path->filename().string()) == (verified ? 0 : 1),
            "a verified sibling lookup must read the matched canonical marker and skip unrelated metadata");
    }
    expect_throws_contains([&] {
        (void)create(active_index, active, "Different sibling request", "sibling-request");
    }, "item_create.idempotency_conflict", "sibling lookup must reject changed payload");
    {
        MutationTimingCapture timing;
        const auto item = create(active_index, active, "Next local request", "primary-next");
        expect(item.id == "SIB-TSK-0376" &&
            timing.metadata_reads(higher.file_path->filename().string()) == (verified ? 0 : 1),
            "normal create must include sibling allocation and skip unrelated verified metadata");
    }
    auto conflict = sibling_store.create("SIB", ItemType::Task, "Legacy marker", 450);
    conflict.external["create_request_key"] = "sibling-request";
    conflict.external["create_request_payload"] = "different request";
    const auto revision = sibling_store.read_write_revision().current;
    write_text(*conflict.file_path,
        kano::backlog_ops::TemplateOps::render_item_body(conflict, "codex", "Raw canonical fixture"));
    expect(sibling_store.read_write_revision().current == revision,
        "external keyed fixture must bypass the native write receipt");
    {
        MutationTimingCapture timing;
        expect_throws_contains([&] {
            (void)create(active_index, active, "Sibling request", "sibling-request");
        }, "item_create.idempotency_conflict", "a stale sibling must reveal the new canonical conflict");
        expect(timing.metadata_reads(conflict.file_path->filename().string()) == 1,
            "a stale sibling proof must fall back to canonical metadata");
    }
    expect(read_text(sibling_db) == cached_bytes,
        "sibling projection queries must not modify its SQLite cache");
    std::cout << "sibling mutation projection: " << (verified ? "verified NTFS" : "canonical fallback") << "\n";
}

void test_indexed_uid_parent_chain(const std::filesystem::path& root) {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;

    CanonicalStore store(root);
    auto unrelated = store.create("ANC", ItemType::Epic, "Unrelated epic", 1);
    store.write(unrelated);
    auto ancestor = store.create("ANC", ItemType::Epic, "UID ancestor", 2);
    store.write(ancestor);
    auto parent = store.create("ANC", ItemType::Feature, "Direct parent", 1, ancestor.uid);
    store.write(parent);
    BacklogIndex index(root / ".cache" / "index" / "backlog.db", "configured-product", root);
    const auto create = [&](const std::string& title, const std::string& key) {
        return WorkitemOps::create_item(index, root, "ANC", ItemType::Task, title, "codex",
            parent.id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
            "", "", duplicate_admission(title), key);
    };
    {
        MutationTimingCapture timing;
        const auto child = create("Cold child with UID ancestor", "cold-uid-ancestor");
        expect(child.read_after_write && timing.full_reads(unrelated.file_path->filename().string()) == 0,
            "keyed create must reuse the bounded ancestor metadata inventory when its index is cold");
    }

    index.rebuild_metadata(root, "configured-product");
    const auto projected = index.verified_mutation_paths(root, ancestor.uid);
    const bool verified = projected == std::vector<std::filesystem::path>{*ancestor.file_path};
    {
        MutationTimingCapture timing;
        const auto child = create("Child with UID ancestor", "uid-ancestor");
        expect(child.read_after_write && store.read(child.path).parent == parent.id,
            "create must preserve the direct parent ID after resolving its UID ancestor");
        expect(timing.full_reads(unrelated.file_path->filename().string()) == 0,
            "UID ancestor validation must skip unrelated canonical bodies");
        if (verified) {
            expect(timing.reads("canonical_store.list_items", "all") == 0,
                "verified UID ancestor validation must skip canonical inventory");
        }
    }

    ancestor.parent = parent.uid;
    store.write(ancestor);
    index.index_item(ancestor);
    const auto count = store.list_items().size();
    expect_throws_contains([&] {
        (void)create("Child of cyclic UID chain", "uid-ancestor-cycle");
    }, "hierarchy.parent_cycle", "create must reject a cycle reached through canonical UID edges");
    expect(store.list_items().size() == count,
        "a UID ancestor cycle must fail before writing a child");
    std::cout << "UID parent-chain mutation: "
              << (verified ? "verified NTFS" : "canonical fallback") << "\n";
}

void test_linked_create_registry_scope(const std::filesystem::path& root) {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;
    const auto primary = root / "primary";
    const auto linked = root / "linked";
    const auto common = primary / ".git";
    const auto registry = common / "worktrees";
    const auto primary_product = primary / "products" / "fixture";
    const auto linked_product = linked / "products" / "fixture";
    const auto ledger_path = common / "kano" / "backlog-id-reservations.db";
    const auto link = [&](const std::string& name) {
        write_text(registry / name / "gitdir", "../../../../" + name + "/.git\n");
        write_text(registry / name / "commondir", "../..\n");
        write_text(root / name / ".git", "gitdir: ../primary/.git/worktrees/" + name + "\n");
    };
    link("linked");
    link("without-product");
    write_text(registry / "prunable" / "gitdir", "../../../../missing-parent/absent/checkout/.git\n");
    BacklogIndex index(linked / ".cache" / "index" / "backlog.db", "fixture", linked_product);
    const auto create = [&](const std::string& title, const std::optional<std::string>& key) {
        return WorkitemOps::create_item(index, linked_product, "REG", ItemType::Task, title, "codex",
            std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
            "", "", duplicate_admission(title), key);
    };

    CanonicalStore primary_store(primary_product);
    CanonicalStore linked_store(linked_product);
    auto legacy = primary_store.create("REG", ItemType::Task, "Primary legacy request", 700);
    legacy.external["create_request_key"] = "primary-legacy";
    legacy.external["create_request_payload"] = "historical fingerprint";
    primary_store.write(legacy);
    expect_throws_contains([&] {
        (void)create("Primary legacy request", "primary-legacy");
    }, "item_create.idempotency_conflict", "linked admission must find a primary-only legacy marker");
    expect(linked_store.list_items().empty(), "primary marker conflict must preserve the linked checkout");
    expect(create("Allocate past primary", "primary-maximum").id == "REG-TSK-0701",
        "linked allocation must advance past the primary checkout's highest filename");

    const auto payload = [](const std::string& title) {
        std::string fingerprint;
        for (const auto& field : {"REG", "TSK", title.c_str(), "codex", "", "P2", "general",
                 "backlog", "", "", "", "", title.c_str(), "test-product", "create", "", "false", "0", "0", "0"}) {
            const std::string value(field);
            fingerprint += std::to_string(value.size()) + ":" + value;
        }
        return fingerprint;
    };
    const std::string scope = "products/fixture";
    const std::string key_prefix = std::to_string(scope.size()) + ":" + scope;
    BacklogIndex ledger(ledger_path);
    const std::string collision_title = "Reserved primary collision";
    const auto reservation = ledger.reserve_next_number_for_request(
        "REG", "TSK", "codex", key_prefix + "primary-collision", payload(collision_title));
    auto collision = primary_store.create("REG", ItemType::Task, "Primary filename", reservation.number);
    primary_store.write(collision);
    expect_throws_contains([&] {
        (void)create(collision_title, "primary-collision");
    }, "duplicate_item_id.allocation_collision", "linked retry must detect its pending ID in the primary checkout");
    expect(linked_store.list_items().size() == 1 &&
        !std::filesystem::exists(primary / ".cache" / "index"),
        "linked admission must neither write the colliding item nor create a primary cache");

#ifdef _WIN32
    const std::string foreign = "/unmounted-container/checkout/.git";
#else
    const std::string foreign = "C:/unmounted-host/checkout/.git";
#endif
    write_text(registry / "foreign" / "gitdir", foreign + "\n");
    const std::string blocked_title = "Unverified registration";
    expect_throws_contains([&] {
        (void)create(blocked_title, "unverified-foreign");
    }, "item_create.worktree_registry_incomplete", "a foreign checkout must fail closed");
    expect_throws_contains([&] {
        (void)create(blocked_title, std::nullopt);
    }, "item_create.worktree_registry_incomplete", "unkeyed allocation must also verify the collision scope");
    expect(!ledger.find_request_reservation(
            "REG", "TSK", key_prefix + "unverified-foreign", payload(blocked_title)) &&
        linked_store.list_items().size() == 1,
        "unverified registry scope must fail before durable reservation or canonical mutation");
    write_text(registry / "foreign" / "gitdir", "../../../../without-product/.git\n");
    expect_throws_contains([&] {
        (void)create(blocked_title, "unverified-reciprocal");
    }, "item_create.worktree_registry_incomplete", "a visible backlink must identify its own registry entry");
    std::filesystem::remove(registry / "foreign" / "gitdir");
    std::filesystem::remove(registry / "foreign");
    write_text(registry / "prunable" / "locked", "temporarily offline\n");
    expect_throws_contains([&] {
        (void)create(blocked_title, "unverified-locked");
    }, "item_create.worktree_registry_incomplete", "a locked missing checkout cannot be treated as prunable");
}

} // namespace

int main(int argc, char** argv) {
    kano::infra::ConfigureUnattendedExecution();
    using kano::backlog_core::ItemType;
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::WorkitemOps;
    if (argc == 4 && std::string_view(argv[1]) == "--crash-fixture") {
        MutationTimingCapture timing([] { std::_Exit(injected_crash_exit); }, argv[3]);
        (void)create_crash_fixture(argv[2]);
        return 1;
    }
    std::filesystem::path root;
    try {
        root = make_temp_root();
        if (argc == 2 && std::string_view(argv[1]) == "--sibling-projection") {
            test_registered_sibling_projection(root);
            test_linked_create_registry_scope(root / "registry");
            std::filesystem::remove_all(root);
            return 0;
        }
        test_indexed_uid_parent_chain(root / "uid-parent-chain");
        {
            const std::string spans[] = {
                "workitem.create_item.request_scan",
                "workitem.create_item.render_template",
                "workitem.create_item.write_file",
                "workitem.create_item.duplicate_admission_receipt",
            };
            for (std::size_t stage = 0; stage < 4; ++stage) {
                const auto product = root / ("crash-stage-" + std::to_string(stage));
                std::filesystem::create_directories(product / ".git");
                run_crash_fixture(argv[0], product, spans[stage]);
                const CanonicalStore store(product);
                const auto before = store.list_items();
                const auto receipt = product / "_meta" / "duplicate-admission" / "CRS-TSK-0001.json";
                const bool canonical_already_written = stage >= 2;
                expect(before.size() == (canonical_already_written ? 1 : 0) &&
                    std::filesystem::exists(receipt) == (stage == 3),
                    "crash fixture must preserve the expected canonical and receipt boundary");
                const std::string previous_uid =
                    canonical_already_written ? store.read(before.front()).uid : "";
                const auto retried = create_crash_fixture(product);
                expect(retried.id == "CRS-TSK-0001" && retried.read_after_write &&
                    retried.idempotent_replay == canonical_already_written &&
                    (!canonical_already_written || retried.uid == previous_uid) &&
                    store.list_items().size() == 1 && std::filesystem::exists(receipt),
                    "same-key retry must converge to one item and receipt after abrupt termination");
            }
        }
        {
            // Keyed parent admission shares one bounded scan, still checking
            // both input forms, unrelated identities and the final live parent.
            const std::string parent_id = "PAR-FTR-0001";
            const std::string parent_uid = CanonicalStore::generate_uuid_v7();
            const auto parent_bytes = [&](const std::string& uid) {
                return "---\nid: " + parent_id + "\nuid: " + uid +
                    "\ntype: Feature\ntitle: Parent identity fixture\nstate: Proposed\n"
                    "created: 2026-10-01\nupdated: 2026-10-01\nexternal: {}\n---\n";
            };
            for (const bool use_key : {false, true}) {
              for (const bool use_uid : {false, true}) {
                const auto product = root / ((use_uid ? std::string("parent-uid") : "parent-id") +
                    (use_key ? "-key" : "-no-key"));
                const auto parent_path = product / "items" / (parent_id + "_parent.md");
                write_text(parent_path, parent_bytes(parent_uid));
                const auto unrelated_path = product / "items" / "unrelated.md";
                write_text(unrelated_path,
                    "---\nid: NBR-ISS-0001\nuid: " + CanonicalStore::generate_uuid_v7() +
                    "\ntype: Issue\ntitle: Unrelated large body\nstate: Proposed\n"
                    "created: 2026-10-01\nupdated: 2026-10-01\nexternal: {}\n---\n" +
                    std::string(1024 * 1024, 'x'));
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                MutationTimingCapture timing;
                const auto child = WorkitemOps::create_item(
                    index, product, "CHD", ItemType::Task, "Parent identity child", "opencode",
                    use_uid ? parent_uid : parent_id, "P2", {}, "general", "backlog",
                    std::nullopt, std::nullopt, "", "", duplicate_admission("Parent identity child"),
                    use_key ? std::optional<std::string>{"parent-key"} : std::nullopt);
                expect(timing.full_reads(parent_path.filename().string()) == (use_key ? 2 : (use_uid ? 3 : 2)),
                    "parent admission must retain its matched reads and final live canonical read");
                expect(timing.full_reads(unrelated_path.filename().string()) == 0,
                    "neither initial admission nor final parent verification may parse unrelated item bodies");
                expect(timing.metadata_reads(parent_path.filename().string()) == (use_key ? 1 : 0) &&
                    timing.metadata_reads(unrelated_path.filename().string()) == (use_key ? 1 : 0),
                    "keyed parent and first request verification must share exactly one bounded scan");
                expect(CanonicalStore(product).read(child.path).parent == parent_id && child.read_after_write,
                    "both parent input forms must persist the canonical parent ID and verify the child UID");
                if (use_key) {
                    const auto child_bytes = read_text(child.path);
                    const auto receipt = product / "_meta" / "duplicate-admission" / (child.id + ".json");
                    const auto receipt_bytes = read_text(receipt);
                    const auto replay = WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Parent identity child", "opencode",
                        use_uid ? parent_id : parent_uid, "P2", {}, "general", "backlog",
                        std::nullopt, std::nullopt, "", "", duplicate_admission("Parent identity child"), "parent-key");
                    expect(replay.id == child.id && replay.uid == child.uid && replay.idempotent_replay &&
                        CanonicalStore(product).list_items().size() == 3 &&
                        read_text(child.path) == child_bytes && read_text(receipt) == receipt_bytes,
                        "a keyed parent replay must normalize ID/UID aliases and retain the original child");
                    std::filesystem::remove(receipt);
                    const auto recovered = WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Parent identity child", "opencode",
                        parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                        "", "", duplicate_admission("Parent identity child"), "parent-key");
                    expect(recovered.idempotent_replay && recovered.id == child.id && recovered.uid == child.uid &&
                        read_text(child.path) == child_bytes && std::filesystem::exists(receipt) &&
                        CanonicalStore(product).find_item_paths_by_id("CHD-TSK-0002").empty(),
                        "keyed parent replay must repair only its missing receipt without a replacement child");
                }
              }
            }
            for (const bool use_key : {false, true}) {
              for (const bool use_uid : {false, true}) {
                const auto product = root / ((use_uid ? std::string("ambiguous-parent-uid") : "ambiguous-parent-id") +
                    (use_key ? "-key" : "-no-key"));
                write_text(product / "items" / (parent_id + "_parent.md"), parent_bytes(parent_uid));
                write_text(product / "items" / (parent_id + "_duplicate.md"),
                    parent_bytes(CanonicalStore::generate_uuid_v7()));
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                expect_throws_contains([&] {
                    (void)WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Ambiguous parent child", "opencode",
                        use_uid ? parent_uid : parent_id, "P2", {}, "general", "backlog",
                        std::nullopt, std::nullopt, "", "", duplicate_admission("Ambiguous parent child"),
                        use_key ? std::optional<std::string>{"ambiguous-parent-key"} : std::nullopt);
                }, "duplicate_item_id.ambiguous_ref", "both parent input forms must reject duplicate display IDs");
                expect(!index.has_sequence("CHD", "TSK") && CanonicalStore(product).list_items().size() == 2,
                    "parent ambiguity must fail before child allocation or canonical mutation");
              }
            }
            for (const bool use_key : {false, true}) {
              for (const bool change_path : {false, true}) {
                const auto product = root / ((change_path ? std::string("parent-path-changed") : "parent-uid-changed") +
                    (use_key ? "-key" : "-no-key"));
                const auto parent_path = product / "items" / (parent_id + "_parent.md");
                write_text(parent_path, parent_bytes(parent_uid));
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                MutationTimingCapture timing([&] {
                    if (change_path) {
                        std::filesystem::rename(parent_path, product / "items" / (parent_id + "_renamed.md"));
                    } else {
                        write_text(parent_path, parent_bytes(CanonicalStore::generate_uuid_v7()));
                    }
                });
                expect_throws_contains([&] {
                    (void)WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Changed parent child", "opencode",
                        parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                        "", "", duplicate_admission("Changed parent child"),
                        use_key ? std::optional<std::string>{"changed-parent-key"} : std::nullopt);
                }, "hierarchy.parent_identity_changed", "the final fresh scan must reject changed parent UID/path");
                timing.verify_callback();
                expect(CanonicalStore(product).find_item_paths_by_id("CHD-TSK-0001").empty(),
                    "a changed parent must stop creation before the child canonical write");
              }
            }
            {
                const auto product = root / "parent-changed-after-metadata";
                const auto parent_path = product / "items" / (parent_id + "_parent.md");
                write_text(parent_path, parent_bytes(parent_uid));
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                MutationTimingCapture timing([&] {
                    write_text(parent_path, parent_bytes(CanonicalStore::generate_uuid_v7()));
                }, "workitem.create_item.parent_request_scan");
                expect_throws_contains([&] {
                    (void)WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Changed metadata parent", "opencode",
                        parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                        "", "", duplicate_admission("Changed metadata parent"), "changed-metadata-parent-key");
                }, "hierarchy.parent_identity_changed", "the matched parent read must reject a stale metadata UID");
                timing.verify_callback();
                expect(!index.has_sequence("CHD", "TSK") && CanonicalStore(product).list_items().size() == 1,
                    "metadata identity changes must fail before allocation");
            }
            {
                // An arbitrary filename with a colliding UID must participate
                // in ID admission just like a duplicate display ID.
                const auto product = root / "parent-id-uid-collision";
                write_text(product / "items" / "parent.md", parent_bytes(parent_uid));
                auto collision = parent_bytes(parent_id);
                collision.replace(collision.find("id: " + parent_id), 4 + parent_id.size(), "id: OTHER-FTR-0001");
                write_text(product / "items" / "unexpected-name.md", collision);
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                expect_throws_contains([&] {
                    (void)WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Colliding parent identity", "opencode",
                        parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                        "", "", duplicate_admission("Colliding parent identity"), "parent-collision-key");
                }, "duplicate_item_id.ambiguous_ref", "the inventory must check UID collisions without filename filtering");
                expect(!index.has_sequence("CHD", "TSK") && CanonicalStore(product).list_items().size() == 2,
                    "a parent ID/UID collision must fail before allocation");
            }
            {
                const auto original = root / "parent-root-original";
                const auto replacement = root / "parent-root-replacement";
                const auto bound = root / "parent-root-bound";
                write_text(original / "items" / "parent.md", parent_bytes(parent_uid));
                write_text(replacement / "items" / "parent.md", parent_bytes(parent_uid));
                std::error_code link_error;
                std::filesystem::create_directory_symlink(original, bound, link_error);
                if (!link_error) {
                    BacklogIndex index(original / ".cache" / "index" / "backlog.db");
                    index.initialize();
                    MutationTimingCapture timing([&] {
                        std::filesystem::remove(bound);
                        std::filesystem::create_directory_symlink(replacement, bound);
                    }, "workitem.create_item.parent_request_scan");
                    expect_throws_contains([&] {
                        (void)WorkitemOps::create_item(
                            index, bound, "CHD", ItemType::Task, "Rebound parent root", "opencode",
                            parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                            "", "", duplicate_admission("Rebound parent root"), "parent-root-key");
                    }, "item_create.request_root_changed", "a root rebind must invalidate the local metadata inventory");
                    timing.verify_callback();
                    expect(!index.has_sequence("CHD", "TSK") &&
                        CanonicalStore(original).list_items().size() == 1 &&
                        CanonicalStore(replacement).list_items().size() == 1,
                        "a rebound root must not allocate or write in either target");
                } else {
                    std::cout << "parent inventory symlink rebind: SKIP (platform denied symlink creation)\n";
                }
            }
            for (const bool before_final_check : {false, true}) {
                const auto repository = root / (before_final_check ? "root-replaced-before-write" : "root-replaced-after-scan");
                const auto product = repository / "product";
                const auto replacement = repository / "replacement";
                const auto retired = repository / "retired";
                // Keep the native request lock and open index outside the root
                // being replaced, including on Windows.
                std::filesystem::create_directories(repository / ".git");
                write_text(product / "items" / "parent.md", parent_bytes(parent_uid));
                write_text(replacement / "items" / "parent.md", parent_bytes(parent_uid));
                write_text(replacement / "items" / "marker.md",
                    "---\nid: OTH-TSK-0001\nuid: " + CanonicalStore::generate_uuid_v7() +
                    "\ntype: Task\ntitle: Conflicting replacement marker\nstate: Proposed\n"
                    "created: 2026-10-01\nupdated: 2026-10-01\nexternal:\n"
                    "  create_request_key: replaced-root-key\n  create_request_payload: different-request\n---\n");
                BacklogIndex index(repository / ".cache" / "index" / "backlog.db");
                index.initialize();
                MutationTimingCapture timing([&] {
                    std::filesystem::rename(product, retired);
                    std::filesystem::rename(replacement, product);
                }, before_final_check ? "workitem.create_item.render_template" : "workitem.create_item.parent_request_scan");
                expect_throws_contains([&] {
                    (void)WorkitemOps::create_item(
                        index, product, "CHD", ItemType::Task, "Replaced parent root", "opencode",
                        parent_id, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                        "", "", duplicate_admission("Replaced parent root"), "replaced-root-key");
                }, "item_create.request_root_changed", "same-path directory replacement must invalidate admission");
                timing.verify_callback();
                expect(CanonicalStore(retired).list_items().size() == 1 &&
                    CanonicalStore(product).list_items().size() == 2 &&
                    CanonicalStore(product).find_item_paths_by_id("CHD-TSK-0001").empty(),
                    "same parent identity must not hide root replacement or permit a child write");
                if (!before_final_check) {
                    expect(!index.has_sequence("CHD", "TSK"), "early root replacement must fail before allocation");
                }
            }
            {
                const auto product = root / "parent-reservation-fresh-recovery";
                write_text(product / "items" / "parent.md", parent_bytes(parent_uid));
                BacklogIndex index(product / ".cache" / "index" / "backlog.db");
                index.initialize();
                const std::string title = "Fresh parent recovery";
                std::string payload;
                for (const auto& field : {"RCP", "TSK", title.c_str(), "opencode", parent_id.c_str(), "P2",
                         "general", "backlog", "", "", "", "", title.c_str(),
                         "test-product", "create", "", "false", "0", "0", "0"}) {
                    const std::string value = field;
                    payload += std::to_string(value.size()) + ":" + value;
                }
                const auto reservation = index.reserve_next_number_for_request(
                    "RCP", "TSK", "opencode", "fresh-parent-key", payload);
                const auto reserved_uid = index.bind_request_uid(
                    "RCP", "TSK", "fresh-parent-key", reservation.number, CanonicalStore::generate_uuid_v7());
                std::filesystem::path recovered_path;
                std::string recovered_bytes;
                MutationTimingCapture timing([&] {
                    CanonicalStore store(product);
                    auto existing = store.create("RCP", ItemType::Task, title, reservation.number, parent_id);
                    existing.uid = reserved_uid;
                    existing.external["create_request_key"] = "fresh-parent-key";
                    existing.external["create_request_payload"] = payload;
                    store.write(existing);
                    recovered_path = *existing.file_path;
                    recovered_bytes = read_text(recovered_path);
                }, "workitem.create_item.next_number");
                const auto recovered = WorkitemOps::create_item(
                    index, product, "RCP", ItemType::Task, title, "opencode", parent_uid,
                    "P2", {}, "general", "backlog", std::nullopt, std::nullopt, "", "",
                    duplicate_admission(title), "fresh-parent-key");
                timing.verify_callback();
                expect(recovered.idempotent_replay && recovered.id == "RCP-TSK-0001" &&
                    recovered.uid == reserved_uid && recovered.path == recovered_path &&
                    read_text(recovered_path) == recovered_bytes &&
                    std::filesystem::exists(product / "_meta" / "duplicate-admission" / (recovered.id + ".json")) &&
                    CanonicalStore(product).list_items().size() == 2 &&
                    CanonicalStore(product).find_item_paths_by_id("RCP-TSK-0002").empty(),
                    "reserved recovery must scan fresh after the cached pass and replay only the exact bound UID");
                expect(timing.metadata_reads("parent.md") == 2,
                    "reserved recovery must not reuse the first parent/request inventory");
                const auto finalized = index.find_request_reservation(
                    "RCP", "TSK", "fresh-parent-key", payload);
                expect(finalized && finalized->committed_request && finalized->uid == reserved_uid,
                    "recovering a canonical write must finalize the original reservation");
            }
        }
        {
            const auto product = root / "atomic-create-collision";
            CanonicalStore store(product);
            BacklogIndex index(product / ".cache" / "index" / "backlog.db");
            auto unrelated = store.create("ATM", ItemType::Task, "Atomic create collision", 1);
            const auto path = *unrelated.file_path;
            std::string inserted_bytes;
            MutationTimingCapture timing([&] {
                store.write(unrelated);
                inserted_bytes = read_text(path);
            });
            expect_throws_contains([&] {
                (void)WorkitemOps::create_item(
                    index, product, "ATM", ItemType::Task, "Atomic create collision", "opencode",
                    std::nullopt, "P2", {}, "general", "backlog", std::nullopt, std::nullopt,
                    "", "", duplicate_admission("Atomic create collision"), "atomic-collision-key");
            }, "canonical_create_no_replace_failed", "existing destination must survive the final write race");
            timing.verify_callback();
            expect(read_text(path) == inserted_bytes && store.read(path).uid == unrelated.uid &&
                !std::filesystem::exists(product / "_meta" / "duplicate-admission" / (unrelated.id + ".json")),
                "atomic no-replace failure must preserve the existing item and produce no create receipt");
        }
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
