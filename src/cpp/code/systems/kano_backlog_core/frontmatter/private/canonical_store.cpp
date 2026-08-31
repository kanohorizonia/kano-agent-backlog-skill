#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/diagnostics/mutation_timing.hpp"
#include "kano/backlog_core/frontmatter/frontmatter.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace kano::backlog_core {

namespace {

constexpr std::string_view kCanonicalWriteRevisionSchema =
    "kob-canonical-write-revision-v2";
constexpr std::uintmax_t kMaximumCanonicalWriteRevisionBytes = 512;
constexpr std::uint64_t kFNVOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFNVPrime = 1099511628211ULL;
constexpr std::uint64_t kMaximumCanonicalItemBytes = 64ULL * 1024ULL * 1024ULL;

std::filesystem::path canonical_write_revision_path(
    const std::filesystem::path& product_root
) {
    return product_root / ".cache" / "canonical-write-revision-v1";
}

std::filesystem::path canonical_write_revision_lock_path(
    const std::filesystem::path& product_root
) {
    return product_root / ".cache" / "canonical-write-revision-v1.lock";
}

class RevisionLock {
public:
    explicit RevisionLock(const std::filesystem::path& path) {
#ifdef _WIN32
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (handle_ == INVALID_HANDLE_VALUE) {
            handle_ = CreateFileW(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                break;
            }
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) {
                throw WriteError("canonical_write_revision_lock_failed");
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw WriteError("canonical_write_revision_lock_timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
#else
        descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (descriptor_ < 0 || ::flock(descriptor_, LOCK_EX) != 0) {
            if (descriptor_ >= 0) {
                ::close(descriptor_);
                descriptor_ = -1;
            }
            throw WriteError("canonical_write_revision_lock_failed");
        }
#endif
    }

    ~RevisionLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

    RevisionLock(const RevisionLock&) = delete;
    RevisionLock& operator=(const RevisionLock&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

bool valid_write_revision_token(const std::string& value) {
    return !value.empty() && value != "-" && value.size() <= 64 &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '-';
        });
}

bool valid_hash_token(const std::string& value) {
    if (value.size() != 24 || !value.starts_with("fnv1a64:")) {
        return false;
    }
    return std::all_of(value.begin() + 8, value.end(), [](const unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::string hash_content(const std::string_view content) {
    std::uint64_t hash = kFNVOffset;
    for (const unsigned char ch : content) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFNVPrime;
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

std::optional<std::uint64_t> parse_uint64(const std::string_view value) {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

std::string source_ref_hash(
    const std::filesystem::path& product_root,
    const std::filesystem::path& source
) {
    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(product_root, error).lexically_normal();
    if (error) {
        throw WriteError("canonical_write_receipt_source_invalid");
    }
    const auto absolute_source = std::filesystem::absolute(source, error).lexically_normal();
    if (error) {
        throw WriteError("canonical_write_receipt_source_invalid");
    }
    const auto relative = absolute_source.lexically_relative(absolute_root).lexically_normal();
    const auto value = relative.generic_string();
    if (relative.empty() || relative.is_absolute() || value == ".."
        || value.starts_with("../") || value.find("/../") != std::string::npos) {
        throw WriteError("canonical_write_receipt_source_invalid");
    }
    return hash_content(value);
}

std::string read_exact_bytes(
    const std::filesystem::path& path,
    const std::uint64_t maximum_bytes
) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_bytes) {
        throw WriteError("canonical_write_readback_failed");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw WriteError("canonical_write_readback_failed");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!result.empty()) {
        input.read(result.data(), static_cast<std::streamsize>(result.size()));
    }
    if (input.bad() || static_cast<std::size_t>(input.gcount()) != result.size()) {
        throw WriteError("canonical_write_readback_failed");
    }
    return result;
}

std::string serialize_write_revision(const CanonicalWriteRevision& revision) {
    const auto source_hash = revision.source_ref_hash.value_or("-");
    const auto content_hash = revision.content_hash.value_or("-");
    std::ostringstream output;
    output << kCanonicalWriteRevisionSchema << '\n'
           << revision.current << '\n'
           << revision.previous.value_or("-") << '\n'
           << revision.operation << '\n'
           << source_hash << '\n'
           << revision.content_size << '\n'
           << content_hash << '\n';
    return output.str();
}

void replace_file_atomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw WriteError("canonical_write_revision_replace_failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw WriteError("canonical_write_revision_replace_failed");
    }
#endif
}

void write_canonical_write_revision(
    const std::filesystem::path& product_root,
    const CanonicalWriteRevision& revision
) {
    const auto path = canonical_write_revision_path(product_root);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    const auto serialized = serialize_write_revision(revision);
    if (serialized.size() > kMaximumCanonicalWriteRevisionBytes) {
        throw WriteError("canonical_write_revision_invalid");
    }
    auto temporary = path;
    temporary += ".tmp-" + CanonicalStore::generate_uuid_v7();
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw WriteError("canonical_write_revision_open_failed");
    }
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.flush();
    if (!output.good()) {
        throw WriteError("canonical_write_revision_write_failed");
    }
    output.close();
    if (output.fail()) {
        throw WriteError("canonical_write_revision_close_failed");
    }
    try {
        if (read_exact_bytes(temporary, kMaximumCanonicalWriteRevisionBytes) != serialized) {
            throw WriteError("canonical_write_revision_readback_failed");
        }
        replace_file_atomically(temporary, path);
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        throw;
    }
}

bool is_blank(const std::string& value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char ch) { return std::isspace(ch); });
}

bool is_null_like(const YAML::Node& node) {
    if (!node || node.IsNull()) {
        return true;
    }
    if (!node.IsScalar()) {
        return false;
    }

    std::string value = node.as<std::string>();
    if (value.empty() || is_blank(value)) {
        return true;
    }

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value == "null" || value == "none";
}

std::optional<std::string> parse_optional_string(const YAML::Node& node) {
    if (is_null_like(node)) {
        return std::nullopt;
    }
    return node.as<std::string>();
}

std::vector<std::string> parse_string_list(const YAML::Node& node) {
    std::vector<std::string> values;
    if (!node || node.IsNull() || !node.IsSequence()) {
        return values;
    }
    for (const auto& entry : node) {
        if (!entry.IsNull()) {
            values.push_back(entry.as<std::string>());
        }
    }
    return values;
}

std::filesystem::path normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    auto normalized = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) {
        normalized = absolute.lexically_normal();
    }
    return normalized;
}

