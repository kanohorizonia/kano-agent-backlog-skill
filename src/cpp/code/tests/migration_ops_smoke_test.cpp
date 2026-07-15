#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/migration/migration_ops.hpp"

#include <algorithm>
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

        const auto source_feature = create_ready_item(source_root, "KTO", ItemType::Feature, 1, "Parametric geometry subtree");
        for (int number = 1; number <= 44; ++number) {
            create_ready_item(
                source_root,
                "KTO",
                ItemType::Task,
                number,
                "Parametric fixture task " + std::to_string(number),
                source_feature.id);
        }
        auto observer = create_ready_item(observer_root, "OBS", ItemType::Task, 1, "External reference observer");
        observer.links.relates.push_back(source_feature.id);
        CanonicalStore(observer_root).write(observer);
        write_text(source_root / "artifacts" / source_feature.id / "fixture.json", "{\"owner\":\"" + source_feature.id + "\"}\n");

        MigrationOps::PlanOptions options;
        options.start_path = root;
        options.backlog_root = backlog_root;
        options.request = MigrationRequest{
            .source_product = "kto-source",
            .source_ref = source_feature.uid,
            .target_product = "parametric-target",
            .expected_target_prefix = "KPG",
            .scope = "subtree",
            .include_owned_artifacts = true,
            .max_items = 45,
            .max_artifacts = 10,
        };

        const auto source_before = read_text(*source_feature.file_path);
        const auto first = MigrationOps::plan(options);
        const auto second = MigrationOps::plan(options);
        if (!first.ready()) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(first.ready(), "45-item fixture should produce a ready plan");
        expect(first.items.size() == 45, "planner should close the full 45-item subtree");
        expect(first.items.front().uid == source_feature.uid, "planner should preserve stable UID in mapping");
        expect(first.artifacts.size() == 1, "planner should include the owned artifact");
        expect(std::any_of(first.references.begin(), first.references.end(), [](const auto& rewrite) {
            return rewrite.external_to_subtree;
        }), "planner should classify external references");
        expect(first.plan_hash.size() == 64 && first.plan_hash == second.plan_hash,
            "identical inputs should produce one deterministic SHA-256 plan hash");
        expect(first.to_json().find("kob.cross_product_migration.plan.v1") != std::string::npos,
            "plan receipt should expose the versioned schema");
        expect(read_text(*source_feature.file_path) == source_before,
            "dry-run planning must not rewrite source items");
        expect(CanonicalStore(target_root).list_items().empty(),
            "dry-run planning must not create target items or mutate target sequences");

        auto stale_options = options;
        stale_options.request.expected_source_revision = std::string(64, '0');
        const auto stale = MigrationOps::plan(stale_options);
        expect(!stale.ready() && contains_prefix(stale.blockers, "source_revision_mismatch"),
            "preflight should reject a stale expected source revision");

        const auto unsupported_reference = observer_root / "artifacts" / "unresolved.bin";
        write_text(unsupported_reference, "opaque:" + source_feature.id);
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

        auto changed_source = CanonicalStore(source_root).read(*source_feature.file_path);
        changed_source.context = "Source revision drift fixture.";
        CanonicalStore(source_root).write(changed_source);
        const auto drifted = MigrationOps::plan(options);
        expect(drifted.source_revision != first.source_revision && drifted.plan_hash != first.plan_hash,
            "source drift should invalidate the prior revision and plan hash");

        MigrationRequest invalid;
        invalid.source_product = "same";
        invalid.target_product = "same";
        invalid.scope = "repository";
        const auto diagnostics = MigrationOps::validate_request(invalid);
        expect(contains_prefix(diagnostics, "source_ref_required") &&
               contains_prefix(diagnostics, "source_and_target_product_must_differ") &&
               contains_prefix(diagnostics, "unsupported_scope"),
            "incomplete or unsupported requests should fail closed");

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
