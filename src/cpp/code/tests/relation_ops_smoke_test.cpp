#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_core/refs/ref_resolver.hpp"
#include "kano/backlog_ops/relation/relation_ops.hpp"

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn>
void expect_throws_contains(Fn&& fn, const std::string& needle, const std::string& message) {
    try {
        fn();
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find(needle) != std::string::npos) {
            return;
        }
        throw std::runtime_error(message + " (unexpected diagnostic: " + ex.what() + ")");
    }
    throw std::runtime_error(message + " (operation unexpectedly succeeded)");
}

std::filesystem::path make_temp_root() {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<unsigned int> distribution(0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << distribution(generator);
    const auto root = std::filesystem::temp_directory_path() / "kano-backlog-relation-smoke" / suffix.str();
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

kano::backlog_core::BacklogItem create_item(
    const std::filesystem::path& product_root,
    const std::string& prefix,
    int number,
    const std::string& title
) {
    kano::backlog_core::CanonicalStore store(product_root);
    auto item = store.create(prefix, kano::backlog_core::ItemType::Task, title, number);
    item.priority = "P2";
    item.area = "test";
    item.iteration = "backlog";
    item.context = "Exercise deterministic cross-product relation behavior.";
    item.goal = "Keep relation semantics safe and idempotent.";
    item.approach = "Use isolated products and canonical frontmatter.";
    item.acceptance_criteria = "Mutation and query receipts match canonical graph behavior.";
    item.risks = "Low; disposable fixture.";
    item.external["fixture_marker"] = "preserve-me";
    store.write(item);
    return item;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

kano::backlog_ops::RelationMutationRequest request(
    const std::filesystem::path& root,
    std::string source_product,
    std::string source_item,
    std::string target_product,
    std::string target_item,
    kano::backlog_ops::RelationType type,
    bool apply = true
) {
    kano::backlog_ops::RelationMutationRequest result;
    result.start_path = root;
    result.source_product = std::move(source_product);
    result.source_item = std::move(source_item);
    result.target_product = std::move(target_product);
    result.target_item = std::move(target_item);
    result.relation_type = type;
    result.agent = "codex-test";
    result.model = "test-model";
    result.idempotency_key = "relation-smoke-v1";
    result.apply = apply;
    return result;
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::RefResolver;
    using kano::backlog_ops::RelationDirection;
    using kano::backlog_ops::RelationListRequest;
    using kano::backlog_ops::RelationOps;
    using kano::backlog_ops::RelationType;

    std::filesystem::path root;
    try {
        root = make_temp_root();
        write_text(
            root / ".kano" / "backlog_config.toml",
            "[products.alpha-product]\n"
            "name = \"Alpha\"\n"
            "prefix = \"ALP\"\n"
            "backlog_root = \"_kano/backlog/products/alpha-product\"\n\n"
            "[products.beta-product]\n"
            "name = \"Beta\"\n"
            "prefix = \"BET\"\n"
            "backlog_root = \"_kano/backlog/products/beta-product\"\n\n"
            "[products.alp]\n"
            "name = \"Alias ambiguity fixture\"\n"
            "prefix = \"AMB\"\n"
            "backlog_root = \"_kano/backlog/products/alp\"\n");

        const auto alpha_root = root / "_kano" / "backlog" / "products" / "alpha-product";
        const auto beta_root = root / "_kano" / "backlog" / "products" / "beta-product";
        const auto alpha_one = create_item(alpha_root, "ALP", 1, "Alpha one");
        const auto alpha_two = create_item(alpha_root, "ALP", 2, "Alpha two");
        const auto beta_one = create_item(beta_root, "BET", 1, "Beta one");
        const auto beta_two = create_item(beta_root, "BET", 2, "Beta two");

        const auto dry_run = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "BET", beta_one.id, RelationType::Relates, false));
        expect(dry_run.status == "dry_run" && dry_run.changed && !dry_run.applied,
            "add should default to a non-mutating plan");
        CanonicalStore alpha_store(alpha_root);
        RefResolver alpha_resolver(alpha_store);
        expect(alpha_resolver.resolve(alpha_one.id).links.relates.empty(), "dry-run must not mutate frontmatter");

        const auto added = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_one.id, RelationType::Relates));
        expect(added.status == "added" && added.applied && added.read_after_write && added.index_refreshed,
            "confirmed relates add should return write and index evidence");
        const auto alpha_after_add = alpha_resolver.resolve(alpha_one.id);
        expect(contains(alpha_after_add.links.relates, beta_one.id), "confirmed add should store the target display ID once");
        expect(alpha_after_add.external.at("fixture_marker") == "preserve-me", "mutation should preserve unrelated metadata");
        const auto worklog_size = alpha_after_add.worklog.size();

        const auto duplicate = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "BET", beta_one.id, RelationType::Relates));
        expect(duplicate.status == "already_present" && duplicate.already_in_desired_state && !duplicate.changed,
            "duplicate add should be idempotent");
        expect(alpha_resolver.resolve(alpha_one.id).worklog.size() == worklog_size,
            "duplicate add must not append another worklog entry");

        RelationListRequest incoming;
        incoming.start_path = root;
        incoming.product = "beta-product";
        incoming.item = beta_one.id;
        incoming.relation_type = RelationType::Relates;
        incoming.direction = RelationDirection::Incoming;
        incoming.limit = 10;
        const auto incoming_result = RelationOps::list(incoming);
        expect(incoming_result.total_matches == 1 && incoming_result.relations.front().source.item_id == alpha_one.id,
            "incoming list should derive a reverse cross-product view");
        auto concurrent_reader = [&]() { return RelationOps::list(incoming); };
        auto first_reader = std::async(std::launch::async, concurrent_reader);
        auto second_reader = std::async(std::launch::async, concurrent_reader);
        expect(first_reader.get().total_matches == 1 && second_reader.get().total_matches == 1,
            "concurrent relation readers should return the same bounded projection");

        const auto second_relates = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_two.id, RelationType::Relates));
        expect(second_relates.status == "added", "second relates edge should be created");
        RelationListRequest paged;
        paged.start_path = root;
        paged.product = "alpha-product";
        paged.item = alpha_one.id;
        paged.relation_type = RelationType::Relates;
        paged.direction = RelationDirection::Outgoing;
        paged.limit = 1;
        const auto first_page = RelationOps::list(paged);
        expect(first_page.relations.size() == 1 && first_page.next_cursor.has_value() && first_page.total_matches == 2,
            "list should expose a deterministic bounded cursor");
        paged.cursor = first_page.next_cursor;
        const auto second_page = RelationOps::list(paged);
        expect(second_page.relations.size() == 1 && !second_page.next_cursor.has_value(),
            "cursor should advance to the final page");

        const auto blocks = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_one.id, RelationType::Blocks));
        expect(blocks.status == "added" && blocks.relation.source.item_id == alpha_one.id,
            "blocks should preserve canonical source-to-target execution direction");
        const auto chain = RelationOps::add(request(
            root, "beta-product", beta_one.id, "alpha-product", alpha_two.id, RelationType::Blocks));
        expect(chain.status == "added", "second dependency edge should be created");
        const auto cycle = RelationOps::add(request(
            root, "alpha-product", alpha_two.id, "alpha-product", alpha_one.id, RelationType::Blocks));
        expect(cycle.status == "cycle_rejected" && !cycle.applied && cycle.cycle_path.size() >= 4,
            "dependency cycles should be rejected with a concrete path");
        const auto relates_cycle = RelationOps::add(request(
            root, "alpha-product", alpha_two.id, "alpha-product", alpha_one.id, RelationType::Relates));
        expect(relates_cycle.status == "added", "relates edges must not participate in dependency cycle checks");

        const auto blocked_by = RelationOps::add(request(
            root, "beta-product", beta_two.id, "alpha-product", alpha_one.id, RelationType::BlockedBy));
        expect(blocked_by.status == "added" && blocked_by.relation.source.item_id == alpha_one.id &&
                blocked_by.relation.target.item_id == beta_two.id,
            "blocked_by should normalize to the equivalent canonical blocks edge");
        const auto equivalent = RelationOps::add(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_two.id, RelationType::Blocks));
        expect(equivalent.status == "already_present", "blocks and inverse blocked_by storage should be semantically idempotent");

        const auto remove_dry_run = RelationOps::remove(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_two.id, RelationType::Blocks, false));
        expect(remove_dry_run.status == "dry_run" && !remove_dry_run.applied,
            "remove should support dry-run");
        CanonicalStore beta_store(beta_root);
        RefResolver beta_resolver(beta_store);
        expect(contains(beta_resolver.resolve(beta_two.id).links.blocked_by, alpha_one.id),
            "dry-run remove must preserve inverse storage");
        const auto removed = RelationOps::remove(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_two.id, RelationType::Blocks));
        expect(removed.status == "removed" && removed.read_after_write,
            "remove should find and mutate inverse blocked_by ownership");
        expect(!contains(beta_resolver.resolve(beta_two.id).links.blocked_by, alpha_one.id),
            "remove should clear the single canonical owner");
        const auto absent = RelationOps::remove(request(
            root, "alpha-product", alpha_one.id, "beta-product", beta_two.id, RelationType::Blocks));
        expect(absent.status == "already_absent" && absent.already_in_desired_state,
            "duplicate remove should be idempotent");

        auto unresolved_owner = alpha_resolver.resolve(alpha_two.id);
        unresolved_owner.links.relates.push_back("BET-TSK-9999");
        alpha_store.write(unresolved_owner);
        const auto unresolved_dry_run = RelationOps::remove(request(
            root, "alpha-product", alpha_two.id, "beta-product", "BET-TSK-9999", RelationType::Relates, false));
        expect(unresolved_dry_run.status == "dry_run" && unresolved_dry_run.changed && !unresolved_dry_run.applied,
            "missing-target relation cleanup should remain dry-run first");
        expect(contains(alpha_resolver.resolve(alpha_two.id).links.relates, "BET-TSK-9999"),
            "missing-target relation cleanup dry-run must not mutate the source");
        const auto unresolved_removed = RelationOps::remove(request(
            root, "alpha-product", alpha_two.id, "beta-product", "BET-TSK-9999", RelationType::Relates));
        expect(unresolved_removed.status == "removed" && unresolved_removed.applied &&
               unresolved_removed.read_after_write && unresolved_removed.worklog_appended,
            "confirmed missing-target relation cleanup should remove the exact source token");
        expect(!contains(alpha_resolver.resolve(alpha_two.id).links.relates, "BET-TSK-9999"),
            "confirmed missing-target relation cleanup should clear the stale edge");
        const auto unresolved_absent = RelationOps::remove(request(
            root, "alpha-product", alpha_two.id, "beta-product", "BET-TSK-9999", RelationType::Relates));
        expect(unresolved_absent.status == "already_absent" && unresolved_absent.already_in_desired_state,
            "missing-target relation cleanup retry should be idempotent");
        expect_throws_contains([&]() {
            (void)RelationOps::add(request(
                root, "alpha-product", alpha_two.id, "beta-product", "BET-TSK-9999", RelationType::Relates));
        }, "not found", "add must continue to reject a missing relation target");
        expect_throws_contains([&]() {
            (void)RelationOps::remove(request(
                root, "alpha-product", alpha_two.id, "beta-product", "ALP-TSK-9999", RelationType::Relates));
        }, "prefix does not match", "missing-target cleanup must match the registered target prefix");

        expect_throws_contains([&]() {
            auto invalid = request(root, "../alpha-product", alpha_one.id, "beta-product", beta_one.id, RelationType::Relates);
            (void)RelationOps::add(invalid);
        }, "path-like", "path-like product identifiers must be rejected");
        expect_throws_contains([&]() {
            auto ambiguous = request(root, "alp", alpha_one.id, "beta-product", beta_one.id, RelationType::Relates);
            (void)RelationOps::add(ambiguous);
        }, "Ambiguous", "ambiguous product aliases must be rejected");
        expect_throws_contains([&]() {
            auto missing = request(root, "alpha-product", "ALP-TSK-9999", "beta-product", beta_one.id, RelationType::Relates);
            (void)RelationOps::add(missing);
        }, "not found", "missing endpoints must be rejected before mutation");
        expect_throws_contains([&]() {
            RelationListRequest bounded;
            bounded.start_path = root;
            bounded.product = "alpha-product";
            bounded.item = alpha_one.id;
            bounded.max_items = 1;
            (void)RelationOps::list(bounded);
        }, "scan limit", "catalog scans must fail closed at the configured item cap");

        std::filesystem::remove_all(root);
        std::cout << "relation_ops_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        if (!root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(root, cleanup_error);
        }
        std::cerr << "relation_ops_smoke_test: FAIL: " << ex.what() << "\n";
        return 1;
    }
}
