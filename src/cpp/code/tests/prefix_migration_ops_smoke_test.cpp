#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/prefix_migration/prefix_migration_ops.hpp"
#include <json/json.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <vector>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

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
    const auto root =
        std::filesystem::temp_directory_path() /
        "kano-backlog-prefix-migration-smoke" / suffix.str();
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
    if (!input.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

kano::backlog_core::BacklogItem create_item(
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
    item.context = "Canonical fixture context.";
    item.goal = "Exercise prefix migration.";
    item.approach = "Use a disposable shared backlog.";
    item.acceptance_criteria = "Identity and references remain valid.";
    item.risks = "Disposable fixture only.";
    item.worklog.push_back(
        "2026-08-25 00:00 [agent=tester] Historical reference " + item.id);
    store.write(item);
    return item;
}

bool contains_prefix(
    const std::vector<std::string>& values,
    const std::string& prefix
) {
    return std::any_of(
        values.begin(), values.end(),
        [&](const auto& value) { return value.starts_with(prefix); });
}

Json::Value parse_json(const std::string& content) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string errors;
    std::istringstream input(content);
    if (!Json::parseFromStream(builder, input, &value, &errors)) {
        throw std::runtime_error("failed to parse fixture JSON");
    }
    return value;
}

std::string json_text(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, value) + "\n";
}