bool is_inside_path(const std::filesystem::path& child_path, const std::filesystem::path& parent_path) {
    const auto child = normalized_absolute_path(child_path);
    const auto parent = normalized_absolute_path(parent_path);
    std::error_code ec;
    auto rel = std::filesystem::relative(child, parent, ec);
    if (ec || rel.empty() || rel.is_absolute()) {
        return false;
    }
    for (const auto& part : rel) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::optional<std::pair<ItemType, int>> parse_display_id_type_and_number(const std::string& id) {
    const auto last_dash = id.rfind('-');
    if (last_dash == std::string::npos || last_dash + 1 >= id.size()) {
        return std::nullopt;
    }
    const auto type_dash = id.rfind('-', last_dash - 1);
    if (type_dash == std::string::npos || type_dash + 1 >= last_dash) {
        return std::nullopt;
    }

    const std::string type_code = id.substr(type_dash + 1, last_dash - type_dash - 1);
    const std::string number_text = id.substr(last_dash + 1);
    if (number_text.empty() || !std::all_of(number_text.begin(), number_text.end(), [](unsigned char ch) {
            return std::isdigit(ch);
        })) {
        return std::nullopt;
    }

    std::optional<ItemType> type;
    if (type_code == "INIT") type = ItemType::Initiative;
    else if (type_code == "EPIC") type = ItemType::Epic;
    else if (type_code == "FTR") type = ItemType::Feature;
    else if (type_code == "USR") type = ItemType::UserStory;
    else if (type_code == "TSK") type = ItemType::Task;
    else if (type_code == "SUBTSK") type = ItemType::SubTask;
    else if (type_code == "BUG") type = ItemType::Bug;
    else if (type_code == "ISS") type = ItemType::Issue;

    if (!type) {
        return std::nullopt;
    }
    return std::make_pair(*type, std::stoi(number_text));
}

std::string item_type_directory(ItemType type) {
    switch (type) {
        case ItemType::Initiative: return "initiative";
        case ItemType::Epic: return "epic";
        case ItemType::Feature: return "feature";
        case ItemType::UserStory: return "userstory";
        case ItemType::Task: return "task";
        case ItemType::SubTask: return "subtask";
        case ItemType::Bug: return "bug";
        case ItemType::Issue: return "issue";
    }
    return "item";
}

constexpr std::size_t kBoundedFrontmatterChunkBytes = 4096;

bool is_frontmatter_delimiter(
    const std::string& retained,
    std::size_t line_start,
    std::size_t line_end
) {
    if (line_end > line_start && retained[line_end - 1] == '\r') {
        --line_end;
    }
    return line_end - line_start == 3 && retained.compare(line_start, 3, "---") == 0;
}

std::string read_bounded_frontmatter_yaml(
    std::ifstream& input,
    const std::filesystem::path& item_path,
    std::size_t maximum_bytes,
    std::size_t& bytes_read
) {
    bytes_read = 0;
    if (maximum_bytes == 0) {
        throw ParseError(item_path, "frontmatter_byte_limit_exceeded");
    }

    std::array<char, kBoundedFrontmatterChunkBytes> chunk{};
    std::string retained;
    retained.reserve(std::min(maximum_bytes, kBoundedFrontmatterChunkBytes));
    bool opening_delimiter_read = false;
    std::size_t line_start = 0;

    while (true) {
        if (bytes_read == maximum_bytes) {
            throw ParseError(item_path, "frontmatter_byte_limit_exceeded");
        }

        const std::size_t requested_bytes = std::min(chunk.size(), maximum_bytes - bytes_read);
        input.read(chunk.data(), static_cast<std::streamsize>(requested_bytes));
        const std::streamsize chunk_bytes = input.gcount();
        if (chunk_bytes < 0) {
            throw ParseError(item_path, "Failed to read file");
        }
        bytes_read += static_cast<std::size_t>(chunk_bytes);

        for (std::streamsize index = 0; index < chunk_bytes; ++index) {
            const char byte = chunk[static_cast<std::size_t>(index)];
            retained.push_back(byte);
            if (byte != '\n') {
                continue;
            }

            const std::size_t line_end = retained.size() - 1;
            if (!opening_delimiter_read) {
                if (!is_frontmatter_delimiter(retained, 0, line_end)) {
                    throw ParseError(item_path, "Invalid or missing frontmatter");
                }
                opening_delimiter_read = true;
                retained.clear();
                line_start = 0;
                continue;
            }

            if (is_frontmatter_delimiter(retained, line_start, line_end)) {
                retained.resize(line_start);
                return retained;
            }
            line_start = retained.size();
        }

        if (input.bad()) {
            throw ParseError(item_path, "Failed to read file");
        }
        if (static_cast<std::size_t>(chunk_bytes) == requested_bytes) {
            continue;
        }

        if (!opening_delimiter_read) {
            if (retained.empty()) {
                throw ParseError(item_path, "Empty item file");
            }
            if (!is_frontmatter_delimiter(retained, 0, retained.size())) {
                throw ParseError(item_path, "Invalid or missing frontmatter");
            }
            opening_delimiter_read = true;
            retained.clear();
            line_start = 0;
        } else if (is_frontmatter_delimiter(retained, line_start, retained.size())) {
            retained.resize(line_start);
            return retained;
        }

        throw ParseError(item_path, "Unclosed frontmatter");
    }
}

BacklogItem item_from_context(
    const std::filesystem::path& item_path,
    const FrontmatterContext& ctx,
    bool include_body_sections
) {
    if (ctx.metadata.IsNull()) {
        throw ParseError(item_path.string(), "Invalid or missing frontmatter");
    }

    try {
        BacklogItem item;
        item.file_path = item_path;

        item.id = ctx.metadata["id"].as<std::string>();
        item.uid = ctx.metadata["uid"].as<std::string>();
        item.type = parse_item_type(ctx.metadata["type"].as<std::string>()).value();
        item.title = ctx.metadata["title"].as<std::string>();
        item.state = parse_item_state(ctx.metadata["state"].as<std::string>()).value();

        item.priority = parse_optional_string(ctx.metadata["priority"]);
        item.parent = parse_optional_string(ctx.metadata["parent"]);
        item.duplicate_of = parse_optional_string(ctx.metadata["duplicate_of"]);
        item.owner = parse_optional_string(ctx.metadata["owner"]);
        item.area = parse_optional_string(ctx.metadata["area"]);
        item.iteration = parse_optional_string(ctx.metadata["iteration"]);
        item.work_intent = parse_optional_string(ctx.metadata["work_intent"]);
        item.execution_mode = parse_optional_string(ctx.metadata["execution_mode"]);
        item.result_contract = parse_optional_string(ctx.metadata["result_contract"]);
        item.evidence_requirement = parse_optional_string(ctx.metadata["evidence_requirement"]);
        item.follow_up_policy = parse_optional_string(ctx.metadata["follow_up_policy"]);
        item.no_go_or_defer_policy = parse_optional_string(ctx.metadata["no_go_or_defer_policy"]);
        item.intent_author = parse_optional_string(ctx.metadata["intent.author"]);
        item.intent_source = parse_optional_string(ctx.metadata["intent.source"]);
        item.intent_owner = parse_optional_string(ctx.metadata["intent.owner"]);
        item.intent_rationale = parse_optional_string(ctx.metadata["intent.rationale"]);
        item.intent_reviewers = parse_string_list(ctx.metadata["intent.reviewers"]);
        item.intent_provenance_refs = parse_string_list(ctx.metadata["intent.provenance_refs"]);
        item.intent_conflicts_with = parse_string_list(ctx.metadata["intent.conflicts_with"]);
        item.intent_supersedes = parse_string_list(ctx.metadata["intent.supersedes"]);
        item.created = ctx.metadata["created"].as<std::string>();
        item.updated = ctx.metadata["updated"].as<std::string>();

        item.tags = parse_string_list(ctx.metadata["tags"]);
        item.decisions = parse_string_list(ctx.metadata["decisions"]);

        if (auto external = ctx.metadata["external"]; external && external.IsMap()) {
            for (const auto& entry : external) {
                if (!entry.first.IsNull() && !entry.second.IsNull()) {
                    item.external[entry.first.as<std::string>()] = entry.second.as<std::string>();
                }
            }
        }

        if (auto links = ctx.metadata["links"]; links && links.IsMap()) {
            item.links.relates = parse_string_list(links["relates"]);
            item.links.blocks = parse_string_list(links["blocks"]);
            item.links.blocked_by = parse_string_list(links["blocked_by"]);
        }

        if (include_body_sections) {
            auto sections = Frontmatter::parse_body_sections(ctx.body);
            if (sections.count("context")) item.context = sections["context"];
            if (sections.count("goal")) item.goal = sections["goal"];
            if (sections.count("non_goals")) item.non_goals = sections["non_goals"];
            if (sections.count("intent_amendments")) item.intent_amendments = sections["intent_amendments"];
            if (sections.count("approach")) item.approach = sections["approach"];
            if (sections.count("alternatives")) item.alternatives = sections["alternatives"];
            if (sections.count("acceptance_criteria")) item.acceptance_criteria = sections["acceptance_criteria"];
            if (sections.count("risks")) item.risks = sections["risks"];

            if (sections.count("worklog")) {
                std::stringstream work_ss(sections["worklog"]);
                std::string line;
                while (std::getline(work_ss, line)) {
                    if (!line.empty()) item.worklog.push_back(line);
                }
            }
        }

        return item;
    } catch (const std::exception& e) {
        throw ParseError(item_path.string(), std::string("Mapping error: ") + e.what());
    }
}

}  // namespace

