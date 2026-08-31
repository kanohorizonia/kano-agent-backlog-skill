#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_ops/index/backlog_index.hpp"
#include "kano/backlog_ops/workitem/workitem_ops.hpp"

namespace {

using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ItemState;
using kano::backlog_core::ItemType;
using kano::backlog_ops::BacklogIndex;
using kano::backlog_ops::IndexQuery;
using kano::backlog_ops::IndexQueryResult;
using kano::backlog_ops::WorkitemOps;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path make_temp_root() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("kano-backlog-metadata-index-" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    return root;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to read fixture");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("failed to write fixture");
    }
    output << value;
}

void execute_sql(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.string().c_str(), &database) != SQLITE_OK) {
        if (database != nullptr) {
            sqlite3_close(database);
        }
        throw std::runtime_error("failed to open fixture database");
    }
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    sqlite3_close(database);
    if (result != SQLITE_OK) {
        throw std::runtime_error("failed to mutate fixture database");
    }
}

struct PersistedProofState {
    std::string kind;
    std::string status;
    std::string root_volume_serial;
    std::string root_file_id;
    std::string journal_id;
    std::int64_t verified_usn = 0;
    std::size_t watch_count = 0;
};

PersistedProofState read_persisted_proof_state(
    const std::filesystem::path& path,
    const std::string& product
) {
    sqlite3* database = nullptr;
    if (sqlite3_open_v2(
            path.string().c_str(),
            &database,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        if (database != nullptr) {
            sqlite3_close(database);
        }
        throw std::runtime_error("failed to open fixture database for proof read");
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT proof_kind, proof_status, proof_root_volume_serial, "
        "proof_root_file_id, proof_journal_id, proof_verified_usn, "
        "(SELECT COUNT(*) FROM metadata_change_watch AS watch "
        " WHERE watch.product = snapshot.product) "
        "FROM metadata_snapshots AS snapshot WHERE product = ?";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
        sqlite3_bind_text(
            statement,
            1,
            product.c_str(),
            static_cast<int>(product.size()),
            SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {
        if (statement != nullptr) {
            sqlite3_finalize(statement);
        }
        sqlite3_close(database);
        throw std::runtime_error("failed to read fixture proof state");
    }
    const auto text_column = [&](const int column) {
        const auto* value = sqlite3_column_text(statement, column);
        const auto bytes = sqlite3_column_bytes(statement, column);
        return value == nullptr || bytes <= 0
            ? std::string{}
            : std::string(
                reinterpret_cast<const char*>(value),
                static_cast<std::size_t>(bytes));
    };
    PersistedProofState result;
    result.kind = text_column(0);
    result.status = text_column(1);
    result.root_volume_serial = text_column(2);
    result.root_file_id = text_column(3);
    result.journal_id = text_column(4);
    result.verified_usn = sqlite3_column_int64(statement, 5);
    result.watch_count = static_cast<std::size_t>(
        sqlite3_column_int64(statement, 6));
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return result;
}

std::int64_t read_verified_usn(
    const std::filesystem::path& path,
    const std::string& product
) {
    return read_persisted_proof_state(path, product).verified_usn;
}

void expect_portable_proof_state(
    const std::filesystem::path& path,
    const std::string& product,
    const std::string& operation
) {
    const auto proof = read_persisted_proof_state(path, product);
    expect(proof.kind == "none" && proof.status == "unsupported" &&
               proof.root_volume_serial == "0" && proof.root_file_id == "0" &&
               proof.journal_id == "0" && proof.verified_usn == 0 &&
               proof.watch_count == 0,
        operation + " must persist unsupported proof state without identities or watches");
}

class ImmediateWriteLock {
public:
    explicit ImmediateWriteLock(const std::filesystem::path& path) {
        if (sqlite3_open(path.string().c_str(), &database_) != SQLITE_OK ||
            sqlite3_exec(
                database_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            if (database_ != nullptr) {
                sqlite3_close(database_);
                database_ = nullptr;
            }
            throw std::runtime_error("failed to hold fixture write lock");
        }
    }

    ~ImmediateWriteLock() {
        if (database_ != nullptr) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_close(database_);
        }
    }

    ImmediateWriteLock(const ImmediateWriteLock&) = delete;
    ImmediateWriteLock& operator=(const ImmediateWriteLock&) = delete;

private:
    sqlite3* database_ = nullptr;
};

void set_ready_fields(BacklogItem& item) {
    item.context = "Exercise a derived metadata index mutation.";
    item.goal = "Keep canonical item metadata authoritative.";
    item.approach = "Update canonical state and publish one derived row.";
    item.acceptance_criteria = "The validated index remains current.";
    item.risks = "Fixture-only mutation.";
}

double percentile_95(std::vector<double> samples) {
    expect(!samples.empty(), "timing samples must not be empty");
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1U;
    return samples[std::min(index, samples.size() - 1U)];
}

template <typename Fn>
double timed_ms(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

void expect_exact(
    const IndexQueryResult& result,
    const std::string& id,
    const std::string& title
) {
    expect(result.items.size() == 1, "exact metadata query must return one item");
    expect(result.items.front().id == id, "exact metadata query returned the wrong ID");
    expect(result.items.front().title == title, "exact metadata query returned a stale title");
}

std::map<std::string, std::pair<ItemState, std::string>> canonical_projection(
    const std::filesystem::path& product_root
) {
    CanonicalStore store(product_root);
    std::map<std::string, std::pair<ItemState, std::string>> result;
    for (const auto& path : store.list_items()) {
        const auto item = store.read_metadata(path);
        result.emplace(item.id, std::make_pair(item.state, item.title));
    }
    return result;
}

std::string expect_product_revision_advanced(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& product_root,
    const std::string& product,
    const std::string& previous_revision,
    const std::string& mutation
) {
    const auto status = kano::backlog_ops::get_index_status(
        backlog_root, product, product_root, previous_revision);
    expect(status.indexes.size() == 1 &&
               status.indexes.front().status == "ready" &&
               !status.indexes.front().unchanged &&
               !status.indexes.front().product_revision.empty() &&
               status.indexes.front().product_revision != previous_revision,
        mutation + " must advance the authoritative product revision");
    return status.indexes.front().product_revision;
}

void expect_product_revision_unchanged(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& product_root,
    const std::string& product,
    const std::string& revision,
    const std::string& mutation
) {
    const auto status = kano::backlog_ops::get_index_status(
        backlog_root, product, product_root, revision);
#ifdef _WIN32
    expect(status.indexes.size() == 1 &&
               status.indexes.front().status == "unchanged" &&
               status.indexes.front().unchanged &&
               status.indexes.front().product_revision == revision &&
               status.indexes.front().scanned_count == 0,
        mutation + " must not advance the authoritative product revision");
#else
    expect(status.indexes.size() == 1 &&
               status.indexes.front().status == "stale" &&
               !status.indexes.front().unchanged &&
               !status.indexes.front().fallback_scan &&
               status.indexes.front().scanned_count == 0 &&
               status.indexes.front().product_revision == revision &&
               status.indexes.front().stale_reason &&
               *status.indexes.front().stale_reason == "change_proof_unsupported",
        mutation + " must preserve revision while unsupported proof fails closed");
#endif
}

void expect_stale_reason(
    const std::filesystem::path& backlog_root,
    const std::filesystem::path& product_root,
    const std::string& product,
    const std::string& revision,
    const std::string& reason
) {
    const auto status = kano::backlog_ops::get_index_status(
        backlog_root, product, product_root, revision);
        expect(status.indexes.size() == 1
            && status.indexes.front().status == "stale"
            && !status.indexes.front().unchanged
            && !status.indexes.front().fallback_scan
            && status.indexes.front().scanned_count == 0
            && status.indexes.front().product_revision == revision
            && status.indexes.front().stale_reason
            && *status.indexes.front().stale_reason == reason
            && status.indexes.front().recovery ==
                "kob index rebuild --product " + product,
        reason + " must fail closed without advancing the product revision");
}

void expect_redacted_requested_status(
    const kano::backlog_ops::GetIndexStatusResult& status,
    const std::string& reason,
    const std::string& operation
) {
    expect(status.indexes.size() == 1, operation + " must return one product status");
    const auto& entry = status.indexes.front();
    expect(entry.status == "stale" && !entry.unchanged && !entry.fallback_scan &&
               entry.scanned_count == 0 && entry.item_count == 0 &&
               entry.index_revision.empty() && entry.canonical_revision.empty() &&
               entry.product_revision.empty() && entry.proof_records_read == 0 &&
               entry.proof_bytes_read == 0 && entry.proof_usn_span == 0 &&
               entry.stale_reason && *entry.stale_reason == reason,
        operation + " must fail closed without exposing persisted projection state");
}

} // namespace

int main() {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();

    std::filesystem::path fixture_root;
    try {
        fixture_root = make_temp_root();
        const auto backlog_root = fixture_root / "backlog";
        const std::string product = "metadata-product";
        const auto product_root = backlog_root / "products" / product;
        const auto index_path = backlog_root / ".cache" / "index" / "backlog.db";
        std::filesystem::create_directories(product_root / "items");

        CanonicalStore store(product_root);
        std::vector<BacklogItem> fixtures;
        fixtures.reserve(650);
        std::string odd_fixture_template;
        std::string even_fixture_template;
        BacklogItem odd_template_item;
        BacklogItem even_template_item;
        const auto replace_fixture_field = [](
            std::string& content,
            const std::string& from,
            const std::string& to
        ) {
            const auto position = content.find(from);
            if (position == std::string::npos) {
                throw std::runtime_error("metadata fixture template field missing");
            }
            content.replace(position, from.size(), to);
        };
        for (int number = 1; number <= 650; ++number) {
            std::ostringstream padded;
            padded.width(4);
            padded.fill('0');
            padded << number;
            auto item = store.create(
                "MDI",
                ItemType::Task,
                "Metadata fixture " + padded.str() + " needle-" + padded.str(),
                number);
            item.priority = number % 2 == 0 ? "P1" : "P2";
            if (number <= 2) {
                store.write(item);
                if (number == 1) {
                    odd_template_item = item;
                    odd_fixture_template = read_text(*item.file_path);
                } else {
                    even_template_item = item;
                    even_fixture_template = read_text(*item.file_path);
                }
            } else {
                const auto& template_item = number % 2 == 0
                    ? even_template_item
                    : odd_template_item;
                auto content = number % 2 == 0
                    ? even_fixture_template
                    : odd_fixture_template;
                replace_fixture_field(content, template_item.id, item.id);
                replace_fixture_field(content, template_item.uid, item.uid);
                replace_fixture_field(content, template_item.title, item.title);
                write_text(*item.file_path, content);
            }
            fixtures.push_back(std::move(item));
        }
        store.reset_write_revision();

        const auto target_id = fixtures.back().id;
        const auto target_uid = fixtures.back().uid;
        IndexQuery exact_query;
        exact_query.exact_ref = target_id;
        exact_query.limit = 1;

        const auto cold = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect_exact(cold, target_id, fixtures.back().title);
        expect(!cold.diagnostics.index_used &&
                   cold.diagnostics.index_status == "missing" &&
                   cold.diagnostics.fallback_scan,
            "cold read must use an explicit canonical fallback");
        expect(cold.diagnostics.scanned_count == 650,
            "cold fallback must not silently omit canonical items");

        const auto built = kano::backlog_ops::build_index(
            product_root, index_path, true, product);
        expect(built.items_indexed == 650, "rebuild must index every canonical item");
        expect(!built.index_revision.empty() &&
                   built.index_revision == built.canonical_revision,
            "rebuild must publish one verified canonical revision");
        const auto built_proof = read_persisted_proof_state(index_path, product);
#ifdef _WIN32
        expect(built_proof.kind == "windows-ntfs-usn-v1" &&
                   built_proof.status == "verified" &&
                   built_proof.root_volume_serial != "0" &&
                   built_proof.root_file_id != "0" &&
                   built_proof.journal_id != "0" && built_proof.watch_count > 0,
            "Windows rebuild must persist verified NTFS identities and watches");
#else
        expect_portable_proof_state(index_path, product, "portable rebuild");
#endif

        const auto baseline_status = kano::backlog_ops::get_index_status(
            backlog_root, product, product_root);
        expect(baseline_status.indexes.size() == 1 &&
                   baseline_status.indexes.front().status == "ready" &&
                   !baseline_status.indexes.front().product_revision.empty() &&
                   baseline_status.indexes.front().scanned_count == 650,
            "full status must publish one authoritative product revision");
        const auto baseline_product_revision =
            baseline_status.indexes.front().product_revision;
        auto current_product_revision = baseline_product_revision;

        double unchanged_status_p95_ms = 0.0;
#ifdef _WIN32
        const auto durable_usn_before = read_verified_usn(index_path, product);
        const auto unrelated_churn_root = fixture_root / "unrelated-churn";
        std::filesystem::create_directories(unrelated_churn_root);
        for (int number = 0; number < 8; ++number) {
            write_text(
                unrelated_churn_root / ("churn-" + std::to_string(number) + ".txt"),
                "unrelated\n");
        }
        kano::backlog_ops::GetIndexStatusResult lazy_checkpoint_status;
        {
            ImmediateWriteLock write_lock(index_path);
            lazy_checkpoint_status = kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                baseline_product_revision);
        }
        expect(lazy_checkpoint_status.indexes.size() == 1 &&
                   lazy_checkpoint_status.indexes.front().status == "unchanged" &&
                   lazy_checkpoint_status.indexes.front().unchanged &&
                   !lazy_checkpoint_status.indexes.front().fallback_scan &&
                   lazy_checkpoint_status.indexes.front().scanned_count == 0 &&
                   lazy_checkpoint_status.indexes.front().proof_records_read > 0 &&
                   lazy_checkpoint_status.indexes.front().proof_bytes_read > 0 &&
                   lazy_checkpoint_status.indexes.front().proof_usn_span > 0 &&
                   !lazy_checkpoint_status.indexes.front()
                        .proof_checkpoint_required &&
                   !lazy_checkpoint_status.indexes.front()
                        .proof_checkpoint_persisted,
            "bounded unrelated USN churn must not require checkpoint CAS");
        expect(read_verified_usn(index_path, product) == durable_usn_before,
            "bounded unrelated USN churn must leave the durable checkpoint unchanged");

        constexpr std::size_t kCheckpointRecordCap = 8192;
        constexpr std::size_t kCheckpointByteCap = 1U * 1024U * 1024U;
        constexpr std::uint64_t kCheckpointUsnSpanCap = 1U * 1024U * 1024U;
        kano::backlog_ops::GetIndexStatusTestHooks below_cap_hooks;
        below_cap_hooks.checkpoint_record_cap = kCheckpointRecordCap;
        below_cap_hooks.checkpoint_byte_cap = kCheckpointByteCap;
        below_cap_hooks.checkpoint_usn_span_cap = kCheckpointUsnSpanCap;
        below_cap_hooks.proof_records_read_override = kCheckpointRecordCap - 1;
        below_cap_hooks.proof_bytes_read_override = kCheckpointByteCap - 1;
        below_cap_hooks.proof_usn_span_override = kCheckpointUsnSpanCap - 1;
        const auto below_cap_status = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            baseline_product_revision,
            below_cap_hooks);
        expect(below_cap_status.indexes.size() == 1 &&
                   below_cap_status.indexes.front().status == "unchanged" &&
                   below_cap_status.indexes.front().unchanged &&
                   !below_cap_status.indexes.front().fallback_scan &&
                   below_cap_status.indexes.front().scanned_count == 0 &&
                   below_cap_status.indexes.front().proof_records_read ==
                       kCheckpointRecordCap - 1 &&
                   below_cap_status.indexes.front().proof_bytes_read ==
                       kCheckpointByteCap - 1 &&
                   below_cap_status.indexes.front().proof_usn_span ==
                       kCheckpointUsnSpanCap - 1 &&
                   !below_cap_status.indexes.front().proof_checkpoint_required &&
                   !below_cap_status.indexes.front().proof_checkpoint_persisted,
            "all proof counters one below their caps must not require a checkpoint");

        const auto expect_exact_cap_crossing = [&](
            const kano::backlog_ops::GetIndexStatusTestHooks& hooks,
            const std::size_t expected_records,
            const std::size_t expected_bytes,
            const std::uint64_t expected_span,
            const std::string& dimension
        ) {
            kano::backlog_ops::GetIndexStatusResult status;
            {
                ImmediateWriteLock write_lock(index_path);
                status = kano::backlog_ops::get_index_status(
                    backlog_root,
                    product,
                    product_root,
                    baseline_product_revision,
                    hooks);
            }
            expect(status.indexes.size() == 1 &&
                       status.indexes.front().status == "stale" &&
                       !status.indexes.front().unchanged &&
                       !status.indexes.front().fallback_scan &&
                       status.indexes.front().scanned_count == 0 &&
                       status.indexes.front().proof_records_read == expected_records &&
                       status.indexes.front().proof_bytes_read == expected_bytes &&
                       status.indexes.front().proof_usn_span == expected_span &&
                       status.indexes.front().proof_checkpoint_required &&
                       !status.indexes.front().proof_checkpoint_persisted &&
                       status.indexes.front().stale_reason &&
                       *status.indexes.front().stale_reason ==
                           "change_proof_checkpoint_commit_failed" &&
                       status.indexes.front().elapsed_ms < 100.0,
                dimension +
                    " equality must require zero-wait checkpoint persistence and fail closed");
        };

        kano::backlog_ops::GetIndexStatusTestHooks record_cap_hooks;
        record_cap_hooks.checkpoint_record_cap = kCheckpointRecordCap;
        record_cap_hooks.checkpoint_byte_cap = kCheckpointByteCap;
        record_cap_hooks.checkpoint_usn_span_cap = kCheckpointUsnSpanCap;
        record_cap_hooks.proof_records_read_override = kCheckpointRecordCap;
        record_cap_hooks.proof_bytes_read_override = kCheckpointByteCap - 1;
        record_cap_hooks.proof_usn_span_override = kCheckpointUsnSpanCap - 1;
        expect_exact_cap_crossing(
            record_cap_hooks,
            kCheckpointRecordCap,
            kCheckpointByteCap - 1,
            kCheckpointUsnSpanCap - 1,
            "record counter cap");

        kano::backlog_ops::GetIndexStatusTestHooks byte_cap_hooks;
        byte_cap_hooks.checkpoint_record_cap = kCheckpointRecordCap;
        byte_cap_hooks.checkpoint_byte_cap = kCheckpointByteCap;
        byte_cap_hooks.checkpoint_usn_span_cap = kCheckpointUsnSpanCap;
        byte_cap_hooks.proof_records_read_override = kCheckpointRecordCap - 1;
        byte_cap_hooks.proof_bytes_read_override = kCheckpointByteCap;
        byte_cap_hooks.proof_usn_span_override = kCheckpointUsnSpanCap - 1;
        expect_exact_cap_crossing(
            byte_cap_hooks,
            kCheckpointRecordCap - 1,
            kCheckpointByteCap,
            kCheckpointUsnSpanCap - 1,
            "byte counter cap");

        kano::backlog_ops::GetIndexStatusTestHooks span_cap_hooks;
        span_cap_hooks.checkpoint_record_cap = kCheckpointRecordCap;
        span_cap_hooks.checkpoint_byte_cap = kCheckpointByteCap;
        span_cap_hooks.checkpoint_usn_span_cap = kCheckpointUsnSpanCap;
        span_cap_hooks.proof_records_read_override = kCheckpointRecordCap - 1;
        span_cap_hooks.proof_bytes_read_override = kCheckpointByteCap - 1;
        span_cap_hooks.proof_usn_span_override = kCheckpointUsnSpanCap;
        expect_exact_cap_crossing(
            span_cap_hooks,
            kCheckpointRecordCap - 1,
            kCheckpointByteCap - 1,
            kCheckpointUsnSpanCap,
            "USN span cap");

        kano::backlog_ops::GetIndexStatusTestHooks cap_hooks;
        cap_hooks.checkpoint_record_cap = 1;
        kano::backlog_ops::GetIndexStatusResult blocked_checkpoint_status;
        {
            ImmediateWriteLock write_lock(index_path);
            blocked_checkpoint_status = kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                baseline_product_revision,
                cap_hooks);
        }
        expect(blocked_checkpoint_status.indexes.size() == 1 &&
                   blocked_checkpoint_status.indexes.front().status == "stale" &&
                   !blocked_checkpoint_status.indexes.front().unchanged &&
                   !blocked_checkpoint_status.indexes.front().fallback_scan &&
                   blocked_checkpoint_status.indexes.front().scanned_count == 0 &&
                   blocked_checkpoint_status.indexes.front().stale_reason &&
                   *blocked_checkpoint_status.indexes.front().stale_reason ==
                       "change_proof_checkpoint_commit_failed" &&
                   blocked_checkpoint_status.indexes.front()
                        .proof_checkpoint_required &&
                   !blocked_checkpoint_status.indexes.front()
                         .proof_checkpoint_persisted &&
                   blocked_checkpoint_status.indexes.front().elapsed_ms < 100.0,
            "cap crossing must fail closed when checkpoint CAS is unavailable");
        expect(read_verified_usn(index_path, product) == durable_usn_before,
            "failed mandatory checkpoint CAS must not advance durable proof state");

        const auto committed_checkpoint_status =
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                baseline_product_revision,
                cap_hooks);
        expect(committed_checkpoint_status.indexes.size() == 1 &&
                   committed_checkpoint_status.indexes.front().status == "unchanged" &&
                   committed_checkpoint_status.indexes.front().unchanged &&
                   committed_checkpoint_status.indexes.front()
                       .proof_checkpoint_required &&
                   committed_checkpoint_status.indexes.front()
                       .proof_checkpoint_persisted,
            "cap crossing must persist the verified checkpoint before unchanged");
        expect(read_verified_usn(index_path, product) > durable_usn_before,
            "successful mandatory checkpoint CAS must advance durable proof state");
        std::filesystem::remove_all(unrelated_churn_root);

        std::vector<double> unchanged_status_samples;
        for (int sample = 0; sample < 30; ++sample) {
            kano::backlog_ops::GetIndexStatusResult unchanged;
            unchanged_status_samples.push_back(timed_ms([&]() {
                unchanged = kano::backlog_ops::get_index_status(
                    backlog_root,
                    product,
                    product_root,
                    baseline_product_revision);
            }));
            expect(unchanged.indexes.size() == 1 &&
                       unchanged.indexes.front().status == "unchanged" &&
                       unchanged.indexes.front().unchanged &&
                       unchanged.indexes.front().product_revision ==
                           baseline_product_revision &&
                       unchanged.indexes.front().scanned_count == 0,
                "matching requested revision must avoid canonical inventory scans");
        }
        unchanged_status_p95_ms = percentile_95(unchanged_status_samples);
        expect(unchanged_status_p95_ms < 100.0,
            "650-item unchanged status p95 must remain below 100 ms (actual " +
                std::to_string(unchanged_status_p95_ms) + " ms)");
#else
        const auto unsupported_revision_status =
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                baseline_product_revision);
        expect(unsupported_revision_status.indexes.size() == 1 &&
                   unsupported_revision_status.indexes.front().status == "stale" &&
                   !unsupported_revision_status.indexes.front().unchanged &&
                   !unsupported_revision_status.indexes.front().fallback_scan &&
                   unsupported_revision_status.indexes.front().scanned_count == 0 &&
                   unsupported_revision_status.indexes.front().proof_records_read == 0 &&
                   unsupported_revision_status.indexes.front().proof_bytes_read == 0 &&
                   unsupported_revision_status.indexes.front().proof_usn_span == 0 &&
                   unsupported_revision_status.indexes.front().stale_reason &&
                   *unsupported_revision_status.indexes.front().stale_reason ==
                       "change_proof_unsupported",
            "portable equal revision checks must fail closed without scans or proof work");
