#include "kano/backlog_core/config/config.hpp"
#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "kano/backlog_ops/product_registration/product_registration_ops.hpp"
#include "kano/backlog_ops/product_relocation/product_relocation_ops.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr int kInjectedProcessExitCode = 86;
std::filesystem::path g_test_binary;

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

#ifdef _WIN32
struct JunctionReparseData {
    DWORD tag;
    WORD data_length;
    WORD reserved;
    WORD substitute_offset;
    WORD substitute_length;
    WORD print_offset;
    WORD print_length;
    WCHAR path_buffer[1];
};

DWORD create_directory_junction(
    const std::filesystem::path& link,
    const std::filesystem::path& target
) {
    std::filesystem::create_directories(link.parent_path());
    if (!CreateDirectoryW(link.c_str(), nullptr)) {
        return GetLastError();
    }
    const auto canonical_target = std::filesystem::canonical(target).native();
    const std::wstring substitute = canonical_target.starts_with(L"\\\\")
        ? L"\\??\\UNC\\" + canonical_target.substr(2)
        : L"\\??\\" + canonical_target;
    const auto substitute_bytes = static_cast<WORD>(
        substitute.size() * sizeof(WCHAR));
    const auto print_bytes = static_cast<WORD>(
        canonical_target.size() * sizeof(WCHAR));
    const auto path_bytes = static_cast<std::size_t>(substitute_bytes) +
                            sizeof(WCHAR) + print_bytes + sizeof(WCHAR);
    if (path_bytes + offsetof(JunctionReparseData, path_buffer) >
        MAXIMUM_REPARSE_DATA_BUFFER_SIZE) {
        std::filesystem::remove(link);
        return ERROR_BUFFER_OVERFLOW;
    }

    std::vector<std::byte> storage(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    auto* data = reinterpret_cast<JunctionReparseData*>(storage.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reserved = 0;
    data->substitute_offset = 0;
    data->substitute_length = substitute_bytes;
    data->print_offset = static_cast<WORD>(
        substitute_bytes + sizeof(WCHAR));
    data->print_length = print_bytes;
    data->data_length = static_cast<WORD>(8u + path_bytes);
    std::memcpy(
        data->path_buffer, substitute.c_str(),
        substitute_bytes + sizeof(WCHAR));
    auto* print_buffer = reinterpret_cast<std::byte*>(data->path_buffer) +
                         data->print_offset;
    std::memcpy(
        print_buffer, canonical_target.c_str(),
        print_bytes + sizeof(WCHAR));

    const HANDLE handle = CreateFileW(
        link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        std::filesystem::remove(link);
        return error;
    }
    DWORD returned = 0;
    const auto input_size = static_cast<DWORD>(
        8u + data->data_length);
    const bool succeeded = DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, data, input_size,
        nullptr, 0, &returned, nullptr) != FALSE;
    const auto error = succeeded ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!succeeded) {
        std::filesystem::remove(link);
    }
    return error;
}