CanonicalStore::CanonicalStore(const std::filesystem::path& product_root)
    : product_root_(product_root), items_root_(product_root / "items") {}

CanonicalWriteRevision CanonicalStore::read_write_revision() const {
    const auto path = canonical_write_revision_path(product_root_);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        throw WriteError("canonical_write_revision_missing");
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaximumCanonicalWriteRevisionBytes) {
        throw WriteError("canonical_write_revision_invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw WriteError("canonical_write_revision_read_failed");
    }
    std::string schema;
    std::string current;
    std::string previous;
    std::string operation;
    std::string source_hash;
    std::string content_size;
    std::string content_hash;
    std::string trailing;
    if (!std::getline(input, schema) ||
        !std::getline(input, current) ||
        !std::getline(input, previous) ||
        !std::getline(input, operation) ||
        !std::getline(input, source_hash) ||
        !std::getline(input, content_size) ||
        !std::getline(input, content_hash) ||
        (std::getline(input, trailing) && !trailing.empty()) ||
        schema != kCanonicalWriteRevisionSchema ||
        !valid_write_revision_token(current) ||
        (previous != "-" && !valid_write_revision_token(previous))) {
        throw WriteError("canonical_write_revision_invalid");
    }
    const auto parsed_size = parse_uint64(content_size);
    const bool bound_write = operation == "write" && valid_hash_token(source_hash)
        && parsed_size && *parsed_size <= kMaximumCanonicalItemBytes
        && valid_hash_token(content_hash);
    const bool bound_delete = operation == "delete" && valid_hash_token(source_hash)
        && parsed_size && *parsed_size == 0 && content_hash == "-";
    const bool unbound = (operation == "unbound" || operation == "reset")
        && source_hash == "-" && parsed_size && *parsed_size == 0 && content_hash == "-";
    if (!bound_write && !bound_delete && !unbound) {
        throw WriteError("canonical_write_revision_invalid");
    }
    return CanonicalWriteRevision{
        current,
        previous == "-" ? std::nullopt : std::optional<std::string>(previous),
        operation,
        source_hash == "-" ? std::nullopt : std::optional<std::string>(source_hash),
        *parsed_size,
        content_hash == "-" ? std::nullopt : std::optional<std::string>(content_hash)};
}