#endif

        const auto fresh = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect_exact(fresh, target_id, fixtures.back().title);
        expect(fresh.diagnostics.index_used &&
                   fresh.diagnostics.index_status == "ready" &&
                   !fresh.diagnostics.fallback_scan,
            "fresh read must use the validated metadata index");
        expect(fresh.diagnostics.index_revision ==
                   fresh.diagnostics.canonical_revision,
            "fresh index and canonical revisions must agree");
        expect(fresh.diagnostics.scanned_count == 1,
            "exact indexed lookup must report bounded row scan work");
        expect(fresh.items.front().uid == target_uid &&
                   fresh.items.front().product == product &&
                   fresh.items.front().estimated_tokens > 0,
            "indexed identity, product, and token metadata must remain intact");
        expect(!std::filesystem::path(fresh.items.front().source_ref).is_absolute() &&
                   fresh.items.front().source_ref.find("..") == std::string::npos &&
                   fresh.items.front().source_ref.find(fixture_root.string()) ==
                       std::string::npos,
            "metadata index must expose only bounded product-relative source refs");
        {
            BacklogIndex unscoped(index_path);
            const auto id_path = unscoped.get_path_by_id(target_id);
            const auto uid_path = unscoped.get_path_by_uid(target_uid);
            expect(id_path && uid_path &&
                       id_path->lexically_normal() ==
                           fixtures.back().file_path->lexically_normal() &&
                       uid_path->lexically_normal() ==
                           fixtures.back().file_path->lexically_normal(),
                "shared unscoped lookups must materialize the product canonical path");
        }

        IndexQuery all_query;
        const auto all_fresh = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, all_query);
        const auto canonical_fresh = canonical_projection(product_root);
        expect(all_fresh.items.size() == canonical_fresh.size(),
            "indexed list count must match canonical metadata");
        for (const auto& indexed : all_fresh.items) {
            const auto canonical = canonical_fresh.find(indexed.id);
            expect(canonical != canonical_fresh.end() &&
                       canonical->second.first == indexed.state &&
                       canonical->second.second == indexed.title,
                "indexed list state/title must match canonical metadata");
        }

        std::vector<double> exact_samples;
        std::vector<double> revision_samples;