bool is_directory_junction(const std::filesystem::path& path) {
    const HANDLE handle = CreateFileW(
        path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::vector<std::byte> storage(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD returned = 0;
    const bool succeeded = DeviceIoControl(
        handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
        storage.data(), static_cast<DWORD>(storage.size()),
        &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return succeeded && returned >= sizeof(DWORD) &&
           *reinterpret_cast<const DWORD*>(storage.data()) ==
               IO_REPARSE_TAG_MOUNT_POINT;
}

void create_junction_or_throw(
    const std::filesystem::path& link,
    const std::filesystem::path& target
) {
    const auto error = create_directory_junction(link, target);
    expect(
        error == ERROR_SUCCESS && is_directory_junction(link),
        "Windows junction fixture creation failed: win32_" +
            std::to_string(error));
}
#endif

std::filesystem::path make_temp_root() {
    std::random_device source;
    std::mt19937 generator(source());
    std::uniform_int_distribution<unsigned int> distribution(
        0, 0xffffff);
    std::ostringstream suffix;
    suffix << std::hex << distribution(generator);
    const auto root =
        std::filesystem::temp_directory_path() / "opencode" /
        "kano-backlog-product-registration-smoke" / suffix.str();
    std::filesystem::create_directories(root);
    return root;
}

using TreeSnapshot = std::map<std::string, std::string>;

TreeSnapshot snapshot_tree(const std::filesystem::path& root) {
    TreeSnapshot snapshot;
    if (!std::filesystem::exists(root)) {
        return snapshot;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        const auto relative =
            std::filesystem::relative(entry.path(), root).generic_string();
        if (entry.is_symlink()) {
            snapshot.emplace(
                "link:" + relative,
                std::filesystem::read_symlink(entry.path()).generic_string());
        } else if (entry.is_directory()) {
            snapshot.emplace("directory:" + relative, std::string{});
        } else if (entry.is_regular_file()) {
            snapshot.emplace("file:" + relative, read_text(entry.path()));
        } else {
            snapshot.emplace("other:" + relative, std::string{});
        }
    }
    return snapshot;
}

bool has_only_canonical_write_receipt(
    const std::filesystem::path& product_root
) {
    const auto cache = snapshot_tree(product_root / ".cache");
    return cache.size() == 2 &&
           cache.contains("file:canonical-write-revision-v1") &&
           cache.contains("file:canonical-write-revision-v1.lock");
}

bool is_strict_uuid_v7(const std::string& value) {
    static const std::regex pattern(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)");
    return std::regex_match(value, pattern);
}

bool is_public_ref(const std::string& value) {
    return !value.empty() &&
           value.find("..") == std::string::npos &&
           value.find('\\') == std::string::npos &&
           !std::filesystem::path(value).is_absolute();
}

std::size_t count_occurrences(
    const std::string& content,
    const std::string& needle
) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = content.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::string sha256_hex(const std::string& content) {
    static constexpr std::array<std::uint32_t, 64> constants = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774u, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
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
            words[index] =
                (static_cast<std::uint32_t>(bytes[base]) << 24u) |
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
            words[index] =
                words[index - 16u] + s0 + words[index - 7u] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = hash;
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 =
                rotate(e, 6u) ^ rotate(e, 11u) ^ rotate(e, 25u);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temp1 =
                h + sum1 + choose + constants[index] + words[index];
            const auto sum0 =
                rotate(a, 2u) ^ rotate(a, 13u) ^ rotate(a, 22u);
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

struct Fixture {
    std::filesystem::path root;
    std::filesystem::path shared;
    std::filesystem::path external_root;
    std::filesystem::path destination;
    std::filesystem::path config;
    std::filesystem::path local_config;
    std::filesystem::path feature_path;
    std::filesystem::path task_path;
    std::string config_before;
    std::string local_config_before;
    std::string feature_bytes;
    std::string task_bytes;
    TreeSnapshot source_before;
    std::vector<std::string> ids;
    std::vector<std::string> uids;
};

kano::backlog_core::BacklogItem create_item(
    const std::filesystem::path& product_root,
    const std::string& prefix,
    kano::backlog_core::ItemType type,
    int number,
    const std::string& title,
    const std::string& uid,
    const std::optional<std::string>& parent = std::nullopt
) {
    kano::backlog_core::CanonicalStore store(product_root);
    auto item = store.create(prefix, type, title, number, parent);
    item.uid = uid;
    item.priority = "P2";
    item.area = "fixture";
    item.iteration = "backlog";
    item.context = "Canonical external-root registration fixture.";
    item.goal = "Register an existing product through shared config only.";
    item.approach =
        "Plan a bounded read-only inventory and publish one registry entry.";
    item.acceptance_criteria =
        "Source bytes stay exact and no canonical destination is created.";
    item.risks =
        "Disposable fixture only; no developer or production path is used.";
    item.worklog.push_back(
        "2026-08-29 00:00 [agent=fixture-runner] Canonical evidence " +
        item.id + " remains byte exact.");
    store.write(item);
    expect(
        is_strict_uuid_v7(item.uid),
        "fixture UID must be strict lowercase UUIDv7");
    return item;
}

Fixture make_fixture() {
    using kano::backlog_core::CanonicalStore;
    using kano::backlog_core::ItemType;

    Fixture fixture;
    fixture.root = make_temp_root();
    fixture.shared = fixture.root / "shared-backlog";
    fixture.external_root =
        fixture.root / "external-products" / "HorizonQuestDemo";
    fixture.destination =
        fixture.shared / "products" / "HorizonQuestDemo";
    fixture.config =
        fixture.shared / ".kano" / "backlog_config.toml";
    fixture.local_config =
        fixture.external_root / "_config" / "config.toml";

    const auto observer_root = fixture.shared / "products" / "observer";
    std::filesystem::create_directories(observer_root / "items");
    fixture.config_before =
        "# Existing shared registry bytes must remain a prefix.\n"
        "[products.observer]\n"
        "name = \"Observer\"\n"
        "prefix = \"OBS\"\n"
        "backlog_root = \"products/observer\"\n";
    write_text(fixture.config, fixture.config_before);

    fixture.local_config_before =
        "[product]\n"
        "name = \"Horizon Quest Demo\"\n"
        "prefix = \"HQST\"\n";
    write_text(fixture.local_config, fixture.local_config_before);

    auto feature = create_item(
        fixture.external_root,
        "HQST",
        ItemType::Feature,
        1,
        "External registration fixture feature",
        "019d0000-0001-7000-8000-000000000001");
    auto task = create_item(
        fixture.external_root,
        "HQST",
        ItemType::Task,
        1,
        "External registration fixture task",
        "019d0000-0002-7000-8000-000000000002",
        feature.id);
    feature.links.relates.push_back(task.id);
    CanonicalStore(fixture.external_root).write(feature);

    fixture.feature_path = *feature.file_path;
    fixture.task_path = *task.file_path;
    fixture.feature_bytes = read_text(fixture.feature_path);
    fixture.task_bytes = read_text(fixture.task_path);
    fixture.ids = {feature.id, task.id};
    fixture.uids = {feature.uid, task.uid};
    fixture.source_before = snapshot_tree(fixture.external_root);

    expect(
        fixture.ids == std::vector<std::string>{
            "HQST-FTR-0001", "HQST-TSK-0001"},
        "fixture must use canonical HQST display IDs");
    expect(
        std::all_of(
            fixture.uids.begin(), fixture.uids.end(), is_strict_uuid_v7),
        "fixture must contain only strict UUIDv7 identities");
    expect(
        !std::filesystem::exists(fixture.destination),
        "canonical destination must begin absent");
    expect(
        has_only_canonical_write_receipt(fixture.external_root) &&
            !std::filesystem::exists(fixture.external_root / "_meta") &&
            !std::filesystem::exists(fixture.external_root / "views"),
        "external fixture must begin with only the canonical write receipt");
    expect(
        fixture.config_before.find("[products.HorizonQuestDemo]") ==
            std::string::npos,
        "fixture must not directly pre-register the target product");
    return fixture;
}

kano::backlog_ops::ProductRegistrationOps::PlanOptions plan_options(
    const Fixture& fixture
) {
    using kano::backlog_ops::ProductRegistrationLimits;
    using kano::backlog_ops::ProductRegistrationRequest;

    kano::backlog_ops::ProductRegistrationOps::PlanOptions options;
    options.backlog_root = fixture.shared;
    options.request = ProductRegistrationRequest{
        .product = "HorizonQuestDemo",
        .product_name = "Horizon Quest Demo",
        .prefix = "HQST",
        .aliases = {" Quest Demo ", "HORIZON-DEMO", "horizon-demo"},
        .repo_bindings = {
            " Repo/HorizonQuestDemo ", "repo/horizonquestdemo"},
        .external_root = fixture.external_root,
    };
    options.limits = ProductRegistrationLimits{
        .max_files = 64,
        .max_bytes = 2u * 1024u * 1024u,
        .max_items = 16,
    };
    return options;
}

kano::backlog_ops::ProductRegistrationOps::ApplyOptions apply_options(
    const Fixture& fixture,
    const std::string& plan_hash
) {
    kano::backlog_ops::ProductRegistrationOps::ApplyOptions options;
    options.plan = plan_options(fixture);
    options.expected_plan_hash = plan_hash;
    options.agent = "sisyphus";
    options.confirm = true;
    return options;
}

#ifdef _WIN32
std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const auto ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2u + 1u, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2u, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}
#endif

int launch_process_death_child(
    const Fixture& fixture,
    const std::string& plan_hash,
    std::string_view phase
) {
    const auto phase_text = std::string(phase);
#ifdef _WIN32
    std::wstring command = quote_windows_argument(g_test_binary.native()) +
        L" --process-death-child " +
        quote_windows_argument(fixture.shared.native()) + L" " +
        quote_windows_argument(fixture.external_root.native()) + L" " +
        quote_windows_argument(
            std::wstring(plan_hash.begin(), plan_hash.end())) + L" " +
        quote_windows_argument(
            std::wstring(phase_text.begin(), phase_text.end()));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    expect(
        CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process) != FALSE,
        "failed to launch process-death child: win32_" +
            std::to_string(GetLastError()));
    const auto wait = WaitForSingleObject(process.hProcess, 30'000u);
    DWORD exit_code = 0;
    const bool read_exit = wait == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    expect(read_exit, "process-death child did not exit deterministically");
    return static_cast<int>(exit_code);
#else
    const auto child = fork();
    expect(child >= 0, "failed to fork process-death child");
    if (child == 0) {
        const auto binary = g_test_binary.string();
        const auto shared = fixture.shared.string();
        const auto external = fixture.external_root.string();
        execl(
            binary.c_str(), binary.c_str(), "--process-death-child",
            shared.c_str(), external.c_str(), plan_hash.c_str(),
            phase_text.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    expect(
        waitpid(child, &status, 0) == child,
        "failed to wait for process-death child");
    expect(WIFEXITED(status), "process-death child did not exit normally");
    return WEXITSTATUS(status);
#endif
}

int run_process_death_child(int argc, char** argv) {
    if (argc != 6) {
        return 2;
    }
    Fixture fixture;
    fixture.shared = std::filesystem::absolute(argv[2]);
    fixture.external_root = std::filesystem::absolute(argv[3]);
    auto options = apply_options(fixture, argv[4]);
    options.agent = "actor-a";
    options.inject_process_exit_after = argv[5];
    options.injected_process_exit_code = kInjectedProcessExitCode;
    const auto unexpected =
        kano::backlog_ops::ProductRegistrationOps::apply(options);
    std::cerr << "process-death child unexpectedly returned: "
              << unexpected.to_json(true) << "\n";
    return 3;
}

std::filesystem::path registration_transaction(
    const Fixture& fixture,
    const std::string& plan_hash
) {
    return fixture.shared / ".kano" / "cache" /
           "product-registrations" / plan_hash;
}

std::filesystem::path registration_root(const Fixture& fixture) {
    return fixture.shared / ".kano" / "cache" /
           "product-registrations";
}

std::filesystem::path config_lock_path(const Fixture& fixture) {
    return fixture.shared / ".kano" /
           "product-registration.config.lock";
}

std::filesystem::path config_guard_path(const Fixture& fixture) {
    return fixture.shared / ".kano" /
           "product-registration.config.guard.lock";
}

bool is_regular_non_reparse_file(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
#else
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
           !std::filesystem::is_symlink(status);
#endif
}

void plant_valid_stale_final_lock(
    const Fixture& fixture,
    const std::string& plan_hash
) {
    constexpr std::string_view token = "0-dead-beef";
    const auto lock = config_lock_path(fixture);
    expect(
        std::filesystem::create_directory(lock),
        "stale-lock fixture must create an exclusive final directory");
    std::ostringstream owner;
    owner << "{\n"
          << "  \"schema\": \"kob.product_registration.lock.v1\",\n"
          << "  \"plan_hash\": \"" << plan_hash << "\",\n"
          << "  \"owner_token\": \"" << token << "\",\n"
          << "  \"pid\": 0,\n"
          << "  \"created_at\": \"2026-08-29T00:00:00Z\"\n"
          << "}\n";
    write_text(
        lock / ("owner." + std::string(token) + ".json"), owner.str());
}

using CrashDebris =
    std::vector<std::pair<std::filesystem::path, std::string>>;

CrashDebris plant_atomic_debris(
    const std::filesystem::path& parent,
    const std::string& plan_hash,
    std::string_view role,
    std::size_t count
) {
    CrashDebris debris;
    debris.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::ostringstream suffix;
        suffix << "dead-beef-" << std::hex << index + 1u;
        const auto path = parent /
            (".kob-pr." + plan_hash.substr(0, 16u) + "." +
             std::string(role) + "." + suffix.str());
        const auto content = "{truncated-" + std::string(role) + "-" +
                             std::to_string(index) + "\n";
        write_text(path, content);
        debris.emplace_back(path, content);
    }
    return debris;
}

void expect_debris_preserved(
    const CrashDebris& debris,
    const std::string& message
) {
    expect(
        std::all_of(
            debris.begin(), debris.end(), [](const auto& artifact) {
                return std::filesystem::is_regular_file(artifact.first) &&
                       read_text(artifact.first) == artifact.second;
            }),
        message);
}

CrashDebris plant_partial_stage_debris(
    const Fixture& fixture,
    const std::string& plan_hash,
    std::size_t count
) {
    CrashDebris debris;
    const auto root = registration_root(fixture);
    std::filesystem::create_directories(root);
    for (std::size_t index = 0; index < count; ++index) {
        std::ostringstream suffix;
        suffix << "dead-beef-" << std::hex << index + 1u;
        const auto token = suffix.str();
        const auto stage = root /
            (".kob-stage." + plan_hash.substr(0, 16u) + "." + token);
        const auto owner = stage / ("stage-owner." + token + ".json");
        const auto before = stage / "config.before";
        const auto owner_bytes = "{\"schema\":\"partial-" +
                                 std::to_string(index) + "\n";
        const auto before_bytes = "partial-before-" +
                                  std::to_string(index) + "\n";
        write_text(owner, owner_bytes);
        write_text(before, before_bytes);
        debris.emplace_back(owner, owner_bytes);
        debris.emplace_back(before, before_bytes);
    }
    return debris;
}

CrashDebris plant_partial_lock_candidate_debris(
    const Fixture& fixture,
    std::size_t count
) {
    CrashDebris debris;
    const auto lock = config_lock_path(fixture);
    for (std::size_t index = 0; index < count; ++index) {
        std::ostringstream suffix;
        suffix << "dead-beef-" << std::hex << index + 1u;
        const auto token = suffix.str();
        auto candidate = lock;
        candidate += ".candidate." + token;
        const auto owner = candidate / ("owner." + token + ".json");
        const auto content = "{\"schema\":\"partial-lock-owner-" +
                             std::to_string(index) + "\n";
        write_text(owner, content);
        debris.emplace_back(owner, content);
    }
    return debris;
}

bool has_registration_staging(const Fixture& fixture) {
    const auto registrations = fixture.shared / ".kano" / "cache" /
                               "product-registrations";
    if (!std::filesystem::is_directory(registrations)) {
        return false;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(registrations)) {
        if (entry.path().filename().generic_string().starts_with(
                ".kob-stage.")) {
            return true;
        }
    }
    return false;
}

kano::backlog_ops::ProductRegistrationOps::PlanOptions
make_rival_plan_options(Fixture& fixture) {
    using kano::backlog_core::ItemType;
    auto options = plan_options(fixture);
    options.request.product = "RivalDemo";
    options.request.product_name = "Rival Demo";
    options.request.prefix = "RIV";
    options.request.aliases = {"Rival Demo Alias"};
    options.request.repo_bindings = {"repo/rivaldemo"};
    options.request.external_root =
        fixture.root / "external-products" / "RivalDemo";
    write_text(
        options.request.external_root / "_config" / "config.toml",
        "[product]\n"
        "name = \"Rival Demo\"\n"
        "prefix = \"RIV\"\n");
    (void)create_item(
        options.request.external_root,
        "RIV",
        ItemType::Task,
        1,
        "Concurrent rival registration fixture",
        "019d0000-0010-7000-8000-000000000010");
    return options;
}

std::string expected_config_after(const Fixture& fixture) {
    return fixture.config_before +
           "\n[products.HorizonQuestDemo]\n"
           "name = \"Horizon Quest Demo\"\n"
           "prefix = \"HQST\"\n"
           "backlog_root = \"" +
           std::filesystem::canonical(fixture.external_root).generic_string() +
           "\"\n"
           "aliases = [\"horizon-demo\", \"quest demo\"]\n"
           "repo_bindings = [\"repo/horizonquestdemo\"]\n";
}

void cleanup(Fixture& fixture) {
    if (!fixture.root.empty()) {
        std::error_code error;
        std::filesystem::remove_all(fixture.root, error);
        fixture.root.clear();
    }
}

template <typename Function>
void with_fixture(Function&& function) {
    auto fixture = make_fixture();
    try {
        function(fixture);
        cleanup(fixture);
    } catch (...) {
        cleanup(fixture);
        throw;
    }
}

template <typename Ops>
concept HasRollback = requires(typename Ops::RecoveryOptions options) {
    Ops::rollback(options);
};

static_assert(
    !HasRollback<kano::backlog_ops::ProductRegistrationOps>,
    "config-only product registration recovers only through exact apply; "
    "it must not expose rollback");

void expect_source_and_destination_invariant(const Fixture& fixture) {
    expect(
        snapshot_tree(fixture.external_root) == fixture.source_before,
        "registration must leave the complete source tree byte invariant");
    expect(
        !std::filesystem::exists(fixture.destination),
        "registration must never scaffold the canonical destination");
    expect(
        has_only_canonical_write_receipt(fixture.external_root) &&
            !std::filesystem::exists(fixture.external_root / "_meta") &&
            !std::filesystem::exists(fixture.external_root / "views"),
        "registration must preserve only the canonical write receipt scaffolding");
}

void expect_blocked_plan(
    Fixture& fixture,
    const kano::backlog_ops::ProductRegistrationOps::PlanOptions& options,
    const std::string& blocker,
    const std::string& message
) {
    using kano::backlog_ops::ProductRegistrationOps;
    const auto shared_before = snapshot_tree(fixture.shared);
    const auto source_before = snapshot_tree(fixture.external_root);
    const auto destination_existed =
        std::filesystem::exists(fixture.destination);
    const auto plan = ProductRegistrationOps::plan(options);
    if (plan.ready()) {
        std::cerr << plan.to_json(true) << "\n";
    }
    expect(
        !plan.ready() && contains_prefix(plan.blockers, blocker), message);
    expect(
        snapshot_tree(fixture.shared) == shared_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            std::filesystem::exists(fixture.destination) ==
                destination_existed,
        "blocked planning must be strictly read-only");
    const auto json = plan.to_json();
    expect(
        json.find(fixture.root.string()) == std::string::npos &&
            json.find(fixture.root.generic_string()) == std::string::npos &&
            json.find(fixture.external_root.generic_string()) ==
                std::string::npos,
        "blocked plan JSON must redact raw fixture paths");
}

void expect_blocked_apply_without_writes(
    Fixture& fixture,
    const kano::backlog_ops::ProductRegistrationOps::ApplyOptions& options,
    const std::string& receipt,
    const std::string& message
) {
    using kano::backlog_ops::ProductRegistrationOps;
    const auto shared_before = snapshot_tree(fixture.shared);
    const auto source_before = snapshot_tree(fixture.external_root);
    const auto destination_existed =
        std::filesystem::exists(fixture.destination);
    const auto result = ProductRegistrationOps::apply(options);
    if (result.status != "blocked") {
        std::cerr << result.to_json(true) << "\n";
    }
    expect(
        result.status == "blocked" &&
            contains_prefix(result.operation_receipts, receipt),
        message);
    expect(
        snapshot_tree(fixture.shared) == shared_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            std::filesystem::exists(fixture.destination) ==
                destination_existed,
        "rejected apply gates must run before transaction writes");
}

void append_registry_entry(
    Fixture& fixture,
    const std::string& product,
    const std::string& name,
    const std::string& prefix,
    const std::filesystem::path& backlog_root,
    const std::vector<std::string>& aliases = {},
    const std::vector<std::string>& repo_bindings = {}
) {
    const auto string_array = [](const std::vector<std::string>& values) {
        std::string serialized = "[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0u) {
                serialized += ", ";
            }
            serialized += "\"" + values[index] + "\"";
        }
        return serialized + "]";
    };
    std::string selectors;
    if (!aliases.empty()) {
        selectors += "aliases = " + string_array(aliases) + "\n";
    }
    if (!repo_bindings.empty()) {
        selectors +=
            "repo_bindings = " + string_array(repo_bindings) + "\n";
    }
    write_text(
        fixture.config,
        read_text(fixture.config) +
            "\n[products." + product + "]\n" +
            "name = \"" + name + "\"\n" +
            "prefix = \"" + prefix + "\"\n" +
            "backlog_root = \"" + backlog_root.generic_string() +
            "\"\n" + selectors);
}

void test_deterministic_read_only_plan() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto options = plan_options(fixture);
        const auto first = ProductRegistrationOps::plan(options);
        const auto second = ProductRegistrationOps::plan(options);
        if (!first.ready()) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(first.ready(), "valid external product should be registrable");
        expect(
            first.schema == "kob.product_registration.plan.v1" &&
                first.status == "ready",
            "plan should expose the versioned ready contract");
        expect(
            first.plan_hash.size() == 64 &&
                first.plan_hash == second.plan_hash &&
                first.to_json() == second.to_json(),
            "identical input must produce one deterministic plan receipt");
        expect(
            first.source_revision.size() == 64 &&
                first.config_revision.size() == 64 &&
                first.proposed_config_revision.size() == 64 &&
                first.config_path_digest.size() == 64 &&
                first.external_root_path_digest.size() == 64 &&
                first.canonical_destination_path_digest.size() == 64,
            "plan must bind source and config revisions plus all path digests");
        expect(
            first.product == "HorizonQuestDemo" &&
                first.product_name == "Horizon Quest Demo" &&
                first.prefix == "HQST" &&
                first.aliases == std::vector<std::string>{
                    "horizon-demo", "quest demo"} &&
                first.repo_bindings == std::vector<std::string>{
                    "repo/horizonquestdemo"} &&
                first.request.aliases == first.aliases &&
                first.request.repo_bindings == first.repo_bindings &&
                first.proposed_config_revision ==
                    sha256_hex(expected_config_after(fixture)),
            "plan must preserve exact key case, normalize selector arrays, "
            "and hash the exact proposed config bytes");
        expect(
            first.config_ref ==
                    "project-config:.kano/backlog_config.toml" &&
                first.source_root_ref ==
                    "registration:HorizonQuestDemo:external-root" &&
                first.canonical_destination_ref ==
                    "product:HorizonQuestDemo:shared-root" &&
                first.canonical_destination_absent,
            "plan must expose bounded public refs and destination absence");
        expect(
            first.files.size() == 3 && first.identities.size() == 2,
            "plan should inventory local config and both canonical items");
        for (const auto& file : first.files) {
            expect(
                is_public_ref(file.ref),
                "inventoried file refs must be bounded and public");
        }
        expect(
            first.identities[0].id == fixture.ids[0] &&
                first.identities[0].uid == fixture.uids[0] &&
                first.identities[1].id == fixture.ids[1] &&
                first.identities[1].uid == fixture.uids[1],
            "plan must bind every canonical display ID to its UUIDv7 UID");
        expect(
            first.dry_run && !first.mutates_backlog &&
                first.external_root_read_only &&
                !first.creates_canonical_destination,
            "planning must explicitly report dry-run source-read-only "
            "and no-destination-creation behavior");

        const auto json = first.to_json();
        expect(
            json.find(fixture.root.string()) == std::string::npos &&
                json.find(fixture.root.generic_string()) ==
                    std::string::npos &&
                json.find(fixture.external_root.generic_string()) ==
                    std::string::npos,
            "public plan JSON must never contain a raw absolute root");
        expect(
            json.find("\"max_files\"") != std::string::npos &&
                json.find("\"max_bytes\"") != std::string::npos &&
                json.find("\"max_items\"") != std::string::npos &&
                json.find("\"aliases\":[\"horizon-demo\",\"quest demo\"]") !=
                    std::string::npos &&
                json.find("\"repo_bindings\":[\"repo/horizonquestdemo\"]") !=
                    std::string::npos,
            "the immutable plan must publish every bound selector array");

        auto changed_limit_options = options;
        ++changed_limit_options.limits.max_files;
        const auto changed_limit =
            ProductRegistrationOps::plan(changed_limit_options);
        expect(
            changed_limit.ready() &&
                changed_limit.plan_hash != first.plan_hash,
            "inventory limits must participate in the reviewed plan hash");
        auto changed_selector_options = options;
        changed_selector_options.request.aliases.push_back("Alternate Demo");
        const auto changed_selector =
            ProductRegistrationOps::plan(changed_selector_options);
        expect(
            changed_selector.ready() &&
                changed_selector.plan_hash != first.plan_hash,
            "normalized selector arrays must participate in the reviewed "
            "plan hash");
        expect(
            read_text(fixture.config) == fixture.config_before,
            "planning must leave shared registry bytes exact");

        auto hyphenated = options;
        hyphenated.request.product = "Horizon-QuestDemo";
        const auto hyphenated_plan = ProductRegistrationOps::plan(hyphenated);
        expect(
            hyphenated_plan.ready() &&
                hyphenated_plan.product == "Horizon-QuestDemo",
            "uppercase and hyphenated safe TOML/path keys must remain valid");
        write_text(
            fixture.config,
            fixture.config_before + "aliases = [\"observer-extra\"]\n");
        const auto changed_existing_selectors =
            ProductRegistrationOps::plan(options);
        expect(
            changed_existing_selectors.ready() &&
                changed_existing_selectors.registry_revision !=
                    first.registry_revision,
            "existing registry selector arrays must participate in the "
            "registry revision");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_derived_source_authority_filter() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto baseline =
            ProductRegistrationOps::plan(plan_options(fixture));
        expect(baseline.ready(), "authority-filter baseline must be ready");
        write_text(fixture.external_root / ".cache" / "noise.bin", "cache-a");
        write_text(fixture.external_root / "views" / "noise.md", "view-a");
        write_text(fixture.external_root / "_views" / "noise.md", "view-b");
        write_text(
            fixture.external_root / "items" / "task" / "0000" /
                "HQST-TSK-9999_noise.index.md",
            "derived-index-a");
        const auto source_with_derived = snapshot_tree(fixture.external_root);
        const auto filtered =
            ProductRegistrationOps::plan(plan_options(fixture));
        expect(
            filtered.ready() &&
                filtered.source_revision == baseline.source_revision &&
                filtered.plan_hash == baseline.plan_hash &&
                filtered.files.size() == baseline.files.size(),
            "planning must exclude .cache, views, _views, and *.index.md "
            "from source authority");

        const auto applied = ProductRegistrationOps::apply(
            apply_options(fixture, baseline.plan_hash));
        if (applied.status != "applied") {
            std::cerr << applied.to_json(true) << "\n";
        }
        expect(
            applied.status == "applied",
            "apply revalidation must use the same derived-source filter");
        expect(
            snapshot_tree(fixture.external_root) == source_with_derived,
            "registration must not mutate derived or canonical source bytes");
        write_text(fixture.external_root / ".cache" / "noise.bin", "cache-b");
        write_text(fixture.external_root / "views" / "noise.md", "view-c");
        ProductRegistrationOps::RecoveryOptions recovery{
            .backlog_root = fixture.shared,
            .plan_hash = baseline.plan_hash,
        };
        expect(
            ProductRegistrationOps::verify(recovery).status == "verified",
            "verification must ignore the same derived source set");
    });
}