CanonicalWriteRevision CanonicalStore::begin_write() const {
    std::error_code parent_error;
    std::filesystem::create_directories(
        canonical_write_revision_path(product_root_).parent_path(), parent_error);
    if (parent_error) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    RevisionLock lock(canonical_write_revision_lock_path(product_root_));
    std::optional<std::string> previous;
    const auto path = canonical_write_revision_path(product_root_);
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        throw WriteError("canonical_write_revision_stat_failed");
    }
    if (exists) {
        previous = read_write_revision().current;
    }
    CanonicalWriteRevision revision{
        generate_uuid_v7(), previous, "unbound", std::nullopt, 0, std::nullopt};
    write_canonical_write_revision(product_root_, revision);
    return revision;
}

CanonicalWriteRevision CanonicalStore::reset_write_revision() const {
    std::error_code parent_error;
    std::filesystem::create_directories(
        canonical_write_revision_path(product_root_).parent_path(), parent_error);
    if (parent_error) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    RevisionLock lock(canonical_write_revision_lock_path(product_root_));
    CanonicalWriteRevision revision{
        generate_uuid_v7(), std::nullopt, "reset", std::nullopt, 0, std::nullopt};
    write_canonical_write_revision(product_root_, revision);
    return revision;
}

CanonicalWriteRevision CanonicalStore::write_materialized(
    const std::filesystem::path& item_path,
    const std::string_view content
) const {
    if (!is_inside_path(item_path, product_root_)) {
        throw WriteError("canonical_write_source_outside_product");
    }
    if (content.size() > kMaximumCanonicalItemBytes) {
        throw WriteError("canonical_write_source_too_large");
    }

    const auto receipt_source_hash = source_ref_hash(product_root_, item_path);
    std::error_code error;
    std::filesystem::create_directories(
        canonical_write_revision_path(product_root_).parent_path(), error);
    if (error) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    RevisionLock lock(canonical_write_revision_lock_path(product_root_));

    std::optional<std::string> previous;
    const auto revision_path = canonical_write_revision_path(product_root_);
    const bool revision_exists = std::filesystem::exists(revision_path, error);
    if (error) {
        throw WriteError("canonical_write_revision_stat_failed");
    }
    if (revision_exists) {
        previous = read_write_revision().current;
    }

    std::filesystem::create_directories(item_path.parent_path(), error);
    if (error) {
        throw WriteError("canonical_write_parent_create_failed");
    }
    std::ofstream output(item_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw WriteError("canonical_write_open_failed");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output.good()) {
        throw WriteError("canonical_write_failed");
    }
    output.close();
    if (output.fail()) {
        throw WriteError("canonical_write_close_failed");
    }
    if (read_exact_bytes(item_path, kMaximumCanonicalItemBytes) != content) {
        throw WriteError("canonical_write_readback_mismatch");
    }

    CanonicalWriteRevision revision{
        generate_uuid_v7(),
        previous,
        "write",
        receipt_source_hash,
        static_cast<std::uint64_t>(content.size()),
        hash_content(content),
    };
    write_canonical_write_revision(product_root_, revision);
    return revision;
}

