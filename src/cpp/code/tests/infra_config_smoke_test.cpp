#include <kano_config.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

struct ArrayObservation {
    bool Found = false;
    bool Empty = false;
};

bool ObserveEmptyArray(const KanoConfigEntryView* entry, void* userData) {
    auto* observation = static_cast<ArrayObservation*>(userData);
    if (entry != nullptr && entry->key != nullptr &&
        std::strcmp(entry->key, "empty") == 0) {
        observation->Found = entry->type == KANO_CONFIG_VALUE_ARRAY;
        observation->Empty =
            entry->string_value != nullptr && std::strcmp(entry->string_value, "[]") == 0;
    }
    return true;
}

}  // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path configPath =
        std::filesystem::temp_directory_path() /
        ("kano-backlog-infra-config-" + std::to_string(nonce) + ".toml");
    {
        std::ofstream output(configPath);
        output << "empty = []\n"
               << "quoted = \"value # retained\" # trailing comment\n";
    }

    KanoConfig config = kano_config_load(configPath.string().c_str());
    std::error_code cleanupError;
    std::filesystem::remove(configPath, cleanupError);
    if (config == nullptr) {
        std::cerr << "FAIL: config parser rejected the regression fixture\n";
        return 1;
    }

    const char* quoted = kano_config_get(config, "quoted");
    ArrayObservation observation;
    const bool iterated = kano_config_foreach(config, ObserveEmptyArray, &observation);
    const bool ok =
        quoted != nullptr &&
        std::strcmp(quoted, "value # retained") == 0 &&
        iterated &&
        observation.Found &&
        observation.Empty;
    kano_config_free(config);

    if (!ok) {
        std::cerr << "FAIL: empty array or quote-aware comment semantics regressed\n";
        return 1;
    }
    std::cout << "PASS: empty arrays and quote-aware comments\n";
    return 0;
}
