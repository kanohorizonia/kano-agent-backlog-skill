#include "kano/backlog_ops/index/backlog_index.hpp"

#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "change_probe.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace kano::backlog_ops {

using namespace kano::backlog_core;

namespace {

constexpr std::uint64_t kFNVOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFNVPrime = 1099511628211ULL;
constexpr std::uint64_t kMaximumIndexedItemBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumQueryTextBytes = 512;
constexpr std::size_t kMaximumQueryTokens = 16;
constexpr std::size_t kMaximumQueryLimit = 20000;
constexpr std::size_t kMaximumProductRevisionBytes = 96;
constexpr std::size_t kMaximumWatchSourceRefBytes = 4096;
constexpr std::size_t kMaximumPersistedReasonBytes = 512;
constexpr std::string_view kProductRevisionPrefix = "kob-pr-v1:";
constexpr std::string_view kFNV1a64RevisionPrefix = "fnv1a64:";
constexpr std::chrono::milliseconds kRequestedRevisionVerificationBudget{80};
constexpr std::chrono::milliseconds kCheckpointPersistenceBusyWait{10};

bool deadline_expired(const std::chrono::steady_clock::time_point deadline) {
    return std::chrono::steady_clock::now() >= deadline;
}

int checkpoint_busy_wait_ms(const std::chrono::steady_clock::time_point deadline) {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        return static_cast<int>(kCheckpointPersistenceBusyWait.count());
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now);
    return static_cast<int>(
        std::min(remaining, kCheckpointPersistenceBusyWait).count());
}

bool ascii_alphanumeric(const unsigned char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z');
}

bool exact_fnv1a64_revision(const std::string_view value) {
    constexpr std::size_t kHexDigitCount = 16;
    if (value.size() != kFNV1a64RevisionPrefix.size() + kHexDigitCount ||
        !value.starts_with(kFNV1a64RevisionPrefix)) {
        return false;
    }
    return std::all_of(
        value.begin() + static_cast<std::ptrdiff_t>(kFNV1a64RevisionPrefix.size()),
        value.end(),
        [](const unsigned char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
                (ch >= 'a' && ch <= 'f');
        });
}

bool canonical_write_revision_safe(const std::string_view value) {
    if (value.empty() || value == "-" || value.size() > 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return ascii_alphanumeric(ch) || ch == '-';
    });
}

bool bounded_relative_generic_source_ref(const std::string_view value) {
    if (value.empty() || value.size() > kMaximumWatchSourceRefBytes ||
        value.front() == '/' || value.back() == '/' ||
        value.find(':') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t segment_start = 0;
    while (segment_start < value.size()) {
        const auto separator = value.find('/', segment_start);
        const auto segment_end = separator == std::string_view::npos
            ? value.size()
            : separator;
        const auto segment = value.substr(segment_start, segment_end - segment_start);
        if (segment.empty() || segment == "." || segment == ".." ||
            (segment_start == 0 && segment.size() == 2 &&
             ascii_alphanumeric(static_cast<unsigned char>(segment.front())) &&
             segment.back() == ':')) {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        segment_start = separator + 1;
    }
    return true;
}

std::optional<std::uint64_t> parse_uint64(const std::string_view value) {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

std::string sqlite_error(sqlite3* db) {
    const char* message = sqlite3_errmsg(db);
    return message ? std::string(message) : std::string("unknown SQLite error");
}

[[noreturn]] void throw_sqlite(sqlite3* db, const std::string& context, int rc) {
    throw std::runtime_error(
        context + " failed (sqlite rc=" + std::to_string(rc) + "): " + sqlite_error(db));
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql, std::string context)
        : db_(db), context_(std::move(context)) {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " prepare", rc);
        }
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const {
        return stmt_;
    }

    void bind_text(int index, const std::string& value) const {
        if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error(context_ + " bind parameter is too large");
        }
        const int rc = sqlite3_bind_text(
            stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind text", rc);
        }
    }

    void bind_int(int index, int value) const {
        const int rc = sqlite3_bind_int(stmt_, index, value);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind int", rc);
        }
    }

    void bind_int64(int index, std::int64_t value) const {
        const int rc = sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value));
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind int64", rc);
        }
    }

    void bind_null(int index) const {
        const int rc = sqlite3_bind_null(stmt_, index);
        if (rc != SQLITE_OK) {
            throw_sqlite(db_, context_ + " bind null", rc);
        }
    }

    int step() const {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            throw_sqlite(db_, context_ + " step", rc);
        }
        return rc;
    }

    void step_done() const {
        if (step() != SQLITE_DONE) {
            throw std::runtime_error(context_ + " returned rows unexpectedly");
        }
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
    std::string context_;
};

std::string column_text(sqlite3_stmt* stmt, int column) {
    const int bytes = sqlite3_column_bytes(stmt, column);
    const unsigned char* value = sqlite3_column_text(stmt, column);
    if (!value || bytes <= 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(value), static_cast<std::size_t>(bytes));
}

std::optional<std::string> column_optional_text(sqlite3_stmt* stmt, int column) {
    if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return column_text(stmt, column);
}

class Fnv1a64 {
public:
    void update(std::string_view value) {
        for (const unsigned char ch : value) {
            value_ ^= static_cast<std::uint64_t>(ch);
            value_ *= kFNVPrime;
        }
    }

    std::string final() const {
        std::ostringstream out;
        out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << value_;
        return out.str();
    }

private:
    std::uint64_t value_ = kFNVOffset;
};

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto raw = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

struct SourceStat {
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
};

SourceStat source_stat_checked(const std::filesystem::path& path) {
    SourceStat result;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(
            path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        throw std::runtime_error("metadata_index_source_stat_failed");
    }
    result.size =
        (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32U) |
        static_cast<std::uint64_t>(data.nFileSizeLow);
    ULARGE_INTEGER modified{};
    modified.HighPart = data.ftLastWriteTime.dwHighDateTime;
    modified.LowPart = data.ftLastWriteTime.dwLowDateTime;
    result.mtime_ns = static_cast<std::int64_t>(modified.QuadPart * 100ULL);
#else
    struct stat data {};
    if (::stat(path.c_str(), &data) != 0 || !S_ISREG(data.st_mode)) {
        throw std::runtime_error("metadata_index_source_stat_failed");
    }
    result.size = static_cast<std::uint64_t>(data.st_size);
#if defined(__APPLE__)
    result.mtime_ns =
        static_cast<std::int64_t>(data.st_mtimespec.tv_sec) * 1000000000LL +
        static_cast<std::int64_t>(data.st_mtimespec.tv_nsec);
#else
    result.mtime_ns =
        static_cast<std::int64_t>(data.st_mtim.tv_sec) * 1000000000LL +
        static_cast<std::int64_t>(data.st_mtim.tv_nsec);
#endif
#endif
    if (result.size > kMaximumIndexedItemBytes) {
        throw std::runtime_error("metadata_index_source_too_large");
    }
    return result;
}

std::string bounded_source_ref(
    const std::filesystem::path& path,
    const std::filesystem::path& product_root
) {
    std::error_code ec;
    const auto absolute_path = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) {
        throw std::runtime_error("metadata_index_source_outside_product");
    }
    const auto absolute_root =
        std::filesystem::absolute(product_root, ec).lexically_normal();
    if (ec) {
        throw std::runtime_error("metadata_index_source_outside_product");
    }
    const auto relative =
        absolute_path.lexically_relative(absolute_root).lexically_normal();
    const auto value = relative.generic_string();
    if (relative.empty() || relative.is_absolute() || value == ".." ||
        value.starts_with("../") || value.find("/../") != std::string::npos) {
        throw std::runtime_error("metadata_index_source_outside_product");
    }
    return value;
}