CanonicalWriteRevision CanonicalStore::remove_file(
    const std::filesystem::path& item_path
) const {
    if (!is_inside_path(item_path, product_root_)) {
        throw WriteError("canonical_delete_source_outside_product");
    }
    const auto receipt_source_hash = source_ref_hash(product_root_, item_path);
    std::error_code error;
    std::filesystem::create_directories(
        canonical_write_revision_path(product_root_).parent_path(), error);
    if (error) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    RevisionLock lock(canonical_write_revision_lock_path(product_root_));

    std::optional<std::string> previous;
    const auto revision_path = canonical_write_revision_path(product_root_);
    const bool revision_exists = std::filesystem::exists(revision_path, error);
    if (error) {
        throw WriteError("canonical_write_revision_stat_failed");
    }
    if (revision_exists) {
        previous = read_write_revision().current;
    }
    if (!std::filesystem::remove(item_path, error) || error) {
        throw WriteError("canonical_delete_failed");
    }
    if (std::filesystem::exists(item_path, error) || error) {
        throw WriteError("canonical_delete_readback_failed");
    }

    CanonicalWriteRevision revision{
        generate_uuid_v7(),
        previous,
        "delete",
        receipt_source_hash,
        0,
        std::nullopt,
    };
    write_canonical_write_revision(product_root_, revision);
    return revision;
}

CanonicalWriteRevision CanonicalStore::move_file(
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination_path
) const {
    if (!is_inside_path(source_path, product_root_)
        || !is_inside_path(destination_path, product_root_)) {
        throw WriteError("canonical_move_source_outside_product");
    }
    const auto receipt_source_hash = source_ref_hash(product_root_, source_path);
    std::error_code error;
    std::filesystem::create_directories(
        canonical_write_revision_path(product_root_).parent_path(), error);
    if (error) {
        throw WriteError("canonical_write_revision_parent_create_failed");
    }
    RevisionLock lock(canonical_write_revision_lock_path(product_root_));

    std::optional<std::string> previous;
    const auto revision_path = canonical_write_revision_path(product_root_);
    const bool revision_exists = std::filesystem::exists(revision_path, error);
    if (error) {
        throw WriteError("canonical_write_revision_stat_failed");
    }
    if (revision_exists) {
        previous = read_write_revision().current;
    }
    std::filesystem::create_directories(destination_path.parent_path(), error);
    if (error) {
        throw WriteError("canonical_move_parent_create_failed");
    }
    std::filesystem::rename(source_path, destination_path, error);
    if (error) {
        throw WriteError("canonical_move_failed");
    }
    if (std::filesystem::exists(source_path, error) || error
        || !std::filesystem::exists(destination_path, error) || error) {
        throw WriteError("canonical_move_readback_failed");
    }

    CanonicalWriteRevision revision{
        generate_uuid_v7(),
        previous,
        "delete",
        receipt_source_hash,
        0,
        std::nullopt,
    };
    write_canonical_write_revision(product_root_, revision);
    return revision;
}

BacklogItem CanonicalStore::read(const std::filesystem::path& item_path) const {
    diagnostics::ScopedMutationSpan span("canonical_store.read", item_path.filename().string());
    if (!is_inside_path(item_path, product_root_)) {
        throw ParseError(item_path, "Item path is outside active product root");
    }
    if (!std::filesystem::exists(item_path)) {
        throw ItemNotFoundError(item_path.string());
    }

    std::ifstream f(item_path);
    if (!f.is_open()) {
        throw ParseError(item_path.string(), "Failed to open file");
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();

    auto ctx = Frontmatter::parse(content);
    return item_from_context(item_path, ctx, true);
}

BacklogItem CanonicalStore::read_metadata(const std::filesystem::path& item_path) const {
    diagnostics::ScopedMutationSpan span("canonical_store.read_metadata", item_path.filename().string());
    if (!is_inside_path(item_path, product_root_)) {
        throw ParseError(item_path, "Item path is outside active product root");
    }
    if (!std::filesystem::exists(item_path)) {
        throw ItemNotFoundError(item_path.string());
    }

    std::ifstream f(item_path);
    if (!f.is_open()) {
        throw ParseError(item_path.string(), "Failed to open file");
    }

    std::string first_line;
    if (!std::getline(f, first_line)) {
        throw ParseError(item_path.string(), "Empty item file");
    }
    if (!first_line.empty() && first_line.back() == '\r') {
        first_line.pop_back();
    }
    if (first_line != "---") {
        throw ParseError(item_path.string(), "Invalid or missing frontmatter");
    }

    std::ostringstream yaml;
    std::string line;
    bool closed = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "---") {
            closed = true;
            break;
        }
        yaml << line << "\n";
    }
    if (!closed) {
        throw ParseError(item_path.string(), "Unclosed frontmatter");
    }

    FrontmatterContext ctx;
    try {
        ctx.metadata = YAML::Load(yaml.str());
    } catch (const YAML::Exception&) {
        ctx.metadata = YAML::Node(YAML::NodeType::Null);
    }
    return item_from_context(item_path, ctx, false);
}