void test_unrelated_registry_collisions_do_not_block_registration() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto first_root =
            fixture.shared / "products" / "unrelated-collision-one";
        const auto second_root =
            fixture.shared / "products" / "unrelated-collision-two";
        std::filesystem::create_directories(first_root / "items");
        std::filesystem::create_directories(second_root / "items");
        append_registry_entry(
            fixture,
            "unrelated-collision-one",
            "Unrelated Collision One",
            "DUP",
            first_root,
            {"unrelated-shared-selector"});
        append_registry_entry(
            fixture,
            "unrelated-collision-two",
            "Unrelated Collision Two",
            "DUP",
            second_root,
            {"unrelated-shared-selector"});

        const auto plan = ProductRegistrationOps::plan(plan_options(fixture));
        if (!plan.ready()) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            plan.ready(),
            "unrelated existing selector and prefix collisions must not "
            "block registration of a unique product");
    });
}

void test_blocked_registration_inputs() {
    using kano::backlog_core::ItemType;

    with_fixture([](Fixture& fixture) {
        const auto options = plan_options(fixture);
        std::filesystem::remove_all(fixture.external_root);
        expect_blocked_plan(
            fixture, options, "source_root_not_found",
            "missing external source must block registration");
    });

    with_fixture([](Fixture& fixture) {
        std::filesystem::remove_all(fixture.external_root / "items");
        expect_blocked_plan(
            fixture, plan_options(fixture), "source_items_missing",
            "missing canonical items authority must block registration");
    });

    with_fixture([](Fixture& fixture) {
        std::filesystem::remove_all(fixture.external_root / "items");
        std::filesystem::create_directories(fixture.external_root / "items");
        expect_blocked_plan(
            fixture, plan_options(fixture), "source_items_empty",
            "empty canonical items authority must block registration");
    });

    with_fixture([](Fixture& fixture) {
        std::filesystem::remove_all(fixture.shared / "products");
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "shared_products_root_not_found",
            "missing shared products authority must block registration");
    });

    with_fixture([](Fixture& fixture) {
        write_text(
            fixture.config,
            "[products.observer\nname = \"unterminated\n");
        expect_blocked_plan(
            fixture, plan_options(fixture), "shared_config_malformed",
            "malformed shared config must fail closed");
    });

    with_fixture([](Fixture& fixture) {
        append_registry_entry(
            fixture,
            "HorizonQuestDemo",
            "Horizon Quest Demo",
            "HQST",
            fixture.external_root);
        expect_blocked_plan(
            fixture, plan_options(fixture), "product_already_registered",
            "an exact existing product must block duplicate registration");
    });

    with_fixture([](Fixture& fixture) {
        append_registry_entry(
            fixture,
            "horizonquestdemo",
            "Case-fold collision",
            "CFR",
            fixture.root / "case-fold-root");
        expect_blocked_plan(
            fixture, plan_options(fixture), "product_case_fold_collision",
            "case-fold-equivalent product slugs must collide");
    });

    with_fixture([](Fixture& fixture) {
        append_registry_entry(
            fixture,
            "prefix-owner",
            "Prefix Owner",
            "HQST",
            fixture.root / "prefix-owner");
        expect_blocked_plan(
            fixture, plan_options(fixture), "prefix_collision",
            "registered prefixes must be unique case-insensitively");
    });

    with_fixture([](Fixture& fixture) {
        const auto nested_registered_root =
            fixture.external_root / "nested-product";
        std::filesystem::create_directories(nested_registered_root / "items");
        append_registry_entry(
            fixture,
            "nested-product",
            "Nested Product",
            "NST",
            nested_registered_root);
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "external_root_overlaps_registered_root",
            "external root must not contain or be contained by a registered root");
    });

    with_fixture([](Fixture& fixture) {
        std::filesystem::create_directories(fixture.destination);
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "canonical_destination_must_be_absent",
            "even an empty canonical destination must block external registration");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.request.prefix = "hr-r";
        expect_blocked_plan(
            fixture, options, "invalid_prefix",
            "prefix must be canonical uppercase ASCII alphanumeric text");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.request.aliases.push_back(" \t ");
        expect_blocked_plan(
            fixture, options, "invalid_alias_selector",
            "aliases that normalize to empty must block registration");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.request.repo_bindings.push_back(" \r\n ");
        expect_blocked_plan(
            fixture, options, "invalid_repo_binding_selector",
            "repository bindings that normalize to empty must block "
            "registration");
    });

    with_fixture([](Fixture& fixture) {
        const auto owner_root = fixture.root / "selector-owner";
        std::filesystem::create_directories(owner_root / "items");
        append_registry_entry(
            fixture,
            "selector-owner",
            "Selector Owner",
            "SEL",
            owner_root,
            {"shared-selector"});
        auto options = plan_options(fixture);
        options.request.aliases = {
            " Shared-Selector ", "shared-selector"};
        expect_blocked_plan(
            fixture, options, "product_selector_collision:shared-selector",
            "normalized duplicate aliases claimed by another product must "
            "block registration");
    });

    with_fixture([](Fixture& fixture) {
        const auto owner_root = fixture.root / "binding-owner";
        std::filesystem::create_directories(owner_root / "items");
        append_registry_entry(
            fixture,
            "binding-owner",
            "Binding Owner",
            "BND",
            owner_root,
            {"cross-kind-selector"});
        auto options = plan_options(fixture);
        options.request.aliases.clear();
        options.request.repo_bindings = {" CROSS-KIND-SELECTOR "};
        expect_blocked_plan(
            fixture, options,
            "product_selector_collision:cross-kind-selector",
            "cross-kind selector claims owned by different products must "
            "block registration");
    });

    with_fixture([](Fixture& fixture) {
        write_text(fixture.task_path, "not canonical frontmatter\n");
        expect_blocked_plan(
            fixture, plan_options(fixture), "malformed_source_item",
            "malformed canonical item bytes must block registration");
    });

    with_fixture([](Fixture& fixture) {
        (void)create_item(
            fixture.external_root,
            "BAD",
            ItemType::Task,
            2,
            "Wrong prefix fixture",
            "019d0000-0003-7000-8000-000000000003");
        expect_blocked_plan(
            fixture, plan_options(fixture), "source_item_prefix_mismatch",
            "every source display ID must use the requested prefix");
    });

    with_fixture([](Fixture& fixture) {
        write_text(
            fixture.task_path.parent_path() /
                "HQST-TSK-0001_duplicate-fixture.md",
            fixture.task_bytes);
        expect_blocked_plan(
            fixture, plan_options(fixture), "duplicate_source_display_id",
            "duplicate source display IDs must fail closed");
    });

    with_fixture([](Fixture& fixture) {
        (void)create_item(
            fixture.external_root,
            "HQST",
            ItemType::Task,
            2,
            "Duplicate UID fixture",
            fixture.uids[1]);
        expect_blocked_plan(
            fixture, plan_options(fixture), "duplicate_source_uid",
            "duplicate source UUIDv7 identities must fail closed");
    });

    with_fixture([](Fixture& fixture) {
        write_text(
            fixture.local_config,
            "[product]\n"
            "name = \"other-product\"\n"
            "prefix = \"BAD\"\n");
        expect_blocked_plan(
            fixture, plan_options(fixture), "source_local_config_mismatch",
            "source-local product identity must match the request");
    });
}