#ifdef _WIN32
        constexpr int kExactSamples = 30;
        constexpr double kExactP95LimitMs = 100.0;
        constexpr double kRevisionP95LimitMs = 100.0;
#else
        constexpr int kExactSamples = 3;
        constexpr double kExactP95LimitMs = 2000.0;
        constexpr double kRevisionP95LimitMs = 2000.0;
#endif
        for (int sample = 0; sample < kExactSamples; ++sample) {
            IndexQueryResult result;
            exact_samples.push_back(timed_ms([&]() {
                result = kano::backlog_ops::query_metadata_index(
                    index_path, product_root, product, exact_query);
            }));
            expect(result.diagnostics.index_used, "exact benchmark must use the index");
            revision_samples.push_back(result.diagnostics.revision_check_ms);
        }
        const auto exact_p95_ms = percentile_95(exact_samples);
        const auto revision_p95_ms = percentile_95(revision_samples);
        expect(exact_p95_ms < kExactP95LimitMs,
            "650-item exact lookup p95 exceeded the platform budget (actual " +
                std::to_string(exact_p95_ms) + " ms)");
        expect(revision_p95_ms < kRevisionP95LimitMs,
            "650-item canonical revision check p95 exceeded the platform budget (actual " +
                std::to_string(revision_p95_ms) + " ms)");

        IndexQuery metadata_query;
        metadata_query.type = ItemType::Task;
        metadata_query.state = ItemState::New;
        std::vector<double> metadata_samples;
        for (int sample = 0; sample < 12; ++sample) {
            metadata_samples.push_back(timed_ms([&]() {
                const auto result = kano::backlog_ops::query_metadata_index(
                    index_path, product_root, product, metadata_query);
                expect(result.items.size() == 650,
                    "metadata filter must preserve canonical parity");
            }));
        }
        const auto metadata_p95_ms = percentile_95(metadata_samples);
        expect(metadata_p95_ms < 500.0,
            "650-item metadata query p95 must remain below 500 ms");

        IndexQuery token_query;
        token_query.text = "needle-0649";
        token_query.limit = 20;
        std::vector<double> token_samples;
        for (int sample = 0; sample < 8; ++sample) {
            token_samples.push_back(timed_ms([&]() {
                const auto result = kano::backlog_ops::query_metadata_index(
                    index_path, product_root, product, token_query);
                expect(result.items.size() == 1 &&
                           result.items.front().id == "MDI-TSK-0649",
                    "bounded token query must return the canonical match");
            }));
        }
        const auto token_p95_ms = percentile_95(token_samples);
        expect(token_p95_ms < 2000.0,
            "650-item bounded token query p95 must remain below 2 seconds");

        constexpr std::size_t kContractFixturePrefixCount = 32;
        constexpr std::size_t kContractFixtureCount =
            kContractFixturePrefixCount + 1;
        for (std::size_t index = kContractFixturePrefixCount;
             index + 1 < fixtures.size();
             ++index) {
            expect(std::filesystem::remove(*fixtures[index].file_path),
                "contract fixture compaction must remove the source file");
        }
        store.reset_write_revision();
        {
            BacklogIndex compacted(index_path, product, product_root);
            compacted.rebuild_metadata(product_root, product);
        }
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "contract fixture compaction");

        {
        BacklogIndex index(index_path, product, product_root);
        index.invalidate_metadata(product, "fixture_explicit_invalidation");
        const auto invalidated = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect(!invalidated.diagnostics.index_used &&
                   invalidated.diagnostics.index_status == "stale" &&
                   invalidated.diagnostics.stale_reason &&
                   *invalidated.diagnostics.stale_reason ==
                       "fixture_explicit_invalidation",
            "explicit invalidation must fail over to canonical metadata");
        index.rebuild_metadata(product_root, product);

        auto target = store.read(*fixtures.back().file_path);
        target.title = "Canonical mutation title";
        store.write(target);
        index.index_item(target);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "tracked canonical update");
        const auto updated = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect_exact(updated, target_id, target.title);
        expect(updated.diagnostics.index_used,
            "tracked canonical update must keep a healthy snapshot readable");

        index.sync_sequences(product_root);
        kano::backlog_ops::DuplicateAdmissionEvidence admission;
        admission.search_query = "Lifecycle-created metadata item";
        admission.search_scope = product;
        admission.decision = "create";
        const auto created = WorkitemOps::create_item(
            index,
            product_root,
            "MDI",
            ItemType::Task,
            "Lifecycle-created metadata item",
            "metadata-index-test",
            std::nullopt,
            "P2",
            {},
            "general",
            "backlog",
            std::nullopt,
            std::nullopt,
            "",
            "",
            admission);
        IndexQuery created_query;
        created_query.exact_ref = created.id;
        created_query.limit = 1;
        const auto created_read = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, created_query);
        expect_exact(created_read, created.id, "Lifecycle-created metadata item");
        expect(created_read.diagnostics.index_used,
            "newly created item must be published to a healthy index");
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "item create");

        auto parent_feature = store.create(
            "MDI", ItemType::Feature, "Metadata lifecycle parent", 1);
        store.write(parent_feature);
        index.index_item(parent_feature);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "parent fixture create");
        WorkitemOps::remap_parent(
            index, product_root, created.id, parent_feature.id, "metadata-index-test");
        const auto reparented = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, created_query);
        expect(reparented.items.front().parent &&
                   *reparented.items.front().parent == parent_feature.id,
            "reparent mutation must update indexed parent metadata");
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "reparent");

        auto created_item = store.read(
            *store.find_item_path_by_id(created.id));
        set_ready_fields(created_item);
        created_item.state = ItemState::Ready;
        store.write(created_item);
        index.index_item(created_item);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "ready fixture update");
        const auto started = WorkitemOps::transition_state_action(
            product_root,
            created.id,
            kano::backlog_core::StateAction::Start,
            std::string("metadata-index-test"),
            std::string("Exercise indexed state transition"),
            std::nullopt,
            &index);
        expect(started.state == ItemState::InProgress,
            "state transition fixture must start");
        const auto started_read = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, created_query);
        expect(started_read.diagnostics.index_used &&
                   started_read.items.front().state == ItemState::InProgress,
            "state transition must publish current indexed state");
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "state transition");

        (void)WorkitemOps::add_decision_writeback(
            index,
            product_root,
            created.id,
            "Keep canonical metadata authoritative.",
            "metadata-index-test",
            std::string("fixture"));
        expect(kano::backlog_ops::query_metadata_index(
                   index_path, product_root, product, created_query)
                   .diagnostics.index_used,
            "decision writeback must preserve a healthy derived snapshot");
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "decision writeback");

        auto worklog_item = store.read(
            *store.find_item_path_by_id(created.id));
        kano::backlog_core::StateMachine::record_worklog(
            worklog_item,
            "metadata-index-test",
            "Revision contract worklog mutation");
        store.write(worklog_item);
        index.index_item(worklog_item);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "worklog append");

        auto relation_item = store.read(
            *store.find_item_path_by_id(created.id));
        relation_item.links.relates.push_back(target_id);
        kano::backlog_core::StateMachine::record_worklog(
            relation_item,
            "metadata-index-test",
            "Revision contract relation mutation");
        store.write(relation_item);
        index.index_item(relation_item);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "relation update");

        auto artifact_item = store.read(
            *store.find_item_path_by_id(created.id));
        kano::backlog_core::StateMachine::record_worklog(
            artifact_item,
            "metadata-index-test",
            "Artifact attached: [revision-evidence.txt](revision-evidence.txt)");
        store.write(artifact_item);
        index.index_item(artifact_item);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "artifact worklog update");

        bool rejected_reparent = false;
        try {
            WorkitemOps::remap_parent(
                index,
                product_root,
                created.id,
                created.id,
                "metadata-index-test");
        } catch (const std::exception&) {
            rejected_reparent = true;
        }
        expect(rejected_reparent, "invalid reparent fixture must be rejected");
        expect_product_revision_unchanged(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rejected reparent");

        auto mismatched_item = store.read(
            *store.find_item_path_by_id(created.id));
        mismatched_item.title = "Unwritten mutation must fail readback";
        bool readback_failed = false;
        try {
            index.index_item(mismatched_item);
        } catch (const std::exception&) {
            readback_failed = true;
        }
        expect(readback_failed,
            "index mutation must reject a canonical readback mismatch");
        const auto failed_readback_status = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision);
        expect_redacted_requested_status(
            failed_readback_status,
            "metadata_snapshot_not_ready",
            "failed mutation readback requested-revision check");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after failed readback");

        const auto removed_id = fixtures[10].id;
        store.remove_file(*fixtures[10].file_path);
        index.remove_item(removed_id);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "tracked delete");
        const auto after_tracked_delete = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, all_query);
        expect(after_tracked_delete.diagnostics.index_used &&
                   after_tracked_delete.items.size() ==
                       kContractFixtureCount + 1,
            "tracked deletion must remove one row without invalidating current data");

        target = store.read(*target.file_path);
        target.title = "Canonical out-of-band title";
        store.write(target);
        const auto out_of_band_status = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision);
        expect(out_of_band_status.indexes.size() == 1 &&
                   out_of_band_status.indexes.front().status == "stale" &&
                   !out_of_band_status.indexes.front().unchanged &&
                   !out_of_band_status.indexes.front().fallback_scan &&
                   out_of_band_status.indexes.front().scanned_count == 0 &&
                   out_of_band_status.indexes.front().product_revision ==
                       current_product_revision &&
                   out_of_band_status.indexes.front().stale_reason &&
                   out_of_band_status.indexes.front().stale_reason->find(
                       fixture_root.string()) == std::string::npos &&
                   out_of_band_status.indexes.front().recovery ==
                       "kob index rebuild --product " + product,
            "direct out-of-band drift must fail closed without advancing revision");
        const auto stale = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect_exact(stale, target_id, target.title);
        expect(!stale.diagnostics.index_used &&
                   stale.diagnostics.index_status == "stale" &&
                   stale.diagnostics.fallback_scan,
            "out-of-band canonical change must use canonical fallback");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after out-of-band update");

        std::filesystem::remove(*fixtures[11].file_path);
        const auto after_untracked_delete = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, all_query);
        expect(!after_untracked_delete.diagnostics.index_used &&
                   after_untracked_delete.diagnostics.index_status == "stale" &&
                   after_untracked_delete.items.size() == kContractFixtureCount,
            "untracked deletion must not leave an orphaned row authoritative");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after untracked delete");

        target = store.read(*target.file_path);
        target.title = "Canonical source hash marker A";
        store.write(target);
        index.index_item(target);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "tracked source hash update");
        const auto original_time = std::filesystem::last_write_time(*target.file_path);
        auto raw = read_text(*target.file_path);
        const auto marker = raw.find("Canonical source hash marker A");
        expect(marker != std::string::npos, "source hash marker must be serialized");
        raw.replace(
            marker,
            std::string("Canonical source hash marker A").size(),
            "Canonical source hash marker B");
        write_text(*target.file_path, raw);
        std::filesystem::last_write_time(*target.file_path, original_time);