BacklogItem CanonicalStore::read_metadata_bounded(
    const std::filesystem::path& item_path,
    std::size_t maximum_bytes,
    std::size_t* bytes_read
) const {
    diagnostics::ScopedMutationSpan span("canonical_store.read_metadata_bounded", item_path.filename().string());
    if (bytes_read != nullptr) {
        *bytes_read = 0;
    }
    if (!is_inside_path(item_path, product_root_)) {
        throw ParseError(item_path, "Item path is outside active product root");
    }
    if (!std::filesystem::exists(item_path)) {
        throw ItemNotFoundError(item_path);
    }

    std::ifstream input(item_path, std::ios::binary);
    if (!input.is_open()) {
        throw ParseError(item_path, "Failed to open file");
    }

    std::size_t bounded_bytes_read = 0;
    std::string yaml;
    try {
        yaml = read_bounded_frontmatter_yaml(input, item_path, maximum_bytes, bounded_bytes_read);
    } catch (...) {
        if (bytes_read != nullptr) {
            *bytes_read = bounded_bytes_read;
        }
        throw;
    }
    if (bytes_read != nullptr) {
        *bytes_read = bounded_bytes_read;
    }

    FrontmatterContext ctx;
    try {
        ctx.metadata = YAML::Load(yaml);
    } catch (const YAML::Exception&) {
        ctx.metadata = YAML::Node(YAML::NodeType::Null);
    }
    return item_from_context(item_path, ctx, false);
}

void CanonicalStore::write(BacklogItem& item) const {
    diagnostics::ScopedMutationSpan span("canonical_store.write", item.id);
    auto errors = Validator::validate_schema(item);
    if (!errors.empty()) {
        std::string err_msg;
        for (const auto& e : errors) err_msg += e + "; ";
        throw ValidationError({err_msg});
    }

    if (!item.file_path) {
        throw WriteError("Item file_path is not set");
    }
    if (!is_inside_path(*item.file_path, product_root_)) {
        throw WriteError("Item file_path is outside active product root: " + item.file_path->string());
    }

    // Update timestamp
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    std::stringstream date_ss;
    date_ss << std::put_time(&buf, "%Y-%m-%d");
    item.updated = date_ss.str();

    // Build metadata node
    YAML::Node metadata;
    metadata["id"] = item.id;
    metadata["uid"] = item.uid;
    metadata["type"] = to_string(item.type);
    metadata["title"] = item.title;
    metadata["state"] = to_string(item.state);
    metadata["priority"] = item.priority ? YAML::Node(*item.priority) : YAML::Node(YAML::NodeType::Null);
    metadata["parent"] = item.parent ? YAML::Node(*item.parent) : YAML::Node(YAML::NodeType::Null);
    metadata["duplicate_of"] = item.duplicate_of ? YAML::Node(*item.duplicate_of) : YAML::Node(YAML::NodeType::Null);
    metadata["owner"] = item.owner ? YAML::Node(*item.owner) : YAML::Node(YAML::NodeType::Null);
    metadata["area"] = item.area ? YAML::Node(*item.area) : YAML::Node(YAML::NodeType::Null);
    metadata["iteration"] = item.iteration ? YAML::Node(*item.iteration) : YAML::Node(YAML::NodeType::Null);
    metadata["work_intent"] = item.work_intent ? YAML::Node(*item.work_intent) : YAML::Node(YAML::NodeType::Null);
    metadata["execution_mode"] = item.execution_mode ? YAML::Node(*item.execution_mode) : YAML::Node(YAML::NodeType::Null);
    metadata["result_contract"] = item.result_contract ? YAML::Node(*item.result_contract) : YAML::Node(YAML::NodeType::Null);
    metadata["evidence_requirement"] = item.evidence_requirement ? YAML::Node(*item.evidence_requirement) : YAML::Node(YAML::NodeType::Null);
    metadata["follow_up_policy"] = item.follow_up_policy ? YAML::Node(*item.follow_up_policy) : YAML::Node(YAML::NodeType::Null);
    metadata["no_go_or_defer_policy"] = item.no_go_or_defer_policy ? YAML::Node(*item.no_go_or_defer_policy) : YAML::Node(YAML::NodeType::Null);
    metadata["intent.author"] = item.intent_author ? YAML::Node(*item.intent_author) : YAML::Node(YAML::NodeType::Null);
    metadata["intent.source"] = item.intent_source ? YAML::Node(*item.intent_source) : YAML::Node(YAML::NodeType::Null);
    metadata["intent.owner"] = item.intent_owner ? YAML::Node(*item.intent_owner) : YAML::Node(YAML::NodeType::Null);
    metadata["intent.rationale"] = item.intent_rationale ? YAML::Node(*item.intent_rationale) : YAML::Node(YAML::NodeType::Null);
    YAML::Node intent_reviewers(YAML::NodeType::Sequence);
    for (const auto& value : item.intent_reviewers) intent_reviewers.push_back(value);
    metadata["intent.reviewers"] = intent_reviewers;
    YAML::Node intent_provenance_refs(YAML::NodeType::Sequence);
    for (const auto& value : item.intent_provenance_refs) intent_provenance_refs.push_back(value);
    metadata["intent.provenance_refs"] = intent_provenance_refs;
    YAML::Node intent_conflicts_with(YAML::NodeType::Sequence);
    for (const auto& value : item.intent_conflicts_with) intent_conflicts_with.push_back(value);
    metadata["intent.conflicts_with"] = intent_conflicts_with;
    YAML::Node intent_supersedes(YAML::NodeType::Sequence);
    for (const auto& value : item.intent_supersedes) intent_supersedes.push_back(value);
    metadata["intent.supersedes"] = intent_supersedes;
    metadata["created"] = item.created;
    metadata["updated"] = item.updated;

    YAML::Node external(YAML::NodeType::Map);
    for (const auto& [key, value] : item.external) {
        external[key] = value;
    }
    metadata["external"] = external;

    YAML::Node links(YAML::NodeType::Map);
    YAML::Node relates(YAML::NodeType::Sequence);
    for (const auto& value : item.links.relates) relates.push_back(value);
    YAML::Node blocks(YAML::NodeType::Sequence);
    for (const auto& value : item.links.blocks) blocks.push_back(value);
    YAML::Node blocked_by(YAML::NodeType::Sequence);
    for (const auto& value : item.links.blocked_by) blocked_by.push_back(value);
    links["relates"] = relates;
    links["blocks"] = blocks;
    links["blocked_by"] = blocked_by;
    metadata["links"] = links;

    YAML::Node decisions(YAML::NodeType::Sequence);
    for (const auto& value : item.decisions) decisions.push_back(value);
    metadata["decisions"] = decisions;
    
    YAML::Node tags(YAML::NodeType::Sequence);
    for (const auto& t : item.tags) tags.push_back(t);
    metadata["tags"] = tags;

    // Body
    std::map<std::string, std::string> sections;
    if (item.context) sections["context"] = *item.context;
    if (item.goal) sections["goal"] = *item.goal;
    if (item.non_goals) sections["non_goals"] = *item.non_goals;
    if (item.intent_amendments) sections["intent_amendments"] = *item.intent_amendments;
    if (item.approach) sections["approach"] = *item.approach;
    if (item.alternatives) sections["alternatives"] = *item.alternatives;
    if (item.acceptance_criteria) sections["acceptance_criteria"] = *item.acceptance_criteria;
    if (item.risks) sections["risks"] = *item.risks;
    
    if (!item.worklog.empty()) {
        std::stringstream work_ss;
        for (const auto& line : item.worklog) work_ss << line << "\n";
        sections["worklog"] = work_ss.str();
    }

    FrontmatterContext ctx;
    ctx.metadata = metadata;
    ctx.body = Frontmatter::serialize_body_sections(sections);

    write_materialized(*item.file_path, Frontmatter::serialize(ctx));
}

