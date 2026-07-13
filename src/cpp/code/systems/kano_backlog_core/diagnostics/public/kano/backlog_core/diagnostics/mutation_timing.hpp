#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace kano::backlog_core::diagnostics {

inline bool& mutation_timing_forced() {
    static bool forced = false;
    return forced;
}

inline void enable_mutation_timing() {
    mutation_timing_forced() = true;
}

inline bool mutation_timing_enabled() {
    if (mutation_timing_forced()) {
        return true;
    }
    const char* raw = std::getenv("KANO_BACKLOG_PROFILE_MUTATIONS");
    if (raw == nullptr || *raw == '\0') {
        raw = std::getenv("KOB_PROFILE_MUTATIONS");
    }
    if (raw == nullptr || *raw == '\0') {
        return false;
    }

    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value != "0" && value != "false" && value != "off" && value != "no";
}

inline std::string escape_json_string(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

inline void emit_mutation_span(const std::string& name, double duration_ms, const std::string& detail) {
    if (!mutation_timing_enabled()) {
        return;
    }
    std::cerr << "KOB_TIMING {\"span\":\"" << escape_json_string(name)
              << "\",\"duration_ms\":" << std::fixed << std::setprecision(3) << duration_ms;
    if (!detail.empty()) {
        std::cerr << ",\"detail\":\"" << escape_json_string(detail) << "\"";
    }
    std::cerr << "}\n";
}

class ScopedMutationSpan {
public:
    explicit ScopedMutationSpan(std::string name, std::string detail = {})
        : enabled_(mutation_timing_enabled()),
          name_(std::move(name)),
          detail_(std::move(detail)),
          start_(Clock::now()) {}

    ~ScopedMutationSpan() noexcept {
        if (!enabled_) {
            return;
        }
        try {
            const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
            emit_mutation_span(name_, elapsed, detail_);
        } catch (...) {
        }
    }

    ScopedMutationSpan(const ScopedMutationSpan&) = delete;
    ScopedMutationSpan& operator=(const ScopedMutationSpan&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    bool enabled_;
    std::string name_;
    std::string detail_;
    Clock::time_point start_;
};

} // namespace kano::backlog_core::diagnostics