#ifdef _WIN32
        const std::string raw_drift_reason = "change_proof_relevant_change";
#else
        const std::string raw_drift_reason = "canonical_content_changed";
#endif
        const auto raw_edit_status = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision);
        expect(raw_edit_status.indexes.size() == 1 &&
                   raw_edit_status.indexes.front().status == "stale" &&
                   !raw_edit_status.indexes.front().unchanged &&
                   !raw_edit_status.indexes.front().fallback_scan &&
                   raw_edit_status.indexes.front().scanned_count == 0 &&
                   raw_edit_status.indexes.front().product_revision ==
                       current_product_revision &&
                   raw_edit_status.indexes.front().stale_reason &&
                   raw_edit_status.indexes.front().recovery ==
                       "kob index rebuild --product " + product,
            "raw same-size/restored-mtime edit must never return unchanged");
        const auto raw_full_status = kano::backlog_ops::get_index_status(
            backlog_root, product, product_root);
        expect(raw_full_status.indexes.size() == 1
                && raw_full_status.indexes.front().status == "stale"
                && raw_full_status.indexes.front().stale_reason
                && *raw_full_status.indexes.front().stale_reason
                    == raw_drift_reason,
            "full status must reject raw canonical content drift");
        const auto hash_fallback = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect_exact(
            hash_fallback, target_id, "Canonical source hash marker B");
        expect(!hash_fallback.diagnostics.index_used &&
                    hash_fallback.diagnostics.index_status == "stale" &&
                    hash_fallback.diagnostics.stale_reason &&
                    *hash_fallback.diagnostics.stale_reason ==
                        raw_drift_reason,
            "exact lookup must reject same-size/mtime canonical content drift");
        const auto hash_doctor = index.doctor_metadata(product_root, product, true);
        expect(!hash_doctor.healthy &&
                   hash_doctor.source_hash_mismatches == 1 &&
                   hash_doctor.diagnostics.stale_reason,
            "deep doctor must detect content drift hidden from cheap inventory metadata");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after source hash drift");

        {
            BacklogIndex restarted(index_path, product, product_root);
            const auto restarted_query = restarted.query_metadata(
                product_root, product, exact_query);
            expect(restarted_query.diagnostics.index_used,
                "reopened metadata index must use persisted readiness state");
        }
        expect_product_revision_unchanged(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "restart with persisted readiness state");

        std::string mismatched_revision =
            "kob-pr-v1:fnv1a64:0000000000000000:1";
        if (mismatched_revision == current_product_revision) {
            mismatched_revision = "kob-pr-v1:fnv1a64:1111111111111111:1";
        }
        const auto valid_mismatch = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            mismatched_revision);
        expect(valid_mismatch.indexes.size() == 1 &&
                   valid_mismatch.indexes.front().status == "ready" &&
                   !valid_mismatch.indexes.front().unchanged &&
                   !valid_mismatch.indexes.front().fallback_scan &&
                   valid_mismatch.indexes.front().scanned_count == 0 &&
                   valid_mismatch.indexes.front().item_count ==
                       static_cast<int>(kContractFixtureCount) &&
                   !valid_mismatch.indexes.front().index_revision.empty() &&
                   valid_mismatch.indexes.front().canonical_revision ==
                       valid_mismatch.indexes.front().index_revision &&
                   valid_mismatch.indexes.front().product_revision ==
                       current_product_revision &&
                   !valid_mismatch.indexes.front().stale_reason,
            "valid mismatched revision must return only a structurally safe ready snapshot");

        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET schema_version = schema_version + 1, "
            "index_revision = '../../private/index', "
            "content_revision = '../../private/content', "
            "revision_epoch = '../../private/epoch', "
            "product_revision = '../../private/product', "
            "canonical_write_revision = '../../private/write', "
            "reason = '../../private/reason' WHERE product = 'metadata-product'");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                mismatched_revision),
            "metadata_snapshot_schema_mismatch",
            "schema-corrupt requested-revision mismatch");
        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET revision_epoch = '' "
            "WHERE product = 'metadata-product'");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after schema-corrupt requested-revision mismatch");

        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET status = 'incomplete', "
            "reason = '../../private/persisted-reason' "
            "WHERE product = 'metadata-product'");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                mismatched_revision),
            "metadata_snapshot_not_ready",
            "non-ready requested-revision mismatch");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after non-ready requested-revision mismatch");

        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET index_revision = '../../private/index', "
            "content_revision = 'fnv1a64:not-hexadecimal', "
            "canonical_write_revision = '../../private/write', "
            "product_revision = '../../private/product', "
            "reason = '../../private/persisted-reason' "
            "WHERE product = 'metadata-product'");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                mismatched_revision),
            "change_proof_malformed",
            "path-tainted requested-revision mismatch");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after path-tainted requested-revision mismatch");

        execute_sql(
            index_path,
            "BEGIN IMMEDIATE;"
            "DELETE FROM metadata_change_watch WHERE product = 'metadata-product';"
            "INSERT INTO metadata_change_watch "
            "(product, source_ref, file_id, is_directory) VALUES "
            "('metadata-product', 'items/task/0000/item.md', 'not-a-file-id', 0);"
            "COMMIT;");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                mismatched_revision),
            "change_proof_watch_invalid",
            "malformed watch requested-revision mismatch");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after malformed watch requested-revision mismatch");

        execute_sql(
            index_path,
            "BEGIN IMMEDIATE;"
            "DELETE FROM metadata_change_watch WHERE product = 'metadata-product';"
            "INSERT INTO metadata_change_watch "
            "(product, source_ref, file_id, is_directory) VALUES "
            "('metadata-product', 'C:private/item.md', '1', 0);"
            "COMMIT;");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                mismatched_revision),
            "change_proof_watch_invalid",
            "drive-relative watch requested-revision mismatch");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after drive-relative watch requested-revision mismatch");