void test_inventory_limits() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto source_before = snapshot_tree(fixture.external_root);

        auto file_options = plan_options(fixture);
        file_options.limits.max_files = 1;
        const auto file_limited = ProductRegistrationOps::plan(file_options);
        expect(
            !file_limited.ready() &&
                contains_prefix(
                    file_limited.blockers,
                    "source_file_limit_exceeded"),
            "source inventory must enforce its file bound");

        auto byte_options = plan_options(fixture);
        byte_options.limits.max_bytes = 1;
        const auto byte_limited = ProductRegistrationOps::plan(byte_options);
        expect(
            !byte_limited.ready() &&
                contains_prefix(
                    byte_limited.blockers,
                    "source_byte_limit_exceeded"),
            "source inventory must enforce its aggregate byte bound");

        auto item_options = plan_options(fixture);
        item_options.limits.max_items = 1;
        const auto item_limited = ProductRegistrationOps::plan(item_options);
        expect(
            !item_limited.ready() &&
                contains_prefix(
                    item_limited.blockers,
                    "source_item_limit_exceeded"),
            "source inventory must enforce its canonical item bound");

        expect(
            snapshot_tree(fixture.external_root) == source_before &&
                read_text(fixture.config) == fixture.config_before &&
                !std::filesystem::exists(fixture.destination),
            "all bounded inventory failures must remain read-only");
    });
}

void test_malformed_source_attempt_consumes_global_item_budget_before_registry_metadata_read() {
    using kano::backlog_ops::ProductRegistrationOps;

    with_fixture([](Fixture& fixture) {
        constexpr std::size_t maximum_frontmatter_bytes =
            1u * 1024u * 1024u;
        write_text(
            fixture.task_path,
            "---\n"
            "id: HQST-TSK-0001\n"
            "uid: 019d0000-0002-7000-8000-000000000002\n");

        std::string hostile_registry_item =
            "---\n"
            "id: OBS-TSK-0001\n";
        hostile_registry_item.append(
            maximum_frontmatter_bytes + 4096u, 'r');
        write_text(
            fixture.shared / "products" / "observer" / "items" /
                "OBS-TSK-0001_hostile.md",
            hostile_registry_item);

        auto options = plan_options(fixture);
        options.limits.max_items = 2;
        const auto shared_before = snapshot_tree(fixture.shared);
        const auto source_before = snapshot_tree(fixture.external_root);
        const auto config_before = read_text(fixture.config);
        const auto plan = ProductRegistrationOps::plan(options);

        const bool blockers_match =
            !plan.ready() &&
            contains_prefix(plan.blockers, "malformed_source_item") &&
            contains_prefix(
                plan.blockers, "registry_item_scan_limit_exceeded") &&
            !contains_prefix(
                plan.blockers,
                "registry_frontmatter_byte_limit_exceeded");
        const bool remains_read_only =
            snapshot_tree(fixture.shared) == shared_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            read_text(fixture.config) == config_before &&
            !std::filesystem::exists(fixture.destination);
        if (!blockers_match || !remains_read_only) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            blockers_match,
            "malformed source canonical metadata attempts must consume the "
            "global source-plus-registry max_items budget before any "
            "registry metadata read");
        expect(
            remains_read_only,
            "the global item-budget regression plan must remain strictly "
            "read-only");
    });
}

void test_pre_materialization_inventory_limits() {
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::ProductRegistrationOps;

    test_malformed_source_attempt_consumes_global_item_budget_before_registry_metadata_read();

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.limits.max_files = 4;
        for (std::size_t index = 0; index < 81u; ++index) {
            write_text(
                fixture.external_root /
                    ("excluded-" + std::to_string(index) + ".index.md"),
                "derived index bytes\n");
        }
        expect_blocked_plan(
            fixture,
            options,
            "source_path_entry_limit_exceeded",
            "excluded source files must consume the pre-materialization "
            "path-entry budget");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.limits.max_files = 4;
        const auto baseline = ProductRegistrationOps::plan(options);
        expect(
            baseline.ready(),
            "dense derived-tree baseline must fit the unchanged file limit");

        auto level = fixture.external_root / ".cache";
        for (std::size_t depth = 0; depth < 16u; ++depth) {
            level /= "d" + std::to_string(depth);
            for (std::size_t file = 0; file < 8u; ++file) {
                write_text(
                    level / ("noise-" + std::to_string(file) + ".bin"),
                    "derived cache bytes\n");
            }
        }
        const auto source_with_dense_cache =
            snapshot_tree(fixture.external_root);
        const auto filtered = ProductRegistrationOps::plan(options);
        expect(
            filtered.ready() &&
                filtered.source_revision == baseline.source_revision &&
                filtered.plan_hash == baseline.plan_hash,
            "a dense multi-level .cache tree must be pruned before descent "
            "under the same bounded limit");
        expect(
            read_text(fixture.config) == fixture.config_before &&
                snapshot_tree(fixture.external_root) ==
                    source_with_dense_cache &&
                !std::filesystem::exists(fixture.destination),
            "derived-tree pruning must leave exact config/source bytes and "
            "the canonical destination absent");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.limits.max_items = 3;
        const auto observer_items =
            fixture.shared / "products" / "observer" / "items";
        for (std::size_t index = 0; index < 77u; ++index) {
            write_text(
                observer_items /
                    ("empty-entry-" + std::to_string(index) + ".tmp"),
                "");
        }
        expect_blocked_plan(
            fixture,
            options,
            "registry_path_entry_limit_exceeded",
            "non-item registry entries must consume one global registered-"
            "root traversal budget");
    });

    with_fixture([](Fixture& fixture) {
        const auto observer = fixture.shared / "products" / "observer";
        (void)create_item(
            observer,
            "OBS",
            ItemType::Task,
            1,
            "Observer item scan fixture",
            "019d0000-0030-7000-8000-000000000030");
        auto options = plan_options(fixture);
        options.limits.max_items = 2;
        expect_blocked_plan(
            fixture,
            options,
            "registry_item_scan_limit_exceeded",
            "registry item slots must be checked before path vector growth "
            "or metadata reads");
    });

    with_fixture([](Fixture& fixture) {
        constexpr std::size_t maximum_frontmatter_bytes =
            1u * 1024u * 1024u;
        std::string hostile =
            "---\n"
            "id: HQST-TSK-0001\n";
        hostile.append(maximum_frontmatter_bytes + 4096u, 's');
        write_text(fixture.task_path, hostile);
        expect_blocked_plan(
            fixture,
            plan_options(fixture),
            "source_frontmatter_byte_limit_exceeded",
            "unclosed source frontmatter beyond one MiB must fail closed");
    });

    with_fixture([](Fixture& fixture) {
        constexpr std::size_t maximum_frontmatter_bytes =
            1u * 1024u * 1024u;
        std::string hostile =
            "---\n"
            "id: OBS-TSK-0001\n";
        hostile.append(maximum_frontmatter_bytes + 4096u, 'r');
        write_text(
            fixture.shared / "products" / "observer" / "items" /
                "OBS-TSK-0001_hostile.md",
            hostile);
        expect_blocked_plan(
            fixture,
            plan_options(fixture),
            "registry_frontmatter_byte_limit_exceeded",
            "unclosed registered-item frontmatter beyond one MiB must fail "
            "closed");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        const auto preliminary = ProductRegistrationOps::plan(options);
        expect(
            preliminary.ready(),
            "aggregate metadata-budget fixture must begin with a ready plan");
        std::uintmax_t authoritative_source_bytes = 0;
        for (const auto& file : preliminary.files) {
            authoritative_source_bytes += file.size;
        }

        const auto observer = fixture.shared / "products" / "observer";
        const auto observer_item = create_item(
            observer,
            "OBS",
            ItemType::Task,
            1,
            "Observer aggregate metadata fixture",
            "019d0000-0031-7000-8000-000000000031");
        auto observer_bytes = read_text(*observer_item.file_path);
        const auto closing = observer_bytes.find("\n---", 4u);
        expect(
            closing != std::string::npos,
            "observer aggregate fixture must have canonical frontmatter");
        observer_bytes.insert(
            closing + 1u,
            "# " +
                std::string(
                    static_cast<std::size_t>(authoritative_source_bytes) +
                        4096u,
                    'p') +
                "\n");
        write_text(*observer_item.file_path, observer_bytes);

        options.limits.max_bytes = authoritative_source_bytes;
        expect_blocked_plan(
            fixture,
            options,
            "registry_frontmatter_byte_limit_exceeded",
            "registered metadata must share the aggregate source metadata "
            "byte budget");
    });
}

void test_apply_gates() {
    using kano::backlog_ops::ProductRegistrationOps;

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        expect(plan.ready(), "apply gate fixture should plan successfully");
        auto missing_actor = apply_options(fixture, plan.plan_hash);
        missing_actor.agent.reset();
        expect_blocked_apply_without_writes(
            fixture, missing_actor, "agent_required",
            "apply must require an explicit actor");

        for (const std::string& actor : {
                 "unknown", "auto", "AUTO", "user", "UsEr",
                 "assistant", "AsSiStAnT",
             }) {
            auto placeholder = apply_options(fixture, plan.plan_hash);
            placeholder.agent = actor;
            expect_blocked_apply_without_writes(
                fixture, placeholder, "invalid_agent",
                "apply must reject placeholder actors case-insensitively");
        }

        auto unconfirmed = apply_options(fixture, plan.plan_hash);
        unconfirmed.confirm = false;
        expect_blocked_apply_without_writes(
            fixture, unconfirmed, "confirmation_required",
            "apply must require explicit confirmation");

        auto wrong_hash = apply_options(fixture, std::string(64, '0'));
        expect_blocked_apply_without_writes(
            fixture, wrong_hash, "expected_plan_hash_mismatch",
            "apply must reject a wrong reviewed hash before writes");

        auto stale_plan = apply_options(fixture, plan.plan_hash);
        ++stale_plan.plan.limits.max_files;
        expect_blocked_apply_without_writes(
            fixture, stale_plan, "stale_or_mismatched_plan_hash",
            "apply must bind the exact reviewed plan options");
    });

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        write_text(
            fixture.config,
            fixture.config_before + "\n# concurrent config drift\n");
        expect_blocked_apply_without_writes(
            fixture, apply_options(fixture, plan.plan_hash),
            "stale_config_revision",
            "apply must reject unrelated shared config drift");
    });

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        write_text(
            fixture.task_path,
            fixture.task_bytes + "\n<!-- concurrent source drift -->\n");
        expect_blocked_apply_without_writes(
            fixture, apply_options(fixture, plan.plan_hash),
            "stale_source_revision",
            "apply must reject source bytes changed after review");
    });

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        append_registry_entry(
            fixture,
            "HorizonQuestDemo",
            "Concurrent Identity",
            "HQST",
            fixture.root / "concurrent-root");
        expect_blocked_apply_without_writes(
            fixture, apply_options(fixture, plan.plan_hash),
            "stale_registry_identity",
            "apply must distinguish a concurrently claimed registry identity");
    });
}