std::filesystem::path infer_product_root(const std::filesystem::path& item_path) {
    auto current = item_path.parent_path();
    while (!current.empty()) {
        if (current.filename() == "items") {
            return current.parent_path();
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    throw std::runtime_error("metadata_index_product_root_unresolved");
}

std::string infer_product_name(const std::filesystem::path& product_root) {
    const auto value = product_root.filename().string();
    return value.empty() ? std::string("default") : value;
}

std::optional<std::filesystem::path> materialize_source_path(
    const std::filesystem::path& db_path,
    const std::optional<std::filesystem::path>& configured_product_root,
    const std::string& product,
    const std::string& source_ref
) {
    const auto relative = std::filesystem::path(source_ref).lexically_normal();
    if (relative.empty() || relative.is_absolute() || source_ref == ".." ||
        source_ref.starts_with("../") || source_ref.starts_with("..\\") ||
        source_ref.find("/../") != std::string::npos ||
        source_ref.find("\\..\\") != std::string::npos) {
        return std::nullopt;
    }
    if (configured_product_root) {
        return (*configured_product_root / relative).lexically_normal();
    }
    if (product.empty() || product == ".." ||
        product.find('/') != std::string::npos ||
        product.find('\\') != std::string::npos) {
        return std::nullopt;
    }
    const auto index_dir = db_path.parent_path();
    const auto cache_dir = index_dir.parent_path();
    if (index_dir.filename() == "index" && cache_dir.filename() == ".cache") {
        const auto owner_root = cache_dir.parent_path();
        const auto product_local = (owner_root / relative).lexically_normal();
        std::error_code exists_error;
        if (owner_root.filename() == product ||
            std::filesystem::exists(product_local, exists_error)) {
            return product_local;
        }
        return (owner_root / "products" / product / relative).lexically_normal();
    }
    return relative;
}

std::string slug_from_source(const std::string& id, const std::filesystem::path& path) {
    auto stem = path.stem().string();
    const auto prefix = id + "_";
    if (stem.starts_with(prefix)) {
        stem.erase(0, prefix.size());
    }
    return stem;
}

std::string hash_file(
    const std::filesystem::path& path,
    std::uint64_t expected_size
) {
    if (expected_size > kMaximumIndexedItemBytes) {
        throw std::runtime_error("metadata_index_source_too_large");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("metadata_index_source_read_failed");
    }
    Fnv1a64 hash;
    std::array<char, 64 * 1024> buffer{};
    std::uint64_t total = 0;
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        total += static_cast<std::uint64_t>(count);
        if (total > kMaximumIndexedItemBytes) {
            throw std::runtime_error("metadata_index_source_too_large");
        }
        hash.update(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
    }
    if (input.bad() || total != expected_size) {
        throw std::runtime_error("metadata_index_source_read_failed");
    }
    return hash.final();
}

std::string hash_content(std::string_view content) {
    Fnv1a64 hash;
    hash.update(content);
    return hash.final();
}

bool write_receipt_matches(
    const CanonicalWriteRevision& receipt,
    const IndexItem& indexed
) {
    return receipt.operation == "write" && receipt.source_ref_hash
        && *receipt.source_ref_hash == hash_content(indexed.source_ref)
        && receipt.content_size == indexed.source_size && receipt.content_hash
        && *receipt.content_hash == indexed.source_hash;
}

bool delete_receipt_matches(
    const CanonicalWriteRevision& receipt,
    const std::set<std::string>& source_refs
) {
    return source_refs.size() == 1 && receipt.operation == "delete"
        && receipt.source_ref_hash
        && *receipt.source_ref_hash == hash_content(*source_refs.begin())
        && receipt.content_size == 0 && !receipt.content_hash;
}

void validate_requested_revision(const std::string& revision) {
    if (revision.empty() || revision.size() > kMaximumProductRevisionBytes ||
        !revision.starts_with(kProductRevisionPrefix)) {
        throw std::runtime_error("metadata_index_requested_revision_invalid");
    }
    const bool valid_characters = std::all_of(
        revision.begin(), revision.end(), [](const unsigned char ch) {
            return ascii_alphanumeric(ch) || ch == ':' || ch == '-' || ch == '_';
        });
    if (!valid_characters) {
        throw std::runtime_error("metadata_index_requested_revision_invalid");
    }
}

std::string new_revision_epoch() {
    Fnv1a64 hash;
    hash.update("kob-product-revision-epoch-v1\n");
    const auto system_ticks = std::chrono::system_clock::now()
        .time_since_epoch().count();
    const auto steady_ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    hash.update(std::to_string(system_ticks));
    hash.update(std::string_view{"\0", 1});
    hash.update(std::to_string(steady_ticks));
    std::random_device entropy;
    for (int sample = 0; sample < 4; ++sample) {
        hash.update(std::string_view{"\0", 1});
        hash.update(std::to_string(entropy()));
    }
    return hash.final();
}

std::string format_product_revision(
    const std::string& epoch,
    std::uint64_t generation
) {
    if (epoch.empty() || generation == 0) {
        return {};
    }
    return std::string(kProductRevisionPrefix) + epoch + ":" +
        std::to_string(generation);
}

struct SourceFingerprint {
    std::filesystem::path path;
    std::string source_ref;
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
};

void update_inventory_hash(Fnv1a64& hash, const SourceFingerprint& source) {
    hash.update(source.source_ref);
    hash.update(std::string_view{"\0", 1});
    hash.update(std::to_string(source.size));
    hash.update(std::string_view{"\0", 1});
    hash.update(std::to_string(source.mtime_ns));
    hash.update("\n");
}

struct CanonicalInventory {
    std::vector<SourceFingerprint> sources;
    std::string revision;
};

CanonicalInventory canonical_inventory(const std::filesystem::path& product_root) {
    CanonicalStore store(product_root);
    CanonicalInventory result;
    for (const auto& path : store.list_items()) {
        SourceFingerprint source;
        source.path = path;
        source.source_ref = bounded_source_ref(path, product_root);
        const auto stat = source_stat_checked(path);
        source.size = stat.size;
        source.mtime_ns = stat.mtime_ns;
        result.sources.push_back(std::move(source));
    }
    std::sort(result.sources.begin(), result.sources.end(), [](const auto& left, const auto& right) {
        return left.source_ref < right.source_ref;
    });
    Fnv1a64 revision;
    revision.update("kob-metadata-inventory-v1\n");
    for (const auto& source : result.sources) {
        update_inventory_hash(revision, source);
    }
    result.revision = revision.final();
    return result;
}

std::vector<change_probe::WatchPath> canonical_watch_paths(
    const std::filesystem::path& product_root,
    const CanonicalInventory& inventory
) {
    std::vector<change_probe::WatchPath> result;
    result.reserve(inventory.sources.size());
    for (const auto& source : inventory.sources) {
        result.push_back({source.source_ref, source.path, false});
    }

    const auto items_root = product_root / "items";
    std::error_code error;
    if (!std::filesystem::exists(items_root, error)) {
        if (error) {
            throw std::runtime_error("metadata_index_change_watch_enumeration_failed");
        }
        result.push_back({"", product_root, true});
        return result;
    }
    result.push_back({"", items_root, true});
    std::filesystem::recursive_directory_iterator iterator(items_root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_directory(error)) {
            result.push_back({"", iterator->path(), true});
        }
        iterator.increment(error);
    }
    if (error) {
        throw std::runtime_error("metadata_index_change_watch_enumeration_failed");
    }
    return result;
}

std::string canonical_content_revision(const CanonicalInventory& inventory) {
    Fnv1a64 content;
    content.update("kob-metadata-content-v1\n");
    for (const auto& source : inventory.sources) {
        content.update(source.source_ref);
        content.update(std::string_view{"\0", 1});
        content.update(hash_file(source.path, source.size));
        content.update("\n");
    }
    return content.final();
}

IndexItem index_item_from_metadata(
    const BacklogItem& item,
    const std::string& product,
    const SourceFingerprint& source,
    std::string source_hash
) {
    IndexItem indexed;
    indexed.id = item.id;
    indexed.uid = item.uid;
    indexed.product = product;
    indexed.type = item.type;
    indexed.title = item.title;
    indexed.state = item.state;
    indexed.priority = item.priority;
    indexed.parent = item.parent;
    indexed.duplicate_of = item.duplicate_of;
    indexed.slug = slug_from_source(item.id, source.path);
    indexed.source_ref = source.source_ref;
    indexed.source_hash = std::move(source_hash);
    indexed.source_size = source.size;
    indexed.source_mtime_ns = source.mtime_ns;
    indexed.estimated_tokens = (source.size + 3ULL) / 4ULL;
    indexed.updated = item.updated;
    return indexed;
}

struct Snapshot {
    bool exists = false;
    bool persisted_fields_valid = true;
    int schema_version = 0;
    int snapshot_schema_version = 0;
    std::string status;
    std::string index_revision;
    std::string content_revision;
    std::string revision_epoch;
    std::string product_revision;
    std::string canonical_write_revision;
    std::string proof_kind;
    std::string proof_status;
    std::optional<std::uint64_t> proof_root_volume_serial;
    std::optional<std::uint64_t> proof_root_file_id;
    std::optional<std::uint64_t> proof_journal_id;
    std::int64_t proof_verified_usn = 0;
    std::optional<change_probe::Checkpoint> proof;
    std::size_t item_count = 0;
    std::uint64_t generation = 0;
    std::optional<std::string> reason;
};

bool same_snapshot_publication_identity(
    const Snapshot& lhs,
    const Snapshot& rhs
) {
    return lhs.exists == rhs.exists &&
        lhs.persisted_fields_valid == rhs.persisted_fields_valid &&
        lhs.schema_version == rhs.schema_version &&
        lhs.snapshot_schema_version == rhs.snapshot_schema_version &&
        lhs.status == rhs.status &&
        lhs.index_revision == rhs.index_revision &&
        lhs.content_revision == rhs.content_revision &&
        lhs.revision_epoch == rhs.revision_epoch &&
        lhs.product_revision == rhs.product_revision &&
        lhs.canonical_write_revision == rhs.canonical_write_revision &&
        lhs.proof_kind == rhs.proof_kind &&
        lhs.proof_status == rhs.proof_status &&
        lhs.proof_root_volume_serial == rhs.proof_root_volume_serial &&
        lhs.proof_root_file_id == rhs.proof_root_file_id &&
        lhs.proof_journal_id == rhs.proof_journal_id &&
        lhs.proof_verified_usn == rhs.proof_verified_usn &&
        lhs.proof.has_value() == rhs.proof.has_value() &&
        lhs.item_count == rhs.item_count &&
        lhs.generation == rhs.generation &&
        lhs.reason == rhs.reason;
}

Snapshot read_snapshot(sqlite3* db, const std::string& product) {
    Statement statement(
        db,
        "SELECT schema_version, snapshot_schema_version, status, index_revision, "
        "content_revision, revision_epoch, product_revision, canonical_write_revision, "
        "proof_kind, proof_status, proof_root_volume_serial, proof_root_file_id, "
        "proof_journal_id, proof_verified_usn, item_count, generation, reason "
        "FROM metadata_snapshots WHERE product = ?",
        "read metadata snapshot");
    statement.bind_text(1, product);
    Snapshot result;
    if (statement.step() != SQLITE_ROW) {
        return result;
    }
    result.exists = true;
    result.persisted_fields_valid =
        sqlite3_column_type(statement.get(), 0) == SQLITE_INTEGER &&
        sqlite3_column_type(statement.get(), 1) == SQLITE_INTEGER &&
        sqlite3_column_type(statement.get(), 13) == SQLITE_INTEGER &&
        sqlite3_column_type(statement.get(), 14) == SQLITE_INTEGER &&
        sqlite3_column_type(statement.get(), 15) == SQLITE_INTEGER &&
        (sqlite3_column_type(statement.get(), 16) == SQLITE_NULL ||
         sqlite3_column_type(statement.get(), 16) == SQLITE_TEXT);
    for (int column = 2; column <= 12; ++column) {
        result.persisted_fields_valid = result.persisted_fields_valid &&
            sqlite3_column_type(statement.get(), column) == SQLITE_TEXT;
    }
    const auto bounded_text = [&](const int column, const std::size_t maximum_bytes) {
        const int bytes = sqlite3_column_bytes(statement.get(), column);
        if (bytes < 0 || static_cast<std::size_t>(bytes) > maximum_bytes) {
            result.persisted_fields_valid = false;
            return std::string{};
        }
        return column_text(statement.get(), column);
    };
    result.schema_version = sqlite3_column_int(statement.get(), 0);
    result.snapshot_schema_version = sqlite3_column_int(statement.get(), 1);
    result.status = bounded_text(2, 32);
    result.index_revision = bounded_text(3, kMaximumProductRevisionBytes);
    result.content_revision = bounded_text(4, kMaximumProductRevisionBytes);
    result.revision_epoch = bounded_text(5, kMaximumProductRevisionBytes);
    result.product_revision = bounded_text(6, kMaximumProductRevisionBytes);
    result.canonical_write_revision = bounded_text(7, 64);
    result.proof_kind = bounded_text(8, 64);
    result.proof_status = bounded_text(9, 64);
    result.proof_root_volume_serial = parse_uint64(bounded_text(10, 20));
    result.proof_root_file_id = parse_uint64(bounded_text(11, 20));
    result.proof_journal_id = parse_uint64(bounded_text(12, 20));
    result.proof_verified_usn = sqlite3_column_int64(statement.get(), 13);
    if (result.proof_status == "verified" && result.proof_root_volume_serial
        && result.proof_root_file_id && result.proof_journal_id
        && result.proof_verified_usn >= 0) {
        result.proof = change_probe::Checkpoint{
            result.proof_kind,
            {*result.proof_root_volume_serial, *result.proof_root_file_id},
            *result.proof_journal_id,
            result.proof_verified_usn,
        };
    }
    const auto item_count = sqlite3_column_int64(statement.get(), 14);
    result.item_count = item_count < 0
        ? std::numeric_limits<std::size_t>::max()
        : static_cast<std::size_t>(item_count);
    const auto generation = sqlite3_column_int64(statement.get(), 15);
    result.generation = generation <= 0
        ? 0
        : static_cast<std::uint64_t>(generation);
    if (sqlite3_column_type(statement.get(), 16) != SQLITE_NULL) {
        const auto reason = bounded_text(16, kMaximumPersistedReasonBytes);
        if (result.persisted_fields_valid) {
            result.reason = reason;
        }
    }
    return result;
}

enum class SnapshotReadinessMode {
    Invalid,
    VerifiedNtfs,
    PortableUnsupported,
};

SnapshotReadinessMode snapshot_readiness_mode(const Snapshot& snapshot) {
    const bool common_ready = snapshot.exists && snapshot.persisted_fields_valid &&
        snapshot.schema_version == kMetadataIndexSchemaVersion &&
        snapshot.snapshot_schema_version == kMetadataSnapshotSchemaVersion &&
        snapshot.status == "ready" &&
        exact_fnv1a64_revision(snapshot.index_revision) &&
        exact_fnv1a64_revision(snapshot.content_revision) &&
        exact_fnv1a64_revision(snapshot.revision_epoch) &&
        canonical_write_revision_safe(snapshot.canonical_write_revision) &&
        snapshot.item_count <= static_cast<std::size_t>(
            std::numeric_limits<int>::max()) &&
        snapshot.generation > 0 &&
        snapshot.generation <= static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) &&
        snapshot.product_revision == format_product_revision(
            snapshot.revision_epoch, snapshot.generation);
    if (!common_ready) {
        return SnapshotReadinessMode::Invalid;
    }
    if (snapshot.proof_status == "verified" && snapshot.proof &&
        snapshot.proof_kind == change_probe::kNtfsUsnProofKind &&
        snapshot.proof_root_volume_serial &&
        *snapshot.proof_root_volume_serial != 0 && snapshot.proof_root_file_id &&
        *snapshot.proof_root_file_id != 0 && snapshot.proof_journal_id &&
        *snapshot.proof_journal_id != 0 && snapshot.proof_verified_usn >= 0) {
        return SnapshotReadinessMode::VerifiedNtfs;
    }
    if (snapshot.proof_kind == "none" && snapshot.proof_status == "unsupported" &&
        !snapshot.proof && snapshot.proof_root_volume_serial &&
        *snapshot.proof_root_volume_serial == 0 && snapshot.proof_root_file_id &&
        *snapshot.proof_root_file_id == 0 && snapshot.proof_journal_id &&
        *snapshot.proof_journal_id == 0 && snapshot.proof_verified_usn == 0) {
        return SnapshotReadinessMode::PortableUnsupported;
    }
    return SnapshotReadinessMode::Invalid;
}

bool snapshot_structurally_ready(const Snapshot& snapshot) {
    return snapshot_readiness_mode(snapshot) != SnapshotReadinessMode::Invalid;
}

bool change_watch_structurally_safe(
    const std::vector<change_probe::WatchEntry>& watch
) {
    return std::all_of(
        watch.begin(), watch.end(), [](const change_probe::WatchEntry& entry) {
            return entry.file_id != 0 &&
                (entry.directory
                    ? entry.source_ref.empty()
                    : bounded_relative_generic_source_ref(entry.source_ref));
        });
}

std::optional<std::vector<change_probe::WatchEntry>> read_change_watch(
    sqlite3* db,
    const std::string& product,
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max(),
    bool* rows_present = nullptr
);

struct RevisionState {
    Snapshot snapshot;
    std::optional<std::vector<change_probe::WatchEntry>> watch;
    bool watch_rows_present = false;
    bool deadline_exhausted = false;
};

bool revision_watch_state_safe(
    const RevisionState& state,
    const SnapshotReadinessMode mode
) {
    if (mode == SnapshotReadinessMode::VerifiedNtfs) {
        return state.watch_rows_present && state.watch && !state.watch->empty() &&
            change_watch_structurally_safe(*state.watch);
    }
    if (mode == SnapshotReadinessMode::PortableUnsupported) {
        return !state.watch_rows_present && !state.watch;
    }
    return false;
}

RevisionState read_revision_state_read_only(
    const std::filesystem::path& index_path,
    const std::string& product,
    const std::chrono::steady_clock::time_point deadline
) {
    RevisionState state;
    if (deadline_expired(deadline)) {
        state.deadline_exhausted = true;
        return state;
    }
    sqlite3* db = nullptr;
    const int open_rc = sqlite3_open_v2(
        index_path.string().c_str(),
        &db,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (open_rc != SQLITE_OK) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        throw std::runtime_error("metadata_index_revision_state_open_failed");
    }
    if (deadline_expired(deadline)) {
        sqlite3_close(db);
        state.deadline_exhausted = true;
        return state;
    }
    bool transaction_open = false;
    try {
        const auto configure_no_wait = [&]() {
            if (deadline_expired(deadline)) {
                return false;
            }
            const int timeout_rc = sqlite3_busy_timeout(db, 0);
            if (timeout_rc != SQLITE_OK) {
                throw std::runtime_error(
                    "metadata_index_revision_state_timeout_configuration_failed");
            }
            return !deadline_expired(deadline);
        };
        if (!configure_no_wait()) {
            sqlite3_close(db);
            state.deadline_exhausted = true;
            return state;
        }
        if (sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(
                "metadata_index_revision_state_transaction_failed");
        }
        transaction_open = true;
        state.snapshot = read_snapshot(db, product);
        if (deadline_expired(deadline)) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            transaction_open = false;
            sqlite3_close(db);
            state.deadline_exhausted = true;
            return state;
        }
        if (deadline_expired(deadline) || !configure_no_wait()) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            transaction_open = false;
            sqlite3_close(db);
            state.deadline_exhausted = true;
            return state;
        }
        state.watch = read_change_watch(
            db, product, deadline, &state.watch_rows_present);
        if (deadline_expired(deadline)) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            transaction_open = false;
            sqlite3_close(db);
            state.deadline_exhausted = true;
            return state;
        }
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(
                "metadata_index_revision_state_transaction_failed");
        }
        transaction_open = false;
        sqlite3_close(db);
        return state;
    } catch (...) {
        const bool exhausted = deadline_expired(deadline);
        if (transaction_open) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        sqlite3_close(db);
        if (exhausted) {
            state.deadline_exhausted = true;
            return state;
        }
        throw;
    }
}

std::uint64_t next_generation(const Snapshot& snapshot) {
    if (snapshot.generation >= static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("metadata_index_product_revision_exhausted");
    }
    return snapshot.generation + 1;
}

void write_snapshot(
    sqlite3* db,
    const std::string& product,
    const std::string& status,
    const std::string& index_revision,
    const std::string& content_revision,
    const std::string& revision_epoch,
    const std::string& product_revision,
    const std::string& canonical_write_revision,
    std::size_t item_count,
    std::uint64_t generation,
    const std::optional<std::string>& reason,
    const std::optional<change_probe::Checkpoint>& proof = std::nullopt
) {
    Statement statement(
        db,
        "INSERT INTO metadata_snapshots "
        "(product, schema_version, snapshot_schema_version, status, index_revision, "
        "content_revision, revision_epoch, product_revision, canonical_write_revision, "
        "proof_kind, proof_status, proof_root_volume_serial, proof_root_file_id, "
        "proof_journal_id, proof_verified_usn, item_count, generation, built_at, reason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(product) DO UPDATE SET "
        "schema_version=excluded.schema_version, "
        "snapshot_schema_version=excluded.snapshot_schema_version, "
        "status=excluded.status, index_revision=excluded.index_revision, "
        "content_revision=excluded.content_revision, revision_epoch=excluded.revision_epoch, "
        "product_revision=excluded.product_revision, "
        "canonical_write_revision=excluded.canonical_write_revision, "
        "proof_kind=excluded.proof_kind, proof_status=excluded.proof_status, "
        "proof_root_volume_serial=excluded.proof_root_volume_serial, "
        "proof_root_file_id=excluded.proof_root_file_id, "
        "proof_journal_id=excluded.proof_journal_id, "
        "proof_verified_usn=excluded.proof_verified_usn, "
        "item_count=excluded.item_count, "
        "generation=excluded.generation, built_at=excluded.built_at, reason=excluded.reason",
        "write metadata snapshot");
    statement.bind_text(1, product);
    statement.bind_int(2, kMetadataIndexSchemaVersion);
    statement.bind_int(3, kMetadataSnapshotSchemaVersion);
    statement.bind_text(4, status);
    statement.bind_text(5, index_revision);
    statement.bind_text(6, content_revision);
    statement.bind_text(7, revision_epoch);
    statement.bind_text(8, product_revision);
    statement.bind_text(9, canonical_write_revision);
    statement.bind_text(10, proof ? proof->proof_kind : "none");
    statement.bind_text(
        11,
        proof ? "verified" : (status == "ready" ? "unsupported" : "unavailable"));
    statement.bind_text(12, proof ? std::to_string(proof->root.volume_serial) : "0");
    statement.bind_text(13, proof ? std::to_string(proof->root.file_id) : "0");
    statement.bind_text(14, proof ? std::to_string(proof->journal_id) : "0");
    statement.bind_int64(15, proof ? proof->usn : 0);
    statement.bind_int64(16, static_cast<std::int64_t>(item_count));
    statement.bind_int64(17, static_cast<std::int64_t>(generation));
    statement.bind_text(18, utc_timestamp());
    if (reason && !reason->empty()) {
        statement.bind_text(19, *reason);
    } else {
        statement.bind_null(19);
    }
    statement.step_done();
}