#ifdef _WIN32
        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET proof_journal_id = "
            "'18446744073709551615' WHERE product = 'metadata-product'");
        expect_stale_reason(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "change_proof_journal_reset");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after journal reset proof");

        execute_sql(
            index_path,
            "UPDATE metadata_snapshots SET proof_kind = 'invalid-proof' "
            "WHERE product = 'metadata-product'");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                current_product_revision),
            "change_proof_malformed",
            "malformed proof requested-revision check");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after malformed proof");

        execute_sql(
            index_path,
            "DELETE FROM metadata_change_watch WHERE product = 'metadata-product'");
        expect_redacted_requested_status(
            kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                current_product_revision),
            "change_proof_watch_invalid",
            "missing proof watch requested-revision check");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after missing proof watch set");
#else
        expect_portable_proof_state(
            index_path, product, "portable restart validation");
#endif

        const auto rebuild_window_time =
            std::filesystem::last_write_time(*target.file_path);
        const auto rebuild_window_original = read_text(*target.file_path);
        auto rebuild_window_changed = rebuild_window_original;
        const auto rebuild_marker = rebuild_window_changed.find(
            "Canonical source hash marker B");
        expect(rebuild_marker != std::string::npos,
            "rebuild-window marker must be present");
        rebuild_window_changed.replace(
            rebuild_marker,
            std::string("Canonical source hash marker B").size(),
            "Canonical source hash marker C");
        BacklogIndex::RebuildMetadataTestHooks rebuild_hooks;
        rebuild_hooks.after_change_watch_capture = [&]() {
            write_text(*target.file_path, rebuild_window_changed);
            std::filesystem::last_write_time(
                *target.file_path, rebuild_window_time);
        };
        std::string rebuild_window_error;
        try {
            index.rebuild_metadata(product_root, product, rebuild_hooks);
        } catch (const std::exception& error) {
            rebuild_window_error = error.what();
        }