void test_success_replay_verify_status_and_relocation_handoff() {
    using kano::backlog_core::ProjectConfig;
    using kano::backlog_ops::ProductRegistrationOps;
    using kano::backlog_ops::ProductRelocationOps;

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        expect(plan.ready(), "success fixture should produce a ready plan");
        const auto apply = apply_options(fixture, plan.plan_hash);
        const auto applied = ProductRegistrationOps::apply(apply);
        if (applied.status != "applied") {
            std::cerr << applied.to_json(true) << "\n";
        }
        expect(
            applied.schema == "kob.product_registration.result.v1" &&
                applied.status == "applied" &&
                applied.plan_hash == plan.plan_hash &&
                applied.apply_agent ==
                    std::optional<std::string>("sisyphus"),
            "confirmed exact-hash apply should publish attributed evidence");
        expect(
            applied.changed_refs ==
                std::vector<std::string>{
                    "project-config:.kano/backlog_config.toml"},
            "registration must publish only the shared config authority");
        expect(
            !applied.receipt_ref.empty() &&
                is_public_ref(applied.receipt_ref) &&
                contains_prefix(
                    applied.operation_receipts,
                    "config_registration_published"),
            "apply must return a bounded durable receipt reference");
        expect(
            applied.to_json().find(fixture.root.generic_string()) ==
                std::string::npos,
            "apply JSON must not expose the external absolute path");

        const auto config_after = read_text(fixture.config);
        expect(
            config_after == expected_config_after(fixture) &&
                count_occurrences(
                    config_after,
                    "[products.HorizonQuestDemo]") == 1 &&
                count_occurrences(
                    config_after,
                    "[products.horizonquestdemo]") == 0,
            "apply must preserve existing config bytes and append one entry");
        const auto config = ProjectConfig::load_from_toml(fixture.config);
        expect(config.has_value(), "registered shared config should parse");
        const auto definition = config->get_product("HorizonQuestDemo");
        expect(
            definition.has_value() &&
                definition->name == "Horizon Quest Demo" &&
                definition->prefix == "HQST" &&
                definition->aliases == std::vector<std::string>{
                    "horizon-demo", "quest demo"} &&
                definition->repo_bindings == std::vector<std::string>{
                    "repo/horizonquestdemo"} &&
                definition->backlog_root ==
                    std::filesystem::canonical(fixture.external_root)
                        .generic_string(),
            "registry entry must preserve the exact requested identity");
        for (const auto& selector : std::vector<std::string>{
                 "HorizonQuestDemo",
                 "HQST",
                 "Horizon Quest Demo",
                 "horizon-demo",
                 "repo/horizonquestdemo",
             }) {
            expect(
                config->resolve_product_name(selector) ==
                    std::optional<std::string>("HorizonQuestDemo"),
                "every persisted canonical, prefix, display, alias, and "
                "repository selector must resolve to the canonical product");
        }
        const auto resolved = config->resolve_backlog_root(
            "HorizonQuestDemo", fixture.config);
        expect(
            resolved.has_value() &&
                std::filesystem::weakly_canonical(*resolved) ==
                    std::filesystem::weakly_canonical(
                        fixture.external_root),
            "registered product must resolve to the exact external root");
        const auto transaction = registration_transaction(
            fixture, plan.plan_hash);
        const auto receipt_bytes = read_text(transaction / "receipt.json");
        const auto journal_bytes = read_text(transaction / "journal.json");
        expect(
            receipt_bytes.find("horizon-demo") != std::string::npos &&
                receipt_bytes.find("repo/horizonquestdemo") !=
                    std::string::npos &&
                journal_bytes.find("horizon-demo") != std::string::npos &&
                journal_bytes.find("repo/horizonquestdemo") !=
                    std::string::npos,
            "persisted receipt and journal evidence must bind normalized "
            "selector arrays");
        expect_source_and_destination_invariant(fixture);

        ProductRegistrationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto verification = ProductRegistrationOps::verify(recovery);
        if (verification.status != "verified") {
            std::cerr << verification.to_json(true) << "\n";
        }
        expect(
            verification.status == "verified" &&
                verification.failures.empty() &&
                contains_prefix(
                    verification.postconditions,
                    "config_registration_exact") &&
                contains_prefix(
                    verification.postconditions,
                    "external_root_resolves_exactly") &&
                contains_prefix(
                    verification.postconditions,
                    "source_bytes_unchanged") &&
                contains_prefix(
                    verification.postconditions,
                    "canonical_destination_absent"),
            "verification must prove every config-only postcondition");
        const auto status = ProductRegistrationOps::status(recovery);
        expect(
            status.status == "applied" &&
                status.stage == "completed" &&
                status.recovery_status == "not_required" &&
                status.apply_agent ==
                    std::optional<std::string>("sisyphus"),
            "status must expose the completed attributed transaction");

        const auto config_before_replay = read_text(fixture.config);
        const auto replay = ProductRegistrationOps::apply(apply);
        expect(
            replay.status == "applied" && replay.idempotent_replay &&
                replay.receipt_ref == applied.receipt_ref &&
                replay.apply_agent == applied.apply_agent &&
                read_text(fixture.config) == config_before_replay,
            "an exact replay must verify without rewriting config or evidence");
        expect_source_and_destination_invariant(fixture);

        ProductRelocationOps::PlanOptions relocation_options;
        relocation_options.start_path = fixture.shared;
        relocation_options.backlog_root = fixture.shared;
        relocation_options.request.product = "HorizonQuestDemo";
        relocation_options.request.destination_root = fixture.destination;
        relocation_options.request.max_files = 64;
        relocation_options.request.max_bytes = 2u * 1024u * 1024u;
        relocation_options.request.max_items = 16;
        const auto relocation =
            ProductRelocationOps::plan(relocation_options);
        if (!relocation.ready()) {
            std::cerr << relocation.to_json(true) << "\n";
        }
        expect(
            relocation.ready() &&
                relocation.source_root_ref ==
                    "product:HorizonQuestDemo:configured-root" &&
                relocation.destination_root_ref ==
                    "product:HorizonQuestDemo:shared-root",
            "successful registration must hand off directly to relocation "
            "without fixture-side target TOML setup");
        ProductRelocationOps::ApplyOptions relocation_apply{
            .plan = relocation_options,
            .expected_plan_hash = relocation.plan_hash,
            .confirm = true,
        };
        const auto relocated = ProductRelocationOps::apply(relocation_apply);
        if (relocated.status != "applied") {
            std::cerr << relocated.to_json(true) << "\n";
        }
        expect(
            relocated.status == "applied" &&
                !std::filesystem::exists(fixture.external_root) &&
                std::filesystem::is_directory(fixture.destination),
            "relocation handoff must retire the old external root");

        const auto registration_journal =
            fixture.shared / ".kano" / "cache" /
            "product-registrations" / plan.plan_hash / "journal.json";
        const auto journal_before_terminal_replay =
            read_text(registration_journal);
        const auto config_before_terminal_replay = read_text(fixture.config);
        const auto terminal_replay = ProductRegistrationOps::apply(apply);
        expect(
            terminal_replay.status == "applied" &&
                terminal_replay.idempotent_replay &&
                terminal_replay.apply_agent ==
                    std::optional<std::string>("sisyphus") &&
                !terminal_replay.recovery_agent &&
                read_text(fixture.config) == config_before_terminal_replay &&
                read_text(registration_journal) ==
                    journal_before_terminal_replay,
            "terminal registration replay after relocation must not require "
            "the old source or mutate config/recovery attribution");
    });
}

void test_automatic_rollback_after_post_publish_failure() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        auto failing = apply_options(fixture, plan.plan_hash);
        failing.agent = "actor-a";
        failing.inject_failure_after = "after_config_publish";
        const auto result = ProductRegistrationOps::apply(failing);
        if (result.status != "rolled_back") {
            std::cerr << result.to_json(true) << "\n";
        }
        expect(
            result.status == "rolled_back" &&
                result.recovery_status == "completed" &&
                contains_prefix(
                    result.operation_receipts,
                    "automatic_rollback_completed") &&
                result.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                result.recovery_agent ==
                    std::optional<std::string>("actor-a"),
            "post-publish failure must automatically restore the before state");
        expect(
            read_text(fixture.config) == fixture.config_before,
            "automatic rollback must restore exact shared config bytes");
        expect_source_and_destination_invariant(fixture);

        ProductRegistrationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto rolled_back_status =
            ProductRegistrationOps::status(recovery);
        expect(
            rolled_back_status.status == "rolled_back" &&
                rolled_back_status.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                rolled_back_status.recovery_agent ==
                    std::optional<std::string>("actor-a") &&
                ProductRegistrationOps::verify(recovery).status ==
                    "not_applied",
            "automatic rollback evidence must be observable without a "
            "manual rollback API");

        auto retry = apply_options(fixture, plan.plan_hash);
        retry.agent = "actor-b";
        const auto retried = ProductRegistrationOps::apply(retry);
        if (retried.status != "applied") {
            std::cerr << retried.to_json(true) << "\n";
        }
        expect(
            retried.status == "applied" &&
                retried.recovery_status == "completed" &&
                retried.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                retried.recovery_agent ==
                    std::optional<std::string>("actor-b") &&
                contains_prefix(
                    retried.operation_receipts,
                    "rolled_back_transaction_retried"),
            "the exact apply must retry a rolled-back transaction while "
            "preserving original and recovery actors");
        const auto retried_status = ProductRegistrationOps::status(recovery);
        const auto retried_verification =
            ProductRegistrationOps::verify(recovery);
        expect(
            retried_status.status == "applied" &&
                retried_status.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                retried_status.recovery_agent ==
                    std::optional<std::string>("actor-b") &&
                retried_verification.status == "verified" &&
                retried_verification.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                retried_verification.recovery_agent ==
                    std::optional<std::string>("actor-b"),
            "status and verification must display separate durable apply "
            "and recovery attribution");
        const auto journal = read_text(
            fixture.shared / ".kano" / "cache" /
            "product-registrations" / plan.plan_hash / "journal.json");
        expect(
            journal.find("\"attempts\"") != std::string::npos &&
                journal.find("actor-a") != std::string::npos &&
                journal.find("actor-b") != std::string::npos,
            "retry must preserve durable attempt history for both actors");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_invalid_atomic_temp_debris_never_blocks_exact_retry() {
    using kano::backlog_ops::ProductRegistrationOps;

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        const auto debris = plant_atomic_debris(
            fixture.shared / ".kano", plan.plan_hash, "publish", 130u);
        const auto result = ProductRegistrationOps::apply(
            apply_options(fixture, plan.plan_hash));
        expect(
            result.status == "applied" &&
                read_text(fixture.config) == expected_config_after(fixture),
            "truncated publish temps must not block an exact apply");
        expect_debris_preserved(
            debris,
            "unknown publish temp bytes must remain untouched even beyond "
            "the bounded cleanup scan");
        expect_source_and_destination_invariant(fixture);
    });

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        const auto debris = plant_atomic_debris(
            fixture.shared / ".kano", plan.plan_hash, "restore", 1u);
        auto failing = apply_options(fixture, plan.plan_hash);
        failing.agent = "actor-a";
        failing.inject_failure_after = "after_config_publish";
        const auto rolled_back = ProductRegistrationOps::apply(failing);
        expect(
            rolled_back.status == "rolled_back" &&
                read_text(fixture.config) == fixture.config_before,
            "truncated restore temp must not block automatic rollback");
        expect_debris_preserved(
            debris, "unknown restore temp bytes must remain untouched");

        auto retry = apply_options(fixture, plan.plan_hash);
        retry.agent = "actor-b";
        const auto applied = ProductRegistrationOps::apply(retry);
        expect(
            applied.status == "applied" &&
                read_text(fixture.config) == expected_config_after(fixture),
            "exact retry after debris-tolerant rollback must apply");
        expect_debris_preserved(
            debris, "unknown restore temp must survive exact retry");
        expect_source_and_destination_invariant(fixture);
    });

    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        auto interrupted = apply_options(fixture, plan.plan_hash);
        interrupted.agent = "actor-a";
        interrupted.inject_interruption_after =
            "after_transaction_publish";
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "journal debris fixture must publish immutable evidence first");
        const auto debris = plant_atomic_debris(
            registration_root(fixture), plan.plan_hash, "journal", 1u);

        auto retry = apply_options(fixture, plan.plan_hash);
        retry.agent = "actor-b";
        const auto applied = ProductRegistrationOps::apply(retry);
        ProductRegistrationOps::RecoveryOptions recovery{
            .backlog_root = fixture.shared,
            .plan_hash = plan.plan_hash,
        };
        expect(
            applied.status == "applied" &&
                applied.apply_agent ==
                    std::optional<std::string>("actor-a") &&
                applied.recovery_agent ==
                    std::optional<std::string>("actor-b") &&
                read_text(fixture.config) == expected_config_after(fixture) &&
                ProductRegistrationOps::verify(recovery).status == "verified",
            "truncated journal temp must not block exact recovery");
        expect_debris_preserved(
            debris, "unknown journal temp bytes must remain untouched");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_partial_stage_debris_is_preserved_and_nonblocking() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        const auto debris = plant_partial_stage_debris(
            fixture, plan.plan_hash, 130u);
        const auto result = ProductRegistrationOps::apply(
            apply_options(fixture, plan.plan_hash));
        ProductRegistrationOps::RecoveryOptions recovery{
            .backlog_root = fixture.shared,
            .plan_hash = plan.plan_hash,
        };
        expect(
            result.status == "applied" &&
                read_text(fixture.config) == expected_config_after(fixture) &&
                ProductRegistrationOps::verify(recovery).status == "verified",
            "partial same-plan stages must not block a new unique stage");
        expect_debris_preserved(
            debris,
            "unvalidated partial stage owner/evidence bytes must be preserved "
            "even beyond the bounded cleanup scan");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_partial_lock_candidates_are_preserved_and_nonblocking() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        const auto debris = plant_partial_lock_candidate_debris(
            fixture, 130u);
        const auto result = ProductRegistrationOps::apply(
            apply_options(fixture, plan.plan_hash));
        ProductRegistrationOps::RecoveryOptions recovery{
            .backlog_root = fixture.shared,
            .plan_hash = plan.plan_hash,
        };
        expect(
            result.status == "applied" &&
                read_text(fixture.config) == expected_config_after(fixture) &&
                ProductRegistrationOps::verify(recovery).status == "verified",
            "partial candidate owners must not block final lock acquisition");
        expect_debris_preserved(
            debris,
            "unknown candidate owner bytes must remain untouched even beyond "
            "the bounded cleanup scan");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_stale_lock_guard_serializes_reclaim_publishers() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto first_options = plan_options(fixture);
        const auto second_options = make_rival_plan_options(fixture);
        const auto first_source = snapshot_tree(fixture.external_root);
        const auto second_source =
            snapshot_tree(second_options.request.external_root);
        const auto first_plan = ProductRegistrationOps::plan(first_options);
        const auto second_plan = ProductRegistrationOps::plan(second_options);
        expect(
            first_plan.ready() && second_plan.ready() &&
                first_plan.plan_hash != second_plan.plan_hash,
            "stale-lock contenders must begin with two ready peer plans");
        plant_valid_stale_final_lock(fixture, first_plan.plan_hash);

        ProductRegistrationOps::ApplyOptions first_apply{
            .plan = first_options,
            .expected_plan_hash = first_plan.plan_hash,
            .agent = "stale-race-a",
            .confirm = true,
        };
        ProductRegistrationOps::ApplyOptions second_apply{
            .plan = second_options,
            .expected_plan_hash = second_plan.plan_hash,
            .agent = "stale-race-b",
            .confirm = true,
        };
        std::barrier stale_owner_validated(2);
        std::barrier resume_reclaimer(2);
        int checkpoint_count = 0;
        first_apply.lock_test_checkpoint = [&](std::string_view checkpoint) {
            if (checkpoint == "after_stale_owner_validation") {
                ++checkpoint_count;
                stale_owner_validated.arrive_and_wait();
                resume_reclaimer.arrive_and_wait();
            }
        };

        kano::backlog_ops::ProductRegistrationResult first_result;
        std::thread first_thread([&] {
            first_result = ProductRegistrationOps::apply(first_apply);
        });
        stale_owner_validated.arrive_and_wait();
        const bool stale_final_still_present =
            std::filesystem::is_directory(config_lock_path(fixture));
        const bool guard_is_regular_while_locked =
            is_regular_non_reparse_file(config_guard_path(fixture));
        const auto second_result = ProductRegistrationOps::apply(second_apply);
        const bool second_wrote_nothing =
            read_text(fixture.config) == fixture.config_before &&
            !std::filesystem::exists(
                registration_transaction(fixture, first_plan.plan_hash)) &&
            !std::filesystem::exists(
                registration_transaction(fixture, second_plan.plan_hash));
        resume_reclaimer.arrive_and_wait();
        first_thread.join();

        const auto first_round_applied =
            static_cast<int>(first_result.status == "applied") +
            static_cast<int>(second_result.status == "applied");
        expect(
            checkpoint_count == 1 && stale_final_still_present &&
                guard_is_regular_while_locked && first_round_applied == 1 &&
                first_result.status == "applied" &&
                second_result.status != "applied" &&
                contains_prefix(
                    second_result.operation_receipts,
                    "product_registration_lock_active") &&
                second_wrote_nothing,
            "the persistent OS guard must block contender B while A pauses "
            "between stale validation and quarantine rename");
        expect(
            !std::filesystem::exists(config_lock_path(fixture)) &&
                is_regular_non_reparse_file(config_guard_path(fixture)) &&
                !std::filesystem::exists(
                    registration_transaction(
                        fixture, second_plan.plan_hash)) &&
                read_text(fixture.config) == expected_config_after(fixture),
            "only A may publish in the barrier-controlled first round");

        const auto fresh_second = ProductRegistrationOps::plan(second_options);
        expect(
            fresh_second.ready(),
            "contender B must replan against A's committed config");
        ProductRegistrationOps::ApplyOptions final_apply{
            .plan = second_options,
            .expected_plan_hash = fresh_second.plan_hash,
            .agent = "stale-race-b",
            .confirm = true,
        };
        const auto final_result = ProductRegistrationOps::apply(final_apply);
        const auto config = read_text(fixture.config);
        expect(
            final_result.status == "applied" &&
                count_occurrences(
                    config, "[products.HorizonQuestDemo]") == 1 &&
                count_occurrences(config, "[products.RivalDemo]") == 1 &&
                !std::filesystem::exists(config_lock_path(fixture)) &&
                is_regular_non_reparse_file(config_guard_path(fixture)),
            "replanned contender B must append without losing A's update");
        expect(
            snapshot_tree(fixture.external_root) == first_source &&
                snapshot_tree(second_options.request.external_root) ==
                    second_source &&
                !std::filesystem::exists(fixture.destination) &&
                !std::filesystem::exists(
                    fixture.shared / "products" / "RivalDemo"),
            "serialized stale-lock recovery must remain config-only");
    });
}