BacklogItem CanonicalStore::create(const std::string& prefix, ItemType type, const std::string& title, int next_number, const std::optional<std::string>& parent) const {
    BacklogItem item;
    item.uid = generate_uuid_v7();
    
    // Abbrev mapping
    std::string abbrev;
    std::string type_dir;
    switch(type) {
        case ItemType::Initiative: abbrev = "INIT"; type_dir = "initiative"; break;
        case ItemType::Epic: abbrev = "EPIC"; type_dir = "epic"; break;
        case ItemType::Feature: abbrev = "FTR"; type_dir = "feature"; break;
        case ItemType::UserStory: abbrev = "USR"; type_dir = "userstory"; break;
        case ItemType::Task: abbrev = "TSK"; type_dir = "task"; break;
        case ItemType::SubTask: abbrev = "SUBTSK"; type_dir = "subtask"; break;
        case ItemType::Bug: abbrev = "BUG"; type_dir = "bug"; break;
        case ItemType::Issue: abbrev = "ISS"; type_dir = "issue"; break;
    }

    // ID format: PREFIX-ABBREV-0001
    std::stringstream id_ss;
    id_ss << prefix << "-" << abbrev << "-" << std::setfill('0') << std::setw(4) << next_number;
    item.id = id_ss.str();
    item.title = title;
    item.type = type;
    item.state = ItemState::New;
    item.parent = parent;
    item.work_intent = "implementation";

    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    std::stringstream date_ss;
    date_ss << std::put_time(&buf, "%Y-%m-%d");
    item.created = date_ss.str();
    item.updated = item.created;

    // File path: items/<type>/<bucket>/<id>_<slug>.md
    int bucket = (next_number / 100) * 100;
    std::stringstream bucket_ss;
    bucket_ss << std::setfill('0') << std::setw(4) << bucket;
    
    std::string filename = item.id + "_" + slugify(title) + ".md";
    item.file_path = items_root_ / type_dir / bucket_ss.str() / filename;

    return item;
}

std::vector<std::filesystem::path> CanonicalStore::list_items(std::optional<ItemType> type) const {
    diagnostics::ScopedMutationSpan span("canonical_store.list_items", type ? to_string(*type) : std::string("all"));
    std::vector<std::filesystem::path> results;
    if (!std::filesystem::exists(items_root_)) return results;

    auto scan_dir = [&](const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md" &&
                !entry.path().filename().string().ends_with(".index.md")) {
                results.push_back(entry.path());
            }
        }
    };

    if (type) {
        std::string type_dir;
        switch(*type) {
            case ItemType::Initiative: type_dir = "initiative"; break;
            case ItemType::Epic: type_dir = "epic"; break;
            case ItemType::Feature: type_dir = "feature"; break;
            case ItemType::UserStory: type_dir = "userstory"; break;
            case ItemType::Task: type_dir = "task"; break;
            case ItemType::SubTask: type_dir = "subtask"; break;
            case ItemType::Bug: type_dir = "bug"; break;
            case ItemType::Issue: type_dir = "issue"; break;
        }
        scan_dir(items_root_ / type_dir);
    } else {
        scan_dir(items_root_);
    }

    return results;
}

