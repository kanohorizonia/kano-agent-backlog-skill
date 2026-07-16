#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/migration/migration_ops.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_root() {
    std::random_device source;
    std::mt19937 generator(source());
    std::uniform_int_distribution<unsigned int> distribution(0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << distribution(generator);
    const auto root = std::filesystem::temp_directory_path() / "kano-backlog-migration-smoke" / suffix.str();
    std::filesystem::create_directories(root / ".kano");
    return root;
}

void write_text(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << content;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string replace_json_unsigned(std::string content, const std::string& key, unsigned long long value) {
    const auto key_offset = content.find("\"" + key + "\"");
    if (key_offset == std::string::npos) {
        throw std::runtime_error("missing JSON key " + key);
    }
    const auto colon = content.find(':', key_offset);
    const auto begin = content.find_first_of("0123456789", colon);
    if (colon == std::string::npos || begin == std::string::npos) {
        throw std::runtime_error("missing JSON unsigned value " + key);
    }
    auto end = begin;
    while (end < content.size() && std::isdigit(static_cast<unsigned char>(content[end]))) {
        ++end;
    }
    content.replace(begin, end - begin, std::to_string(value));
    return content;
}

kano::backlog_core::BacklogItem create_ready_item(
    const std::filesystem::path& product_root,
    const std::string& prefix,
    kano::backlog_core::ItemType type,
    int number,
    const std::string& title,
    const std::optional<std::string>& parent = std::nullopt
) {
    kano::backlog_core::CanonicalStore store(product_root);
    auto item = store.create(prefix, type, title, number, parent);
    item.priority = "P2";
    item.area = "fixture";
    item.iteration = "backlog";
    item.context = "Deterministic cross-product migration fixture.";
    item.goal = "Exercise immutable migration planning.";
    item.approach = "Use disposable canonical items.";
    item.acceptance_criteria = "Plan preserves UID and closes the subtree.";
    item.risks = "Disposable fixture only.";
    store.write(item);
    return item;
}

bool contains_prefix(const std::vector<std::string>& values, const std::string& prefix) {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) {
        return value.starts_with(prefix);
    });
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::MigrationOps;
    using kano::backlog_ops::MigrationRequest;

    std::filesystem::path root;
    try {
        root = make_temp_root();
        const std::string config_text =
            "[products.kto-source]\n"
            "name = \"KTO source\"\n"
            "prefix = \"KTO\"\n"
            "backlog_root = \"_kano/backlog/products/kto-source\"\n\n"
            "[products.parametric-target]\n"
            "name = \"Parametric target\"\n"
            "prefix = \"KPG\"\n"
            "backlog_root = \"_kano/backlog/products/parametric-target\"\n\n"
            "[products.observer]\n"
            "name = \"Observer\"\n"
            "prefix = \"OBS\"\n"
            "backlog_root = \"_kano/backlog/products/observer\"\n";
        write_text(root / ".kano" / "backlog_config.toml", config_text);

        const auto backlog_root = root / "_kano" / "backlog";
        const auto source_root = backlog_root / "products" / "kto-source";
        const auto target_root = backlog_root / "products" / "parametric-target";
        const auto observer_root = backlog_root / "products" / "observer";
        std::filesystem::create_directories(target_root / "items");

        const auto source_initiative = create_ready_item(source_root, "KTO", ItemType::Initiative, 1, "Parametric geometry subtree");
        for (int number = 1; number <= 44; ++number) {
            create_ready_item(
                source_root,
                "KTO",
                ItemType::Task,
                number,
                "Parametric fixture task " + std::to_string(number),
                source_initiative.id);
        }
        auto observer = create_ready_item(observer_root, "OBS", ItemType::Task, 1, "External reference observer");
        observer.links.relates.push_back(source_initiative.id);
        observer.context = "Keep the unrelated KTO-TSK-00010 boundary sentinel byte-identical.";
        CanonicalStore(observer_root).write(observer);
        const auto source_artifact = source_root / "artifacts" / source_initiative.id / "views" / "fixture.json";
        write_text(source_artifact, "{\"owner\":\"" + source_initiative.id + "\"}\n");

        MigrationOps::PlanOptions options;
        options.start_path = root;
        options.backlog_root = backlog_root;
        options.request = MigrationRequest{
            .source_product = "kto-source",
            .source_ref = source_initiative.uid,
            .target_product = "parametric-target",
            .scope = "subtree",
            .include_owned_artifacts = true,
            .max_items = 45,
            .max_artifacts = 10,
            .max_source_inventory_items = 100,
            .max_target_inventory_items = 100,
            .max_artifact_bytes = 1024u * 1024u,
            .max_materialization_bytes = 8u * 1024u * 1024u,
            .max_reference_files = 100,
            .max_reference_bytes = 2u * 1024u * 1024u,
            .max_references = 100,
        };

        const auto source_before = read_text(*source_initiative.file_path);
        const auto observer_before = read_text(*observer.file_path);
        const auto artifact_before = read_text(source_artifact);
        const auto first = MigrationOps::plan(options);
        const auto second = MigrationOps::plan(options);
        if (!first.ready()) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(first.ready(), "45-item fixture should produce a ready plan");
        expect(first.items.size() == 45, "planner should close the full 45-item subtree");
        expect(first.items.front().uid == source_initiative.uid, "planner should preserve stable UID in mapping");
        expect(first.artifacts.size() == 1, "planner should include the owned artifact");
        expect(std::any_of(first.references.begin(), first.references.end(), [](const auto& rewrite) {
            return rewrite.external_to_subtree;
        }), "planner should classify external references");
        expect(first.plan_hash.size() == 64 && first.plan_hash == second.plan_hash,
            "identical inputs should produce one deterministic SHA-256 plan hash");
        expect(first.request.expected_source_revision == first.source_revision &&
               first.request.expected_target_prefix == first.target_prefix,
            "ready plans should normalize resolved source revision and target prefix guards into the hashed request");
        expect(first.to_json().find("kob.cross_product_migration.plan.v1") != std::string::npos,
            "plan receipt should expose the versioned schema");
        expect(first.to_json().find("\"max_reference_files\":100") != std::string::npos &&
               first.to_json().find("\"max_reference_bytes\":2097152") != std::string::npos &&
               first.to_json().find("\"max_references\":100") != std::string::npos &&
               first.to_json().find("\"max_source_inventory_items\":100") != std::string::npos &&
               first.to_json().find("\"max_target_inventory_items\":100") != std::string::npos &&
               first.to_json().find("\"max_artifact_bytes\":1048576") != std::string::npos &&
               first.to_json().find("\"max_materialization_bytes\":8388608") != std::string::npos,
            "plan receipt should persist deterministic inventory, artifact, materialization, and reference bounds");
        expect(read_text(*source_initiative.file_path) == source_before,
            "dry-run planning must not rewrite source items");
        expect(CanonicalStore(target_root).list_items().empty(),
            "dry-run planning must not create target items or mutate target sequences");

        auto file_bounded_options = options;
        file_bounded_options.request.max_reference_files = 1;
        const auto file_bounded = MigrationOps::plan(file_bounded_options);
        expect(!file_bounded.ready() && contains_prefix(file_bounded.blockers, "reference_enumeration_limit_exceeded"),
            "planner should fail closed when reference file enumeration exceeds its request bound");

        const auto entry_bound_root = observer_root / "entry-bound";
        for (int index = 0; index < 75; ++index) {
            std::filesystem::create_directories(entry_bound_root / std::to_string(index));
        }
        auto entry_bounded_options = options;
        entry_bounded_options.request.max_reference_files = 10;
        const auto entry_bounded = MigrationOps::plan(entry_bounded_options);
        const auto entry_bounded_repeat = MigrationOps::plan(entry_bounded_options);
        expect(!entry_bounded.ready() && contains_prefix(entry_bounded.blockers, "reference_enumeration_limit_exceeded"),
            "planner should bound directory entries even when empty directories do not consume the file limit");
        expect(entry_bounded.to_json() == entry_bounded_repeat.to_json(),
            "combined file and derived entry overflow should produce one deterministic blocked receipt");
        std::filesystem::remove_all(entry_bound_root);

        std::uintmax_t source_inventory_bytes = 0;
        for (const auto& path : CanonicalStore(source_root).list_items()) {
            source_inventory_bytes += std::filesystem::file_size(path);
        }
        auto inventory_byte_bounded_options = options;
        inventory_byte_bounded_options.request.max_reference_bytes = source_inventory_bytes - 1;
        const auto inventory_byte_bounded = MigrationOps::plan(inventory_byte_bounded_options);
        expect(!inventory_byte_bounded.ready() &&
               contains_prefix(inventory_byte_bounded.blockers, "source_inventory_byte_limit_exceeded"),
            "planner should apply the aggregate scan byte budget before canonical metadata parsing");

        auto byte_bounded_options = options;
        byte_bounded_options.request.max_reference_bytes = source_inventory_bytes;
        const auto byte_bounded = MigrationOps::plan(byte_bounded_options);
        expect(!byte_bounded.ready() && contains_prefix(byte_bounded.blockers, "reference_byte_limit_exceeded"),
            "planner should fail closed when aggregate reference scan bytes exceed the request bound");

        auto reference_bounded_options = options;
        reference_bounded_options.request.max_references = 1;
        const auto reference_bounded = MigrationOps::plan(reference_bounded_options);
        const auto reference_bounded_repeat = MigrationOps::plan(reference_bounded_options);
        expect(!reference_bounded.ready() && contains_prefix(reference_bounded.blockers, "reference_limit_exceeded"),
            "planner should fail closed when reference rewrite records exceed the request bound");
        expect(reference_bounded.to_json() == reference_bounded_repeat.to_json(),
            "reference record truncation should remain deterministic at the configured boundary");

        auto source_inventory_options = options;
        source_inventory_options.request.max_source_inventory_items = 44;
        const auto source_inventory_bounded = MigrationOps::plan(source_inventory_options);
        expect(!source_inventory_bounded.ready() &&
               contains_prefix(source_inventory_bounded.blockers, "source_inventory_limit_exceeded"),
            "planner should bound complete source canonical inventory enumeration before metadata parsing");

        const auto target_fixture_one = create_ready_item(
            target_root, "KPG", ItemType::Task, 1, "Target inventory fixture one");
        const auto target_fixture_two = create_ready_item(
            target_root, "KPG", ItemType::Task, 2, "Target inventory fixture two");
        auto target_inventory_options = options;
        target_inventory_options.request.max_target_inventory_items = 1;
        const auto target_inventory_bounded = MigrationOps::plan(target_inventory_options);
        expect(!target_inventory_bounded.ready() &&
               contains_prefix(target_inventory_bounded.blockers, "target_inventory_limit_exceeded"),
            "planner should bound complete target canonical inventory enumeration before metadata parsing");
        auto target_allocation_fixture = read_text(*target_fixture_one.file_path);
        const auto target_id_offset = target_allocation_fixture.find(target_fixture_one.id);
        expect(target_id_offset != std::string::npos, "target allocation fixture should expose its canonical ID");
        target_allocation_fixture.replace(
            target_id_offset,
            target_fixture_one.id.size(),
            "KPG-TSK-2147483647");
        write_text(*target_fixture_one.file_path, target_allocation_fixture);
        const auto target_allocation_bounded = MigrationOps::plan(options);
        expect(!target_allocation_bounded.ready() &&
               contains_prefix(target_allocation_bounded.blockers, "target_id_allocation_limit_exceeded:TSK"),
            "planner should reject target display-ID allocation beyond the signed canonical range");
        std::filesystem::remove(*target_fixture_one.file_path);
        std::filesystem::remove(*target_fixture_two.file_path);

        auto artifact_inventory_options = options;
        artifact_inventory_options.request.max_artifacts = 0;
        const auto artifact_inventory_bounded = MigrationOps::plan(artifact_inventory_options);
        expect(!artifact_inventory_bounded.ready() &&
               contains_prefix(artifact_inventory_bounded.blockers, "artifact_enumeration_limit_exceeded"),
            "planner should stream owned artifact enumeration through the existing file-count bound");

        auto artifact_byte_options = options;
        artifact_byte_options.request.max_artifact_bytes = 1;
        const auto artifact_byte_bounded = MigrationOps::plan(artifact_byte_options);
        expect(!artifact_byte_bounded.ready() &&
               contains_prefix(artifact_byte_bounded.blockers, "artifact_byte_limit_exceeded"),
            "planner should fail closed before aggregate owned artifact reads exceed their byte bound");

        MigrationOps::ApplyOptions apply_options;
        apply_options.plan = options;
        apply_options.plan.request.expected_source_revision = first.source_revision;
        apply_options.plan.request.expected_target_prefix = first.target_prefix;
        apply_options.expected_plan_hash = first.plan_hash;
        const auto unconfirmed = MigrationOps::apply(apply_options);
        expect(unconfirmed.status == "blocked" && contains_prefix(unconfirmed.operation_receipts, "confirm_required"),
            "apply should require an explicit confirmation gate");

        auto materialization_options = options;
        materialization_options.request.max_materialization_bytes = 1;
        const auto materialization_plan = MigrationOps::plan(materialization_options);
        expect(materialization_plan.ready(),
            "materialization capacity should remain part of the immutable apply contract without mutating planning");
        MigrationOps::ApplyOptions materialization_apply_options;
        materialization_apply_options.plan = materialization_options;
        materialization_apply_options.expected_plan_hash = materialization_plan.plan_hash;
        materialization_apply_options.confirm = true;
        const auto materialization_bounded = MigrationOps::apply(materialization_apply_options);
        expect(materialization_bounded.status == "blocked" &&
               contains_prefix(materialization_bounded.operation_receipts, "materialization_byte_limit_exceeded"),
            "confirmed apply should fail closed before retaining mutation output beyond its materialization bound");
        expect(read_text(*source_initiative.file_path) == source_before &&
               CanonicalStore(target_root).list_items().empty(),
            "materialization overflow must not publish or retire canonical files");

        auto changed_bound_apply_options = apply_options;
        ++changed_bound_apply_options.plan.request.max_reference_bytes;
        changed_bound_apply_options.confirm = true;
        const auto changed_bound_apply = MigrationOps::apply(changed_bound_apply_options);
        expect(changed_bound_apply.status == "blocked" &&
               contains_prefix(changed_bound_apply.operation_receipts, "plan_hash_mismatch:"),
            "apply should reject a request bound that differs from the approved immutable plan");

        apply_options.confirm = true;
        for (const auto& phase : {
                 "after_stage",
                 "after_target_publish",
                 "after_reference_rewrite",
                 "after_source_retire",
             }) {
            apply_options.inject_failure_after = phase;
            const auto failed = MigrationOps::apply(apply_options);
            expect(failed.status == "rolled_back" && failed.recovery_status == "completed",
                std::string("injected failure should automatically roll back phase ") + phase);
            expect(read_text(*source_initiative.file_path) == source_before,
                "automatic rollback should restore the exact source item bytes");
            expect(read_text(*observer.file_path) == observer_before,
                "automatic rollback should restore external reference bytes");
            expect(read_text(source_artifact) == artifact_before,
                "automatic rollback should restore owned artifact bytes");
            expect(CanonicalStore(target_root).list_items().empty(),
                "automatic rollback must not expose a partial target subtree");
        }

        apply_options.inject_failure_after.reset();
        const auto applied = MigrationOps::apply(apply_options);
        if (applied.status != "applied") {
            std::cerr << applied.to_json(true) << "\n";
        }
        expect(applied.status == "applied" && applied.recovery_status == "available",
            "confirmed exact-hash apply should publish the complete migration");
        expect(CanonicalStore(source_root).list_items().empty(),
            "successful apply should retire every source subtree item");
        expect(CanonicalStore(target_root).list_items().size() == 45,
            "successful apply should publish all 45 target items");
        const auto root_mapping = std::find_if(first.items.begin(), first.items.end(), [&](const auto& mapping) {
            return mapping.source_id == source_initiative.id;
        });
        expect(root_mapping != first.items.end(), "plan should contain the source root mapping");
        const auto target_root_item = CanonicalStore(target_root).read(backlog_root / root_mapping->target_path);
        expect(target_root_item.id == root_mapping->target_id && target_root_item.uid == source_initiative.uid,
            "apply should preserve UID while publishing the mapped target ID");
        const auto observer_after = read_text(*observer.file_path);
        expect(observer_after.find(root_mapping->target_id) != std::string::npos &&
               observer_after.find(source_initiative.id) == std::string::npos,
            "apply should rewrite the external canonical reference");
        expect(observer_after.find("KTO-TSK-00010") != std::string::npos,
            "boundary-aware rewrite must leave unrelated ID-like text byte-identical");
        expect(std::filesystem::is_regular_file(backlog_root / first.artifacts.front().target_path),
            "apply should publish the owned artifact with its target owner");
        expect(!std::filesystem::exists(source_artifact),
            "apply should retire the source-owned artifact");
        expect(std::filesystem::is_regular_file(
            target_root / "_meta" / "migrations" / (first.plan_hash + ".json")),
            "apply should persist the old-ID alias mapping as migration metadata");

        MigrationOps::RecoveryOptions recovery_options{
            .start_path = root,
            .backlog_root = backlog_root,
            .plan_hash = first.plan_hash,
            .confirm = false,
        };

        const auto journal_path = backlog_root / ".cache" / "migrations" / first.plan_hash / "journal.json";
        const auto journal_before_tamper = read_text(journal_path);
        write_text(
            journal_path,
            replace_json_unsigned(
                journal_before_tamper,
                "max_reference_bytes",
                options.request.max_reference_bytes + 1ull));
        const auto tampered_verification = MigrationOps::verify(recovery_options);
        expect(tampered_verification.status == "failed" &&
               contains_prefix(tampered_verification.failures, "journal_embedded_plan_hash_mismatch") &&
               std::find(
                   tampered_verification.postconditions.begin(),
                   tampered_verification.postconditions.end(),
                   "canonical_references_rewritten") == tampered_verification.postconditions.end(),
            "verification should reject tampered persisted bounds before scanning or reporting postconditions");
        write_text(journal_path, journal_before_tamper);

        std::string unresolved_reference_fixture;
        for (const auto& mapping : first.items) {
            unresolved_reference_fixture += mapping.source_id + "\n";
        }
        const auto reference_overflow_root = observer_root / "verification-reference-bound";
        for (int index = 0; index < 3; ++index) {
            write_text(reference_overflow_root / (std::to_string(index) + ".txt"), unresolved_reference_fixture);
        }
        const auto reference_overflow_verification = MigrationOps::verify(recovery_options);
        expect(reference_overflow_verification.status == "failed" &&
               contains_prefix(reference_overflow_verification.failures, "verification_reference_limit_exceeded"),
            "verification should reuse the persisted reference record bound");
        std::filesystem::remove_all(reference_overflow_root);

        const auto unsupported_verification_path = observer_root / "verification-unsupported-reference.bin";
        write_text(unsupported_verification_path, first.items.front().source_id);
        const auto unsupported_verification = MigrationOps::verify(recovery_options);
        expect(unsupported_verification.status == "failed" &&
               contains_prefix(unsupported_verification.failures, "verification_unsupported_reference_class:"),
            "verification should account for unsupported files with unresolved migrated IDs exactly as planning does");
        std::filesystem::remove(unsupported_verification_path);

        const auto byte_overflow_path = observer_root / "verification-byte-bound.bin";
        write_text(byte_overflow_path, std::string(options.request.max_reference_bytes, 'x'));
        const auto byte_overflow_verification = MigrationOps::verify(recovery_options);
        expect(byte_overflow_verification.status == "failed" &&
               contains_prefix(byte_overflow_verification.failures, "verification_reference_byte_limit_exceeded"),
            "verification should reuse the persisted aggregate reference byte bound");
        std::filesystem::remove(byte_overflow_path);

        const auto file_overflow_root = observer_root / "verification-file-bound";
        for (int index = 0; index < 60; ++index) {
            write_text(file_overflow_root / (std::to_string(index) + ".txt"), "bounded\n");
        }
        const auto file_overflow_verification = MigrationOps::verify(recovery_options);
        expect(file_overflow_verification.status == "failed" &&
               contains_prefix(file_overflow_verification.failures, "verification_reference_enumeration_limit_exceeded"),
            "verification should reuse the persisted reference file enumeration bound");
        std::filesystem::remove_all(file_overflow_root);

        const auto verification = MigrationOps::verify(recovery_options);
        if (verification.status != "verified") {
            std::cerr << verification.to_json(true) << "\n";
        }
        expect(verification.status == "verified" && verification.failures.empty(),
            "verification should prove hashes, UID preservation, aliases, and reference closure");
        const auto replay = MigrationOps::apply(apply_options);
        expect(replay.status == "already_applied" && replay.idempotent_replay,
            "repeated apply should return a verified idempotent replay receipt");
        expect(MigrationOps::status(recovery_options).status == "applied",
            "status should expose the persisted applied transaction");
        {
            kano::backlog_ops::BacklogIndex migrated_index(backlog_root / ".cache" / "index" / "backlog.db");
            expect(migrated_index.get_path_by_id(observer.id).has_value(),
                "a newly created shared index should retain unrelated registered-product items");
        }

        auto unconfirmed_rollback_options = recovery_options;
        const auto unconfirmed_rollback = MigrationOps::rollback(unconfirmed_rollback_options);
        expect(unconfirmed_rollback.status == "failed" && contains_prefix(unconfirmed_rollback.failures, "confirm_required"),
            "rollback should require an explicit confirmation gate");

        const auto target_before_drift = read_text(backlog_root / root_mapping->target_path);
        write_text(backlog_root / root_mapping->target_path, target_before_drift + "\nrollback drift\n");
        auto rollback_options = recovery_options;
        rollback_options.confirm = true;
        const auto drifted_rollback = MigrationOps::rollback(rollback_options);
        expect(drifted_rollback.status == "failed" && contains_prefix(drifted_rollback.failures, "rollback_drift:"),
            "rollback should fail closed instead of overwriting post-apply drift");
        write_text(backlog_root / root_mapping->target_path, target_before_drift);

        const auto rolled_back = MigrationOps::rollback(rollback_options);
        expect(rolled_back.status == "rolled_back" && rolled_back.failures.empty(),
            "rollback should restore the complete exact pre-migration state");
        expect(read_text(*source_initiative.file_path) == source_before &&
               read_text(*observer.file_path) == observer_before &&
               read_text(source_artifact) == artifact_before,
            "rollback should restore source, artifact, and external-reference bytes exactly");
        expect(CanonicalStore(source_root).list_items().size() == 45 &&
               CanonicalStore(target_root).list_items().empty(),
            "rollback should leave only the complete source subtree active");
        expect(MigrationOps::rollback(rollback_options).status == "not_needed",
            "repeated rollback should be idempotent");
        expect(MigrationOps::verify(recovery_options).status == "not_applied",
            "verification after rollback should not report a live migration");

        auto duplicate_uid_item = create_ready_item(
            source_root, "KTO", ItemType::Task, 45, "Duplicate UID fixture", source_initiative.id);
        duplicate_uid_item.uid = source_initiative.uid;
        CanonicalStore(source_root).write(duplicate_uid_item);
        const auto duplicate_uid_plan = MigrationOps::plan(options);
        expect(!duplicate_uid_plan.ready() && contains_prefix(duplicate_uid_plan.blockers, "duplicate_source_uid:"),
            "planner should fail closed on duplicate source UIDs");
        std::filesystem::remove(*duplicate_uid_item.file_path);

        auto missing_parent_item = create_ready_item(
            source_root, "KTO", ItemType::Task, 45, "Missing parent fixture", "KTO-TSK-9999");
        const auto missing_parent_plan = MigrationOps::plan(options);
        expect(!missing_parent_plan.ready() && contains_prefix(missing_parent_plan.blockers, "missing_parent:"),
            "planner should fail closed on missing canonical parents");
        std::filesystem::remove(*missing_parent_item.file_path);

        const auto task_paths = CanonicalStore(source_root).list_items(ItemType::Task);
        expect(task_paths.size() >= 2, "cycle fixture requires two tasks");
        const auto first_task_before = read_text(task_paths[0]);
        const auto second_task_before = read_text(task_paths[1]);
        auto first_task = CanonicalStore(source_root).read(task_paths[0]);
        auto second_task = CanonicalStore(source_root).read(task_paths[1]);
        first_task.parent = second_task.id;
        second_task.parent = first_task.id;
        CanonicalStore(source_root).write(first_task);
        CanonicalStore(source_root).write(second_task);
        const auto cycle_plan = MigrationOps::plan(options);
        expect(!cycle_plan.ready() && contains_prefix(cycle_plan.blockers, "hierarchy_cycle:"),
            "planner should fail closed on canonical hierarchy cycles");
        write_text(task_paths[0], first_task_before);
        write_text(task_paths[1], second_task_before);

        auto stale_options = options;
        stale_options.request.expected_source_revision = std::string(64, '0');
        const auto stale = MigrationOps::plan(stale_options);
        expect(!stale.ready() && contains_prefix(stale.blockers, "source_revision_mismatch"),
            "preflight should reject a stale expected source revision");

        const auto unsupported_reference = observer_root / "artifacts" / "unresolved.bin";
        write_text(unsupported_reference, "opaque:" + source_initiative.id);
        const auto unsupported = MigrationOps::plan(options);
        expect(!unsupported.ready() && contains_prefix(unsupported.blockers, "unsupported_reference_class:"),
            "preflight should fail closed on an unresolved external reference class");
        std::filesystem::remove(unsupported_reference);

        write_text(
            root / ".kano" / "backlog_config.toml",
            config_text +
            "\n[products.prefix-collision]\n"
            "name = \"Prefix collision\"\n"
            "prefix = \"KPG\"\n"
            "backlog_root = \"_kano/backlog/products/prefix-collision\"\n");
        std::filesystem::create_directories(backlog_root / "products" / "prefix-collision" / "items");
        const auto collision = MigrationOps::plan(options);
        expect(!collision.ready() && contains_prefix(collision.blockers, "target_prefix_collision"),
            "preflight should reject a registered target prefix collision");
        write_text(root / ".kano" / "backlog_config.toml", config_text);

        std::filesystem::remove_all(target_root);
        const auto partial_registration = MigrationOps::plan(options);
        expect(!partial_registration.ready() && contains_prefix(partial_registration.blockers, "target_product_root_not_found"),
            "preflight should reject a partial target registration without a scaffold");
        std::filesystem::create_directories(target_root / "items");

        auto changed_source = CanonicalStore(source_root).read(*source_initiative.file_path);
        changed_source.context = "Source revision drift fixture.";
        CanonicalStore(source_root).write(changed_source);
        const auto drifted = MigrationOps::plan(options);
        expect(drifted.source_revision != first.source_revision && drifted.plan_hash != first.plan_hash,
            "source drift should invalidate the prior revision and plan hash");

        MigrationRequest invalid;
        invalid.source_product = "same";
        invalid.target_product = "same";
        invalid.scope = "repository";
        invalid.max_source_inventory_items = 0;
        invalid.max_target_inventory_items = 0;
        invalid.max_materialization_bytes = 0;
        invalid.max_reference_files = 0;
        invalid.max_reference_bytes = 0;
        invalid.max_references = 0;
        const auto diagnostics = MigrationOps::validate_request(invalid);
        expect(contains_prefix(diagnostics, "source_ref_required") &&
               contains_prefix(diagnostics, "source_and_target_product_must_differ") &&
               contains_prefix(diagnostics, "unsupported_scope") &&
               contains_prefix(diagnostics, "max_source_inventory_items_out_of_range") &&
               contains_prefix(diagnostics, "max_target_inventory_items_out_of_range") &&
               contains_prefix(diagnostics, "max_materialization_bytes_out_of_range") &&
               contains_prefix(diagnostics, "max_reference_files_out_of_range") &&
               contains_prefix(diagnostics, "max_reference_bytes_out_of_range") &&
               contains_prefix(diagnostics, "max_references_out_of_range"),
            "incomplete or unsupported requests should fail closed");

        auto excessive_options = options;
        excessive_options.request.max_source_inventory_items = 500001;
        excessive_options.request.max_target_inventory_items = 500001;
        excessive_options.request.max_artifact_bytes = 256ull * 1024ull * 1024ull * 1024ull + 1ull;
        excessive_options.request.max_materialization_bytes = 512ull * 1024ull * 1024ull * 1024ull + 1ull;
        excessive_options.request.max_reference_files = 500001;
        excessive_options.request.max_reference_bytes = 64ull * 1024ull * 1024ull * 1024ull + 1ull;
        excessive_options.request.max_references = 500001;
        const auto excessive = MigrationOps::plan(excessive_options);
        expect(!excessive.ready() &&
               contains_prefix(excessive.blockers, "max_source_inventory_items_out_of_range") &&
               contains_prefix(excessive.blockers, "max_target_inventory_items_out_of_range") &&
               contains_prefix(excessive.blockers, "max_artifact_bytes_out_of_range") &&
               contains_prefix(excessive.blockers, "max_materialization_bytes_out_of_range") &&
               contains_prefix(excessive.blockers, "max_reference_files_out_of_range") &&
               contains_prefix(excessive.blockers, "max_reference_bytes_out_of_range") &&
               contains_prefix(excessive.blockers, "max_references_out_of_range") &&
               !contains_prefix(excessive.blockers, "preflight_failed:"),
            "planner should reject above-maximum bounds before filesystem enumeration");

        std::filesystem::remove_all(root);
        std::cout << "migration_ops_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
        std::cerr << "migration_ops_smoke_test: FAIL: " << error.what() << "\n";
        return 1;
    }
}