void test_interrupted_after_publish_exact_apply_reconciliation() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        auto interrupted = apply_options(fixture, plan.plan_hash);
        interrupted.inject_interruption_after = "after_config_publish";
        const auto first = ProductRegistrationOps::apply(interrupted);
        if (first.status != "recovery_required") {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(
            first.status == "recovery_required" &&
                first.recovery_status == "exact_apply_required",
            "post-publish interruption must require exact-apply reconciliation");
        expect_source_and_destination_invariant(fixture);

        ProductRegistrationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto interrupted_status =
            ProductRegistrationOps::status(recovery);
        if (interrupted_status.status != "recovery_required" ||
            interrupted_status.stage != "after_config_publish") {
            std::cerr << interrupted_status.to_json(true) << "\n";
        }
        expect(
            interrupted_status.status == "recovery_required" &&
                interrupted_status.stage == "after_config_publish" &&
                interrupted_status.recovery_status ==
                    "exact_apply_required",
            "status must identify the exact interrupted publication stage");

        const auto exact_apply = apply_options(fixture, plan.plan_hash);
        const auto reconciled = ProductRegistrationOps::apply(exact_apply);
        if (reconciled.status != "applied") {
            std::cerr << reconciled.to_json(true) << "\n";
        }
        expect(
            reconciled.status == "applied" &&
                reconciled.recovery_status == "completed" &&
                contains_prefix(
                    reconciled.operation_receipts,
                    "interrupted_publish_reconciled"),
            "repeated exact apply must finish a known after-state safely");
        const auto config_after_reconcile = read_text(fixture.config);
        const auto replay = ProductRegistrationOps::apply(exact_apply);
        expect(
            replay.status == "applied" && replay.idempotent_replay &&
                read_text(fixture.config) == config_after_reconcile &&
                ProductRegistrationOps::verify(recovery).status ==
                    "verified",
            "reconciled transaction must become an idempotent verified replay");
        expect_source_and_destination_invariant(fixture);
    });
}

void test_interrupted_third_state_config_fails_closed() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        auto interrupted = apply_options(fixture, plan.plan_hash);
        interrupted.inject_interruption_after = "after_config_publish";
        const auto first = ProductRegistrationOps::apply(interrupted);
        expect(
            first.status == "recovery_required",
            "third-state fixture must first interrupt after publication");

        const auto third_state =
            read_text(fixture.config) + "\n# unrelated third-state drift\n";
        write_text(fixture.config, third_state);
        const auto result = ProductRegistrationOps::apply(
            apply_options(fixture, plan.plan_hash));
        expect(
            result.status == "failed" &&
                result.recovery_status == "config_third_state" &&
                contains_prefix(
                    result.operation_receipts,
                    "config_third_state"),
            "reconciliation must fail closed without overwriting config "
            "that is neither the reviewed before nor intended after state");
        expect(
            read_text(fixture.config) == third_state,
            "third-state reconciliation must preserve concurrent bytes");
        expect_source_and_destination_invariant(fixture);

        ProductRegistrationOps::RecoveryOptions recovery;
        recovery.backlog_root = fixture.shared;
        recovery.plan_hash = plan.plan_hash;
        const auto status = ProductRegistrationOps::status(recovery);
        expect(
            status.status == "failed" &&
                status.recovery_status == "config_third_state",
            "status must expose the fail-closed third state precisely");
    });
}

void test_all_interruption_windows_reconcile_without_cleanup() {
    using kano::backlog_ops::ProductRegistrationOps;
    constexpr std::array<std::string_view, 4> phases = {
        "after_staged_evidence",
        "after_transaction_publish",
        "after_config_publish",
        "after_postcondition_check",
    };
    for (const auto phase : phases) {
        with_fixture([&](Fixture& fixture) {
            const auto plan =
                ProductRegistrationOps::plan(plan_options(fixture));
            auto interrupted = apply_options(fixture, plan.plan_hash);
            interrupted.agent = "actor-a";
            interrupted.inject_interruption_after = std::string(phase);
            const auto first = ProductRegistrationOps::apply(interrupted);
            if (first.status != "recovery_required") {
                std::cerr << first.to_json(true) << "\n";
            }
            expect(
                first.status == "recovery_required" &&
                    first.recovery_status == "exact_apply_required",
                "every evidence/config interruption must expose exact-apply "
                "recovery");

            auto retry = apply_options(fixture, plan.plan_hash);
            retry.agent = "actor-b";
            const auto recovered = ProductRegistrationOps::apply(retry);
            if (recovered.status != "applied") {
                std::cerr << "phase=" << phase << "\n"
                          << recovered.to_json(true) << "\n";
            }
            expect(
                recovered.status == "applied" &&
                    !has_registration_staging(fixture),
                "exact retry must reclaim same-plan staging and complete "
                "without manual deletion");
            if (phase == "after_staged_evidence") {
                expect(
                    recovered.apply_agent ==
                            std::optional<std::string>("actor-b") &&
                        !recovered.recovery_agent,
                    "unpublished staged evidence must not claim a durable "
                    "original actor");
            } else {
                expect(
                    recovered.apply_agent ==
                            std::optional<std::string>("actor-a") &&
                        recovered.recovery_agent ==
                            std::optional<std::string>("actor-b"),
                    "published interruption recovery must preserve both "
                    "actors");
            }
            ProductRegistrationOps::RecoveryOptions recovery{
                .backlog_root = fixture.shared,
                .plan_hash = plan.plan_hash,
            };
            expect(
                ProductRegistrationOps::verify(recovery).status ==
                    "verified",
                "every recovered interruption must verify");
            expect_source_and_destination_invariant(fixture);
        });
    }
}

void test_all_failure_windows_retry_without_cleanup() {
    using kano::backlog_ops::ProductRegistrationOps;
    constexpr std::array<std::string_view, 4> phases = {
        "after_staged_evidence",
        "after_transaction_publish",
        "after_config_publish",
        "after_postcondition_check",
    };
    for (const auto phase : phases) {
        with_fixture([&](Fixture& fixture) {
            const auto plan =
                ProductRegistrationOps::plan(plan_options(fixture));
            auto failing = apply_options(fixture, plan.plan_hash);
            failing.agent = "actor-a";
            failing.inject_failure_after = std::string(phase);
            const auto first = ProductRegistrationOps::apply(failing);
            const auto expected = phase == "after_staged_evidence"
                ? "recovery_required"
                : "rolled_back";
            if (first.status != expected) {
                std::cerr << "phase=" << phase << "\n"
                          << first.to_json(true) << "\n";
            }
            expect(
                first.status == expected &&
                    read_text(fixture.config) == fixture.config_before,
                "failure injection must leave or restore exact before bytes");

            auto retry = apply_options(fixture, plan.plan_hash);
            retry.agent = "actor-b";
            const auto recovered = ProductRegistrationOps::apply(retry);
            expect(
                recovered.status == "applied" &&
                    !has_registration_staging(fixture),
                "failure retry must complete without deleting leftovers "
                "manually");
            expect_source_and_destination_invariant(fixture);
        });
    }
}

int test_real_process_death_recovery() {
    using kano::backlog_ops::ProductRegistrationOps;
    constexpr std::array<std::string_view, 3> phases = {
        "after_staged_evidence",
        "after_transaction_publish",
        "after_config_publish",
    };
    int executed = 0;
    for (const auto phase : phases) {
        with_fixture([&](Fixture& fixture) {
            const auto plan =
                ProductRegistrationOps::plan(plan_options(fixture));
            const auto exit_code = launch_process_death_child(
                fixture, plan.plan_hash, phase);
            ++executed;
            expect(
                exit_code == kInjectedProcessExitCode,
                "child must terminate through the deterministic lock-held "
                "process-death hook");
            const auto lock = config_lock_path(fixture);
            const auto guard = config_guard_path(fixture);
            expect(
                std::filesystem::is_directory(lock) &&
                    is_regular_non_reparse_file(guard),
                "abrupt child death must leave a stale final lock while the "
                "persistent OS guard remains a safe regular file");
            const auto evidence_published =
                phase != "after_staged_evidence";
            expect(
                has_registration_staging(fixture) == !evidence_published &&
                    std::filesystem::is_directory(
                        registration_transaction(fixture, plan.plan_hash)) ==
                        evidence_published,
                "child death must occur at the requested evidence boundary");
            expect(
                read_text(fixture.config) ==
                    (phase == "after_config_publish"
                         ? expected_config_after(fixture)
                         : fixture.config_before),
                "child death must leave an exact reviewed before/after config "
                "state");

            auto retry = apply_options(fixture, plan.plan_hash);
            retry.agent = "actor-b";
            const auto recovered = ProductRegistrationOps::apply(retry);
            ProductRegistrationOps::RecoveryOptions recovery{
                .backlog_root = fixture.shared,
                .plan_hash = plan.plan_hash,
            };
            const auto verification =
                ProductRegistrationOps::verify(recovery);
            const auto status = ProductRegistrationOps::status(recovery);
            expect(
                recovered.status == "applied" &&
                    read_text(fixture.config) ==
                        expected_config_after(fixture) &&
                    !std::filesystem::exists(lock) &&
                    is_regular_non_reparse_file(guard) &&
                    !has_registration_staging(fixture) &&
                    verification.status == "verified" &&
                    status.status == "applied",
                "process death must release the OS guard automatically so "
                "actor B can reclaim stale state while preserving the guard");
            if (evidence_published) {
                expect(
                    recovered.apply_agent ==
                            std::optional<std::string>("actor-a") &&
                        recovered.recovery_agent ==
                            std::optional<std::string>("actor-b") &&
                        verification.apply_agent ==
                            std::optional<std::string>("actor-a") &&
                        verification.recovery_agent ==
                            std::optional<std::string>("actor-b"),
                    "published child evidence must retain durable apply and "
                    "recovery attribution");
            } else {
                expect(
                    recovered.apply_agent ==
                            std::optional<std::string>("actor-b") &&
                        !recovered.recovery_agent,
                    "unpublished child staging must not claim durable actor A");
            }
            expect_source_and_destination_invariant(fixture);
        });
    }
    return executed;
}