std::optional<std::filesystem::path> CanonicalStore::find_item_path_by_id(const std::string& id) const {
    diagnostics::ScopedMutationSpan span("canonical_store.find_item_path_by_id", id);
    const auto candidates = find_item_paths_by_id(id);
    if (candidates.size() == 1) {
        return candidates.front();
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> CanonicalStore::find_item_paths_by_id(
    const std::string& id,
    ItemIdLookupDiagnostics* diagnostics
) const {
    diagnostics::ScopedMutationSpan span("canonical_store.find_item_paths_by_id", id);
    ItemIdLookupDiagnostics local_diagnostics;
    ItemIdLookupDiagnostics& lookup = diagnostics ? *diagnostics : local_diagnostics;
    lookup = {};
    std::vector<std::filesystem::path> matches;
    if (!parse_display_id_type_and_number(id)) {
        return matches;
    }

    if (!std::filesystem::exists(items_root_)) {
        return matches;
    }

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        items_root_,
        std::filesystem::directory_options::skip_permission_denied,
        iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            iterator_error.clear();
            continue;
        }
        const auto& path = iterator->path();
        if (path.extension() != ".md") {
            continue;
        }
        ++lookup.item_files_scanned;
        const std::string filename = path.filename().string();
        if (filename.rfind(id, 0) != 0 ||
            (filename.size() > id.size() && filename[id.size()] != '_' && filename[id.size()] != '.')) {
            continue;
        }

        std::error_code file_error;
        if (!iterator->is_regular_file(file_error) || file_error) {
            continue;
        }
        ++lookup.candidate_files_read;
        try {
            const auto item = read_metadata(path);
            if (item.id == id) {
                matches.push_back(path);
            }
        } catch (const std::exception&) {
            // Keep lookup tolerant so malformed unrelated items do not hide resolvable refs.
        }
    }

    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    lookup.matches = matches.size();
    return matches;
}

int CanonicalStore::get_next_id_number(ItemType type) const {
    auto items = list_items(type);
    int max_num = 0;

    for (const auto& p : items) {
        try {
            const auto item = read_metadata(p);
            if (item.type != type) {
                continue;
            }
            const auto parsed = parse_display_id_type_and_number(item.id);
            if (parsed && parsed->first == type && parsed->second > max_num) {
                max_num = parsed->second;
            }
        } catch (const std::exception&) {
            // Fall back to the filename so malformed legacy items still reserve their number.
            static const std::regex filename_number_regex(R"(-(\d{4})_)");
            std::string name = p.filename().string();
            std::smatch match;
            if (std::regex_search(name, match, filename_number_regex)) {
                int num = std::stoi(match[1].str());
                if (num > max_num) max_num = num;
            }
        }
    }
    return max_num + 1;
}

int CanonicalStore::get_max_id_number(const std::string& prefix, ItemType type) const {
    if (!std::filesystem::exists(items_root_)) {
        return 0;
    }

    const std::string type_code = [&]() {
        switch (type) {
            case ItemType::Initiative: return std::string("INIT");
            case ItemType::Epic: return std::string("EPIC");
            case ItemType::Feature: return std::string("FTR");
            case ItemType::UserStory: return std::string("USR");
            case ItemType::Task: return std::string("TSK");
            case ItemType::SubTask: return std::string("SUBTSK");
            case ItemType::Bug: return std::string("BUG");
            case ItemType::Issue: return std::string("ISS");
        }
        return std::string();
    }();
    const std::string id_prefix = prefix + "-" + type_code + "-";
    int max_number = 0;

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        items_root_,
        std::filesystem::directory_options::skip_permission_denied,
        iterator_error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            iterator_error.clear();
            continue;
        }
        const auto& path = iterator->path();
        if (path.extension() != ".md") {
            continue;
        }
        const std::string filename = path.filename().string();
        if (filename.rfind(id_prefix, 0) != 0) {
            continue;
        }
        const auto number_begin = id_prefix.size();
        const auto number_end = filename.find_first_not_of("0123456789", number_begin);
        if (number_end == number_begin ||
            (number_end != std::string::npos && filename[number_end] != '_' && filename[number_end] != '.')) {
            continue;
        }
        try {
            max_number = std::max(max_number, std::stoi(filename.substr(number_begin, number_end - number_begin)));
        } catch (const std::exception&) {
        }
    }
    return max_number;
}

std::string CanonicalStore::slugify(const std::string& text) {
    std::string slug;
    bool pending_separator = false;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '_') {
            if (pending_separator && !slug.empty()) {
                slug.push_back('-');
            }
            slug.push_back(static_cast<char>(std::tolower(c)));
            pending_separator = false;
        } else if (std::isspace(c) || c == '-') {
            pending_separator = !slug.empty();
        }
    }
    if (slug.length() > 50) {
        slug = slug.substr(0, 50);
        while (!slug.empty() && slug.back() == '-') {
            slug.pop_back();
        }
    }
    return slug;
}

std::string CanonicalStore::generate_uuid_v7() {
    // Simplified UUIDv7: 48-bit timestamp + 74-bit random (placeholder for exact bits)
    auto now = std::chrono::system_clock::now();
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFF);

    uint64_t r1 = dist(gen);
    uint64_t r2 = dist(gen);

    // Draft bits for UUIDv7 (approximate)
    // ttt tttt-tttt-7rrr-yrrr-rrrr rrrr rrrr
    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (ms >> 16) << "-"
       << std::setw(4) << (ms & 0xFFFF) << "-"
       << "7" << std::setw(3) << (r1 >> 60) << "-" // marker 7
       << std::setw(1) << (8 | (r1 >> 62 & 0x3)) << std::setw(3) << (r1 & 0xFFF) << "-" // marker 8/9/a/b
       << std::setw(12) << (r2 & 0xFFFFFFFFFFFF);
    
    return ss.str();
}

} // namespace kano::backlog_core