#ifdef _WIN32
        const std::string rebuild_window_reason =
            "metadata_index_canonical_changed_during_rebuild:"
            "change_proof_relevant_change";
#else
        const std::string rebuild_window_reason =
            "metadata_index_canonical_changed_during_rebuild:"
            "canonical_content_changed";
#endif
        expect(rebuild_window_error.starts_with(
                   rebuild_window_reason),
            "rebuild publication must reject a raw edit inside its validation window");
        write_text(*target.file_path, rebuild_window_original);
        std::filesystem::last_write_time(*target.file_path, rebuild_window_time);
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after rejected raw proof window");

        const auto write_revision_path =
            product_root / ".cache" / "canonical-write-revision-v1";
#ifdef _WIN32
        const std::string requested_write_revision_failure =
            "canonical_write_revision_unavailable";
#else
        const std::string requested_write_revision_failure =
            "change_proof_unsupported";
#endif
        std::filesystem::remove(write_revision_path);
        const auto missing_write_revision = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision);
        expect(missing_write_revision.indexes.size() == 1 &&
                   missing_write_revision.indexes.front().status == "stale" &&
                   !missing_write_revision.indexes.front().unchanged &&
                   !missing_write_revision.indexes.front().fallback_scan &&
                   missing_write_revision.indexes.front().scanned_count == 0 &&
                   missing_write_revision.indexes.front().product_revision ==
                       current_product_revision &&
                   missing_write_revision.indexes.front().stale_reason &&
                   *missing_write_revision.indexes.front().stale_reason ==
                       requested_write_revision_failure &&
                   missing_write_revision.indexes.front().recovery ==
                       "kob index rebuild --product " + product,
            "missing canonical write revision must fail closed");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after missing canonical write revision");

        write_text(write_revision_path, "invalid-write-revision\n");
        const auto corrupt_write_revision = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision);
        expect(corrupt_write_revision.indexes.size() == 1 &&
                   corrupt_write_revision.indexes.front().status == "stale" &&
                   !corrupt_write_revision.indexes.front().unchanged &&
                   !corrupt_write_revision.indexes.front().fallback_scan &&
                   corrupt_write_revision.indexes.front().scanned_count == 0 &&
                   corrupt_write_revision.indexes.front().product_revision ==
                       current_product_revision &&
                   corrupt_write_revision.indexes.front().stale_reason &&
                   *corrupt_write_revision.indexes.front().stale_reason ==
                       requested_write_revision_failure &&
                   corrupt_write_revision.indexes.front().stale_reason->find(
                       fixture_root.string()) == std::string::npos &&
                   corrupt_write_revision.indexes.front().recovery ==
                       "kob index rebuild --product " + product,
            "corrupt canonical write revision must fail closed with bounded diagnostics");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after corrupt canonical write revision");
        expect_product_revision_unchanged(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "restart with persisted canonical write revision");

        const auto incomplete_path =
            backlog_root / ".cache" / "index" / "incomplete.db";
        {
            BacklogIndex incomplete(incomplete_path, product, product_root);
            incomplete.initialize();
        }
        const auto incomplete = kano::backlog_ops::query_metadata_index(
            incomplete_path, product_root, product, exact_query);
        expect(!incomplete.diagnostics.index_used &&
                   incomplete.diagnostics.index_status == "missing" &&
                   incomplete.diagnostics.fallback_scan,
            "incomplete snapshot must fall back to canonical metadata");

        const auto corrupt_path =
            backlog_root / ".cache" / "index" / "corrupt.db";
        write_text(corrupt_path, "not-a-sqlite-index");
        const auto corrupt = kano::backlog_ops::query_metadata_index(
            corrupt_path, product_root, product, all_query);
        expect(!corrupt.diagnostics.index_used &&
                   corrupt.diagnostics.index_status == "corrupt" &&
                   corrupt.diagnostics.fallback_scan &&
                   corrupt.items.size() == kContractFixtureCount,
            "corrupt index must fall back without omitting canonical items");

        const std::string second_product = "metadata-product-two";
        const auto second_root = backlog_root / "products" / second_product;
        CanonicalStore second_store(second_root);
        auto second_item = second_store.create(
            "MDT", ItemType::Task, "Second product isolated item", 1);
        second_store.write(second_item);
        auto second_removed_item = second_store.create(
            "MDT", ItemType::Task, "Second product removable item", 2);
        second_store.write(second_removed_item);
        const auto second_build = kano::backlog_ops::build_index(
            second_root, index_path, true, second_product);
        expect(second_build.items_indexed == 2,
            "second product rebuild must remain product-scoped");
        IndexQuery second_exact;
        second_exact.exact_ref = second_item.id;
        second_exact.limit = 1;
        const auto second_result = kano::backlog_ops::query_metadata_index(
            index_path, second_root, second_product, second_exact);
        expect_exact(second_result, second_item.id, second_item.title);
        const auto first_result = kano::backlog_ops::query_metadata_index(
            index_path, product_root, product, exact_query);
        expect(first_result.diagnostics.index_used &&
                   first_result.items.front().product == product,
            "shared index database must preserve product isolation");
        expect_product_revision_unchanged(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "unrelated product rebuild");

