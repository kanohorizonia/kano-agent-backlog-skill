#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/product_relocation/product_relocation_ops.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
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

bool contains_prefix(
    const std::vector<std::string>& values,
    const std::string& prefix
) {
    return std::any_of(
        values.begin(), values.end(),
        [&](const auto& value) { return value.starts_with(prefix); });
}

void write_text(
    const std::filesystem::path& path,
    const std::string& content
) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write fixture file");
    }
    output.write(
        content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read fixture file");
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::filesystem::path make_temp_root() {
    std::random_device source;
    std::mt19937 generator(source());
    std::uniform_int_distribution<unsigned int> distribution(
        0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << distribution(generator);
    const auto root =
        std::filesystem::temp_directory_path() /
        "kano-backlog-product-relocation-smoke" / suffix.str();
    std::filesystem::create_directories(root);
    return root;
}

struct Fixture {
    std::filesystem::path root;
    std::filesystem::path shared;
    std::filesystem::path source;
    std::filesystem::path destination;
    std::filesystem::path config;
    std::string config_before;
    std::vector<std::string> ids;
    std::vector<std::string> uids;
    std::string feature_bytes;
    std::string task_bytes;
    std::string artifact_bytes;
    std::string receipt_bytes;
};

kano::backlog_core::BacklogItem create_item(
    const std::filesystem::path& product_root,
    const std::string& prefix,
    kano::backlog_core::ItemType type,
    int number,
    const std::string& title,
    const std::optional<std::string>& parent = std::nullopt
) {
    kano::backlog_core::CanonicalStore store(product_root);
    auto item = store.create(
        prefix, type, title, number, parent);
    item.priority = "P2";
    item.area = "fixture";
    item.iteration = "backlog";
    item.context = "Canonical HRR-shaped relocation fixture.";
    item.goal = "Preserve identity, history, links, and evidence.";
    item.approach =
        "Exercise guarded copy-verify-publish-retire relocation.";
    item.acceptance_criteria =
        "All canonical bytes and immutable identities remain exact.";
    item.risks =
        "Disposable fixture only; production HRR is never selected.";
    item.worklog.push_back(
        "2026-08-26 00:00 [agent=fixture-runner] Historical " +
        item.id + " evidence remains byte exact.");
    store.write(item);
    return item;
}

Fixture make_fixture(bool raw_path_ref = false) {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::BacklogIndex;
    using kano::backlog_ops::build_index;

    Fixture fixture;
    fixture.root = make_temp_root();
    fixture.shared = fixture.root / "shared-backlog";
    fixture.source =
        fixture.root / "external-product-roots" /
        "human-rig-runtime";
    fixture.destination =
        fixture.shared / "products" / "human-rig-runtime";
    fixture.config =
        fixture.shared / ".kano" / "backlog_config.toml";
    const auto observer =
        fixture.shared / "products" / "observer";
    std::filesystem::create_directories(fixture.config.parent_path());
    std::filesystem::create_directories(observer / "items");

    const std::string config =
        "[products.human-rig-runtime]\n"
        "name = \"Human Rig Runtime\"\n"
        "prefix = \"HRR\"\n"
        "backlog_root = \"" +
        fixture.source.generic_string() +
        "\" # external root before relocation\n"
        "aliases = [\"human-rig-runtime-relocation\"]\n\n"
        "[products.human-rig-runtime-shadow]\n"
        "name = \"Human Rig Runtime canonical shadow\"\n"
        "prefix = \"HRS\"\n"
        "backlog_root = \"products/human-rig-runtime-shadow\"\n"
        "aliases = [\"human-rig-runtime\"]\n\n"
        "[products.observer]\n"
        "name = \"Observer\"\n"
        "prefix = \"OBS\"\n"
        "backlog_root = \"products/observer\"\n";
    write_text(fixture.config, config);
    fixture.config_before = config;
    std::filesystem::create_directories(
        fixture.shared / "products" / "human-rig-runtime-shadow" / "items");

    auto feature = create_item(
        fixture.source, "HRR", ItemType::Feature, 1,
        "HRR relocation fixture feature");
    std::vector<kano::backlog_core::BacklogItem> tasks;
    for (int number = 1; number <= 28; ++number) {
        tasks.push_back(create_item(
            fixture.source, "HRR", ItemType::Task, number,
            "HRR relocation fixture task " + std::to_string(number),
            feature.id));
    }
    feature.context =
        "Feature evidence links " + tasks.front().id +
        " and preserves missing gap HRR-TSK-9999.";
    feature.links.relates.push_back(tasks.front().id);
    CanonicalStore(fixture.source).write(feature);
    tasks.front().links.blocks.push_back(tasks[1].id);
    tasks.front().decisions.push_back(feature.id);
    if (raw_path_ref) {
        tasks.front().external["evidence"] =
            "C:/private/identity/evidence.json";
    }
    CanonicalStore(fixture.source).write(tasks.front());

    fixture.ids.push_back(feature.id);
    fixture.uids.push_back(feature.uid);
    for (const auto& item : tasks) {
        fixture.ids.push_back(item.id);
        fixture.uids.push_back(item.uid);
    }

    auto observer_item = create_item(
        observer, "OBS", ItemType::Task, 1,
        "Cross-product observer");
    observer_item.links.relates.push_back(tasks.front().id);
    CanonicalStore(observer).write(observer_item);

    const auto artifact =
        fixture.source / "artifacts" / tasks.front().id /
        "evidence.json";
    fixture.artifact_bytes =
        "{\"item_ref\":\"" + tasks.front().id +
        "\",\"result\":\"verified\"}\n";
    write_text(artifact, fixture.artifact_bytes);
    const auto receipt =
        fixture.source / "_meta" / "receipts" /
        (tasks.front().id + ".json");
    fixture.receipt_bytes =
        "{\"receipt_for\":\"" + tasks.front().id +
        "\",\"historical\":true}\n";
    write_text(receipt, fixture.receipt_bytes);
    write_text(
        fixture.source / "architecture" / "ADR-0001.md",
        "# ADR-0001\n\nImpacts " + feature.id +
            " and retains accepted/rejected evidence.\n");
    write_text(
        fixture.source / "views" / "summary.md",
        "# Disposable derived view\n");

    const auto index_path =
        fixture.source / ".cache" / "index" / "backlog.db";
    build_index(
        fixture.source, index_path, true, "human-rig-runtime");
    {
        BacklogIndex index(
            index_path, "human-rig-runtime", fixture.source);
        index.sync_sequences(fixture.source);
        (void)index.reserve_next_number(
            "HRR", "TSK", "fixture-runner");
    }

    fixture.feature_bytes = read_text(*feature.file_path);
    fixture.task_bytes = read_text(*tasks.front().file_path);
    expect(
        fixture.ids.size() == 29 && fixture.uids.size() == 29,
        "fixture must contain exactly 29 HRR-shaped items");
    return fixture;
}

kano::backlog_ops::ProductRelocationOps::PlanOptions plan_options(
    const Fixture& fixture
) {
    kano::backlog_ops::ProductRelocationOps::PlanOptions options;
    options.start_path = fixture.shared;
    options.backlog_root = fixture.shared;
    options.request.product = "human-rig-runtime-relocation";
    options.request.destination_root = fixture.destination;
    options.request.max_files = 1000;
    options.request.max_bytes = 16u * 1024u * 1024u;
    options.request.max_items = 1000;
    return options;
}

void cleanup(Fixture& fixture) {
    if (!fixture.root.empty()) {
        std::error_code error;
        std::filesystem::remove_all(fixture.root, error);
        fixture.root.clear();
    }
}

void test_plan_and_collisions() {
    using kano::backlog_core::ProjectConfig;
    using kano::backlog_ops::ProductRelocationOps;
    auto fixture = make_fixture();
    try {
        const auto project_config =
            ProjectConfig::load_from_toml(fixture.config);
        expect(project_config.has_value(), "selector fixture config should parse");
        const auto unique_product =
            project_config->resolve_product("human-rig-runtime-relocation");
        expect(
            unique_product &&
                unique_product->canonical_slug == "human-rig-runtime",
            "the unique relocation alias should resolve to the intended canonical product");
        bool canonical_product_ambiguous = false;
        try {
            (void)project_config->resolve_product("human-rig-runtime");
        } catch (const std::exception& error) {
            canonical_product_ambiguous = std::string(error.what()).find(
                "Product selector collision: normalized selector 'human-rig-runtime'") !=
                std::string::npos;
        }
        expect(
            canonical_product_ambiguous,
            "the ambiguous canonical-looking relocation token should fail closed as public input");

        const auto options = plan_options(fixture);
        const auto first = ProductRelocationOps::plan(options);
        const auto second = ProductRelocationOps::plan(options);
        if (!first.ready()) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(first.ready(), "HRR-shaped plan should be ready");
        expect(
            first.request.product == "human-rig-runtime-relocation" &&
                first.product == "human-rig-runtime",
            "alias-based relocation should store the intended canonical product identity");
        expect(
            first.plan_hash.size() == 64 &&
                first.plan_hash == second.plan_hash,
            "identical relocation input should have one deterministic hash");
        expect(
            first.identities.size() == 29,
            "planner should inventory all 29 immutable identities");
        expect(
            first.cross_volume_safe &&
                first.relocation_strategy ==
                    "copy-verify-publish-retire",
            "planner should use the cross-volume-safe strategy");
        expect(
            first.sequence_count > 0 &&
                first.reservation_count == 1,
            "planner should preserve durable allocation state");
        expect(
            contains_prefix(
                first.derived_surfaces,
                "product-derived:human-rig-runtime/views/") &&
                contains_prefix(
                    first.derived_surfaces,
                    "product-cache:human-rig-runtime/index/"),
            "planner should classify views and index as derived");
        expect(
            first.to_json().find(fixture.root.string()) ==
                    std::string::npos &&
                first.to_json().find(
                    fixture.root.generic_string()) ==
                    std::string::npos,
            "plan JSON must not expose raw filesystem roots");
        expect(
            !std::filesystem::exists(fixture.destination),
            "dry-run must not create the destination");

        write_text(
            fixture.destination / "unexpected.md",
            "occupied destination\n");
        const auto collision = ProductRelocationOps::plan(options);
        expect(
            !collision.ready() &&
                contains_prefix(
                    collision.blockers,
                    "destination_not_empty_directory"),
            "non-empty destination should fail closed");
        std::filesystem::remove_all(fixture.destination);
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

void test_raw_path_ref_rejection() {
    using kano::backlog_ops::ProductRelocationOps;
    auto fixture = make_fixture(true);
    try {
        const auto plan =
            ProductRelocationOps::plan(plan_options(fixture));
        expect(
            !plan.ready() &&
                contains_prefix(
                    plan.blockers,
                    "raw_filesystem_navigation_ref:"),
            "raw filesystem navigation refs should block relocation");
        expect(
            plan.to_json().find("C:/private/identity") ==
                std::string::npos,
            "raw ref diagnostic should not echo private path bytes");
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

void test_stale_and_automatic_rollback() {
    using kano::backlog_ops::ProductRelocationOps;
    auto fixture = make_fixture();
    try {
        std::filesystem::create_directories(fixture.destination);
        const auto options = plan_options(fixture);
        const auto plan = ProductRelocationOps::plan(options);
        expect(plan.ready(), "stale fixture plan should be ready");
        expect(
            plan.destination_preexisted_empty,
            "planner should record an existing empty destination");
        const auto artifact =
            fixture.source / "artifacts" / fixture.ids[1] /
            "evidence.json";
        write_text(
            artifact,
            fixture.artifact_bytes + "{\"drift\":true}\n");
        ProductRelocationOps::ApplyOptions stale{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .confirm = true,
        };
        const auto stale_result =
            ProductRelocationOps::apply(stale);
        expect(
            stale_result.status == "blocked" &&
                contains_prefix(
                    stale_result.operation_receipts,
                    "stale_or_mismatched_plan_hash"),
            "apply should reject canonical drift after review");
        write_text(artifact, fixture.artifact_bytes);

        auto unconfirmed = stale;
        unconfirmed.confirm = false;
        const auto unconfirmed_result =
            ProductRelocationOps::apply(unconfirmed);
        expect(
            unconfirmed_result.status == "blocked" &&
                contains_prefix(
                    unconfirmed_result.operation_receipts,
                    "confirmation_required"),
            "apply should require explicit confirmation");

        auto failing = stale;
        failing.inject_failure_after = "after_target_publish";
        const auto failed = ProductRelocationOps::apply(failing);
        expect(
            failed.status == "rolled_back" &&
                contains_prefix(
                    failed.operation_receipts,
                    "automatic_rollback_completed"),
            "mid-transaction failure should roll back automatically");
        expect(
            read_text(fixture.config) == fixture.config_before &&
                std::filesystem::is_directory(fixture.source) &&
                std::filesystem::is_directory(fixture.destination) &&
                std::filesystem::is_empty(fixture.destination),
            "automatic rollback should restore config/source/empty-target state");
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

void test_recoverable_interruption() {
    using kano::backlog_ops::ProductRelocationOps;
    auto fixture = make_fixture();
    try {
        const auto options = plan_options(fixture);
        const auto plan = ProductRelocationOps::plan(options);
        ProductRelocationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_config_publish",
        };
        const auto result =
            ProductRelocationOps::apply(interrupted);
        if (result.status != "recovery_required") {
            std::cerr << result.to_json(true) << "\n";
        }
        expect(
            result.status == "recovery_required" &&
                result.recovery_status == "required",
            "injected interruption should leave deterministic recovery");

        ProductRelocationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto status = ProductRelocationOps::status(recovery);
        expect(
            status.status == "recovery_required" &&
                status.stage == "after_config_publish" &&
                status.rollback_supported,
            "status should identify the interrupted stage");
        recovery.confirm = true;
        const auto rolled_back =
            ProductRelocationOps::rollback(recovery);
        expect(
            rolled_back.status == "rolled_back",
            "explicit rollback should recover interrupted relocation");
        expect(
            read_text(fixture.config) == fixture.config_before &&
                std::filesystem::is_directory(fixture.source) &&
                !std::filesystem::exists(fixture.destination),
            "interruption rollback should restore exact pre-state");
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

void test_success_verify_replay_and_rollback() {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ProjectConfig;
    using kano::backlog_ops::ProductRelocationOps;
    auto fixture = make_fixture();
    try {
        const auto options = plan_options(fixture);
        const auto plan = ProductRelocationOps::plan(options);
        ProductRelocationOps::ApplyOptions apply{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .confirm = true,
        };
        const auto stale_lock =
            fixture.shared / ".kano" / "cache" /
            "product-relocations" / "apply.lock";
        write_text(
            stale_lock / "owner.json",
            "{\"schema\":\"kob.product_root_relocation.lock.v1\","
            "\"plan_hash\":\"dead-owner\","
            "\"pid\":4294967294}\n");
        const auto applied = ProductRelocationOps::apply(apply);
        if (applied.status != "applied") {
            std::cerr << applied.to_json(true) << "\n";
        }
        expect(
            applied.status == "applied",
            "confirmed relocation should apply");
        expect(
            !std::filesystem::exists(stale_lock),
            "dead process lock should be reclaimed and released");
        expect(
            !std::filesystem::exists(fixture.source) &&
                std::filesystem::is_directory(fixture.destination),
            "source should retire only after target/config verification");
        expect(
            !std::filesystem::exists(
                fixture.destination / "views" / "summary.md"),
            "derived views should not be copied as authority");
        expect(
            std::filesystem::is_regular_file(
                fixture.destination / ".cache" / "index" /
                "backlog.db"),
            "target metadata index should be rebuilt");
        expect(
            read_text(
                fixture.destination / "artifacts" /
                fixture.ids[1] / "evidence.json") ==
                    fixture.artifact_bytes &&
                read_text(
                    fixture.destination / "_meta" / "receipts" /
                    (fixture.ids[1] + ".json")) ==
                    fixture.receipt_bytes,
            "artifact and receipt bytes should remain exact");

        CanonicalStore target(fixture.destination);
        expect(
            target.list_items().size() == 29,
            "all 29 HRR-shaped items should remain readable");
        for (std::size_t index = 0; index < fixture.ids.size(); ++index) {
            const auto path =
                target.find_item_path_by_id(fixture.ids[index]);
            expect(path.has_value(), "relocated item ID should resolve");
            expect(
                target.read(*path).uid == fixture.uids[index],
                "relocated item UID should remain immutable");
        }
        const auto config =
            ProjectConfig::load_from_toml(fixture.config);
        expect(config.has_value(), "relocated config should parse");
        const auto resolved = config->resolve_backlog_root(
            "human-rig-runtime-relocation", fixture.config);
        expect(
            resolved.has_value() &&
                std::filesystem::weakly_canonical(*resolved) ==
                    std::filesystem::weakly_canonical(
                        fixture.destination),
            "config should resolve the shared product root");

        ProductRelocationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto verification =
            ProductRelocationOps::verify(recovery);
        if (verification.status != "verified") {
            std::cerr << verification.to_json(true) << "\n";
        }
        expect(
            verification.status == "verified",
            "post-apply verification should cover identity/ref/index state");
        const auto replay = ProductRelocationOps::apply(apply);
        expect(
            replay.status == "applied" &&
                replay.idempotent_replay,
            "exact retry should verify and replay idempotently");

        recovery.confirm = true;
        const auto rolled_back =
            ProductRelocationOps::rollback(recovery);
        expect(
            rolled_back.status == "rolled_back",
            "confirmed rollback should restore pre-relocation state");
        expect(
            read_text(fixture.config) == fixture.config_before &&
                std::filesystem::is_directory(fixture.source) &&
                !std::filesystem::exists(fixture.destination),
            "rollback should restore config/source and retire target");
        CanonicalStore restored(fixture.source);
        expect(
            read_text(*restored.find_item_path_by_id(fixture.ids[0])) ==
                    fixture.feature_bytes &&
                read_text(*restored.find_item_path_by_id(fixture.ids[1])) ==
                    fixture.task_bytes &&
                read_text(
                    fixture.source / "views" / "summary.md") ==
                    "# Disposable derived view\n",
            "rollback should restore exact canonical and original derived bytes");
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    try {
        test_plan_and_collisions();
        test_raw_path_ref_rejection();
        test_stale_and_automatic_rollback();
        test_recoverable_interruption();
        test_success_verify_replay_and_rollback();
        std::cout << "product_relocation_ops_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "product_relocation_ops_smoke_test: FAIL: "
                  << error.what() << "\n";
        return 1;
    }
}