void test_recovery_enforces_embedded_inventory_limits() {
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::ProductRegistrationOps;

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.limits.max_files = 4;
        const auto plan = ProductRegistrationOps::plan(options);
        expect(
            plan.ready(),
            "path-entry-bound recovery fixture must plan under max_files=4");
        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "path-entry-bound recovery fixture must publish prepared "
            "evidence");
        for (std::size_t index = 0; index < 81u; ++index) {
            write_text(
                fixture.external_root /
                    ("recovery-excluded-" + std::to_string(index) +
                     ".index.md"),
                "post-plan derived index bytes\n");
        }
        const auto source_after_growth =
            snapshot_tree(fixture.external_root);
        auto recovery = interrupted;
        recovery.agent = "actor-b";
        recovery.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(recovery);
        expect(
            result.status == "recovery_required" &&
                contains_prefix(
                    result.operation_receipts,
                    "source_path_entry_limit_exceeded") &&
                read_text(fixture.config) == fixture.config_before &&
                snapshot_tree(fixture.external_root) == source_after_growth &&
                !std::filesystem::exists(fixture.destination),
            "exact actor-B recovery must enforce the embedded source entry "
            "budget without config/source/destination mutation");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        const auto preliminary = ProductRegistrationOps::plan(options);
        options.limits.max_files = preliminary.files.size();
        const auto plan = ProductRegistrationOps::plan(options);
        expect(plan.ready(), "exact-file-bound recovery fixture must plan");
        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "file-bound recovery fixture must publish prepared evidence");
        write_text(
            fixture.external_root / "000-file-growth.bin",
            "post-plan authoritative file growth\n");
        const auto source_after_growth =
            snapshot_tree(fixture.external_root);
        auto recovery = interrupted;
        recovery.agent = "actor-b";
        recovery.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(recovery);
        expect(
            result.status == "recovery_required" &&
                contains_prefix(
                    result.operation_receipts,
                    "source_file_limit_exceeded") &&
                read_text(fixture.config) == fixture.config_before &&
                snapshot_tree(fixture.external_root) == source_after_growth &&
                !std::filesystem::exists(fixture.destination),
            "recovery must enforce embedded file limits without mutating "
            "deliberate fixture growth");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        const auto preliminary = ProductRegistrationOps::plan(options);
        std::uintmax_t planned_bytes = 0;
        for (const auto& file : preliminary.files) {
            planned_bytes += file.size;
        }
        options.limits.max_bytes = planned_bytes;
        const auto plan = ProductRegistrationOps::plan(options);
        expect(plan.ready(), "exact-byte-bound recovery fixture must plan");
        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "bounded recovery fixture must publish prepared evidence");
        write_text(
            fixture.external_root / "000-large-growth.bin",
            std::string(4u * 1024u * 1024u, 'x'));
        auto recovery = interrupted;
        recovery.agent = "actor-b";
        recovery.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(recovery);
        expect(
            result.status == "recovery_required" &&
                contains_prefix(
                    result.operation_receipts,
                    "source_byte_limit_exceeded") &&
                read_text(fixture.config) == fixture.config_before,
            "recovery must pre-stat and reject post-plan byte growth using "
            "embedded limits");
    });

    with_fixture([](Fixture& fixture) {
        auto options = plan_options(fixture);
        options.limits.max_items = 2;
        const auto plan = ProductRegistrationOps::plan(options);
        expect(plan.ready(), "exact-item-bound recovery fixture must plan");
        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "item-bound recovery fixture must publish prepared evidence");
        (void)create_item(
            fixture.external_root,
            "HQST",
            ItemType::Task,
            2,
            "Post-plan item growth",
            "019d0000-0020-7000-8000-000000000020");
        auto recovery = interrupted;
        recovery.agent = "actor-b";
        recovery.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(recovery);
        expect(
            result.status == "recovery_required" &&
                contains_prefix(
                    result.operation_receipts,
                    "source_item_limit_exceeded") &&
                read_text(fixture.config) == fixture.config_before,
            "recovery must enforce embedded item limits after source growth");
    });
}

bool test_recovery_revalidates_registry_identity_collisions() {
    using kano::backlog_core::ItemType;
    using kano::backlog_ops::ProductRegistrationOps;

    bool passed = false;
    with_fixture([&](Fixture& fixture) {
        const auto source_before = snapshot_tree(fixture.external_root);
        const auto options = plan_options(fixture);
        const auto plan = ProductRegistrationOps::plan(options);
        if (!plan.ready()) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            plan.ready(),
            "registry-collision recovery fixture must start from a ready "
            "reviewed plan");

        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        const auto first = ProductRegistrationOps::apply(interrupted);
        const bool evidence_published =
            first.status == "recovery_required" &&
            first.recovery_status == "exact_apply_required" &&
            std::filesystem::is_directory(
                registration_transaction(fixture, plan.plan_hash)) &&
            read_text(fixture.config) == fixture.config_before;
        if (!evidence_published) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(
            evidence_published,
            "registry-collision recovery fixture must publish evidence while "
            "preserving exact config-before bytes");

        const auto observer = fixture.shared / "products" / "observer";
        (void)create_item(
            observer,
            "HQST",
            ItemType::Task,
            1,
            "Post-plan source identity collision",
            fixture.uids[1]);

        auto retry = interrupted;
        retry.agent = "actor-b";
        retry.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(retry);
        passed =
            result.status == "recovery_required" &&
            result.recovery_status == "exact_apply_required" &&
            contains_prefix(
                result.operation_receipts, "stale_registry_identity") &&
            contains_prefix(
                result.operation_receipts,
                "display_id_collision:" + fixture.ids[1] + ":observer") &&
            contains_prefix(
                result.operation_receipts,
                "uid_collision:" + fixture.uids[1] + ":observer") &&
            read_text(fixture.config) == fixture.config_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            !std::filesystem::exists(fixture.destination);
        if (!passed) {
            std::cerr
                << "recovery registry identity collision mismatch:\n"
                << result.to_json(true) << "\n";
        }
    });
    return passed;
}

bool test_recovery_enforces_registry_path_entry_limit() {
    using kano::backlog_ops::ProductRegistrationOps;

    constexpr std::size_t kMaximumItems = 2u;
    constexpr std::size_t kRegistryPathEntryBudget =
        4u * kMaximumItems + 64u;
    bool passed = false;
    with_fixture([&](Fixture& fixture) {
        const auto source_before = snapshot_tree(fixture.external_root);
        auto options = plan_options(fixture);
        options.limits.max_items = kMaximumItems;
        const auto plan = ProductRegistrationOps::plan(options);
        if (!plan.ready()) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            plan.ready(),
            "registry-path recovery fixture must start from a ready reviewed "
            "plan with an evidence-bound item limit");

        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        const auto first = ProductRegistrationOps::apply(interrupted);
        const bool evidence_published =
            first.status == "recovery_required" &&
            first.recovery_status == "exact_apply_required" &&
            std::filesystem::is_directory(
                registration_transaction(fixture, plan.plan_hash)) &&
            read_text(fixture.config) == fixture.config_before;
        if (!evidence_published) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(
            evidence_published,
            "registry-path recovery fixture must publish evidence while "
            "preserving exact config-before bytes");

        const auto observer_items =
            fixture.shared / "products" / "observer" / "items";
        for (std::size_t index = 0;
             index <= kRegistryPathEntryBudget;
             ++index) {
            write_text(
                observer_items /
                    ("post-plan-non-item-" + std::to_string(index) + ".tmp"),
                "");
        }

        auto retry = interrupted;
        retry.agent = "actor-b";
        retry.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(retry);
        passed =
            result.status == "recovery_required" &&
            result.recovery_status == "exact_apply_required" &&
            contains_prefix(
                result.operation_receipts,
                "registry_path_entry_limit_exceeded") &&
            read_text(fixture.config) == fixture.config_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            !std::filesystem::exists(fixture.destination);
        if (!passed) {
            std::cerr
                << "recovery registry path-entry limit mismatch:\n"
                << result.to_json(true) << "\n";
        }
    });
    return passed;
}

bool test_recovery_enforces_registry_item_scan_limit() {
    using kano::backlog_ops::ProductRegistrationOps;

    constexpr std::size_t kMaximumItems = 3u;
    bool passed = false;
    with_fixture([&](Fixture& fixture) {
        const auto source_before = snapshot_tree(fixture.external_root);
        auto options = plan_options(fixture);
        options.limits.max_items = kMaximumItems;
        const auto plan = ProductRegistrationOps::plan(options);
        if (!plan.ready()) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            plan.ready(),
            "registry-item recovery fixture must start from a ready reviewed "
            "plan with one remaining registry item slot");

        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        const auto first = ProductRegistrationOps::apply(interrupted);
        const bool evidence_published =
            first.status == "recovery_required" &&
            first.recovery_status == "exact_apply_required" &&
            std::filesystem::is_directory(
                registration_transaction(fixture, plan.plan_hash)) &&
            read_text(fixture.config) == fixture.config_before;
        if (!evidence_published) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(
            evidence_published,
            "registry-item recovery fixture must publish evidence while "
            "preserving exact config-before bytes");

        const auto observer_items =
            fixture.shared / "products" / "observer" / "items";
        write_text(
            observer_items / "000-post-plan-unreadable.md",
            "registry metadata must not be read\n");
        write_text(
            observer_items / "001-post-plan-item-limit.md",
            "registry metadata must not be read\n");

        auto retry = interrupted;
        retry.agent = "actor-b";
        retry.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(retry);
        passed =
            result.status == "recovery_required" &&
            result.recovery_status == "exact_apply_required" &&
            contains_prefix(
                result.operation_receipts,
                "registry_item_scan_limit_exceeded") &&
            !contains_prefix(
                result.operation_receipts, "registry_item_unreadable") &&
            !contains_prefix(
                result.operation_receipts,
                "registry_frontmatter_byte_limit_exceeded") &&
            read_text(fixture.config) == fixture.config_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            !std::filesystem::exists(fixture.destination);
        if (!passed) {
            std::cerr
                << "recovery registry item-slot limit mismatch:\n"
                << result.to_json(true) << "\n";
        }
    });
    return passed;
}

bool test_recovery_enforces_registry_frontmatter_limit() {
    using kano::backlog_ops::ProductRegistrationOps;

    constexpr std::size_t kMaximumItems = 3u;
    constexpr std::size_t kMaximumFrontmatterBytes =
        1u * 1024u * 1024u;
    bool passed = false;
    with_fixture([&](Fixture& fixture) {
        const auto source_before = snapshot_tree(fixture.external_root);
        auto options = plan_options(fixture);
        options.limits.max_items = kMaximumItems;
        const auto plan = ProductRegistrationOps::plan(options);
        if (!plan.ready()) {
            std::cerr << plan.to_json(true) << "\n";
        }
        expect(
            plan.ready(),
            "registry-frontmatter recovery fixture must start from a ready "
            "reviewed plan with one remaining registry item slot");

        ProductRegistrationOps::ApplyOptions interrupted{
            .plan = options,
            .expected_plan_hash = plan.plan_hash,
            .agent = "actor-a",
            .confirm = true,
            .inject_failure_after = std::nullopt,
            .inject_interruption_after = "after_transaction_publish",
        };
        const auto first = ProductRegistrationOps::apply(interrupted);
        const bool evidence_published =
            first.status == "recovery_required" &&
            first.recovery_status == "exact_apply_required" &&
            std::filesystem::is_directory(
                registration_transaction(fixture, plan.plan_hash)) &&
            read_text(fixture.config) == fixture.config_before;
        if (!evidence_published) {
            std::cerr << first.to_json(true) << "\n";
        }
        expect(
            evidence_published,
            "registry-frontmatter recovery fixture must publish evidence "
            "while preserving exact config-before bytes");

        std::string hostile_frontmatter =
            "---\n"
            "id: OBS-TSK-0001\n";
        hostile_frontmatter.append(
            kMaximumFrontmatterBytes + 4096u, 'r');
        write_text(
            fixture.shared / "products" / "observer" / "items" /
                "OBS-TSK-0001_post-plan-hostile.md",
            hostile_frontmatter);

        auto retry = interrupted;
        retry.agent = "actor-b";
        retry.inject_interruption_after.reset();
        const auto result = ProductRegistrationOps::apply(retry);
        passed =
            result.status == "recovery_required" &&
            result.recovery_status == "exact_apply_required" &&
            contains_prefix(
                result.operation_receipts,
                "registry_frontmatter_byte_limit_exceeded") &&
            read_text(fixture.config) == fixture.config_before &&
            snapshot_tree(fixture.external_root) == source_before &&
            !std::filesystem::exists(fixture.destination);
        if (!passed) {
            std::cerr
                << "recovery registry frontmatter limit mismatch:\n"
                << result.to_json(true) << "\n";
        }
    });
    return passed;
}