void invalidate_stale_query_snapshot(
    sqlite3* db,
    const std::string& product,
    const Snapshot& observed_snapshot,
    const std::optional<CanonicalWriteRevision>& observed_write_revision,
    const std::string& reason
) {
    const auto execute_transaction = [&](const char* sql, const std::string& context) {
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            throw_sqlite(db, context, rc);
        }
    };

    bool transaction_open = false;
    try {
        execute_transaction("BEGIN IMMEDIATE", "begin stale metadata invalidation");
        transaction_open = true;
        const auto current = read_snapshot(db, product);
        const bool contiguous_publication = observed_write_revision &&
            observed_write_revision->previous &&
            *observed_write_revision->previous ==
                observed_snapshot.canonical_write_revision;
        const bool observed_snapshot_current =
            same_snapshot_publication_identity(current, observed_snapshot);
        const bool publication_pending = observed_snapshot_current &&
            contiguous_publication &&
            reason == "canonical_write_revision_changed" &&
            snapshot_structurally_ready(current);
        if (observed_snapshot_current && !publication_pending) {
            write_snapshot(
                db,
                product,
                "stale",
                current.index_revision,
                current.content_revision,
                current.revision_epoch,
                current.product_revision,
                current.canonical_write_revision,
                current.item_count,
                current.generation,
                reason);
        }
        execute_transaction("COMMIT", "commit stale metadata invalidation");
        transaction_open = false;
    } catch (...) {
        if (transaction_open) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        throw;
    }
}

std::optional<std::vector<change_probe::WatchEntry>> read_change_watch(
    sqlite3* db,
    const std::string& product,
    const std::chrono::steady_clock::time_point deadline,
    bool* rows_present
) {
    if (rows_present) {
        *rows_present = false;
    }
    if (deadline_expired(deadline)) {
        return std::nullopt;
    }
    Statement statement(
        db,
        "SELECT source_ref, file_id, is_directory FROM metadata_change_watch "
        "WHERE product = ? ORDER BY file_id, source_ref",
        "read metadata change watch");
    statement.bind_text(1, product);
    std::vector<change_probe::WatchEntry> result;
    while (!deadline_expired(deadline)) {
        if (statement.step() != SQLITE_ROW) {
            break;
        }
        if (rows_present) {
            *rows_present = true;
        }
        const int source_ref_bytes = sqlite3_column_bytes(statement.get(), 0);
        const int file_id_bytes = sqlite3_column_bytes(statement.get(), 1);
        const auto directory_value = sqlite3_column_int(statement.get(), 2);
        if (sqlite3_column_type(statement.get(), 0) != SQLITE_TEXT ||
            sqlite3_column_type(statement.get(), 1) != SQLITE_TEXT ||
            sqlite3_column_type(statement.get(), 2) != SQLITE_INTEGER ||
            source_ref_bytes < 0 ||
            static_cast<std::size_t>(source_ref_bytes) >
                kMaximumWatchSourceRefBytes ||
            file_id_bytes <= 0 || file_id_bytes > 20) {
            return std::nullopt;
        }
        const auto parsed_file_id = parse_uint64(column_text(statement.get(), 1));
        if (!parsed_file_id || *parsed_file_id == 0
            || (directory_value != 0 && directory_value != 1)) {
            return std::nullopt;
        }
        result.push_back({
            column_text(statement.get(), 0),
            *parsed_file_id,
            directory_value == 1,
        });
    }
    if (deadline_expired(deadline)) {
        return std::nullopt;
    }
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

bool change_watch_rows_empty(sqlite3* db, const std::string& product) {
    Statement statement(
        db,
        "SELECT 1 FROM metadata_change_watch WHERE product = ? LIMIT 1",
        "inspect metadata change watch presence");
    statement.bind_text(1, product);
    return statement.step() != SQLITE_ROW;
}

void append_watch_entries(
    std::vector<change_probe::WatchEntry>& destination,
    const std::vector<change_probe::WatchEntry>& source
) {
    std::set<std::tuple<std::uint64_t, bool, std::string>> keys;
    for (const auto& entry : destination) {
        keys.emplace(entry.file_id, entry.directory, entry.source_ref);
    }
    for (const auto& entry : source) {
        if (keys.emplace(entry.file_id, entry.directory, entry.source_ref).second) {
            destination.push_back(entry);
        }
    }
}

std::vector<change_probe::WatchEntry> updated_change_watch(
    const std::vector<change_probe::WatchEntry>& previous,
    const std::vector<change_probe::WatchEntry>& current,
    const std::set<std::string>& replaced_source_refs
) {
    std::vector<change_probe::WatchEntry> result;
    result.reserve(previous.size() + current.size());
    for (const auto& entry : previous) {
        if (!entry.directory && replaced_source_refs.contains(entry.source_ref)) {
            continue;
        }
        result.push_back(entry);
    }
    append_watch_entries(result, current);
    return result;
}

std::unordered_set<std::uint64_t> mutation_allowed_file_ids(
    const std::vector<change_probe::WatchEntry>& watch,
    const std::set<std::string>& source_refs
) {
    std::unordered_set<std::uint64_t> result;
    for (const auto& entry : watch) {
        if (entry.directory || source_refs.contains(entry.source_ref)) {
            result.insert(entry.file_id);
        }
    }
    return result;
}

struct CheckpointPersistenceResult {
    bool persisted = false;
    bool completed_within_deadline = false;
};

struct SnapshotCurrentResult {
    bool current = false;
    bool completed = false;
    bool deadline_exhausted = false;
};

SnapshotCurrentResult verified_snapshot_is_current(
    const std::filesystem::path& index_path,
    const std::string& product,
    const Snapshot& expected,
    const std::chrono::steady_clock::time_point deadline
) {
    SnapshotCurrentResult result;
    if (!expected.proof || deadline_expired(deadline)) {
        result.deadline_exhausted = deadline_expired(deadline);
        return result;
    }

    sqlite3* db = nullptr;
    const int open_rc = sqlite3_open_v2(
        index_path.string().c_str(),
        &db,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (open_rc != SQLITE_OK) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return result;
    }
    try {
        if (deadline_expired(deadline)) {
            result.deadline_exhausted = true;
            sqlite3_close(db);
            return result;
        }
        if (sqlite3_busy_timeout(db, 0) != SQLITE_OK) {
            sqlite3_close(db);
            return result;
        }
        {
            Statement statement(
                db,
                "SELECT 1 FROM metadata_snapshots WHERE product = ? "
                "AND schema_version = ? AND snapshot_schema_version = ? "
                "AND status = 'ready' AND product_revision = ? "
                "AND canonical_write_revision = ? AND proof_kind = ? "
                "AND proof_root_volume_serial = ? AND proof_root_file_id = ? "
                "AND proof_journal_id = ? AND proof_verified_usn = ?",
                "verify current metadata snapshot");
            statement.bind_text(1, product);
            statement.bind_int(2, kMetadataIndexSchemaVersion);
            statement.bind_int(3, kMetadataSnapshotSchemaVersion);
            statement.bind_text(4, expected.product_revision);
            statement.bind_text(5, expected.canonical_write_revision);
            statement.bind_text(6, expected.proof->proof_kind);
            statement.bind_text(7, std::to_string(expected.proof->root.volume_serial));
            statement.bind_text(8, std::to_string(expected.proof->root.file_id));
            statement.bind_text(9, std::to_string(expected.proof->journal_id));
            statement.bind_int64(10, expected.proof->usn);
            result.current = statement.step() == SQLITE_ROW;
        }
        result.deadline_exhausted = deadline_expired(deadline);
        result.completed = !result.deadline_exhausted;
        sqlite3_close(db);
        return result;
    } catch (...) {
        result.deadline_exhausted = deadline_expired(deadline);
        sqlite3_close(db);
        return result;
    }
}

CheckpointPersistenceResult compare_and_swap_verified_checkpoint(
    const std::filesystem::path& index_path,
    const std::string& product,
    const Snapshot& expected,
    const change_probe::Checkpoint& endpoint,
    const std::chrono::steady_clock::time_point deadline
) {
    if (!expected.proof ||
        endpoint.proof_kind != expected.proof->proof_kind ||
        endpoint.root != expected.proof->root ||
        endpoint.journal_id != expected.proof->journal_id ||
        endpoint.usn < expected.proof->usn || deadline_expired(deadline)) {
        return {};
    }

    sqlite3* db = nullptr;
    const int open_rc = sqlite3_open_v2(
        index_path.string().c_str(),
        &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (open_rc != SQLITE_OK) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return {};
    }
    if (deadline_expired(deadline)) {
        sqlite3_close(db);
        return {};
    }
    try {
        if (sqlite3_busy_timeout(db, checkpoint_busy_wait_ms(deadline)) != SQLITE_OK) {
            sqlite3_close(db);
            return {};
        }
        if (deadline_expired(deadline)) {
            sqlite3_close(db);
            return {};
        }
        bool advanced = false;
        bool attempted = false;
        {
            Statement statement(
                db,
                "UPDATE metadata_snapshots SET proof_verified_usn = "
                "MAX(proof_verified_usn, ?) WHERE product = ? "
                "AND schema_version = ? AND snapshot_schema_version = ? "
                "AND status = 'ready' AND product_revision = ? "
                "AND canonical_write_revision = ? AND proof_kind = ? "
                "AND proof_status = 'verified' AND proof_root_volume_serial = ? "
                "AND proof_root_file_id = ? AND proof_journal_id = ? "
                "AND proof_verified_usn >= ?",
                "advance verified metadata change checkpoint");
            if (!deadline_expired(deadline)) {
                statement.bind_int64(1, endpoint.usn);
                statement.bind_text(2, product);
                statement.bind_int(3, kMetadataIndexSchemaVersion);
                statement.bind_int(4, kMetadataSnapshotSchemaVersion);
                statement.bind_text(5, expected.product_revision);
                statement.bind_text(6, expected.canonical_write_revision);
                statement.bind_text(7, expected.proof->proof_kind);
                statement.bind_text(8, std::to_string(expected.proof->root.volume_serial));
                statement.bind_text(9, std::to_string(expected.proof->root.file_id));
                statement.bind_text(10, std::to_string(expected.proof->journal_id));
                statement.bind_int64(11, expected.proof->usn);
                if (!deadline_expired(deadline)) {
                    attempted = true;
                    statement.step_done();
                    advanced = sqlite3_changes(db) == 1;
                }
            }
        }
        const bool completed_within_deadline =
            attempted && !deadline_expired(deadline);
        sqlite3_close(db);
        return {
            advanced,
            completed_within_deadline && !deadline_expired(deadline),
        };
    } catch (...) {
        sqlite3_close(db);
        return {};
    }
}

bool advance_verified_checkpoint(
    const std::filesystem::path& index_path,
    const std::string& product,
    const Snapshot& expected,
    const change_probe::Checkpoint& endpoint
) {
    if (!expected.proof || endpoint.proof_kind != expected.proof->proof_kind ||
        endpoint.root != expected.proof->root ||
        endpoint.journal_id != expected.proof->journal_id ||
        endpoint.usn < expected.proof->usn) {
        return false;
    }
    if (expected.proof->usn == endpoint.usn) {
        return true;
    }
    return compare_and_swap_verified_checkpoint(
        index_path,
        product,
        expected,
        endpoint,
        std::chrono::steady_clock::time_point::max()).persisted;
}

void replace_change_watch(
    sqlite3* db,
    const std::string& product,
    const std::vector<change_probe::WatchEntry>& entries
) {
    {
        Statement statement(
            db,
            "DELETE FROM metadata_change_watch WHERE product = ?",
            "clear metadata change watch");
        statement.bind_text(1, product);
        statement.step_done();
    }
    Statement statement(
        db,
        "INSERT INTO metadata_change_watch(product, source_ref, file_id, is_directory) "
        "VALUES (?, ?, ?, ?)",
        "write metadata change watch");
    for (const auto& entry : entries) {
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
        statement.bind_text(1, product);
        statement.bind_text(2, entry.source_ref);
        statement.bind_text(3, std::to_string(entry.file_id));
        statement.bind_int(4, entry.directory ? 1 : 0);
        statement.step_done();
    }
}

void bind_optional_text(const Statement& statement, int index, const std::optional<std::string>& value) {
    if (value && !value->empty()) {
        statement.bind_text(index, *value);
    } else {
        statement.bind_null(index);
    }
}

void upsert_index_row(sqlite3* db, const IndexItem& item) {
    Statement statement(
        db,
        "INSERT INTO items "
        "(product, id, uid, type, title, state, priority, parent_ref, duplicate_of, slug, "
        "source_ref, source_hash, source_size, source_mtime_ns, estimated_tokens, updated) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(product, id) DO UPDATE SET "
        "uid=excluded.uid, type=excluded.type, title=excluded.title, state=excluded.state, "
        "priority=excluded.priority, parent_ref=excluded.parent_ref, "
        "duplicate_of=excluded.duplicate_of, slug=excluded.slug, "
        "source_ref=excluded.source_ref, source_hash=excluded.source_hash, "
        "source_size=excluded.source_size, source_mtime_ns=excluded.source_mtime_ns, "
        "estimated_tokens=excluded.estimated_tokens, updated=excluded.updated",
        "upsert metadata index row");
    statement.bind_text(1, item.product);
    statement.bind_text(2, item.id);
    statement.bind_text(3, item.uid);
    statement.bind_text(4, to_string(item.type));
    statement.bind_text(5, item.title);
    statement.bind_text(6, to_string(item.state));
    bind_optional_text(statement, 7, item.priority);
    bind_optional_text(statement, 8, item.parent);
    bind_optional_text(statement, 9, item.duplicate_of);
    statement.bind_text(10, item.slug);
    statement.bind_text(11, item.source_ref);
    statement.bind_text(12, item.source_hash);
    statement.bind_int64(13, static_cast<std::int64_t>(item.source_size));
    statement.bind_int64(14, item.source_mtime_ns);
    statement.bind_int64(15, static_cast<std::int64_t>(item.estimated_tokens));
    statement.bind_text(16, item.updated);
    statement.step_done();
}

struct RowRevisions {
    std::string inventory;
    std::string content;
    std::size_t count = 0;
};

RowRevisions revisions_from_index_items(const std::vector<IndexItem>& items) {
    std::vector<const IndexItem*> ordered;
    ordered.reserve(items.size());
    for (const auto& item : items) {
        ordered.push_back(&item);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->source_ref < right->source_ref;
    });

    Fnv1a64 inventory;
    Fnv1a64 content;
    inventory.update("kob-metadata-inventory-v1\n");
    content.update("kob-metadata-content-v1\n");
    for (const auto* item : ordered) {
        SourceFingerprint source;
        source.source_ref = item->source_ref;
        source.size = item->source_size;
        source.mtime_ns = item->source_mtime_ns;
        update_inventory_hash(inventory, source);
        content.update(item->source_ref);
        content.update(std::string_view{"\0", 1});
        content.update(item->source_hash);
        content.update("\n");
    }
    return {inventory.final(), content.final(), items.size()};
}

RowRevisions canonical_revisions(const CanonicalInventory& inventory) {
    return {
        inventory.revision,
        canonical_content_revision(inventory),
        inventory.sources.size(),
    };
}

std::optional<std::string> revision_mismatch_reason(
    const RowRevisions& indexed,
    const RowRevisions& canonical
) {
    if (indexed.count != canonical.count) {
        return "canonical_item_count_changed";
    }
    if (indexed.inventory != canonical.inventory) {
        return "canonical_revision_changed";
    }
    if (indexed.content != canonical.content) {
        return "canonical_content_changed";
    }
    return std::nullopt;
}