std::string sha256_hex(const std::string& content) {
    static constexpr std::array<std::uint32_t, 64> constants = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774u, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    const auto rotate = [](std::uint32_t value, unsigned bits) {
        return (value >> bits) | (value << (32u - bits));
    };
    std::vector<std::uint8_t> bytes(content.begin(), content.end());
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8u;
    bytes.push_back(0x80u);
    while (bytes.size() % 64u != 56u) {
        bytes.push_back(0u);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }
    std::array<std::uint32_t, 8> hash = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64u) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16u; ++index) {
            const auto base = offset + index * 4u;
            words[index] = (static_cast<std::uint32_t>(bytes[base]) << 24u) |
                           (static_cast<std::uint32_t>(bytes[base + 1u]) << 16u) |
                           (static_cast<std::uint32_t>(bytes[base + 2u]) << 8u) |
                           static_cast<std::uint32_t>(bytes[base + 3u]);
        }
        for (std::size_t index = 16u; index < words.size(); ++index) {
            const auto s0 = rotate(words[index - 15u], 7u) ^
                            rotate(words[index - 15u], 18u) ^
                            (words[index - 15u] >> 3u);
            const auto s1 = rotate(words[index - 2u], 17u) ^
                            rotate(words[index - 2u], 19u) ^
                            (words[index - 2u] >> 10u);
            words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = hash;
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate(e, 6u) ^ rotate(e, 11u) ^ rotate(e, 25u);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temp1 = h + sum1 + choose + constants[index] + words[index];
            const auto sum0 = rotate(a, 2u) ^ rotate(a, 13u) ^ rotate(a, 22u);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : hash) {
        output << std::setw(8) << value;
    }
    return output.str();
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;
    using kano::backlog_core::ProjectConfig;
    using kano::backlog_ops::PrefixMigrationOps;

    std::filesystem::path root;
    try {
        root = make_temp_root();
        const std::string config =
            "[products.quick-source]\n"
            "name = \"Quick source\"\n"
            "prefix = \"QS\"\n"
            "backlog_root = \"products/quick-source\"\n"
            "aliases = [\"quick-source-migration\"]\n\n"
            "[products.quick-source-shadow]\n"
            "name = \"Quick source canonical shadow\"\n"
            "prefix = \"QSS\"\n"
            "backlog_root = \"products/quick-source-shadow\"\n"
            "aliases = [\"quick-source\"]\n\n"
            "[products.parametric]\n"
            "name = \"Parametric\"\n"
            "prefix = \"NEWQSEO\"\n"
            "backlog_root = \"products/parametric\"\n\n"
            "[products.observer]\n"
            "name = \"Observer\"\n"
            "prefix = \"OBS\"\n"
            "backlog_root = \"products/observer\"\n";
        write_text(root / ".kano" / "backlog_config.toml", config);

        const auto source_root = root / "products" / "quick-source";
        const auto observer_root = root / "products" / "observer";
        std::filesystem::create_directories(
            root / "products" / "parametric" / "items");
        std::filesystem::create_directories(
            root / "products" / "quick-source-shadow" / "items");

        const auto project_config = ProjectConfig::load_from_toml(
            root / ".kano" / "backlog_config.toml");
        expect(project_config.has_value(), "selector fixture config should parse");
        const auto unique_source =
            project_config->resolve_product("quick-source-migration");
        expect(
            unique_source && unique_source->canonical_slug == "quick-source",
            "the unique source alias should resolve to the intended canonical product");
        bool canonical_source_ambiguous = false;
        try {
            (void)project_config->resolve_product("quick-source");
        } catch (const std::exception& error) {
            canonical_source_ambiguous = std::string(error.what()).find(
                "Product selector collision: normalized selector 'quick-source'") !=
                std::string::npos;
        }
        expect(
            canonical_source_ambiguous,
            "the ambiguous canonical-looking source token should fail closed as public input");

        auto feature = create_item(
            source_root, "QS", ItemType::Feature, 1, "Prefix fixture feature");
        auto task = create_item(
            source_root, "QS", ItemType::Task, 1, "Prefix fixture task",
            feature.id);
        feature.context =
            "Canonical dependency " + task.id +
            " and missing ref QS-TSK-9999 must migrate.";
        CanonicalStore(source_root).write(feature);

        auto observer = create_item(
            observer_root, "OBS", ItemType::Task, 1, "External observer");
        observer.links.relates.push_back(task.id);
        observer.context = "Canonical external reference " + task.id + ".";
        observer.worklog.push_back(
            "2026-08-25 00:01 [agent=tester] Historical external " + task.id);
        CanonicalStore(observer_root).write(observer);

        const auto artifact =
            source_root / "artifacts" / task.id / "evidence.json";
        const std::string artifact_bytes =
            "{\"historical_owner\":\"" + task.id + "\"}\n";
        write_text(artifact, artifact_bytes);

        const auto admission =
            source_root / "_meta" / "duplicate-admission" /
            (task.id + ".json");
        const std::string admission_bytes =
            "{\"item_id\":\"" + task.id + "\",\"historical\":true}\n";
        write_text(admission, admission_bytes);

        const auto derived =
            source_root / "items" / "feature" / "0000" /
            (feature.id + "_fixture.index.md");
        write_text(
            derived,
            "# Fixture index\n\nCanonical " + feature.id +
                " links " + task.id + ".\n");
        write_text(
            source_root / "_meta" / "indexes.md",
            "- " + feature.id + " -> " + derived.filename().generic_string() +
                "\n");

        PrefixMigrationOps::PlanOptions options;
        options.start_path = root;
        options.backlog_root = root;
        options.request.product = "quick-source-migration";
        options.request.expected_from_prefix = "QS";
        options.request.to_prefix = "NEWQS";
        options.request.max_files = 100;
        options.request.max_bytes = 4u * 1024u * 1024u;

        const auto source_feature_before = read_text(*feature.file_path);
        const auto source_task_before = read_text(*task.file_path);
        const auto observer_before = read_text(*observer.file_path);
        const auto config_before = read_text(
            root / ".kano" / "backlog_config.toml");

        const auto first = PrefixMigrationOps::plan(options);
        const auto second = PrefixMigrationOps::plan(options);
        if (!first.ready()) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(first.ready(), "fixture should produce a ready plan");
        expect(first.items.size() == 2, "planner should map both source items");
        expect(
            first.request.product == "quick-source-migration" &&
                first.product == "quick-source",
            "alias-based planning should store the intended canonical product identity");
        expect(
            first.plan_hash.size() == 64 &&
                first.plan_hash == second.plan_hash,
            "identical inputs should produce one deterministic SHA-256 plan");
        expect(
            first.schema == "kob.product_prefix_migration.plan.v2" &&
                first.to_json().find("apply_agent") == std::string::npos &&
                first.to_json().find("rollback_agent") == std::string::npos,
            "execution actors must not change the v2 plan or its hash inputs");
        expect(
            first.compatibility_policy == "no-legacy-prefix-alias",
            "pre-1.0 policy should not invent a legacy prefix alias");
        expect(
            contains_prefix(
                first.resolver_checks,
                "token_boundary_distinct:NEWQS:NEWQSEO:parametric"),
            "planner should report the longer-prefix resolver check");
        expect(
            first.to_json().find(root.generic_string()) == std::string::npos &&
                first.to_json().find(root.string()) == std::string::npos,
            "plan output should expose only backlog-relative paths");
        expect(
            read_text(*feature.file_path) == source_feature_before &&
                read_text(*task.file_path) == source_task_before &&
                read_text(*observer.file_path) == observer_before,
            "dry-run must not mutate canonical items");

        const auto transaction =
            root / ".kano" / "cache" / "prefix-migrations" / first.plan_hash;
        auto missing_actor = PrefixMigrationOps::ApplyOptions{
            .plan = options,
            .expected_plan_hash = first.plan_hash,
            .confirm = true,
        };
        const auto missing_actor_result = PrefixMigrationOps::apply(missing_actor);
        expect(
            missing_actor_result.status == "blocked" &&
                contains_prefix(
                    missing_actor_result.operation_receipts, "agent_required") &&
                !std::filesystem::exists(transaction),
            "apply should reject a missing actor before transaction writes");
        for (const std::string forbidden_actor : {
                 "unknown", "auto", "AUTO", "user", "UsEr", "assistant",
                 "AsSiStAnT",
             }) {
            auto invalid_actor = missing_actor;
            invalid_actor.agent = forbidden_actor;
            const auto invalid_actor_result =
                PrefixMigrationOps::apply(invalid_actor);
            expect(
                invalid_actor_result.status == "blocked" &&
                    contains_prefix(
                        invalid_actor_result.operation_receipts,
                        "invalid_agent") &&
                    !std::filesystem::exists(transaction),
                "apply should reject canonical placeholder actors before transaction writes");
        }

        auto stale_apply = missing_actor;
        stale_apply.agent = "sisyphus";
        write_text(
            *feature.file_path,
            source_feature_before + "\n<!-- concurrent drift -->\n");
        const auto stale_result = PrefixMigrationOps::apply(stale_apply);
        expect(
            stale_result.status == "blocked" &&
                contains_prefix(
                    stale_result.operation_receipts,
                    "stale_or_mismatched_plan_hash"),
            "apply should reject a stale reviewed plan hash");
        write_text(*feature.file_path, source_feature_before);

        auto unconfirmed = stale_apply;
        unconfirmed.confirm = false;
        const auto unconfirmed_result =
            PrefixMigrationOps::apply(unconfirmed);
        expect(
            unconfirmed_result.status == "blocked" &&
                contains_prefix(
                    unconfirmed_result.operation_receipts,
                    "confirmation_required"),
            "apply should require explicit confirmation");

        auto injected = stale_apply;
        injected.inject_failure_after = "after_config_publish";
        const auto injected_result = PrefixMigrationOps::apply(injected);
        expect(
            injected_result.status == "rolled_back" &&
                contains_prefix(
                    injected_result.operation_receipts,
                    "automatic_rollback_completed") &&
                injected_result.apply_agent ==
                    std::optional<std::string>("sisyphus") &&
                injected_result.rollback_agent ==
                    std::optional<std::string>("sisyphus") &&
                injected_result.rollback_mode ==
                    std::optional<std::string>("automatic") &&
                injected_result.rollback_attempted_at.has_value() &&
                injected_result.rolled_back_at.has_value(),
            "a deterministic mid-apply failure should roll back automatically");
        const auto automatic_journal_text = read_text(transaction / "journal.json");
        expect(
            automatic_journal_text.find(
                "kob.product_prefix_migration.journal.v3") != std::string::npos &&
                automatic_journal_text.find("\"apply_agent\" : \"sisyphus\"") !=
                    std::string::npos &&
                automatic_journal_text.find("\"rollback_mode\" : \"automatic\"") !=
                    std::string::npos,
            "automatic rollback should persist the validated apply actor");
        expect(
            read_text(root / ".kano" / "backlog_config.toml") == config_before &&
                read_text(*feature.file_path) == source_feature_before &&
                read_text(*task.file_path) == source_task_before,
            "automatic rollback should restore exact pre-migration bytes");

        auto failed_automatic = injected;
        failed_automatic.inject_rollback_failure_after = 1;
        const auto failed_automatic_result =
            PrefixMigrationOps::apply(failed_automatic);
        expect(
            failed_automatic_result.status == "recovery_required" &&
                !failed_automatic_result.changed_paths.empty() &&
                failed_automatic_result.rollback_agent ==
                    std::optional<std::string>("sisyphus") &&
                failed_automatic_result.rollback_mode ==
                    std::optional<std::string>("automatic") &&
                failed_automatic_result.rollback_attempted_at.has_value() &&
                !failed_automatic_result.rolled_back_at,
            "automatic rollback failure should retain truthful retryable provenance");
        const auto failed_automatic_journal =
            parse_json(read_text(transaction / "journal.json"));
        expect(
            failed_automatic_journal["rollback_attempts"].isArray() &&
                failed_automatic_journal["rollback_attempts"].size() == 1u &&
                failed_automatic_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                failed_automatic_journal["rollback_attempts"][0]["mode"].asString() ==
                    "automatic",
            "automatic rollback failure should persist one failed append-only attempt");
        PrefixMigrationOps::RecoveryOptions automatic_retry;
        automatic_retry.backlog_root = root;
        automatic_retry.plan_hash = first.plan_hash;
        const auto failed_automatic_status =
            PrefixMigrationOps::status(automatic_retry);
        expect(
            failed_automatic_status.status == "recovery_required" &&
                failed_automatic_status.rollback_mode ==
                    std::optional<std::string>("automatic") &&
                failed_automatic_status.rollback_attempted_at.has_value() &&
                !failed_automatic_status.rolled_back_at,
            "status should expose an incomplete automatic rollback attempt");
        automatic_retry.agent = "recovery-agent";
        automatic_retry.confirm = true;
        const auto recovered_automatic =
            PrefixMigrationOps::rollback(automatic_retry);
        expect(
            recovered_automatic.status == "rolled_back" &&
                recovered_automatic.rollback_mode ==
                    std::optional<std::string>("manual") &&
                recovered_automatic.rolled_back_at.has_value(),
            "manual retry should complete an interrupted automatic rollback");
        const auto recovered_automatic_journal =
            parse_json(read_text(transaction / "journal.json"));
        expect(
            recovered_automatic_journal["rollback_attempts"].size() == 2u &&
                recovered_automatic_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                recovered_automatic_journal["rollback_attempts"][1]["status"].asString() ==
                    "completed" &&
                recovered_automatic_journal["rollback_attempts"][1]["agent"].asString() ==
                    "recovery-agent",
            "manual retry should append without overwriting automatic attempt evidence");

        auto tampered_historical_actor = recovered_automatic_journal;
        tampered_historical_actor["rollback_attempts"][0]["agent"] =
            "tampered-automatic-agent";
        write_text(
            transaction / "journal.json", json_text(tampered_historical_actor));
        PrefixMigrationOps::RecoveryOptions historical_actor_read;
        historical_actor_read.backlog_root = root;
        historical_actor_read.plan_hash = first.plan_hash;
        const auto historical_actor_verification =
            PrefixMigrationOps::verify(historical_actor_read);
        const auto historical_actor_status =
            PrefixMigrationOps::status(historical_actor_read);
        auto historical_actor_rollback_options = historical_actor_read;
        historical_actor_rollback_options.agent = "reviewer";
        historical_actor_rollback_options.confirm = true;
        const auto historical_actor_rollback =
            PrefixMigrationOps::rollback(historical_actor_rollback_options);
        expect(
            historical_actor_verification.status == "failed" &&
                contains_prefix(
                    historical_actor_verification.failures,
                    "automatic_rollback_agent_mismatch") &&
                historical_actor_status.status == "failed" &&
                historical_actor_status.recovery_status ==
                    "automatic_rollback_agent_mismatch" &&
                historical_actor_rollback.status == "failed" &&
                contains_prefix(
                    historical_actor_rollback.failures,
                    "automatic_rollback_agent_mismatch"),
            "all recovery APIs should reject a tampered historical automatic actor");
        write_text(
            transaction / "journal.json",
            json_text(recovered_automatic_journal));

        auto chained_automatic = injected;
        chained_automatic.inject_automatic_recovery_failure =
            "reported_failure_then_exception";
        const auto chained_automatic_result =
            PrefixMigrationOps::apply(chained_automatic);
        expect(
            chained_automatic_result.status == "recovery_required" &&
                !chained_automatic_result.changed_paths.empty() &&
                contains_prefix(
                    chained_automatic_result.operation_receipts,
                    "injected_rollback_reported_failure") &&
                contains_prefix(
                    chained_automatic_result.operation_receipts,
                    "automatic_rollback_failed:injected_automatic_recovery_exception_after_restore"),
            "automatic recovery should retain reported restore failure and later exception");
        const auto chained_automatic_journal =
            parse_json(read_text(transaction / "journal.json"));
        const auto chained_attempt_error =
            chained_automatic_journal["rollback_attempts"][0]["error"].asString();
        expect(
            chained_automatic_journal["rollback_attempts"].size() == 1u &&
                chained_automatic_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                chained_attempt_error.find(
                    "injected_rollback_reported_failure") != std::string::npos &&
                chained_attempt_error.find(
                    "injected_automatic_recovery_exception_after_restore") !=
                    std::string::npos,
            "persisted automatic attempt should merge every observed failure cause");
        auto chained_retry_options = historical_actor_read;
        chained_retry_options.agent = "chained-recovery-agent";
        chained_retry_options.confirm = true;
        const auto chained_retry =
            PrefixMigrationOps::rollback(chained_retry_options);
        expect(
            chained_retry.status == "rolled_back",
            "fixture should recover after chained automatic failures");

        const auto applied = PrefixMigrationOps::apply(stale_apply);
        if (applied.status != "applied") {
            std::cerr << applied.to_json(true) << "\n";
        }
        expect(
            applied.status == "applied" &&
                applied.schema == "kob.product_prefix_migration.result.v3" &&
                applied.apply_agent == std::optional<std::string>("sisyphus"),
            "confirmed apply should complete with v3 actor provenance");
        expect(
            std::filesystem::is_regular_file(root / applied.receipt_path),
            "apply should persist an immutable migration receipt");
        const auto receipt_text = read_text(root / applied.receipt_path);
        expect(
            receipt_text.find(" \n") == std::string::npos &&
                receipt_text.find("\t\n") == std::string::npos,
            "migration receipt should not contain trailing whitespace");
        expect(
            receipt_text.find("kob.product_prefix_migration.receipt.v3") !=
                    std::string::npos &&
                receipt_text.find("\"apply_agent\" : \"sisyphus\"") !=
                    std::string::npos &&
                read_text(transaction / "journal.json").find(
                    "kob.product_prefix_migration.journal.v3") !=
                    std::string::npos,
            "new apply evidence should persist v3 apply provenance");
        const auto original_journal_text = read_text(transaction / "journal.json");
        auto replay_apply = stale_apply;
        replay_apply.agent = "replay-agent";
        const auto replayed = PrefixMigrationOps::apply(replay_apply);
        expect(
            replayed.status == "applied" &&
                replayed.idempotent_replay &&
                replayed.apply_agent == std::optional<std::string>("sisyphus") &&
                replayed.receipt_path == applied.receipt_path &&
                contains_prefix(
                    replayed.operation_receipts, "postconditions_verified") &&
                read_text(transaction / "journal.json") == original_journal_text &&
                read_text(root / applied.receipt_path) == receipt_text,
            "an exact confirmed replay should verify and return the applied receipt");

        PrefixMigrationOps::RecoveryOptions recovery;
        recovery.backlog_root = root;
        recovery.plan_hash = first.plan_hash;
        const auto verification = PrefixMigrationOps::verify(recovery);
        if (verification.status != "verified") {
            std::cerr << verification.to_json(true) << "\n";
        }
        expect(
            verification.status == "verified" &&
                verification.apply_agent ==
                    std::optional<std::string>("sisyphus"),
            "post-apply verification should pass");
        const auto status = PrefixMigrationOps::status(recovery);
        expect(
            status.status == "applied" && status.rollback_supported &&
                status.apply_agent == std::optional<std::string>("sisyphus"),
            "applied transaction should expose rollback status");

        const auto outside_sentinel =
            root.parent_path() / (root.filename().string() + "-outside-sentinel.txt");
        write_text(outside_sentinel, "outside-sentinel-unchanged\n");
        const auto expect_recovery_rejected = [&](
            const Json::Value& tampered_journal,
            const std::string& expected_error,
            const std::string& message
        ) {
            write_text(transaction / "journal.json", json_text(tampered_journal));
            const auto tampered_verification = PrefixMigrationOps::verify(recovery);
            const auto tampered_status = PrefixMigrationOps::status(recovery);
            auto rollback_options = recovery;
            rollback_options.agent = "reviewer";
            rollback_options.confirm = true;
            const auto tampered_rollback =
                PrefixMigrationOps::rollback(rollback_options);
            expect(
                tampered_verification.status == "failed" &&
                    contains_prefix(
                        tampered_verification.failures, expected_error) &&
                    tampered_status.status == "failed" &&
                    tampered_status.recovery_status == expected_error &&
                    tampered_rollback.status == "failed" &&
                    contains_prefix(tampered_rollback.failures, expected_error) &&
                    read_text(outside_sentinel) == "outside-sentinel-unchanged\n",
                message);
            write_text(transaction / "journal.json", original_journal_text);
        };
        const auto expect_failed_status = [&]
            (const Json::Value& tampered_journal, const std::string& expected_error) {
                write_text(transaction / "journal.json", json_text(tampered_journal));
                const auto tampered_status = PrefixMigrationOps::status(recovery);
                expect(
                    tampered_status.status == "failed" &&
                        tampered_status.recovery_status == expected_error,
                    "tampered journal should fail closed with its bounded error");
                write_text(transaction / "journal.json", original_journal_text);
            };
        const auto original_journal = parse_json(original_journal_text);

        auto escaped_operation = original_journal;
        escaped_operation["operations"][0]["path"] =
            "../" + outside_sentinel.filename().generic_string();
        expect_recovery_rejected(
            escaped_operation, "invalid_journal_operation_path",
            "verify status and rollback should reject operation path escapes before mutation");

        auto escaped_backup = original_journal;
        bool changed_backup = false;
        for (auto& operation : escaped_backup["operations"]) {
            if (operation["before_exists"].asBool()) {
                operation["backup_path"] = "../escape.bin";
                changed_backup = true;
                break;
            }
        }
        expect(changed_backup, "fixture should contain a backup operation");
        expect_recovery_rejected(
            escaped_backup, "invalid_journal_backup_path",
            "verify status and rollback should reject backup path escapes before mutation");

        auto escaped_stage = original_journal;
        bool changed_stage = false;
        for (auto& operation : escaped_stage["operations"]) {
            if (operation["after_exists"].asBool()) {
                operation["stage_path"] = "../escape.bin";
                changed_stage = true;
                break;
            }
        }
        expect(changed_stage, "fixture should contain a staged operation");
        expect_recovery_rejected(
            escaped_stage, "invalid_journal_stage_path",
            "verify status and rollback should reject stage path escapes before mutation");

        auto mismatched_receipt_path = original_journal;
        const std::string wrong_receipt_path =
            "products/observer/_meta/prefix-migrations/QS-to-NEWQS.json";
        mismatched_receipt_path["receipt_path"] = wrong_receipt_path;
        for (auto& operation : mismatched_receipt_path["operations"]) {
            if (operation["kind"].asString() == "migration_receipt") {
                operation["path"] = wrong_receipt_path;
            }
        }
        expect_failed_status(
            mismatched_receipt_path, "journal_receipt_path_mismatch");

        std::string receipt_stage_path;
        for (const auto& operation : original_journal["operations"]) {
            if (operation["kind"].asString() == "migration_receipt") {
                receipt_stage_path = operation["stage_path"].asString();
                break;
            }
        }
        expect(!receipt_stage_path.empty(), "fixture should bind a staged receipt");
        const auto staged_receipt_path = transaction / receipt_stage_path;
        const auto staged_receipt_text = read_text(staged_receipt_path);
        std::filesystem::remove(staged_receipt_path);
        expect_recovery_rejected(
            original_journal, "staged_migration_receipt_missing",
            "verify status and rollback should reject a missing staged receipt");
        write_text(staged_receipt_path, staged_receipt_text);

        write_text(staged_receipt_path, staged_receipt_text + "\n");
        expect_recovery_rejected(
            original_journal, "staged_migration_receipt_hash_mismatch",
            "verify status and rollback should reject a staged receipt hash mismatch");
        write_text(staged_receipt_path, staged_receipt_text);

        auto staged_identity_receipt = parse_json(staged_receipt_text);
        staged_identity_receipt["product"] = "other-product";
        const auto staged_identity_text = json_text(staged_identity_receipt);
        write_text(staged_receipt_path, staged_identity_text);
        auto staged_identity_journal = original_journal;
        staged_identity_journal["status"] = "applying";
        for (auto& operation : staged_identity_journal["operations"]) {
            if (operation["kind"].asString() == "migration_receipt") {
                operation["after_sha256"] = sha256_hex(staged_identity_text);
            }
        }
        std::filesystem::remove(root / applied.receipt_path);
        expect_recovery_rejected(
            staged_identity_journal,
            "staged_migration_receipt_identity_mismatch",
            "verify status and rollback should distinguish staged receipt identity failure");
        write_text(root / applied.receipt_path, receipt_text);
        write_text(staged_receipt_path, staged_receipt_text);

        auto mismatched_journal_hash = original_journal;
        mismatched_journal_hash["plan_hash"] = std::string(64, '0');
        expect_failed_status(
            mismatched_journal_hash, "journal_plan_hash_mismatch");
        auto mismatched_embedded_hash = original_journal;
        mismatched_embedded_hash["plan"]["plan_hash"] = std::string(64, '0');
        expect_failed_status(
            mismatched_embedded_hash, "embedded_plan_identity_mismatch");
        auto tampered_embedded_plan = original_journal;
        tampered_embedded_plan["plan"]["product"] = "other-product";
        expect_failed_status(
            tampered_embedded_plan, "embedded_plan_hash_mismatch");

        std::filesystem::remove(root / applied.receipt_path);
        const auto missing_receipt_verification =
            PrefixMigrationOps::verify(recovery);
        const auto missing_receipt_status = PrefixMigrationOps::status(recovery);
        const auto missing_receipt_rollback =
            PrefixMigrationOps::rollback([&] {
                auto options = recovery;
                options.agent = "reviewer";
                options.confirm = true;
                return options;
            }());
        expect(
            missing_receipt_verification.status == "failed" &&
                contains_prefix(
                    missing_receipt_verification.failures,
                    "migration_receipt_missing") &&
                missing_receipt_status.status == "failed" &&
                missing_receipt_rollback.status == "failed",
            "verify status and rollback should reject a missing applied receipt");
        write_text(root / applied.receipt_path, receipt_text);

        write_text(root / applied.receipt_path, receipt_text + "\n");
        const auto tampered_receipt_status = PrefixMigrationOps::status(recovery);
        expect(
            tampered_receipt_status.status == "failed" &&
                tampered_receipt_status.recovery_status ==
                    "migration_receipt_hash_mismatch",
            "status should bind the complete receipt hash");
        write_text(root / applied.receipt_path, receipt_text);

        auto tampered_identity_receipt = parse_json(receipt_text);
        tampered_identity_receipt["product"] = "other-product";
        const auto tampered_identity_text = json_text(tampered_identity_receipt);
        write_text(root / applied.receipt_path, tampered_identity_text);
        auto tampered_identity_journal = original_journal;
        for (auto& operation : tampered_identity_journal["operations"]) {
            if (operation["path"].asString() == applied.receipt_path) {
                operation["after_sha256"] = sha256_hex(tampered_identity_text);
            }
        }
        expect_failed_status(
            tampered_identity_journal, "migration_receipt_identity_mismatch");
        write_text(root / applied.receipt_path, receipt_text);

        auto incomplete_rollback_tuple = original_journal;
        incomplete_rollback_tuple["status"] = "recovery_required";
        incomplete_rollback_tuple["rollback_agent"] = "reviewer";
        expect_failed_status(
            incomplete_rollback_tuple, "incomplete_v3_rollback_provenance");

        auto mismatched_receipt = parse_json(receipt_text);
        mismatched_receipt["apply_agent"] = "other-agent";
        const auto mismatched_receipt_text = json_text(mismatched_receipt);
        write_text(root / applied.receipt_path, mismatched_receipt_text);
        auto mismatched_journal = parse_json(original_journal_text);
        for (auto& operation : mismatched_journal["operations"]) {
            if (operation["path"].asString() == applied.receipt_path) {
                operation["after_sha256"] = sha256_hex(mismatched_receipt_text);
            }
        }
        write_text(transaction / "journal.json", json_text(mismatched_journal));
        const auto mismatched_verification = PrefixMigrationOps::verify(recovery);
        expect(
            mismatched_verification.status == "failed" &&
                contains_prefix(
                    mismatched_verification.failures, "apply_agent_mismatch"),
            "v3 verification should require matching valid receipt and journal actors");
        write_text(root / applied.receipt_path, receipt_text);
        write_text(transaction / "journal.json", original_journal_text);

        CanonicalStore migrated(source_root);
        const auto migrated_feature_path =
            migrated.find_item_path_by_id("NEWQS-FTR-0001");
        const auto migrated_task_path =
            migrated.find_item_path_by_id("NEWQS-TSK-0001");
        expect(
            migrated_feature_path && migrated_task_path,
            "migrated IDs should be readable");
        const auto migrated_feature = migrated.read(*migrated_feature_path);
        const auto migrated_task = migrated.read(*migrated_task_path);
        expect(
            migrated_feature.uid == feature.uid &&
                migrated_task.uid == task.uid,
            "migration should preserve immutable UIDs");
        expect(
            migrated_task.parent == std::optional<std::string>(
                "NEWQS-FTR-0001"),
            "migration should preserve hierarchy through rewritten refs");
        expect(
            read_text(*migrated_feature_path).find(
                "Historical reference QS-FTR-0001") != std::string::npos,
            "historical Worklog text should remain byte-preserved");
        expect(
            read_text(*migrated_feature_path).find(
                "Canonical dependency NEWQS-TSK-0001") !=
                std::string::npos,
            "canonical refs outside Worklog should migrate");
        expect(
            read_text(*migrated_feature_path).find(
                "missing ref NEWQS-TSK-9999") != std::string::npos &&
                contains_prefix(first.warnings, "missing_reference_reprefixed:"),
            "missing canonical refs should remain missing under the new prefix and be diagnosed");

        const auto migrated_observer =
            CanonicalStore(observer_root).read(*observer.file_path);
        expect(
            migrated_observer.links.relates ==
                std::vector<std::string>{"NEWQS-TSK-0001"},
            "cross-product canonical refs should migrate");
        expect(
            read_text(*observer.file_path).find(
                "Historical external QS-TSK-0001") != std::string::npos,
            "cross-product Worklog history should remain unchanged");

        const auto migrated_artifact =
            source_root / "artifacts" / "NEWQS-TSK-0001" /
            "evidence.json";
        const auto migrated_admission =
            source_root / "_meta" / "duplicate-admission" /
            "NEWQS-TSK-0001.json";
        expect(
            read_text(migrated_artifact) == artifact_bytes &&
                read_text(migrated_admission) == admission_bytes,
            "artifact and duplicate-admission payload bytes should be immutable");
        expect(
            std::filesystem::is_regular_file(
                source_root / "items" / "feature" / "0000" /
                "NEWQS-FTR-0001_fixture.index.md"),
            "derived index path should migrate");
        expect(
            migrated.get_max_id_number("NEWQS", ItemType::Task) == 1,
            "sequence allocation should derive from migrated IDs");

        auto interrupted_journal =
            parse_json(read_text(transaction / "journal.json"));
        interrupted_journal["status"] = "recovery_required";
        interrupted_journal["rollback_agent"] = "interrupted-reviewer";
        interrupted_journal["rollback_mode"] = "manual";
        interrupted_journal["rollback_attempted_at"] =
            "2026-08-28T00:00:00Z";
        interrupted_journal.removeMember("rolled_back_at");
        interrupted_journal["last_error"] = "manual_rollback_in_progress";
        Json::Value interrupted_attempt(Json::objectValue);
        interrupted_attempt["agent"] = "interrupted-reviewer";
        interrupted_attempt["mode"] = "manual";
        interrupted_attempt["attempted_at"] = "2026-08-28T00:00:00Z";
        interrupted_attempt["status"] = "in_progress";
        interrupted_journal["rollback_attempts"] = Json::Value(Json::arrayValue);
        interrupted_journal["rollback_attempts"].append(interrupted_attempt);
        write_text(transaction / "journal.json", json_text(interrupted_journal));

        auto interrupted_retry_options = recovery;
        interrupted_retry_options.agent = "crash-retry-reviewer";
        interrupted_retry_options.confirm = true;
        const auto interrupted_retry =
            PrefixMigrationOps::rollback(interrupted_retry_options);
        expect(
            interrupted_retry.status == "rolled_back" &&
                interrupted_retry.rollback_agent ==
                    std::optional<std::string>("crash-retry-reviewer") &&
                interrupted_retry.rolled_back_at.has_value(),
            "confirmed manual retry should resume an interrupted in-progress attempt");
        const auto interrupted_retry_journal =
            parse_json(read_text(transaction / "journal.json"));
        expect(
            interrupted_retry_journal["rollback_attempts"].size() == 2u &&
                interrupted_retry_journal["rollback_attempts"][0]["agent"].asString() ==
                    "interrupted-reviewer" &&
                interrupted_retry_journal["rollback_attempts"][0]["mode"].asString() ==
                    "manual" &&
                interrupted_retry_journal["rollback_attempts"][0]["attempted_at"].asString() ==
                    "2026-08-28T00:00:00Z" &&
                interrupted_retry_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                interrupted_retry_journal["rollback_attempts"][0]["error"].asString() ==
                    "interrupted_before_confirmed_retry" &&
                interrupted_retry_journal["rollback_attempts"][0]["failed_at"].isString() &&
                interrupted_retry_journal["rollback_attempts"][1]["agent"].asString() ==
                    "crash-retry-reviewer" &&
                interrupted_retry_journal["rollback_attempts"][1]["status"].asString() ==
                    "completed",
            "retry should atomically close interrupted history before appending completion");
        const auto interrupted_retry_status = PrefixMigrationOps::status(recovery);
        expect(
            interrupted_retry_status.status == "rolled_back" &&
                interrupted_retry_status.rollback_agent ==
                    std::optional<std::string>("crash-retry-reviewer") &&
                read_text(root / ".kano" / "backlog_config.toml") == config_before &&
                read_text(*feature.file_path) == source_feature_before &&
                read_text(*task.file_path) == source_task_before &&
                read_text(*observer.file_path) == observer_before,
            "interrupted retry should leave valid status and restore canonical before-state");
        const auto crash_retry_reapplied = PrefixMigrationOps::apply(stale_apply);
        expect(
            crash_retry_reapplied.status == "applied",
            "fixture should reapply after interrupted-attempt recovery");

        auto partial_recovery = recovery;
        partial_recovery.agent = "reviewer";
        partial_recovery.confirm = true;
        partial_recovery.inject_rollback_failure_after = 1;
        const auto partial_rollback =
            PrefixMigrationOps::rollback(partial_recovery);
        expect(
            partial_rollback.status == "recovery_required" &&
                !partial_rollback.restored_paths.empty() &&
                partial_rollback.rollback_agent ==
                    std::optional<std::string>("reviewer") &&
                partial_rollback.rollback_mode ==
                    std::optional<std::string>("manual") &&
                partial_rollback.rollback_attempted_at.has_value() &&
                !partial_rollback.rolled_back_at,
            "partial manual rollback should retain truthful attempt evidence");
        const auto partial_rollback_journal =
            parse_json(read_text(transaction / "journal.json"));
        expect(
            partial_rollback_journal["rollback_attempts"].isArray() &&
                partial_rollback_journal["rollback_attempts"].size() == 1u &&
                partial_rollback_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                partial_rollback_journal["rollback_attempts"][0]["agent"].asString() ==
                    "reviewer",
            "partial manual rollback should retain its failed attempt entry");
        const auto partial_status = PrefixMigrationOps::status(recovery);
        expect(
            partial_status.status == "recovery_required" &&
                partial_status.rollback_attempted_at.has_value() &&
                !partial_status.rolled_back_at,
            "status should expose retryable partial manual rollback state");
        partial_recovery.inject_rollback_failure_after.reset();
        partial_recovery.agent = "retry-reviewer";
        const auto retried_rollback =
            PrefixMigrationOps::rollback(partial_recovery);
        expect(
            retried_rollback.status == "rolled_back" &&
                retried_rollback.rollback_agent ==
                    std::optional<std::string>("retry-reviewer") &&
                retried_rollback.rollback_attempted_at.has_value() &&
                retried_rollback.rolled_back_at.has_value(),
            "manual rollback retry should finish from a partially restored state");
        const auto retried_rollback_journal =
            parse_json(read_text(transaction / "journal.json"));
        expect(
            retried_rollback_journal["rollback_attempts"].size() == 2u &&
                retried_rollback_journal["rollback_attempts"][0]["status"].asString() ==
                    "failed" &&
                retried_rollback_journal["rollback_attempts"][1]["status"].asString() ==
                    "completed" &&
                retried_rollback_journal["rollback_attempts"][1]["agent"].asString() ==
                    "retry-reviewer",
            "manual retry should append a completed attempt without rewriting history");
        const auto reapplied = PrefixMigrationOps::apply(stale_apply);
        expect(
            reapplied.status == "applied",
            "fixture should reapply after successful rollback retry");
        const auto v2_source_receipt_text =
            read_text(root / reapplied.receipt_path);
        const auto v2_source_journal_text =
            read_text(transaction / "journal.json");

        auto legacy_receipt = parse_json(v2_source_receipt_text);
        legacy_receipt["schema"] = "kob.product_prefix_migration.receipt.v2";
        legacy_receipt.removeMember("apply_agent");
        const auto legacy_receipt_text = json_text(legacy_receipt);
        write_text(root / applied.receipt_path, legacy_receipt_text);

        auto legacy_journal = parse_json(v2_source_journal_text);
        legacy_journal["schema"] = "kob.product_prefix_migration.journal.v2";
        legacy_journal.removeMember("apply_agent");
        std::string legacy_receipt_stage_path;
        for (auto& operation : legacy_journal["operations"]) {
            if (operation["path"].asString() == applied.receipt_path) {
                operation["after_sha256"] = sha256_hex(legacy_receipt_text);
                legacy_receipt_stage_path = operation["stage_path"].asString();
            }
        }
        expect(
            !legacy_receipt_stage_path.empty(),
            "legacy fixture should retain its staged receipt binding");
        write_text(transaction / legacy_receipt_stage_path, legacy_receipt_text);
        write_text(transaction / "journal.json", json_text(legacy_journal));

        const auto legacy_verification = PrefixMigrationOps::verify(recovery);
        expect(
            legacy_verification.status == "verified" &&
                !legacy_verification.apply_agent,
            "verification should accept actorless v2 journal and receipt evidence");
        const auto legacy_status = PrefixMigrationOps::status(recovery);
        expect(
            legacy_status.status == "applied" && legacy_status.rollback_supported &&
                !legacy_status.apply_agent,
            "status should preserve legacy v2 apply provenance as null");

        auto attributed_read = recovery;
        attributed_read.agent = "sisyphus";
        const auto attributed_verification =
            PrefixMigrationOps::verify(attributed_read);
        const auto attributed_status = PrefixMigrationOps::status(attributed_read);
        expect(
            attributed_verification.status == "failed" &&
                contains_prefix(
                    attributed_verification.failures,
                    "agent_not_allowed_for_read_only_mode") &&
                attributed_status.status == "failed",
            "read-only recovery APIs should reject an explicitly supplied actor");

        const auto legacy_journal_before_replay =
            read_text(transaction / "journal.json");
        const auto legacy_receipt_before_replay =
            read_text(root / applied.receipt_path);
        auto legacy_replay_apply = stale_apply;
        legacy_replay_apply.agent = "legacy-replay";
        const auto legacy_replay = PrefixMigrationOps::apply(legacy_replay_apply);
        expect(
            legacy_replay.status == "applied" &&
                legacy_replay.idempotent_replay && !legacy_replay.apply_agent &&
                read_text(transaction / "journal.json") ==
                    legacy_journal_before_replay &&
                read_text(root / applied.receipt_path) ==
                    legacy_receipt_before_replay,
            "v2 replay should remain actorless and never rewrite legacy evidence");

        recovery.confirm = true;
        const auto missing_rollback_actor =
            PrefixMigrationOps::rollback(recovery);
        expect(
            missing_rollback_actor.status == "blocked" &&
                contains_prefix(
                    missing_rollback_actor.failures, "agent_required") &&
                read_text(transaction / "journal.json") ==
                    legacy_journal_before_replay,
            "manual rollback should require an actor before journal mutation");
        recovery.agent = "reviewer";
        const auto rolled_back = PrefixMigrationOps::rollback(recovery);
        expect(
            rolled_back.status == "rolled_back" && !rolled_back.apply_agent &&
                rolled_back.rollback_agent ==
                    std::optional<std::string>("reviewer") &&
                rolled_back.rollback_mode ==
                    std::optional<std::string>("manual") &&
                rolled_back.rollback_attempted_at.has_value() &&
                rolled_back.rolled_back_at.has_value(),
            "confirmed rollback should preserve legacy apply provenance and record its actor");
        const auto legacy_rollback_journal =
            read_text(transaction / "journal.json");
        expect(
            legacy_rollback_journal.find(
                "kob.product_prefix_migration.journal.v2") != std::string::npos &&
                legacy_rollback_journal.find("\"apply_agent\"") ==
                    std::string::npos &&
                legacy_rollback_journal.find("\"rollback_agent\" : \"reviewer\"") !=
                    std::string::npos &&
                legacy_rollback_journal.find("\"rollback_mode\" : \"manual\"") !=
                    std::string::npos &&
                legacy_rollback_journal.find("\"rollback_attempted_at\"") !=
                    std::string::npos &&
                legacy_rollback_journal.find("\"rollback_attempts\"") !=
                    std::string::npos,
            "manual rollback should keep v2 apply evidence actorless while adding provenance");
        PrefixMigrationOps::RecoveryOptions rolled_back_status_options;
        rolled_back_status_options.backlog_root = root;
        rolled_back_status_options.plan_hash = first.plan_hash;
        const auto rolled_back_status =
            PrefixMigrationOps::status(rolled_back_status_options);
        expect(
            rolled_back_status.status == "rolled_back" &&
                !rolled_back_status.apply_agent &&
                rolled_back_status.rollback_agent ==
                    std::optional<std::string>("reviewer") &&
                rolled_back_status.rollback_mode ==
                    std::optional<std::string>("manual") &&
                rolled_back_status.rollback_attempted_at.has_value(),
            "status should expose nullable v2 apply and manual rollback provenance");
        expect(
            read_text(root / ".kano" / "backlog_config.toml") == config_before &&
                read_text(*feature.file_path) == source_feature_before &&
                read_text(*task.file_path) == source_task_before &&
                read_text(*observer.file_path) == observer_before &&
                read_text(artifact) == artifact_bytes &&
                read_text(admission) == admission_bytes,
            "rollback should restore exact canonical and historical bytes");
        expect(
            !std::filesystem::exists(root / applied.receipt_path),
            "rollback should remove the migration receipt");
        std::filesystem::remove(outside_sentinel);

        const auto duplicate_path =
            source_root / "items" / "task" / "0000" /
            "QS-TSK-0001_duplicate-fixture.md";
        write_text(duplicate_path, source_task_before);
        const auto duplicate_plan = PrefixMigrationOps::plan(options);
        expect(
            !duplicate_plan.ready() &&
                contains_prefix(
                    duplicate_plan.blockers,
                    "duplicate_source_display_id:QS-TSK-0001"),
            "planner should fail closed on duplicate source display IDs");
        std::filesystem::remove(duplicate_path);

        std::filesystem::remove_all(root);
        std::cout << "prefix_migration_ops_smoke_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty()) {
            std::filesystem::remove_all(root);
        }
        std::cerr << "prefix_migration_ops_smoke_test: FAIL: "
                  << error.what() << "\n";
        return 1;
    }
}