void test_evidence_tampering_fails_closed() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        auto interrupted = apply_options(fixture, plan.plan_hash);
        interrupted.inject_interruption_after =
            "after_transaction_publish";
        expect(
            ProductRegistrationOps::apply(interrupted).status ==
                "recovery_required",
            "tamper fixture must publish immutable evidence");
        const auto receipt =
            registration_transaction(fixture, plan.plan_hash) /
            "receipt.json";
        write_text(receipt, read_text(receipt) + " ");
        auto recovery = apply_options(fixture, plan.plan_hash);
        recovery.agent = "actor-b";
        const auto result = ProductRegistrationOps::apply(recovery);
        expect(
            result.status == "blocked" &&
                result.recovery_status == "exact_apply_required" &&
                contains_prefix(
                    result.operation_receipts,
                    "immutable_evidence_hash_mismatch") &&
                read_text(fixture.config) == fixture.config_before,
            "tampered immutable evidence must fail closed before config write");
    });
}

void test_two_plan_registration_race_has_no_lost_update() {
    using kano::backlog_ops::ProductRegistrationOps;
    with_fixture([](Fixture& fixture) {
        const auto first_options = plan_options(fixture);
        const auto second_options = make_rival_plan_options(fixture);
        const auto first_source = snapshot_tree(fixture.external_root);
        const auto second_source =
            snapshot_tree(second_options.request.external_root);
        const auto first_plan = ProductRegistrationOps::plan(first_options);
        const auto second_plan = ProductRegistrationOps::plan(second_options);
        expect(
            first_plan.ready() && second_plan.ready() &&
                first_plan.plan_hash != second_plan.plan_hash,
            "both concurrent plans must review the same initial config");

        ProductRegistrationOps::ApplyOptions first_apply{
            .plan = first_options,
            .expected_plan_hash = first_plan.plan_hash,
            .agent = "race-a",
            .confirm = true,
        };
        ProductRegistrationOps::ApplyOptions second_apply{
            .plan = second_options,
            .expected_plan_hash = second_plan.plan_hash,
            .agent = "race-b",
            .confirm = true,
        };
        kano::backlog_ops::ProductRegistrationResult first_result;
        kano::backlog_ops::ProductRegistrationResult second_result;
        std::barrier start(3);
        std::thread first_thread([&] {
            start.arrive_and_wait();
            first_result = ProductRegistrationOps::apply(first_apply);
        });
        std::thread second_thread([&] {
            start.arrive_and_wait();
            second_result = ProductRegistrationOps::apply(second_apply);
        });
        start.arrive_and_wait();
        first_thread.join();
        second_thread.join();

        const auto applied_count =
            static_cast<int>(first_result.status == "applied") +
            static_cast<int>(second_result.status == "applied");
        expect(
            applied_count == 1,
            "common config lock must allow exactly one stale peer plan to "
            "publish");
        const auto& loser_options = first_result.status == "applied"
            ? second_options
            : first_options;
        const auto fresh_loser = ProductRegistrationOps::plan(loser_options);
        expect(
            fresh_loser.ready(),
            "the losing product must replan against the winner config");
        ProductRegistrationOps::ApplyOptions final_apply{
            .plan = loser_options,
            .expected_plan_hash = fresh_loser.plan_hash,
            .agent = "race-finalizer",
            .confirm = true,
        };
        const auto final_result = ProductRegistrationOps::apply(final_apply);
        if (final_result.status != "applied") {
            std::cerr << final_result.to_json(true) << "\n";
        }
        const auto config = read_text(fixture.config);
        expect(
            final_result.status == "applied" &&
                count_occurrences(
                    config, "[products.HorizonQuestDemo]") == 1 &&
                count_occurrences(config, "[products.RivalDemo]") == 1,
            "replanned loser must append without losing the winner update");
        expect(
            snapshot_tree(fixture.external_root) == first_source &&
                snapshot_tree(second_options.request.external_root) ==
                    second_source &&
                !std::filesystem::exists(fixture.destination) &&
                !std::filesystem::exists(
                    fixture.shared / "products" / "RivalDemo"),
            "concurrent registration must remain config-only for both roots");
    });
}

int test_symlink_or_reparse_safety() {
    using kano::backlog_ops::ProductRegistrationOps;
    int skipped_capabilities = 0;
    with_fixture([&](Fixture& fixture) {
        const auto escape = fixture.root / "symlink-escape-target";
        std::filesystem::create_directories(escape);
        write_text(escape / "sentinel.txt", "outside source sentinel\n");
        const auto link = fixture.external_root / "linked-escape";
        std::error_code error;
        std::filesystem::create_directory_symlink(escape, link, error);
        if (error) {
            ++skipped_capabilities;
            std::cout
                << "product_registration_ops_smoke_test: SKIP "
                << "symlink/reparse capability unavailable; error="
                << error.value() << "\n";
            return;
        }

        const auto source_before = snapshot_tree(fixture.external_root);
        const auto plan =
            ProductRegistrationOps::plan(plan_options(fixture));
        expect(
            !plan.ready() &&
                contains_prefix(
                    plan.blockers,
                    "source_symlink_or_reparse_not_supported"),
            "source symlink/reparse entries must block bounded inventory");
        expect(
            snapshot_tree(fixture.external_root) == source_before &&
                read_text(escape / "sentinel.txt") ==
                    "outside source sentinel\n" &&
                read_text(fixture.config) == fixture.config_before &&
                !std::filesystem::exists(fixture.destination),
            "symlink/reparse rejection must not traverse or mutate its target");
    });
    return skipped_capabilities;
}

int test_windows_junction_authority_paths() {
    using kano::backlog_ops::ProductRegistrationOps;
    int executed = 0;
#ifdef _WIN32
    with_fixture([&](Fixture& fixture) {
        const auto alias = fixture.root / "shared-root-junction";
        create_junction_or_throw(alias, fixture.shared);
        ++executed;
        auto options = plan_options(fixture);
        options.backlog_root = alias;
        expect_blocked_plan(
            fixture, options, "shared_path_reparse_not_supported",
            "raw backlog-root junction ancestry must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto alias = fixture.root / "external-root-junction";
        create_junction_or_throw(alias, fixture.external_root);
        ++executed;
        auto options = plan_options(fixture);
        options.request.external_root = alias;
        expect_blocked_plan(
            fixture, options, "source_root_reparse_not_supported",
            "raw external-root junction ancestry must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto ancestor = fixture.external_root.parent_path();
        const auto target =
            fixture.root / "external-products-ancestor-junction-target";
        std::filesystem::rename(ancestor, target);
        create_junction_or_throw(ancestor, target);
        ++executed;
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "source_root_reparse_not_supported",
            "junction-backed external-root ancestor must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto kano = fixture.shared / ".kano";
        const auto target = fixture.root / "shared-kano-junction-target";
        std::filesystem::rename(kano, target);
        create_junction_or_throw(kano, target);
        ++executed;
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "shared_path_reparse_not_supported",
            "junction-backed shared .kano authority must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto products = fixture.shared / "products";
        const auto target = fixture.root / "products-junction-target";
        std::filesystem::rename(products, target);
        create_junction_or_throw(products, target);
        ++executed;
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "shared_path_reparse_not_supported",
            "junction-backed shared products authority must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto observer = fixture.shared / "products" / "observer";
        const auto target = fixture.root / "observer-junction-target";
        std::filesystem::rename(observer, target);
        create_junction_or_throw(observer, target);
        ++executed;
        expect_blocked_plan(
            fixture, plan_options(fixture),
            "registered_root_unresolved:observer",
            "junction-backed existing registry roots must block planning");
    });

    with_fixture([&](Fixture& fixture) {
        const auto plan = ProductRegistrationOps::plan(plan_options(fixture));
        expect(plan.ready(), "cache-junction fixture must first plan");
        const auto target = fixture.root / "cache-junction-target";
        std::filesystem::create_directories(target);
        const auto cache = fixture.shared / ".kano" / "cache";
        create_junction_or_throw(cache, target);
        ++executed;
        expect_blocked_apply_without_writes(
            fixture, apply_options(fixture, plan.plan_hash),
            "path_reparse_or_alias_not_supported",
            "preexisting junction-backed cache must block apply");
    });

    with_fixture([&](Fixture& fixture) {
        const auto plan = ProductRegistrationOps::plan(plan_options(fixture));
        expect(plan.ready(), "transaction-junction fixture must first plan");
        const auto registrations =
            fixture.shared / ".kano" / "cache" /
            "product-registrations";
        std::filesystem::create_directories(registrations);
        const auto target = fixture.root / "transaction-junction-target";
        std::filesystem::create_directories(target);
        create_junction_or_throw(registrations / plan.plan_hash, target);
        ++executed;
        expect_blocked_apply_without_writes(
            fixture, apply_options(fixture, plan.plan_hash),
            "path_reparse_or_alias_not_supported",
            "preexisting junction-backed transaction must block apply");
    });

    expect(
        executed == 8,
        "Windows path-safety coverage must execute real junction cases");
#else
    std::cout
        << "product_registration_ops_smoke_test: SKIP "
        << "Windows junction cases on non-Windows host\n";
#endif
    return executed;
}

}

int main(int argc, char** argv) {
    kano::backlog_core::ConfigureNoninteractiveErrorHandling();
    try {
        g_test_binary = std::filesystem::absolute(argv[0]);
        if (argc >= 2 &&
            std::string_view(argv[1]) == "--process-death-child") {
            return run_process_death_child(argc, argv);
        }
        test_deterministic_read_only_plan();
        test_derived_source_authority_filter();
        test_unrelated_registry_collisions_do_not_block_registration();
        test_blocked_registration_inputs();
        test_inventory_limits();
        test_pre_materialization_inventory_limits();
        test_apply_gates();
        test_success_replay_verify_status_and_relocation_handoff();
        test_automatic_rollback_after_post_publish_failure();
        test_invalid_atomic_temp_debris_never_blocks_exact_retry();
        test_partial_stage_debris_is_preserved_and_nonblocking();
        test_partial_lock_candidates_are_preserved_and_nonblocking();
        test_stale_lock_guard_serializes_reclaim_publishers();
        test_interrupted_after_publish_exact_apply_reconciliation();
        test_interrupted_third_state_config_fails_closed();
        test_all_interruption_windows_reconcile_without_cleanup();
        test_all_failure_windows_retry_without_cleanup();
        const auto executed_process_deaths =
            test_real_process_death_recovery();
        expect(
            executed_process_deaths == 3,
            "real process-death coverage must execute every crash phase");
        test_recovery_enforces_embedded_inventory_limits();
        const auto recovery_registry_identities_revalidated =
            test_recovery_revalidates_registry_identity_collisions();
        const auto recovery_registry_path_limit_enforced =
            test_recovery_enforces_registry_path_entry_limit();
        const auto recovery_registry_item_limit_enforced =
            test_recovery_enforces_registry_item_scan_limit();
        const auto recovery_registry_frontmatter_limit_enforced =
            test_recovery_enforces_registry_frontmatter_limit();
        expect(
            recovery_registry_identities_revalidated,
            "nonterminal exact recovery must revalidate registered display "
            "ID and UID collisions before config publication");
        expect(
            recovery_registry_path_limit_enforced,
            "nonterminal exact recovery must enforce the evidence-bound "
            "registry path-entry limit before config publication");
        expect(
            recovery_registry_item_limit_enforced,
            "nonterminal exact recovery must exhaust source-plus-registry "
            "item slots before registry metadata reads");
        expect(
            recovery_registry_frontmatter_limit_enforced,
            "nonterminal exact recovery must reject oversized unclosed "
            "registry frontmatter before config publication");
        test_evidence_tampering_fails_closed();
        test_two_plan_registration_race_has_no_lost_update();
        const auto skipped_capabilities = test_symlink_or_reparse_safety();
        const auto executed_junctions =
            test_windows_junction_authority_paths();
        std::cout
            << "product_registration_ops_smoke_test: PASS; "
            << "skipped_capabilities=" << skipped_capabilities
            << "; executed_process_deaths=" << executed_process_deaths
            << "; executed_junctions=" << executed_junctions << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "product_registration_ops_smoke_test: FAIL: "
                  << error.what() << "\n";
        return 1;
    }
}