#ifdef _WIN32
        execute_sql(
            index_path,
            "BEGIN IMMEDIATE;"
            "UPDATE metadata_snapshots SET proof_kind = 'none', "
            "proof_status = 'unsupported', proof_root_volume_serial = '0', "
            "proof_root_file_id = '0', proof_journal_id = '0', "
            "proof_verified_usn = 0 WHERE product = '" + second_product + "';"
            "DELETE FROM metadata_change_watch WHERE product = '" +
                second_product + "';"
            "COMMIT;");
#endif
        expect_portable_proof_state(
            index_path, second_product, "portable snapshot setup");
        const auto portable_query = kano::backlog_ops::query_metadata_index(
            index_path, second_root, second_product, second_exact);
        expect_exact(portable_query, second_item.id, second_item.title);
        expect(portable_query.diagnostics.index_used &&
                   portable_query.diagnostics.index_status == "ready" &&
                   !portable_query.diagnostics.fallback_scan,
            "ordinary portable query must validate canonical content before using rows");
        const auto portable_doctor = kano::backlog_ops::doctor_metadata_index(
            index_path, second_root, second_product, false);
        expect(portable_doctor.healthy &&
                   portable_doctor.diagnostics.index_status == "ready" &&
                   portable_doctor.diagnostics.index_used,
            "portable doctor must accept coherent canonical inventory and content");
        const auto portable_equal_revision = kano::backlog_ops::get_index_status(
            backlog_root,
            second_product,
            second_root,
            second_build.product_revision);
        expect(portable_equal_revision.indexes.size() == 1 &&
                   portable_equal_revision.indexes.front().status == "stale" &&
                   !portable_equal_revision.indexes.front().unchanged &&
                   !portable_equal_revision.indexes.front().fallback_scan &&
                   portable_equal_revision.indexes.front().scanned_count == 0 &&
                   portable_equal_revision.indexes.front().proof_records_read == 0 &&
                   portable_equal_revision.indexes.front().proof_bytes_read == 0 &&
                   portable_equal_revision.indexes.front().proof_usn_span == 0 &&
                   portable_equal_revision.indexes.front().stale_reason &&
                   *portable_equal_revision.indexes.front().stale_reason ==
                       "change_proof_unsupported",
            "portable equal revision checks must fail closed without proof or fallback work");

        BacklogIndex portable_index(index_path, second_product, second_root);
        second_item = second_store.read(*second_item.file_path);
        second_item.title = "Portable source marker A";
        second_store.write(second_item);
        portable_index.index_item(second_item);
        const auto portable_updated = kano::backlog_ops::query_metadata_index(
            index_path, second_root, second_product, second_exact);
        expect_exact(portable_updated, second_item.id, second_item.title);
        expect(portable_updated.diagnostics.index_used,
            "portable tracked update must preserve ready index rows");
        expect_portable_proof_state(
            index_path, second_product, "portable tracked update");

        second_store.remove_file(*second_removed_item.file_path);
        portable_index.remove_item(second_removed_item.id);
        IndexQuery portable_all;
        const auto portable_after_delete = kano::backlog_ops::query_metadata_index(
            index_path, second_root, second_product, portable_all);
        expect(portable_after_delete.diagnostics.index_used &&
                   portable_after_delete.items.size() == 1,
            "portable tracked delete must preserve one coherent ready row");
        expect_portable_proof_state(
            index_path, second_product, "portable tracked delete");

        const auto portable_original_time =
            std::filesystem::last_write_time(*second_item.file_path);
        auto portable_raw = read_text(*second_item.file_path);
        const auto portable_marker = portable_raw.find("Portable source marker A");
        expect(portable_marker != std::string::npos,
            "portable raw-content marker must be serialized");
        portable_raw.replace(
            portable_marker,
            std::string("Portable source marker A").size(),
            "Portable source marker B");
        write_text(*second_item.file_path, portable_raw);
        std::filesystem::last_write_time(
            *second_item.file_path, portable_original_time);
        const auto portable_raw_query = kano::backlog_ops::query_metadata_index(
            index_path, second_root, second_product, second_exact);
        expect_exact(
            portable_raw_query, second_item.id, "Portable source marker B");
        expect(!portable_raw_query.diagnostics.index_used &&
                   portable_raw_query.diagnostics.index_status == "stale" &&
                   portable_raw_query.diagnostics.fallback_scan &&
                   portable_raw_query.diagnostics.stale_reason &&
                   *portable_raw_query.diagnostics.stale_reason ==
                       "canonical_content_changed",
            "portable query must reject raw content drift before trusting rows");
        const auto portable_raw_doctor = kano::backlog_ops::doctor_metadata_index(
            index_path, second_root, second_product, false);
        expect(!portable_raw_doctor.healthy &&
                   portable_raw_doctor.diagnostics.stale_reason &&
                   *portable_raw_doctor.diagnostics.stale_reason ==
                       "canonical_content_changed",
            "portable doctor must reject raw content drift without proof fallback");