RowRevisions revisions_from_rows(sqlite3* db, const std::string& product) {
    Statement statement(
        db,
        "SELECT source_ref, source_size, source_mtime_ns, source_hash "
        "FROM items WHERE product = ? ORDER BY source_ref",
        "read metadata index revisions");
    statement.bind_text(1, product);
    Fnv1a64 inventory;
    Fnv1a64 content;
    inventory.update("kob-metadata-inventory-v1\n");
    content.update("kob-metadata-content-v1\n");
    RowRevisions result;
    while (statement.step() == SQLITE_ROW) {
        SourceFingerprint source;
        source.source_ref = column_text(statement.get(), 0);
        source.size = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 1));
        source.mtime_ns = static_cast<std::int64_t>(sqlite3_column_int64(statement.get(), 2));
        update_inventory_hash(inventory, source);
        content.update(source.source_ref);
        content.update(std::string_view{"\0", 1});
        content.update(column_text(statement.get(), 3));
        content.update("\n");
        ++result.count;
    }
    result.inventory = inventory.final();
    result.content = content.final();
    return result;
}

std::vector<std::string> query_tokens(const std::optional<std::string>& text) {
    std::vector<std::string> tokens;
    if (!text || text->empty()) {
        return tokens;
    }
    if (text->size() > kMaximumQueryTextBytes) {
        throw std::runtime_error("metadata_index_query_text_too_large");
    }
    std::istringstream input(lower_ascii(*text));
    std::string token;
    while (input >> token) {
        if (tokens.size() == kMaximumQueryTokens) {
            throw std::runtime_error("metadata_index_query_token_limit_exceeded");
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool text_matches(const IndexItem& item, const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        return true;
    }
    std::string haystack = lower_ascii(
        item.id + " " + item.uid + " " + item.title + " " + item.slug + " " +
        to_string(item.type) + " " + to_string(item.state) + " " +
        item.parent.value_or("") + " " + item.priority.value_or(""));
    return std::all_of(tokens.begin(), tokens.end(), [&](const auto& token) {
        return haystack.find(token) != std::string::npos;
    });
}

void validate_query(const IndexQuery& query) {
    if (query.limit == 0 || query.limit > kMaximumQueryLimit) {
        throw std::runtime_error("metadata_index_query_limit_out_of_range");
    }
    if (query.exact_ref && (query.exact_ref->size() > 160 ||
        query.exact_ref->find('/') != std::string::npos ||
        query.exact_ref->find('\\') != std::string::npos)) {
        throw std::runtime_error("metadata_index_exact_ref_must_be_canonical");
    }
    (void)query_tokens(query.text);
}

std::vector<IndexItem> read_index_rows(
    sqlite3* db,
    const std::string& product,
    const IndexQuery& query,
    std::size_t& invalid_rows
) {
    std::string sql =
        "SELECT id, uid, product, type, title, state, priority, parent_ref, duplicate_of, "
        "slug, source_ref, source_hash, source_size, source_mtime_ns, estimated_tokens, updated "
        "FROM items WHERE product = ?";
    std::vector<std::string> parameters{product};
    if (query.type) {
        sql += " AND type = ?";
        parameters.push_back(to_string(*query.type));
    }
    if (query.state) {
        sql += " AND state = ?";
        parameters.push_back(to_string(*query.state));
    }
    if (query.exact_ref) {
        sql += " AND (id = ? OR uid = ?)";
        parameters.push_back(*query.exact_ref);
        parameters.push_back(*query.exact_ref);
    }
    sql += " ORDER BY updated DESC, id ASC";

    Statement statement(db, sql.c_str(), "query metadata index");
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        statement.bind_text(static_cast<int>(index + 1), parameters[index]);
    }

    const auto tokens = query_tokens(query.text);
    std::vector<IndexItem> result;
    while (statement.step() == SQLITE_ROW) {
        IndexItem item;
        item.id = column_text(statement.get(), 0);
        item.uid = column_text(statement.get(), 1);
        item.product = column_text(statement.get(), 2);
        const auto type = parse_item_type(column_text(statement.get(), 3));
        const auto state = parse_item_state(column_text(statement.get(), 5));
        if (!type || !state) {
            ++invalid_rows;
            continue;
        }
        item.type = *type;
        item.title = column_text(statement.get(), 4);
        item.state = *state;
        item.priority = column_optional_text(statement.get(), 6);
        item.parent = column_optional_text(statement.get(), 7);
        item.duplicate_of = column_optional_text(statement.get(), 8);
        item.slug = column_text(statement.get(), 9);
        item.source_ref = column_text(statement.get(), 10);
        item.source_hash = column_text(statement.get(), 11);
        item.source_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 12));
        item.source_mtime_ns = static_cast<std::int64_t>(sqlite3_column_int64(statement.get(), 13));
        item.estimated_tokens = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 14));
        item.updated = column_text(statement.get(), 15);
        if (text_matches(item, tokens)) {
            result.push_back(std::move(item));
            if (result.size() == query.limit) {
                break;
            }
        }
    }
    return result;
}

struct CanonicalQueryResult {
    std::vector<IndexItem> items;
    std::size_t scanned_count = 0;
};

CanonicalQueryResult query_canonical(
    const std::filesystem::path& product_root,
    const std::string& product,
    const CanonicalInventory& inventory,
    const IndexQuery& query
) {
    validate_query(query);
    const auto tokens = query_tokens(query.text);
    CanonicalStore store(product_root);
    CanonicalQueryResult result;
    for (const auto& source : inventory.sources) {
        ++result.scanned_count;
        BacklogItem item;
        try {
            item = store.read_metadata(source.path);
        } catch (...) {
            throw std::runtime_error(
                "canonical_metadata_read_failed:" + source.source_ref);
        }
        auto indexed = index_item_from_metadata(item, product, source, {});
        if ((query.type && indexed.type != *query.type) ||
            (query.state && indexed.state != *query.state) ||
            (query.exact_ref && indexed.id != *query.exact_ref && indexed.uid != *query.exact_ref) ||
            !text_matches(indexed, tokens)) {
            continue;
        }
        result.items.push_back(std::move(indexed));
        if (result.items.size() == query.limit) {
            break;
        }
    }
    std::sort(result.items.begin(), result.items.end(), [](const auto& left, const auto& right) {
        if (left.updated != right.updated) {
            return left.updated > right.updated;
        }
        return left.id < right.id;
    });
    return result;
}

std::set<std::string> table_columns(sqlite3* db, const std::string& table) {
    Statement statement(db, ("PRAGMA table_info(" + table + ")").c_str(), "inspect table columns");
    std::set<std::string> result;
    while (statement.step() == SQLITE_ROW) {
        result.insert(column_text(statement.get(), 1));
    }
    return result;
}

bool metadata_columns_current(sqlite3* db) {
    const auto item_columns = table_columns(db, "items");
    const std::set<std::string> required_items = {
        "product", "id", "uid", "type", "title", "state", "priority", "parent_ref",
        "duplicate_of", "slug", "source_ref", "source_hash", "source_size",
        "source_mtime_ns", "estimated_tokens", "updated"
    };
    const auto snapshot_columns = table_columns(db, "metadata_snapshots");
    const std::set<std::string> required_snapshots = {
        "product", "schema_version", "snapshot_schema_version", "status",
        "index_revision", "content_revision", "revision_epoch", "product_revision",
        "canonical_write_revision", "proof_kind", "proof_status",
        "proof_root_volume_serial", "proof_root_file_id", "proof_journal_id",
        "proof_verified_usn", "item_count", "generation", "built_at", "reason"
    };
    const auto watch_columns = table_columns(db, "metadata_change_watch");
    const std::set<std::string> required_watch = {
        "product", "source_ref", "file_id", "is_directory"
    };
    return std::includes(
               item_columns.begin(), item_columns.end(),
               required_items.begin(), required_items.end()) &&
        std::includes(
               snapshot_columns.begin(), snapshot_columns.end(),
               required_snapshots.begin(), required_snapshots.end()) &&
        std::includes(
               watch_columns.begin(), watch_columns.end(),
               required_watch.begin(), required_watch.end());
}

double elapsed_ms(const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

IndexQueryResult fallback_query(
    const std::filesystem::path& product_root,
    const std::string& product,
    const IndexQuery& query,
    const std::string& status,
    const std::string& reason,
    const std::chrono::steady_clock::time_point start
) {
    const auto revision_start = std::chrono::steady_clock::now();
    const auto inventory = canonical_inventory(product_root);
    const auto revision_ms = elapsed_ms(revision_start);
    auto canonical = query_canonical(product_root, product, inventory, query);
    IndexQueryResult result;
    result.items = std::move(canonical.items);
    result.diagnostics.index_used = false;
    result.diagnostics.index_status = status;
    result.diagnostics.canonical_revision = inventory.revision;
    result.diagnostics.fallback_scan = true;
    result.diagnostics.scanned_count = canonical.scanned_count;
    result.diagnostics.matched_count = result.items.size();
    result.diagnostics.revision_check_ms = revision_ms;
    result.diagnostics.elapsed_ms = elapsed_ms(start);
    result.diagnostics.stale_reason = reason;
    result.diagnostics.recovery = "kob index rebuild --product " + product;
    return result;
}

} // namespace