#ifdef _WIN32
        kano::backlog_ops::GetIndexStatusTestHooks revision_race_hooks;
        revision_race_hooks.after_revision_state_read = [&]() {
            execute_sql(
                index_path,
                "BEGIN IMMEDIATE;"
                "UPDATE metadata_snapshots SET generation = generation + 1, "
                "product_revision = 'kob-pr-v1:' || revision_epoch || ':' || "
                "CAST(generation + 1 AS TEXT) WHERE product = '" + product + "';"
                "DELETE FROM metadata_change_watch WHERE product = '" + product + "';"
                "COMMIT;");
        };
        const auto revision_race_status = kano::backlog_ops::get_index_status(
            backlog_root,
            product,
            product_root,
            current_product_revision,
            revision_race_hooks);
        expect(revision_race_status.indexes.size() == 1 &&
                   revision_race_status.indexes.front().status == "stale" &&
                   !revision_race_status.indexes.front().unchanged &&
                   !revision_race_status.indexes.front().fallback_scan &&
                   revision_race_status.indexes.front().scanned_count == 0 &&
                   revision_race_status.indexes.front().stale_reason &&
                   *revision_race_status.indexes.front().stale_reason ==
                        "change_proof_snapshot_changed" &&
                   revision_race_status.indexes.front().elapsed_ms < 100.0,
            "concurrent snapshot/watch replacement must fail closed without mixing revisions");
        index.rebuild_metadata(product_root, product);
        current_product_revision = expect_product_revision_advanced(
            backlog_root,
            product_root,
            product,
            current_product_revision,
            "rebuild after concurrent revision-state replacement");
#endif

        const auto missing_backlog_root = fixture_root / "missing-backlog";
        const auto missing_product_root =
            missing_backlog_root / "products" / product;
        const auto sentinel_content = read_text(*target.file_path);
        write_text(
            missing_product_root /
                "items/task/0000/MISSING-TSK-0001_requested-status-sentinel.md",
            sentinel_content);
        const auto missing_status = kano::backlog_ops::get_index_status(
            missing_backlog_root,
            product,
            missing_product_root,
            current_product_revision);
        expect(missing_status.indexes.size() == 1 &&
                   missing_status.indexes.front().status == "missing" &&
                   !missing_status.indexes.front().unchanged &&
                   !missing_status.indexes.front().fallback_scan &&
                   missing_status.indexes.front().scanned_count == 0 &&
                   missing_status.indexes.front().item_count == 0 &&
                   missing_status.indexes.front().index_revision.empty() &&
                   missing_status.indexes.front().canonical_revision.empty() &&
                   missing_status.indexes.front().product_revision.empty() &&
                   missing_status.indexes.front().stale_reason &&
                   *missing_status.indexes.front().stale_reason == "index_missing",
            "missing revision state must fail closed with bounded diagnostics");

        const auto corrupt_backlog_root = fixture_root / "corrupt-backlog";
        const auto corrupt_product_root =
            corrupt_backlog_root / "products" / product;
        const auto corrupt_status_path =
            corrupt_backlog_root / ".cache" / "index" / "backlog.db";
        write_text(
            corrupt_product_root /
                "items/task/0000/CORRUPT-TSK-0001_requested-status-sentinel.md",
            sentinel_content);
        std::filesystem::create_directories(corrupt_status_path.parent_path());
        write_text(corrupt_status_path, "not-a-sqlite-index");
        const auto corrupt_status = kano::backlog_ops::get_index_status(
            corrupt_backlog_root,
            product,
            corrupt_product_root,
            current_product_revision);
        expect(corrupt_status.indexes.size() == 1 &&
                   corrupt_status.indexes.front().status == "corrupt" &&
                   !corrupt_status.indexes.front().unchanged &&
                   !corrupt_status.indexes.front().fallback_scan &&
                   corrupt_status.indexes.front().scanned_count == 0 &&
                   corrupt_status.indexes.front().item_count == 0 &&
                   corrupt_status.indexes.front().index_revision.empty() &&
                   corrupt_status.indexes.front().canonical_revision.empty() &&
                   corrupt_status.indexes.front().product_revision.empty() &&
                   corrupt_status.indexes.front().stale_reason &&
                   *corrupt_status.indexes.front().stale_reason ==
                       "metadata_index_open_or_doctor_failed" &&
                   corrupt_status.indexes.front().stale_reason->find(
                       fixture_root.string()) == std::string::npos,
            "corrupt revision state must fail closed without exposing raw paths");

        std::string invalid_revision_error;
        try {
            (void)kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                "../../outside");
        } catch (const std::exception& ex) {
            invalid_revision_error = ex.what();
        }
        expect(invalid_revision_error ==
                   "metadata_index_requested_revision_invalid",
            "invalid requested revisions must fail with a bounded path-safe error");

        std::string oversized_revision = "kob-pr-v1:";
        oversized_revision.append(97 - oversized_revision.size(), 'a');
        expect(oversized_revision.size() == 97,
            "oversized requested-revision fixture must be exactly 97 bytes");
        std::string oversized_revision_error;
        try {
            (void)kano::backlog_ops::get_index_status(
                backlog_root,
                product,
                product_root,
                oversized_revision);
        } catch (const std::exception& ex) {
            oversized_revision_error = ex.what();
        }
        expect(oversized_revision_error ==
                   "metadata_index_requested_revision_invalid",
            "97-byte otherwise-safe requested revision must fail before traversal");

        const auto status = kano::backlog_ops::get_index_status(
            backlog_root, product, product_root);
        expect(status.indexes.size() == 1 &&
                   status.indexes.front().status == "ready" &&
                   status.indexes.front().product_revision ==
                       current_product_revision &&
                   status.indexes.front().index_ref ==
                        "product-cache/index/backlog.db",
            "status must expose a bounded ready snapshot reference");
        }

        std::cout << "metadata_index_smoke_test: PASS"
                  << " exact_p95_ms=" << exact_p95_ms
                  << " metadata_p95_ms=" << metadata_p95_ms
                  << " token_p95_ms=" << token_p95_ms
                  << " revision_p95_ms=" << revision_p95_ms
                  << " unchanged_status_p95_ms=" << unchanged_status_p95_ms
                  << "\n";
        std::filesystem::remove_all(fixture_root);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "metadata_index_smoke_test: FAIL: " << ex.what() << "\n";
        if (!fixture_root.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(fixture_root, cleanup_error);
        }
        return 1;
    }
}