BacklogIndex::BacklogIndex(
    const std::filesystem::path& db_path,
    std::optional<std::string> product_name,
    std::optional<std::filesystem::path> product_root
) : db_path_(db_path),
    product_name_(std::move(product_name)),
    product_root_(std::move(product_root)) {
    if (!db_path_.parent_path().empty()) {
        std::filesystem::create_directories(db_path_.parent_path());
    }
    const int open_rc = sqlite3_open(db_path_.string().c_str(), &db_);
    if (open_rc != SQLITE_OK) {
        const auto message = sqlite_error(db_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("metadata_index_open_failed: " + message);
    }
    const int timeout_rc = sqlite3_busy_timeout(db_, 5000);
    if (timeout_rc != SQLITE_OK) {
        throw_sqlite(db_, "configure bounded index lock wait", timeout_rc);
    }
}

BacklogIndex::~BacklogIndex() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void BacklogIndex::initialize() {
    if (initialized_) {
        return;
    }
    execute(
        "CREATE TABLE IF NOT EXISTS metadata_index_meta ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ")"
    );

    bool schema_current = false;
    {
        Statement statement(
            db_,
            "SELECT value FROM metadata_index_meta WHERE key = 'schema_version'",
            "read metadata schema version");
        if (statement.step() == SQLITE_ROW) {
            schema_current =
                column_text(statement.get(), 0) == std::to_string(kMetadataIndexSchemaVersion);
        }
    }
    if (schema_current) {
        schema_current = metadata_columns_current(db_);
    }

    if (!schema_current) {
        execute("BEGIN IMMEDIATE");
        try {
            execute("DROP TABLE IF EXISTS items");
            execute("DROP TABLE IF EXISTS metadata_snapshots");
            execute("DROP TABLE IF EXISTS metadata_change_watch");
            execute(
                "CREATE TABLE items ("
                "  product TEXT NOT NULL,"
                "  id TEXT NOT NULL,"
                "  uid TEXT NOT NULL,"
                "  type TEXT NOT NULL,"
                "  title TEXT NOT NULL,"
                "  state TEXT NOT NULL,"
                "  priority TEXT,"
                "  parent_ref TEXT,"
                "  duplicate_of TEXT,"
                "  slug TEXT NOT NULL,"
                "  source_ref TEXT NOT NULL,"
                "  source_hash TEXT NOT NULL,"
                "  source_size INTEGER NOT NULL,"
                "  source_mtime_ns INTEGER NOT NULL,"
                "  estimated_tokens INTEGER NOT NULL,"
                "  updated TEXT NOT NULL,"
                "  PRIMARY KEY(product, id),"
                "  UNIQUE(product, uid),"
                "  UNIQUE(product, source_ref)"
                ")"
            );
            execute(
                "CREATE TABLE metadata_snapshots ("
                "  product TEXT PRIMARY KEY,"
                "  schema_version INTEGER NOT NULL,"
                "  snapshot_schema_version INTEGER NOT NULL,"
                "  status TEXT NOT NULL,"
                "  index_revision TEXT NOT NULL,"
                "  content_revision TEXT NOT NULL,"
                "  revision_epoch TEXT NOT NULL,"
                "  product_revision TEXT NOT NULL,"
                "  canonical_write_revision TEXT NOT NULL,"
                "  proof_kind TEXT NOT NULL,"
                "  proof_status TEXT NOT NULL,"
                "  proof_root_volume_serial TEXT NOT NULL,"
                "  proof_root_file_id TEXT NOT NULL,"
                "  proof_journal_id TEXT NOT NULL,"
                "  proof_verified_usn INTEGER NOT NULL,"
                "  item_count INTEGER NOT NULL,"
                "  generation INTEGER NOT NULL,"
                "  built_at TEXT NOT NULL,"
                "  reason TEXT"
                ")"
            );
            execute(
                "CREATE TABLE metadata_change_watch ("
                "  product TEXT NOT NULL,"
                "  source_ref TEXT NOT NULL,"
                "  file_id TEXT NOT NULL,"
                "  is_directory INTEGER NOT NULL CHECK(is_directory IN (0, 1)),"
                "  PRIMARY KEY(product, file_id, source_ref)"
                ")"
            );
            {
                Statement statement(
                    db_,
                    "INSERT INTO metadata_index_meta(key, value) VALUES('schema_version', ?) "
                    "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                    "write metadata schema version");
                statement.bind_text(1, std::to_string(kMetadataIndexSchemaVersion));
                statement.step_done();
            }
            execute("COMMIT");
        } catch (...) {
            try {
                execute("ROLLBACK");
            } catch (...) {
            }
            throw;
        }
    }

    execute(
        "CREATE TABLE IF NOT EXISTS metadata_snapshots ("
        "  product TEXT PRIMARY KEY,"
        "  schema_version INTEGER NOT NULL,"
        "  snapshot_schema_version INTEGER NOT NULL,"
        "  status TEXT NOT NULL,"
        "  index_revision TEXT NOT NULL,"
        "  content_revision TEXT NOT NULL,"
        "  revision_epoch TEXT NOT NULL,"
        "  product_revision TEXT NOT NULL,"
        "  canonical_write_revision TEXT NOT NULL,"
        "  proof_kind TEXT NOT NULL,"
        "  proof_status TEXT NOT NULL,"
        "  proof_root_volume_serial TEXT NOT NULL,"
        "  proof_root_file_id TEXT NOT NULL,"
        "  proof_journal_id TEXT NOT NULL,"
        "  proof_verified_usn INTEGER NOT NULL,"
        "  item_count INTEGER NOT NULL,"
        "  generation INTEGER NOT NULL,"
        "  built_at TEXT NOT NULL,"
        "  reason TEXT"
        ")"
    );
    execute(
        "CREATE TABLE IF NOT EXISTS metadata_change_watch ("
        "  product TEXT NOT NULL,"
        "  source_ref TEXT NOT NULL,"
        "  file_id TEXT NOT NULL,"
        "  is_directory INTEGER NOT NULL CHECK(is_directory IN (0, 1)),"
        "  PRIMARY KEY(product, file_id, source_ref)"
        ")"
    );
    execute("CREATE INDEX IF NOT EXISTS idx_items_product_type ON items(product, type)");
    execute("CREATE INDEX IF NOT EXISTS idx_items_product_state ON items(product, state)");
    execute("CREATE INDEX IF NOT EXISTS idx_items_product_parent ON items(product, parent_ref)");
    execute("CREATE INDEX IF NOT EXISTS idx_items_product_priority ON items(product, priority)");
    execute("CREATE INDEX IF NOT EXISTS idx_items_product_updated ON items(product, updated DESC)");
    execute(
        "CREATE TABLE IF NOT EXISTS id_sequences ("
        "  prefix TEXT NOT NULL,"
        "  type_code TEXT NOT NULL,"
        "  next_number INTEGER NOT NULL DEFAULT 1,"
        "  PRIMARY KEY (prefix, type_code)"
        ")"
    );
    execute(
        "CREATE TABLE IF NOT EXISTS id_reservations ("
        "  prefix TEXT NOT NULL,"
        "  type_code TEXT NOT NULL,"
        "  number INTEGER NOT NULL,"
        "  owner TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "  committed_at INTEGER,"
        "  PRIMARY KEY (prefix, type_code, number)"
        ")"
    );
    initialized_ = true;
}

void BacklogIndex::index_item(const BacklogItem& item) {
    initialize();
    if (!item.file_path) {
        throw std::runtime_error("metadata_index_item_has_no_source");
    }

    const auto root = product_root_.value_or(infer_product_root(*item.file_path));
    const auto product = product_name_.value_or(infer_product_name(root));
    const auto previous = read_snapshot(db_, product);
    const auto previous_mode = snapshot_readiness_mode(previous);
    const auto previous_watch = previous_mode == SnapshotReadinessMode::VerifiedNtfs
        ? read_change_watch(db_, product)
        : std::nullopt;
    IndexItem indexed;
    CanonicalWriteRevision write_revision;
    std::optional<change_probe::Checkpoint> proof_endpoint;
    std::optional<RowRevisions> portable_canonical;
    std::vector<change_probe::WatchEntry> next_watch;
    std::string failure_reason = "mutation_read_after_write_failed";
    try {
        SourceFingerprint source;
        source.path = *item.file_path;
        source.source_ref = bounded_source_ref(source.path, root);
        CanonicalStore store(root);
        const auto readback = store.read(source.path);
        const auto stat = source_stat_checked(source.path);
        source.size = stat.size;
        source.mtime_ns = stat.mtime_ns;
        indexed = index_item_from_metadata(
            readback, product, source, hash_file(source.path, source.size));
        if (previous_mode != SnapshotReadinessMode::Invalid) {
            write_revision = store.read_write_revision();
            if (write_revision.current == previous.canonical_write_revision) {
                failure_reason = "mutation_read_after_write_failed";
                throw std::runtime_error("metadata_index_mutation_read_after_write_mismatch");
            }
            const bool receipt_contiguous = write_revision.previous
                && *write_revision.previous == previous.canonical_write_revision
                && write_receipt_matches(write_revision, indexed);
            if (!receipt_contiguous) {
                failure_reason = "canonical_mutation_receipt_invalid";
            } else if (previous_mode == SnapshotReadinessMode::VerifiedNtfs &&
                       (!previous.proof || !previous_watch)) {
                failure_reason = "canonical_mutation_receipt_invalid";
            } else if (previous_mode == SnapshotReadinessMode::PortableUnsupported &&
                       !change_watch_rows_empty(db_, product)) {
                failure_reason = "change_proof_watch_invalid";
            } else if (previous_mode == SnapshotReadinessMode::VerifiedNtfs) {
                const auto current_watch = change_probe::capture_watch_set(
                    root, {{source.source_ref, source.path, false}});
                if (current_watch.status != change_probe::Status::Clean) {
                    failure_reason = std::string(change_probe::reason(current_watch.status));
                    throw std::runtime_error(std::string(change_probe::reason(current_watch.status)));
                }
                auto verification_watch = *previous_watch;
                append_watch_entries(verification_watch, current_watch.entries);
                const std::set<std::string> changed_sources{source.source_ref};
                const auto proof = change_probe::verify_window(
                    root,
                    *previous.proof,
                    verification_watch,
                    mutation_allowed_file_ids(verification_watch, changed_sources));
                if (proof.status != change_probe::Status::Clean || !proof.endpoint) {
                    failure_reason = std::string(change_probe::reason(proof.status));
                    throw std::runtime_error(std::string(change_probe::reason(proof.status)));
                }
                const auto final_stat = source_stat_checked(source.path);
                if (final_stat.size != indexed.source_size
                    || hash_file(source.path, final_stat.size) != indexed.source_hash) {
                    failure_reason = "mutation_read_after_write_failed";
                    throw std::runtime_error("metadata_index_mutation_read_after_write_mismatch");
                }
                proof_endpoint = proof.endpoint;
                next_watch = updated_change_watch(
                    *previous_watch, current_watch.entries, changed_sources);
            } else {
                const auto final_stat = source_stat_checked(source.path);
                if (final_stat.size != indexed.source_size ||
                    hash_file(source.path, final_stat.size) != indexed.source_hash) {
                    failure_reason = "mutation_read_after_write_failed";
                    throw std::runtime_error(
                        "metadata_index_mutation_read_after_write_mismatch");
                }
                portable_canonical = canonical_revisions(canonical_inventory(root));
                const auto write_validated = store.read_write_revision();
                if (write_validated.current != write_revision.current) {
                    portable_canonical.reset();
                    failure_reason = "canonical_mutation_receipt_invalid";
                }
            }
        }
    } catch (const std::exception&) {
        if (snapshot_structurally_ready(previous)) {
            try {
                if (previous_mode == SnapshotReadinessMode::PortableUnsupported) {
                    replace_change_watch(db_, product, {});
                }
                invalidate_metadata(product, failure_reason);
            } catch (const std::exception&) {
            }
        }
        throw std::runtime_error("metadata_index_" + failure_reason);
    }

    execute("BEGIN IMMEDIATE");
    try {
        upsert_index_row(db_, indexed);
        const auto revisions = revisions_from_rows(db_, product);
        const bool verified_ready =
            previous_mode == SnapshotReadinessMode::VerifiedNtfs &&
            proof_endpoint.has_value();
        const bool portable_ready =
            previous_mode == SnapshotReadinessMode::PortableUnsupported &&
            portable_canonical &&
            !revision_mismatch_reason(revisions, *portable_canonical);
        const bool remains_ready = verified_ready || portable_ready;
        if (previous_mode == SnapshotReadinessMode::PortableUnsupported &&
            portable_canonical && !portable_ready) {
            failure_reason = "mutation_read_after_write_failed";
        }
        const auto generation = remains_ready
            ? next_generation(previous)
            : previous.generation;
        const auto product_revision = remains_ready
            ? format_product_revision(previous.revision_epoch, generation)
            : previous.product_revision;
        if (verified_ready) {
            replace_change_watch(db_, product, next_watch);
        } else if (previous_mode == SnapshotReadinessMode::PortableUnsupported) {
            replace_change_watch(db_, product, {});
        }
        write_snapshot(
            db_,
            product,
            remains_ready ? "ready" : "incomplete",
            revisions.inventory,
            revisions.content,
            previous.revision_epoch,
            product_revision,
            remains_ready
                ? write_revision.current
                : previous.canonical_write_revision,
            revisions.count,
            generation,
            remains_ready
                ? std::optional<std::string>{}
                : std::optional<std::string>{
                    snapshot_structurally_ready(previous)
                        ? failure_reason
                        : "rebuild_required_before_index_reads"},
            verified_ready ? proof_endpoint : std::nullopt);
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        try {
            invalidate_metadata(product, "mutation_index_commit_failed");
        } catch (...) {
        }
        throw;
    }
}

void BacklogIndex::index_materialized_item(
    const BacklogItem& item,
    const std::string& canonical_content
) {
    index_materialized_items({MaterializedIndexInput{item, canonical_content}});
}

void BacklogIndex::index_materialized_items(
    const std::vector<MaterializedIndexInput>& items
) {
    initialize();
    if (items.empty()) {
        return;
    }

    std::vector<IndexItem> indexed_items;
    indexed_items.reserve(items.size());
    std::map<std::string, Snapshot> previous;
    for (const auto& input : items) {
        if (!input.item.file_path) {
            throw std::runtime_error("metadata_index_item_has_no_source");
        }
        if (input.canonical_content.size() > kMaximumIndexedItemBytes) {
            throw std::runtime_error("metadata_index_source_too_large");
        }
        const auto root = product_root_.value_or(infer_product_root(*input.item.file_path));
        const auto product = product_name_.value_or(infer_product_name(root));
        SourceFingerprint source;
        source.path = *input.item.file_path;
        source.source_ref = bounded_source_ref(source.path, root);
        source.size = input.canonical_content.size();
        source.mtime_ns = 0;
        indexed_items.push_back(index_item_from_metadata(
            input.item, product, source, hash_content(input.canonical_content)));
        previous.try_emplace(product, read_snapshot(db_, product));
    }

    execute("BEGIN IMMEDIATE");
    try {
        for (const auto& indexed : indexed_items) {
            upsert_index_row(db_, indexed);
        }
        for (const auto& [product, before] : previous) {
            const auto revisions = revisions_from_rows(db_, product);
            write_snapshot(
                db_,
                product,
                "incomplete",
                revisions.inventory,
                revisions.content,
                before.revision_epoch,
                before.product_revision,
                before.canonical_write_revision,
                revisions.count,
                before.generation,
                "staged_materialization_requires_rebuild");
        }
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

void BacklogIndex::remove_item(const std::string& id) {
    remove_items({id});
}

void BacklogIndex::remove_items(const std::vector<std::string>& ids) {
    initialize();
    std::map<std::string, std::vector<std::string>> ids_by_product;
    for (const auto& id : ids) {
        if (product_name_ && !product_name_->empty()) {
            ids_by_product[*product_name_].push_back(id);
        } else {
            Statement statement(
                db_,
                "SELECT DISTINCT product FROM items WHERE id = ? ORDER BY product",
                "resolve metadata row products");
            statement.bind_text(1, id);
            while (statement.step() == SQLITE_ROW) {
                ids_by_product[column_text(statement.get(), 0)].push_back(id);
            }
        }
    }
    if (ids_by_product.empty()) {
        return;
    }

    std::map<std::string, std::set<std::string>> source_refs_by_product;
    for (const auto& [product, product_ids] : ids_by_product) {
        for (const auto& id : product_ids) {
            Statement statement(
                db_,
                "SELECT source_ref FROM items WHERE product = ? AND id = ?",
                "resolve removed metadata source");
            statement.bind_text(1, product);
            statement.bind_text(2, id);
            if (statement.step() == SQLITE_ROW) {
                source_refs_by_product[product].insert(column_text(statement.get(), 0));
            }
        }
    }

    std::map<std::string, Snapshot> previous;
    std::map<std::string, CanonicalWriteRevision> write_revisions;
    std::map<std::string, change_probe::Checkpoint> proof_endpoints;
    std::map<std::string, RowRevisions> portable_canonical_revisions;
    std::map<std::string, std::vector<change_probe::WatchEntry>> next_watches;
    std::map<std::string, std::string> proof_failures;
    for (const auto& [product, _] : ids_by_product) {
        const auto before = read_snapshot(db_, product);
        previous.emplace(product, before);
        proof_failures[product] = "change_proof_unavailable";
        const auto mode = snapshot_readiness_mode(before);
        if (mode == SnapshotReadinessMode::Invalid || !product_root_) {
            continue;
        }
        try {
            const auto receipt = CanonicalStore(*product_root_).read_write_revision();
            write_revisions.emplace(product, receipt);
            const auto source_refs = source_refs_by_product[product];
            if (!receipt.previous ||
                *receipt.previous != before.canonical_write_revision
                || !delete_receipt_matches(receipt, source_refs)) {
                proof_failures[product] = "canonical_mutation_receipt_invalid";
                continue;
            }
            const auto canonical_sources_absent = [&]() {
                for (const auto& source_ref : source_refs) {
                    const auto source_path = materialize_source_path(
                        db_path_, product_root_, product, source_ref);
                    std::error_code error;
                    if (!source_path ||
                        std::filesystem::exists(*source_path, error) || error) {
                        return false;
                    }
                }
                return true;
            };
            if (mode == SnapshotReadinessMode::VerifiedNtfs) {
                const auto watch = read_change_watch(db_, product);
                if (!before.proof || !watch) {
                    proof_failures[product] = "canonical_mutation_receipt_invalid";
                    continue;
                }
                const auto proof = change_probe::verify_window(
                    *product_root_,
                    *before.proof,
                    *watch,
                    mutation_allowed_file_ids(*watch, source_refs));
                if (proof.status != change_probe::Status::Clean || !proof.endpoint) {
                    proof_failures[product] =
                        std::string(change_probe::reason(proof.status));
                    continue;
                }
                if (!canonical_sources_absent()) {
                    proof_failures[product] = "canonical_delete_readback_failed";
                    continue;
                }
                proof_endpoints.emplace(product, *proof.endpoint);
                next_watches.emplace(
                    product,
                    updated_change_watch(*watch, {}, source_refs));
            } else {
                if (!change_watch_rows_empty(db_, product)) {
                    proof_failures[product] = "change_proof_watch_invalid";
                    continue;
                }
                if (!canonical_sources_absent()) {
                    proof_failures[product] = "canonical_delete_readback_failed";
                    continue;
                }
                const auto canonical = canonical_revisions(
                    canonical_inventory(*product_root_));
                const auto write_validated =
                    CanonicalStore(*product_root_).read_write_revision();
                if (write_validated.current != receipt.current) {
                    proof_failures[product] = "canonical_mutation_receipt_invalid";
                    continue;
                }
                portable_canonical_revisions.emplace(product, canonical);
            }
        } catch (const std::exception&) {
            proof_failures[product] =
                mode == SnapshotReadinessMode::PortableUnsupported
                ? "mutation_read_after_write_failed"
                : "change_proof_unavailable";
        }
    }

    execute("BEGIN IMMEDIATE");
    try {
        for (const auto& [product, product_ids] : ids_by_product) {
            for (const auto& id : product_ids) {
                Statement statement(
                    db_,
                    "DELETE FROM items WHERE product = ? AND id = ?",
                    "remove metadata index row");
                statement.bind_text(1, product);
                statement.bind_text(2, id);
                statement.step_done();
            }

            const auto revisions = revisions_from_rows(db_, product);
            const auto& before = previous.at(product);
            const auto mode = snapshot_readiness_mode(before);
            const auto write_revision = write_revisions.find(product);
            const auto proof_endpoint = proof_endpoints.find(product);
            const auto portable_canonical =
                portable_canonical_revisions.find(product);
            const bool verified_ready =
                mode == SnapshotReadinessMode::VerifiedNtfs &&
                write_revision != write_revisions.end() &&
                proof_endpoint != proof_endpoints.end();
            const bool portable_ready =
                mode == SnapshotReadinessMode::PortableUnsupported &&
                write_revision != write_revisions.end() &&
                portable_canonical != portable_canonical_revisions.end() &&
                !revision_mismatch_reason(
                    revisions, portable_canonical->second);
            const bool remains_ready = verified_ready || portable_ready;
            if (mode == SnapshotReadinessMode::PortableUnsupported &&
                portable_canonical != portable_canonical_revisions.end() &&
                !portable_ready) {
                proof_failures[product] = "mutation_read_after_write_failed";
            }
            const auto generation = remains_ready
                ? next_generation(before)
                : before.generation;
            const auto product_revision = remains_ready
                ? format_product_revision(before.revision_epoch, generation)
                : before.product_revision;
            if (verified_ready) {
                replace_change_watch(db_, product, next_watches.at(product));
            } else if (mode == SnapshotReadinessMode::PortableUnsupported) {
                replace_change_watch(db_, product, {});
            }
            write_snapshot(
                db_,
                product,
                remains_ready ? "ready" : "incomplete",
                revisions.inventory,
                revisions.content,
                before.revision_epoch,
                product_revision,
                remains_ready
                    ? write_revision->second.current
                    : before.canonical_write_revision,
                revisions.count,
                generation,
                remains_ready
                    ? std::optional<std::string>{}
                    : std::optional<std::string>{
                        snapshot_structurally_ready(before)
                            ? proof_failures.at(product)
                            : "rebuild_required_before_index_reads"},
                verified_ready
                    ? std::optional<change_probe::Checkpoint>{proof_endpoint->second}
                    : std::nullopt);
        }
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

void BacklogIndex::invalidate_metadata(const std::string& product, const std::string& reason) {
    initialize();
    const auto snapshot = read_snapshot(db_, product);
    if (!snapshot.exists) {
        return;
    }
    write_snapshot(
        db_,
        product,
        "stale",
        snapshot.index_revision,
        snapshot.content_revision,
        snapshot.revision_epoch,
        snapshot.product_revision,
        snapshot.canonical_write_revision,
        snapshot.item_count,
        snapshot.generation,
        reason);
}

void BacklogIndex::rebuild_metadata(
    const std::filesystem::path& product_root,
    const std::string& product,
    const RebuildMetadataTestHooks& test_hooks
) {
    initialize();
    if (product.empty()) {
        throw std::runtime_error("metadata_index_product_required");
    }

    const auto proof_start = change_probe::capture_checkpoint(product_root);
    SnapshotReadinessMode rebuild_mode = SnapshotReadinessMode::Invalid;
    if (proof_start.status == change_probe::Status::Clean && proof_start.checkpoint) {
        rebuild_mode = SnapshotReadinessMode::VerifiedNtfs;
    } else if (proof_start.status == change_probe::Status::Unsupported &&
               !proof_start.checkpoint) {
        rebuild_mode = SnapshotReadinessMode::PortableUnsupported;
    } else {
        throw std::runtime_error(
            "metadata_index_change_proof_capture_failed:" +
            std::string(change_probe::reason(proof_start.status)));
    }
    CanonicalStore store(product_root);
    CanonicalWriteRevision write_start;
    try {
        write_start = store.read_write_revision();
    } catch (const std::exception&) {
        write_start = store.reset_write_revision();
    }
    const auto inventory = canonical_inventory(product_root);
    std::vector<IndexItem> rows;
    rows.reserve(inventory.sources.size());
    for (const auto& source : inventory.sources) {
        try {
            const auto item = store.read_metadata(source.path);
            rows.push_back(index_item_from_metadata(
                item, product, source, hash_file(source.path, source.size)));
        } catch (const std::exception&) {
            throw std::runtime_error(
                "metadata_index_rebuild_source_failed:" + source.source_ref);
        }
    }
    const auto built_revisions = revisions_from_index_items(rows);
    if (built_revisions.inventory != inventory.revision ||
        built_revisions.count != inventory.sources.size()) {
        throw std::runtime_error("metadata_index_rebuild_revision_mismatch");
    }
    const auto watch = change_probe::capture_watch_set(
        product_root, canonical_watch_paths(product_root, inventory));
    const bool verified_watch =
        rebuild_mode == SnapshotReadinessMode::VerifiedNtfs &&
        watch.status == change_probe::Status::Clean;
    const bool portable_watch =
        rebuild_mode == SnapshotReadinessMode::PortableUnsupported &&
        watch.status == change_probe::Status::Unsupported && watch.entries.empty();
    if (!verified_watch && !portable_watch) {
        throw std::runtime_error(
            "metadata_index_change_watch_capture_failed:" +
            std::string(change_probe::reason(watch.status)));
    }
    if (test_hooks.after_change_watch_capture) {
        test_hooks.after_change_watch_capture();
    }
    const auto write_end = store.read_write_revision();
    if (write_end.current != write_start.current) {
        throw std::runtime_error("metadata_index_canonical_changed_during_rebuild");
    }

    std::optional<change_probe::Checkpoint> proof_endpoint;
    std::optional<RowRevisions> portable_validation;
    if (rebuild_mode == SnapshotReadinessMode::VerifiedNtfs) {
        const auto proof_end = change_probe::verify_window(
            product_root, *proof_start.checkpoint, watch.entries);
        if (proof_end.status != change_probe::Status::Clean || !proof_end.endpoint) {
            throw std::runtime_error(
                "metadata_index_canonical_changed_during_rebuild:" +
                std::string(change_probe::reason(proof_end.status)));
        }
        proof_endpoint = proof_end.endpoint;
    } else {
        const auto validation_inventory = canonical_inventory(product_root);
        portable_validation = canonical_revisions(validation_inventory);
        if (const auto mismatch = revision_mismatch_reason(
                built_revisions, *portable_validation)) {
            throw std::runtime_error(
                "metadata_index_canonical_changed_during_rebuild:" + *mismatch);
        }
        CanonicalWriteRevision write_validated;
        try {
            write_validated = store.read_write_revision();
        } catch (const std::exception&) {
            throw std::runtime_error(
                "metadata_index_canonical_changed_during_rebuild:"
                "canonical_write_revision_unavailable");
        }
        if (write_validated.current != write_end.current) {
            throw std::runtime_error("metadata_index_canonical_changed_during_rebuild");
        }
    }

    const auto previous = read_snapshot(db_, product);
    execute("BEGIN IMMEDIATE");
    try {
        {
            Statement statement(
                db_,
                "DELETE FROM items WHERE product = ?",
                "clear product metadata rows");
            statement.bind_text(1, product);
            statement.step_done();
        }
        for (const auto& row : rows) {
            upsert_index_row(db_, row);
        }
        const auto revisions = revisions_from_rows(db_, product);
        if (revision_mismatch_reason(revisions, built_revisions) ||
            (portable_validation &&
             revision_mismatch_reason(revisions, *portable_validation))) {
            throw std::runtime_error("metadata_index_rebuild_revision_mismatch");
        }
        replace_change_watch(
            db_,
            product,
            rebuild_mode == SnapshotReadinessMode::VerifiedNtfs
                ? watch.entries
                : std::vector<change_probe::WatchEntry>{});
        const auto generation = next_generation(previous);
        const auto revision_epoch = previous.revision_epoch.empty()
            ? new_revision_epoch()
            : previous.revision_epoch;
        write_snapshot(
            db_,
            product,
            "ready",
            revisions.inventory,
            revisions.content,
            revision_epoch,
            format_product_revision(revision_epoch, generation),
            write_end.current,
            revisions.count,
            generation,
            std::nullopt,
            proof_endpoint);
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

IndexQueryResult BacklogIndex::query_metadata(
    const std::filesystem::path& product_root,
    const std::string& product,
    const IndexQuery& query,
    const QueryMetadataTestHooks& test_hooks
) {
    const auto start = std::chrono::steady_clock::now();
    validate_query(query);
    initialize();

    const auto revision_start = std::chrono::steady_clock::now();
    const auto snapshot = read_snapshot(db_, product);
    const auto readiness = snapshot_readiness_mode(snapshot);
    std::optional<CanonicalWriteRevision> write_revision;
    try {
        write_revision = CanonicalStore(product_root).read_write_revision();
    } catch (const std::exception&) {
    }
    const auto watch = readiness == SnapshotReadinessMode::VerifiedNtfs
        ? read_change_watch(db_, product)
        : std::nullopt;
    std::string change_proof_failure;
    if (readiness == SnapshotReadinessMode::VerifiedNtfs && write_revision
        && write_revision->current == snapshot.canonical_write_revision) {
        if (!snapshot.proof || !watch) {
            change_proof_failure = "change_proof_watch_invalid";
        } else {
            const auto proof = change_probe::verify_window(
                product_root, *snapshot.proof, *watch);
            if (proof.status != change_probe::Status::Clean || !proof.endpoint) {
                change_proof_failure = std::string(change_probe::reason(proof.status));
            } else if (!advance_verified_checkpoint(
                           db_path_, product, snapshot, *proof.endpoint)) {
                change_proof_failure = "change_proof_checkpoint_commit_failed";
            }
        }
    } else if (readiness == SnapshotReadinessMode::PortableUnsupported &&
               write_revision &&
               write_revision->current == snapshot.canonical_write_revision &&
               !change_watch_rows_empty(db_, product)) {
        change_proof_failure = "change_proof_watch_invalid";
    }
    if (test_hooks.after_change_proof_verification) {
        test_hooks.after_change_proof_verification();
    }

    const bool snapshot_ready = readiness != SnapshotReadinessMode::Invalid &&
        write_revision &&
        write_revision->current == snapshot.canonical_write_revision &&
        change_proof_failure.empty();
    if (query.exact_ref &&
        readiness == SnapshotReadinessMode::VerifiedNtfs && snapshot_ready) {
        std::size_t invalid_rows = 0;
        auto exact_items = read_index_rows(db_, product, query, invalid_rows);
        bool exact_source_valid = invalid_rows == 0 && !exact_items.empty();
        if (exact_source_valid) {
            for (const auto& item : exact_items) {
                try {
                    const auto source_path =
                        (product_root / item.source_ref).lexically_normal();
                    if (bounded_source_ref(source_path, product_root) !=
                            item.source_ref ||
                        hash_file(source_path, item.source_size) != item.source_hash) {
                        exact_source_valid = false;
                        break;
                    }
                } catch (...) {
                    exact_source_valid = false;
                    break;
                }
            }
        }
        if (exact_source_valid) {
            IndexQueryResult result;
            result.items = std::move(exact_items);
            result.diagnostics.index_used = true;
            result.diagnostics.index_status = "ready";
            result.diagnostics.index_revision = snapshot.index_revision;
            result.diagnostics.canonical_revision = snapshot.index_revision;
            result.diagnostics.product_revision = snapshot.product_revision;
            result.diagnostics.fallback_scan = false;
            result.diagnostics.scanned_count = result.items.size();
            result.diagnostics.matched_count = result.items.size();
            result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
            result.diagnostics.elapsed_ms = elapsed_ms(start);
            return result;
        }
    }

    const auto inventory = canonical_inventory(product_root);
    std::optional<RowRevisions> portable_canonical;
    std::optional<RowRevisions> portable_rows;
    std::string portable_validation_failure;
    if (readiness == SnapshotReadinessMode::PortableUnsupported) {
        portable_canonical = canonical_revisions(inventory);
        portable_rows = revisions_from_rows(db_, product);
        try {
            const auto write_validated =
                CanonicalStore(product_root).read_write_revision();
            if (!write_revision ||
                write_validated.current != write_revision->current) {
                portable_validation_failure = "canonical_write_revision_changed";
            }
        } catch (const std::exception&) {
            portable_validation_failure = "canonical_write_revision_unavailable";
        }
    }
    const auto revision_ms = elapsed_ms(revision_start);

    std::string stale_reason;
    if (!snapshot.exists) {
        stale_reason = "snapshot_missing";
    } else if (snapshot.schema_version != kMetadataIndexSchemaVersion ||
               snapshot.snapshot_schema_version != kMetadataSnapshotSchemaVersion) {
        stale_reason = "snapshot_schema_mismatch";
    } else if (snapshot.status != "ready") {
        stale_reason = snapshot.reason.value_or("snapshot_" + snapshot.status);
    } else if (!snapshot_structurally_ready(snapshot)) {
        stale_reason = "product_revision_state_invalid";
    } else if (!write_revision) {
        stale_reason = "canonical_write_revision_unavailable";
    } else if (write_revision->current != snapshot.canonical_write_revision) {
        stale_reason = "canonical_write_revision_changed";
    } else if (!portable_validation_failure.empty()) {
        stale_reason = portable_validation_failure;
    } else if (!change_proof_failure.empty()) {
        stale_reason = change_proof_failure;
    } else if (snapshot.item_count != inventory.sources.size()) {
        stale_reason = "canonical_item_count_changed";
    } else if (snapshot.index_revision != inventory.revision) {
        stale_reason = "canonical_revision_changed";
    } else if (portable_canonical &&
               snapshot.content_revision != portable_canonical->content) {
        stale_reason = "canonical_content_changed";
    } else if (portable_canonical && portable_rows &&
               revision_mismatch_reason(*portable_rows, *portable_canonical)) {
        stale_reason = "index_row_revision_mismatch";
    }

    if (!stale_reason.empty()) {
        if (snapshot.exists && snapshot.status == "ready") {
            try {
                invalidate_stale_query_snapshot(
                    db_, product, snapshot, write_revision, stale_reason);
            } catch (...) {
            }
        }
        auto canonical = query_canonical(product_root, product, inventory, query);
        IndexQueryResult result;
        result.items = std::move(canonical.items);
        result.diagnostics.index_used = false;
        result.diagnostics.index_status = snapshot.exists ? "stale" : "missing";
        result.diagnostics.index_revision = snapshot.index_revision;
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.product_revision = snapshot.product_revision;
        result.diagnostics.fallback_scan = true;
        result.diagnostics.scanned_count = canonical.scanned_count;
        result.diagnostics.matched_count = result.items.size();
        result.diagnostics.revision_check_ms = revision_ms;
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = stale_reason;
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        return result;
    }

    std::size_t invalid_rows = 0;
    auto items = read_index_rows(db_, product, query, invalid_rows);
    if (invalid_rows != 0) {
        try {
            invalidate_stale_query_snapshot(
                db_, product, snapshot, write_revision, "invalid_index_rows");
        } catch (...) {
        }
        auto canonical = query_canonical(product_root, product, inventory, query);
        IndexQueryResult result;
        result.items = std::move(canonical.items);
        result.diagnostics.index_used = false;
        result.diagnostics.index_status = "corrupt";
        result.diagnostics.index_revision = snapshot.index_revision;
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.product_revision = snapshot.product_revision;
        result.diagnostics.fallback_scan = true;
        result.diagnostics.scanned_count = canonical.scanned_count;
        result.diagnostics.matched_count = result.items.size();
        result.diagnostics.revision_check_ms = revision_ms;
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = "invalid_index_rows";
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        return result;
    }

    if (query.exact_ref && items.empty()) {
        auto canonical = query_canonical(product_root, product, inventory, query);
        const bool index_omitted_canonical_match = !canonical.items.empty();
        if (index_omitted_canonical_match) {
            try {
                invalidate_stale_query_snapshot(
                    db_,
                    product,
                    snapshot,
                    write_revision,
                    "exact_ref_missing_from_index");
            } catch (...) {
            }
        }
        IndexQueryResult result;
        result.items = std::move(canonical.items);
        result.diagnostics.index_used = false;
        result.diagnostics.index_status =
            index_omitted_canonical_match ? "stale" : "ready";
        result.diagnostics.index_revision = snapshot.index_revision;
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.product_revision = snapshot.product_revision;
        result.diagnostics.fallback_scan = true;
        result.diagnostics.scanned_count = canonical.scanned_count;
        result.diagnostics.matched_count = result.items.size();
        result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = index_omitted_canonical_match
            ? "exact_ref_missing_from_index"
            : "exact_ref_not_found";
        if (index_omitted_canonical_match) {
            result.diagnostics.recovery =
                "kob index rebuild --product " + product;
        }
        return result;
    }

    std::string content_stale_reason;
    if (readiness == SnapshotReadinessMode::PortableUnsupported) {
        content_stale_reason.clear();
    } else if (query.exact_ref) {
        for (const auto& item : items) {
            try {
                const auto source_path =
                    (product_root / item.source_ref).lexically_normal();
                if (bounded_source_ref(source_path, product_root) !=
                        item.source_ref ||
                    hash_file(source_path, item.source_size) != item.source_hash) {
                    content_stale_reason = "canonical_source_hash_changed";
                    break;
                }
            } catch (...) {
                content_stale_reason = "canonical_source_hash_changed";
                break;
            }
        }
    } else if (snapshot.content_revision !=
               canonical_content_revision(inventory)) {
        content_stale_reason = "canonical_content_changed";
    }

    if (!content_stale_reason.empty()) {
        try {
            invalidate_stale_query_snapshot(
                db_, product, snapshot, write_revision, content_stale_reason);
        } catch (...) {
        }
        auto canonical = query_canonical(product_root, product, inventory, query);
        IndexQueryResult result;
        result.items = std::move(canonical.items);
        result.diagnostics.index_used = false;
        result.diagnostics.index_status = "stale";
        result.diagnostics.index_revision = snapshot.index_revision;
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.product_revision = snapshot.product_revision;
        result.diagnostics.fallback_scan = true;
        result.diagnostics.scanned_count = canonical.scanned_count;
        result.diagnostics.matched_count = result.items.size();
        result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = content_stale_reason;
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        return result;
    }

    IndexQueryResult result;
    result.items = std::move(items);
    result.diagnostics.index_used = true;
    result.diagnostics.index_status = "ready";
    result.diagnostics.index_revision = snapshot.index_revision;
    result.diagnostics.canonical_revision = inventory.revision;
    result.diagnostics.product_revision = snapshot.product_revision;
    result.diagnostics.fallback_scan = false;
    result.diagnostics.scanned_count =
        query.exact_ref ? std::min<std::size_t>(snapshot.item_count, 1) : snapshot.item_count;
    result.diagnostics.matched_count = result.items.size();
    result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
    result.diagnostics.elapsed_ms = elapsed_ms(start);
    return result;
}

IndexDoctorResult BacklogIndex::doctor_metadata(
    const std::filesystem::path& product_root,
    const std::string& product,
    bool verify_source_hashes
) {
    const auto start = std::chrono::steady_clock::now();
    initialize();
    const auto revision_start = std::chrono::steady_clock::now();
    const auto inventory = canonical_inventory(product_root);
    const auto revision_ms = elapsed_ms(revision_start);
    const auto snapshot = read_snapshot(db_, product);
    const auto readiness = snapshot_readiness_mode(snapshot);
    std::optional<CanonicalWriteRevision> write_revision;
    try {
        write_revision = CanonicalStore(product_root).read_write_revision();
    } catch (const std::exception&) {
    }
    const auto watch = readiness == SnapshotReadinessMode::VerifiedNtfs
        ? read_change_watch(db_, product)
        : std::nullopt;
    std::string change_proof_failure;
    if (readiness == SnapshotReadinessMode::VerifiedNtfs && write_revision
        && write_revision->current == snapshot.canonical_write_revision) {
        if (!snapshot.proof || !watch) {
            change_proof_failure = "change_proof_watch_invalid";
        } else {
            const auto proof = change_probe::verify_window(
                product_root, *snapshot.proof, *watch);
            if (proof.status != change_probe::Status::Clean || !proof.endpoint) {
                change_proof_failure = std::string(change_probe::reason(proof.status));
            } else if (!advance_verified_checkpoint(
                           db_path_, product, snapshot, *proof.endpoint)) {
                change_proof_failure = "change_proof_checkpoint_commit_failed";
            }
        }
    } else if (readiness == SnapshotReadinessMode::PortableUnsupported &&
               write_revision &&
               write_revision->current == snapshot.canonical_write_revision &&
               !change_watch_rows_empty(db_, product)) {
        change_proof_failure = "change_proof_watch_invalid";
    }

    std::optional<RowRevisions> portable_canonical;
    std::string portable_validation_failure;
    if (readiness == SnapshotReadinessMode::PortableUnsupported) {
        portable_canonical = canonical_revisions(inventory);
        try {
            const auto write_validated =
                CanonicalStore(product_root).read_write_revision();
            if (!write_revision ||
                write_validated.current != write_revision->current) {
                portable_validation_failure = "canonical_write_revision_changed";
            }
        } catch (const std::exception&) {
            portable_validation_failure = "canonical_write_revision_unavailable";
        }
    }

    IndexQuery all;
    all.limit = kMaximumQueryLimit;
    std::size_t invalid_rows = 0;
    const auto rows = read_index_rows(db_, product, all, invalid_rows);
    std::map<std::string, IndexItem> rows_by_source;
    for (const auto& row : rows) {
        rows_by_source.emplace(row.source_ref, row);
    }
    std::map<std::string, SourceFingerprint> canonical_by_source;
    for (const auto& source : inventory.sources) {
        canonical_by_source.emplace(source.source_ref, source);
    }

    IndexDoctorResult result;
    for (const auto& source : inventory.sources) {
        const auto row = rows_by_source.find(source.source_ref);
        if (row == rows_by_source.end()) {
            ++result.missing_rows;
            continue;
        }
        if (row->second.source_size != source.size ||
            row->second.source_mtime_ns != source.mtime_ns) {
            ++result.stale_rows;
            continue;
        }
        if (verify_source_hashes &&
            row->second.source_hash != hash_file(source.path, source.size)) {
            ++result.source_hash_mismatches;
        }
    }
    for (const auto& [source_ref, _] : rows_by_source) {
        if (!canonical_by_source.contains(source_ref)) {
            ++result.orphaned_rows;
        }
    }
    result.stale_rows += invalid_rows;
    const auto portable_rows = portable_canonical
        ? std::optional<RowRevisions>{revisions_from_rows(db_, product)}
        : std::nullopt;

    std::string reason;
    if (!snapshot.exists) {
        reason = "snapshot_missing";
    } else if (snapshot.schema_version != kMetadataIndexSchemaVersion ||
               snapshot.snapshot_schema_version != kMetadataSnapshotSchemaVersion) {
        reason = "snapshot_schema_mismatch";
    } else if (snapshot.status != "ready") {
        reason = snapshot.reason.value_or("snapshot_" + snapshot.status);
    } else if (!snapshot_structurally_ready(snapshot)) {
        reason = "product_revision_state_invalid";
    } else if (!write_revision) {
        reason = "canonical_write_revision_unavailable";
    } else if (write_revision->current != snapshot.canonical_write_revision) {
        reason = "canonical_write_revision_changed";
    } else if (!portable_validation_failure.empty()) {
        reason = portable_validation_failure;
    } else if (!change_proof_failure.empty()) {
        reason = change_proof_failure;
    } else if (snapshot.item_count != inventory.sources.size()) {
        reason = "canonical_item_count_changed";
    } else if (snapshot.index_revision != inventory.revision) {
        reason = "canonical_revision_changed";
    } else if (portable_canonical &&
               snapshot.content_revision != portable_canonical->content) {
        reason = "canonical_content_changed";
    } else if (portable_canonical && portable_rows &&
               revision_mismatch_reason(*portable_rows, *portable_canonical)) {
        reason = "index_row_revision_mismatch";
    } else if (result.missing_rows != 0) {
        reason = "missing_index_rows";
    } else if (result.orphaned_rows != 0) {
        reason = "orphaned_index_rows";
    } else if (result.stale_rows != 0) {
        reason = "stale_index_rows";
    } else if (result.source_hash_mismatches != 0) {
        reason = "source_hash_mismatch";
    }

    result.healthy = reason.empty();
    result.diagnostics.index_used = result.healthy;
    result.diagnostics.index_status =
        result.healthy ? "ready" : (snapshot.exists ? "stale" : "missing");
    result.diagnostics.index_revision = snapshot.index_revision;
    result.diagnostics.canonical_revision = inventory.revision;
    result.diagnostics.product_revision = snapshot.product_revision;
    result.diagnostics.fallback_scan = false;
    result.diagnostics.scanned_count = inventory.sources.size();
    result.diagnostics.matched_count = rows.size();
    result.diagnostics.revision_check_ms = revision_ms;
    result.diagnostics.elapsed_ms = elapsed_ms(start);
    if (!reason.empty()) {
        result.diagnostics.stale_reason = reason;
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        if (snapshot.exists && snapshot.status == "ready") {
            try {
                invalidate_metadata(product, reason);
            } catch (...) {
            }
        }
    }
    return result;
}

int BacklogIndex::get_next_number(const std::string& prefix, const std::string& type_code) {
    initialize();
    execute("BEGIN IMMEDIATE");
    try {
        {
            Statement statement(
                db_,
                "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES (?, ?, 1) "
                "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = next_number + 1",
                "advance id sequence");
            statement.bind_text(1, prefix);
            statement.bind_text(2, type_code);
            statement.step_done();
        }
        Statement statement(
            db_,
            "SELECT next_number FROM id_sequences WHERE prefix = ? AND type_code = ?",
            "read id sequence");
        statement.bind_text(1, prefix);
        statement.bind_text(2, type_code);
        if (statement.step() != SQLITE_ROW) {
            throw std::runtime_error("id_sequence_row_missing");
        }
        const int number = sqlite3_column_int(statement.get(), 0);
        execute("COMMIT");
        return number;
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

int BacklogIndex::reserve_next_number(
    const std::string& prefix,
    const std::string& type_code,
    const std::string& owner
) {
    initialize();
    execute("BEGIN IMMEDIATE");
    try {
        Statement advance(
            db_,
            "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES (?, ?, 1) "
            "ON CONFLICT(prefix, type_code) DO UPDATE SET next_number = next_number + 1",
            "advance reserved id sequence");
        advance.bind_text(1, prefix);
        advance.bind_text(2, type_code);
        advance.step_done();

        Statement read(
            db_,
            "SELECT next_number FROM id_sequences WHERE prefix = ? AND type_code = ?",
            "read reserved id sequence");
        read.bind_text(1, prefix);
        read.bind_text(2, type_code);
        if (read.step() != SQLITE_ROW) {
            throw std::runtime_error("reserved_id_sequence_row_missing");
        }
        const int number = sqlite3_column_int(read.get(), 0);

        Statement reservation(
            db_,
            "INSERT INTO id_reservations (prefix, type_code, number, owner) VALUES (?, ?, ?, ?)",
            "record id reservation");
        reservation.bind_text(1, prefix);
        reservation.bind_text(2, type_code);
        reservation.bind_int(3, number);
        reservation.bind_text(4, owner.substr(0, 160));
        reservation.step_done();
        execute("COMMIT");
        return number;
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

void BacklogIndex::commit_reservation(
    const std::string& prefix,
    const std::string& type_code,
    int number
) {
    initialize();
    Statement statement(
        db_,
        "UPDATE id_reservations SET committed_at = unixepoch() "
        "WHERE prefix = ? AND type_code = ? AND number = ?",
        "commit id reservation");
    statement.bind_text(1, prefix);
    statement.bind_text(2, type_code);
    statement.bind_int(3, number);
    statement.step_done();
}

std::vector<std::string> BacklogIndex::stale_reservation_diagnostics(
    const std::string& prefix,
    const std::string& type_code,
    int minimum_age_seconds
) {
    initialize();
    Statement statement(
        db_,
        "SELECT number, owner, unixepoch() - created_at FROM id_reservations "
        "WHERE prefix = ? AND type_code = ? AND committed_at IS NULL "
        "AND unixepoch() - created_at >= ? ORDER BY number LIMIT 20",
        "inspect stale id reservations");
    statement.bind_text(1, prefix);
    statement.bind_text(2, type_code);
    statement.bind_int(3, std::max(0, minimum_age_seconds));
    std::vector<std::string> diagnostics;
    while (statement.step() == SQLITE_ROW) {
        diagnostics.push_back(
            prefix + "-" + type_code + "-" +
            std::to_string(sqlite3_column_int(statement.get(), 0)) +
            " owner=" + column_text(statement.get(), 1) +
            " age_seconds=" + std::to_string(sqlite3_column_int(statement.get(), 2)) +
            " recovery=allocate-next-id-without-reusing-reservation");
    }
    return diagnostics;
}

bool BacklogIndex::has_sequence(const std::string& prefix, const std::string& type_code) {
    initialize();
    Statement statement(
        db_,
        "SELECT 1 FROM id_sequences WHERE prefix = ? AND type_code = ? LIMIT 1",
        "check id sequence");
    statement.bind_text(1, prefix);
    statement.bind_text(2, type_code);
    return statement.step() == SQLITE_ROW;
}

void BacklogIndex::ensure_sequence_at_least(
    const std::string& prefix,
    const std::string& type_code,
    int number
) {
    initialize();
    execute("BEGIN IMMEDIATE");
    try {
        Statement statement(
            db_,
            "INSERT INTO id_sequences (prefix, type_code, next_number) VALUES (?, ?, ?) "
            "ON CONFLICT(prefix, type_code) DO UPDATE SET "
            "next_number = MAX(next_number, excluded.next_number)",
            "repair id sequence floor");
        statement.bind_text(1, prefix);
        statement.bind_text(2, type_code);
        statement.bind_int(3, number);
        statement.step_done();
        execute("COMMIT");
    } catch (...) {
        try {
            execute("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}

BacklogIndex::SyncSequencesResult BacklogIndex::sync_sequences(
    const std::filesystem::path& product_root
) {
    initialize();
    SyncSequencesResult result;
    result.max_number_found = 0;
    std::map<std::pair<std::string, std::string>, int> maximums;

    CanonicalStore store(product_root);
    for (const auto& item_path : store.list_items()) {
        try {
            const auto item = store.read_metadata(item_path);
            const auto last_dash = item.id.rfind('-');
            if (last_dash == std::string::npos) {
                continue;
            }
            const auto second_dash = item.id.rfind('-', last_dash - 1);
            if (second_dash == std::string::npos) {
                continue;
            }
            const auto prefix = item.id.substr(0, second_dash);
            const auto type_code = item.id.substr(second_dash + 1, last_dash - second_dash - 1);
            const int number = std::stoi(item.id.substr(last_dash + 1));
            auto& current = maximums[{prefix, type_code}];
            current = std::max(current, number);
            result.max_number_found = std::max(result.max_number_found, number);
        } catch (...) {
        }
    }

    for (const auto& [key, maximum] : maximums) {
        ensure_sequence_at_least(key.first, key.second, maximum);
        result.synced_pairs.push_back(
            key.first + "-" + key.second + " -> " + std::to_string(maximum + 1));
    }
    return result;
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_id(const std::string& id) {
    initialize();
    const bool scoped = product_name_ && !product_name_->empty();
    Statement statement(
        db_,
        scoped
            ? "SELECT product, source_ref FROM items WHERE product = ? AND id = ? LIMIT 1"
            : "SELECT product, source_ref FROM items WHERE id = ? ORDER BY product LIMIT 1",
        "lookup item source ref by id");
    int binding = 1;
    if (scoped) {
        statement.bind_text(binding++, *product_name_);
    }
    statement.bind_text(binding, id);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return materialize_source_path(
        db_path_,
        product_root_,
        column_text(statement.get(), 0),
        column_text(statement.get(), 1));
}

std::optional<std::filesystem::path> BacklogIndex::get_path_by_uid(const std::string& uid) {
    initialize();
    const bool scoped = product_name_ && !product_name_->empty();
    Statement statement(
        db_,
        scoped
            ? "SELECT product, source_ref FROM items WHERE product = ? AND uid = ? LIMIT 1"
            : "SELECT product, source_ref FROM items WHERE uid = ? ORDER BY product LIMIT 1",
        "lookup item source ref by uid");
    int binding = 1;
    if (scoped) {
        statement.bind_text(binding++, *product_name_);
    }
    statement.bind_text(binding, uid);
    if (statement.step() != SQLITE_ROW) {
        return std::nullopt;
    }
    return materialize_source_path(
        db_path_,
        product_root_,
        column_text(statement.get(), 0),
        column_text(statement.get(), 1));
}

std::vector<IndexItem> BacklogIndex::query_items(
    std::optional<ItemType> type,
    std::optional<ItemState> state,
    std::optional<std::string> product
) {
    initialize();
    if (!product && product_name_) {
        product = product_name_;
    }

    std::vector<std::string> products;
    if (product && !product->empty()) {
        products.push_back(*product);
    } else {
        Statement statement(
            db_,
            "SELECT DISTINCT product FROM items ORDER BY product",
            "list metadata index products");
        while (statement.step() == SQLITE_ROW) {
            products.push_back(column_text(statement.get(), 0));
        }
    }

    IndexQuery query;
    query.type = type;
    query.state = state;
    query.limit = kMaximumQueryLimit;
    std::vector<IndexItem> result;
    for (const auto& scoped_product : products) {
        std::size_t invalid_rows = 0;
        auto rows = read_index_rows(db_, scoped_product, query, invalid_rows);
        result.insert(
            result.end(),
            std::make_move_iterator(rows.begin()),
            std::make_move_iterator(rows.end()));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.updated != right.updated) {
            return left.updated > right.updated;
        }
        if (left.id != right.id) {
            return left.id < right.id;
        }
        return left.product < right.product;
    });
    return result;
}

void BacklogIndex::execute(const std::string& sql) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        const std::string detail = message ? std::string(message) : sqlite_error(db_);
        sqlite3_free(message);
        throw std::runtime_error("metadata_index_sql_failed: " + detail);
    }
}

BuildIndexResult build_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    bool force,
    std::optional<std::string> product_name
) {
    const auto start = std::chrono::steady_clock::now();
    const auto product = product_name.value_or(infer_product_name(product_root));
    if (!force) {
        const auto current = doctor_metadata_index(
            index_path, product_root, product, true);
        if (current.healthy) {
            BuildIndexResult result;
            result.index_path = index_path;
            result.items_indexed = static_cast<int>(current.diagnostics.matched_count);
            result.build_time_ms = elapsed_ms(start);
            result.index_revision = current.diagnostics.index_revision;
            result.canonical_revision = current.diagnostics.canonical_revision;
            result.product_revision = current.diagnostics.product_revision;
            return result;
        }
    }

    BacklogIndex index(index_path, product, product_root);
    index.rebuild_metadata(product_root, product);
    const auto verified = index.doctor_metadata(product_root, product, true);
    if (!verified.healthy) {
        throw std::runtime_error("metadata_index_rebuild_verification_failed");
    }

    BuildIndexResult result;
    result.index_path = index_path;
    result.items_indexed = static_cast<int>(verified.diagnostics.matched_count);
    result.build_time_ms = elapsed_ms(start);
    result.index_revision = verified.diagnostics.index_revision;
    result.canonical_revision = verified.diagnostics.canonical_revision;
    result.product_revision = verified.diagnostics.product_revision;
    return result;
}

RefreshIndexResult refresh_index(
    const std::filesystem::path& product_root,
    const std::filesystem::path& index_path,
    std::optional<std::string> product_name
) {
    const auto start = std::chrono::steady_clock::now();
    const auto product = product_name.value_or(infer_product_name(product_root));
    BacklogIndex index(index_path, product, product_root);
    index.rebuild_metadata(product_root, product);
    const auto verified = index.doctor_metadata(product_root, product, true);
    if (!verified.healthy) {
        throw std::runtime_error("metadata_index_refresh_verification_failed");
    }

    RefreshIndexResult result;
    result.index_path = index_path;
    result.items_added = static_cast<int>(verified.diagnostics.matched_count);
    result.refresh_time_ms = elapsed_ms(start);
    result.index_revision = verified.diagnostics.index_revision;
    result.canonical_revision = verified.diagnostics.canonical_revision;
    result.product_revision = verified.diagnostics.product_revision;
    return result;
}

IndexQueryResult query_metadata_index(
    const std::filesystem::path& index_path,
    const std::filesystem::path& product_root,
    const std::string& product,
    const IndexQuery& query,
    const BacklogIndex::QueryMetadataTestHooks& test_hooks
) {
    const auto start = std::chrono::steady_clock::now();
    validate_query(query);
    std::error_code exists_error;
    if (!std::filesystem::exists(index_path, exists_error) || exists_error) {
        return fallback_query(
            product_root, product, query, "missing", "index_missing", start);
    }
    try {
        BacklogIndex index(index_path, product, product_root);
        return index.query_metadata(product_root, product, query, test_hooks);
    } catch (const std::exception&) {
        return fallback_query(
            product_root,
            product,
            query,
            "corrupt",
            "metadata_index_open_or_query_failed",
            start);
    }
}

IndexDoctorResult doctor_metadata_index(
    const std::filesystem::path& index_path,
    const std::filesystem::path& product_root,
    const std::string& product,
    bool verify_source_hashes
) {
    const auto start = std::chrono::steady_clock::now();
    std::error_code exists_error;
    if (!std::filesystem::exists(index_path, exists_error) || exists_error) {
        const auto revision_start = std::chrono::steady_clock::now();
        const auto inventory = canonical_inventory(product_root);
        IndexDoctorResult result;
        result.missing_rows = inventory.sources.size();
        result.diagnostics.index_status = "missing";
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.scanned_count = inventory.sources.size();
        result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = "index_missing";
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        return result;
    }
    try {
        BacklogIndex index(index_path, product, product_root);
        return index.doctor_metadata(
            product_root, product, verify_source_hashes);
    } catch (const std::exception&) {
        const auto revision_start = std::chrono::steady_clock::now();
        const auto inventory = canonical_inventory(product_root);
        IndexDoctorResult result;
        result.stale_rows = inventory.sources.size();
        result.diagnostics.index_status = "corrupt";
        result.diagnostics.canonical_revision = inventory.revision;
        result.diagnostics.scanned_count = inventory.sources.size();
        result.diagnostics.revision_check_ms = elapsed_ms(revision_start);
        result.diagnostics.elapsed_ms = elapsed_ms(start);
        result.diagnostics.stale_reason = "metadata_index_open_or_doctor_failed";
        result.diagnostics.recovery = "kob index rebuild --product " + product;
        return result;
    }
}

GetIndexStatusResult get_index_status(
    const std::filesystem::path& backlog_root,
    const std::optional<std::string>& product_name,
    const std::optional<std::filesystem::path>& product_root,
    const std::optional<std::string>& requested_revision,
    const GetIndexStatusTestHooks& test_hooks
) {
    const auto start = std::chrono::steady_clock::now();
    const auto verification_deadline = requested_revision
        ? start + kRequestedRevisionVerificationBudget
        : std::chrono::steady_clock::time_point::max();
    if (requested_revision) {
        validate_requested_revision(*requested_revision);
    }
    GetIndexStatusResult result;
    const auto product = product_name.value_or("default");
    const auto root = product_root.value_or(backlog_root / "products" / product);
    const auto index_path = backlog_root / ".cache" / "index" / "backlog.db";

    IndexStatusEntry entry;
    entry.product = product;
    entry.requested_revision = requested_revision;
    std::error_code ec;
    entry.exists = std::filesystem::exists(index_path, ec) && !ec;
    if (entry.exists) {
        entry.size_bytes = static_cast<std::size_t>(
            std::filesystem::file_size(index_path, ec));
        if (ec) {
            entry.size_bytes = 0;
            ec.clear();
        }
        const auto modified = std::filesystem::last_write_time(index_path, ec);
        if (!ec) {
            entry.last_modified = std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    modified.time_since_epoch()).count());
        }
    }

    if (requested_revision) {
        const auto finish_requested = [&](
            const std::string& status,
            const std::optional<std::string>& reason = std::nullopt
        ) {
            entry.status = status;
            entry.unchanged = false;
            entry.fallback_scan = false;
            entry.scanned_count = 0;
            entry.revision_check_ms = elapsed_ms(start);
            entry.elapsed_ms = elapsed_ms(start);
            entry.stale_reason = reason;
            if (reason) {
                entry.recovery = "kob index rebuild --product " + product;
            }
            result.indexes.push_back(std::move(entry));
        };

        if (!entry.exists) {
            finish_requested("missing", "index_missing");
            return result;
        }

        std::optional<RevisionState> revision_state;
        try {
            revision_state = read_revision_state_read_only(
                index_path,
                product,
                verification_deadline);
        } catch (const std::exception&) {
            finish_requested(
                deadline_expired(verification_deadline) ? "stale" : "corrupt",
                deadline_expired(verification_deadline)
                    ? "change_proof_budget_exhausted"
                    : "metadata_index_open_or_doctor_failed");
            return result;
        }
        if (!revision_state->deadline_exhausted &&
            test_hooks.after_revision_state_read) {
            test_hooks.after_revision_state_read();
        }
        const auto& snapshot = revision_state->snapshot;
        if (revision_state->deadline_exhausted ||
            deadline_expired(verification_deadline)) {
            finish_requested("stale", "change_proof_budget_exhausted");
            return result;
        }
        if (!snapshot.exists) {
            finish_requested("missing", "metadata_snapshot_missing");
            return result;
        }
        const auto readiness = snapshot_readiness_mode(snapshot);
        if (readiness == SnapshotReadinessMode::Invalid) {
            const auto reason =
                snapshot.schema_version != kMetadataIndexSchemaVersion
                    || snapshot.snapshot_schema_version != kMetadataSnapshotSchemaVersion
                ? "metadata_snapshot_schema_mismatch"
                : snapshot.status != "ready"
                ? "metadata_snapshot_not_ready"
                : "change_proof_malformed";
            finish_requested("stale", reason);
            return result;
        }
        if (!revision_watch_state_safe(*revision_state, readiness)) {
            finish_requested("stale", "change_proof_watch_invalid");
            return result;
        }
        if (deadline_expired(verification_deadline)) {
            finish_requested("stale", "change_proof_budget_exhausted");
            return result;
        }
        entry.item_count = static_cast<int>(snapshot.item_count);
        entry.index_revision = snapshot.index_revision;
        entry.canonical_revision = snapshot.index_revision;
        entry.product_revision = snapshot.product_revision;
        if (snapshot.product_revision != *requested_revision) {
            finish_requested("ready");
            return result;
        }
        if (readiness == SnapshotReadinessMode::PortableUnsupported) {
            finish_requested("stale", "change_proof_unsupported");
            return result;
        }

        try {
            CanonicalWriteRevision write_revision;
            if (deadline_expired(verification_deadline)) {
                finish_requested("stale", "change_proof_budget_exhausted");
                return result;
            }
            try {
                write_revision = CanonicalStore(root).read_write_revision();
            } catch (const std::exception&) {
                finish_requested(
                    "stale",
                    deadline_expired(verification_deadline)
                        ? "change_proof_budget_exhausted"
                        : "canonical_write_revision_unavailable");
                return result;
            }
            if (deadline_expired(verification_deadline)) {
                finish_requested("stale", "change_proof_budget_exhausted");
                return result;
            }
            if (write_revision.current != snapshot.canonical_write_revision) {
                finish_requested("stale", "canonical_write_revision_changed");
                return result;
            }
            if (!snapshot.proof || !revision_state->watch) {
                finish_requested("stale", "change_proof_watch_invalid");
                return result;
            }
            change_probe::VerifyBudget verify_budget;
            verify_budget.deadline = verification_deadline;
            const auto proof = change_probe::verify_window(
                root,
                *snapshot.proof,
                *revision_state->watch,
                {},
                verify_budget);
            if (proof.status != change_probe::Status::Clean || !proof.endpoint
                || deadline_expired(verification_deadline)) {
                finish_requested(
                    "stale",
                    deadline_expired(verification_deadline)
                        ? "change_proof_budget_exhausted"
                        : std::string(change_probe::reason(proof.status)));
                return result;
            }
            const auto effective_records_read =
                test_hooks.proof_records_read_override.value_or(proof.records_read);
            const auto effective_bytes_read =
                test_hooks.proof_bytes_read_override.value_or(proof.bytes_read);
            const auto effective_usn_span =
                test_hooks.proof_usn_span_override.value_or(proof.usn_span);
            const GetIndexStatusTestHooks diagnostic_caps;
            entry.proof_records_read = std::min(
                effective_records_read, diagnostic_caps.checkpoint_record_cap);
            entry.proof_bytes_read = std::min(
                effective_bytes_read, diagnostic_caps.checkpoint_byte_cap);
            entry.proof_usn_span = std::min(
                effective_usn_span, diagnostic_caps.checkpoint_usn_span_cap);
            entry.proof_checkpoint_required =
                effective_records_read >= test_hooks.checkpoint_record_cap
                || effective_bytes_read >= test_hooks.checkpoint_byte_cap
                || effective_usn_span >= test_hooks.checkpoint_usn_span_cap;
            if (entry.proof_checkpoint_required) {
                const auto persistence = compare_and_swap_verified_checkpoint(
                    index_path,
                    product,
                    snapshot,
                    *proof.endpoint,
                    verification_deadline);
                entry.proof_checkpoint_persisted = persistence.persisted;
                if (!persistence.persisted
                    || !persistence.completed_within_deadline) {
                    finish_requested(
                        "stale",
                        "change_proof_checkpoint_commit_failed");
                    return result;
                }
            } else {
                const auto current = verified_snapshot_is_current(
                    index_path, product, snapshot, verification_deadline);
                if (current.deadline_exhausted) {
                    finish_requested("stale", "change_proof_budget_exhausted");
                    return result;
                }
                if (!current.completed) {
                    finish_requested("stale", "change_proof_unavailable");
                    return result;
                }
                if (!current.current) {
                    finish_requested("stale", "change_proof_snapshot_changed");
                    return result;
                }
            }
            if (deadline_expired(verification_deadline)) {
                finish_requested("stale", "change_proof_budget_exhausted");
                return result;
            }
            entry.status = "unchanged";
            entry.unchanged = true;
            entry.fallback_scan = false;
            entry.scanned_count = 0;
            entry.revision_check_ms = elapsed_ms(start);
            entry.elapsed_ms = elapsed_ms(start);
            result.indexes.push_back(std::move(entry));
            return result;
        } catch (...) {
            finish_requested(
                "stale",
                deadline_expired(verification_deadline)
                    ? "change_proof_budget_exhausted"
                    : "change_proof_unavailable");
            return result;
        }
    }

    const auto doctor = doctor_metadata_index(
        index_path, root, product, false);
    entry.item_count = static_cast<int>(doctor.diagnostics.matched_count);
    entry.status = doctor.diagnostics.index_status;
    entry.index_revision = doctor.diagnostics.index_revision;
    entry.canonical_revision = doctor.diagnostics.canonical_revision;
    entry.product_revision = doctor.diagnostics.product_revision;
    entry.fallback_scan = false;
    entry.scanned_count = doctor.diagnostics.scanned_count;
    entry.revision_check_ms = doctor.diagnostics.revision_check_ms;
    entry.elapsed_ms = elapsed_ms(start);
    entry.stale_reason = doctor.diagnostics.stale_reason;
    entry.recovery = doctor.diagnostics.recovery;
    result.indexes.push_back(std::move(entry));
    return result;
}

} // namespace kano::backlog_ops
