#pragma once
#include <kano_build_info.h>
#include <string>
#include <string_view>

namespace kano::backlog {

namespace detail {

struct InfraBuildInfoSnapshot {
    std::string version;
    std::string vcs;
    std::string branch;
    std::string revision;
    std::string revision_hash_short;
    std::string revision_hash;
    std::string dirty;
    std::string host;
    std::string platform;
    std::string toolchain;
};

inline std::string CopyOrFallback(const char* value, std::string_view fallback) {
    if (value != nullptr && value[0] != '\0') {
        return value;
    }
    return std::string(fallback);
}

inline const InfraBuildInfoSnapshot& GetInfraBuildInfoSnapshot() {
    static const InfraBuildInfoSnapshot snapshot = [] {
        KanoBuildInfo info = kano_build_info_discover();

        InfraBuildInfoSnapshot out{
            CopyOrFallback(kano_build_info_get_version(info),
#ifdef KB_BUILD_VERSION
                KB_BUILD_VERSION
#elif defined(KB_VERSION)
                KB_VERSION
#else
                "unknown"
#endif
            ),
            CopyOrFallback(kano_build_info_get_vcs_status(info),
#ifdef KB_BUILD_VCS
                KB_BUILD_VCS
#else
                "unknown"
#endif
            ),
            CopyOrFallback(kano_build_info_get_vcs_branch(info),
#ifdef KB_BUILD_BRANCH
                KB_BUILD_BRANCH
#else
                "unknown"
#endif
            ),
            CopyOrFallback(kano_build_info_get_vcs_revision(info),
#ifdef KB_BUILD_REVISION
                KB_BUILD_REVISION
#else
                "unknown"
#endif
            ),
#ifdef KB_BUILD_REVISION_HASH_SHORT
            std::string(KB_BUILD_REVISION_HASH_SHORT),
#else
            std::string("unknown"),
#endif
#ifdef KB_BUILD_REVISION_HASH
            std::string(KB_BUILD_REVISION_HASH),
#else
            std::string("unknown"),
#endif
            CopyOrFallback(kano_build_info_get_vcs_status(info),
#ifdef KB_BUILD_DIRTY
                KB_BUILD_DIRTY
#else
                "unknown"
#endif
            ),
#ifdef KB_BUILD_HOST_NAME
            std::string(KB_BUILD_HOST_NAME),
#else
            std::string("unknown"),
#endif
#ifdef KB_BUILD_PLATFORM
            std::string(KB_BUILD_PLATFORM),
#else
            std::string("unknown"),
#endif
            CopyOrFallback(kano_build_info_get_compiler(info),
#ifdef KB_BUILD_TOOLCHAIN
                KB_BUILD_TOOLCHAIN
#else
                "unknown"
#endif
            )
        };

        kano_build_info_free(info);
        return out;
    }();

    return snapshot;
}

} // namespace detail

inline std::string_view GetBuildVersion() {
    return detail::GetInfraBuildInfoSnapshot().version;
}

inline std::string_view GetBuildVCS() {
    return detail::GetInfraBuildInfoSnapshot().vcs;
}

inline std::string_view GetBuildBranch() {
    return detail::GetInfraBuildInfoSnapshot().branch;
}

inline std::string_view GetBuildRevision() {
    return detail::GetInfraBuildInfoSnapshot().revision;
}

inline std::string_view GetBuildRevisionHashShort() {
    return detail::GetInfraBuildInfoSnapshot().revision_hash_short;
}

inline std::string_view GetBuildRevisionHash() {
    return detail::GetInfraBuildInfoSnapshot().revision_hash;
}

inline std::string_view GetBuildDirty() {
    return detail::GetInfraBuildInfoSnapshot().dirty;
}

inline std::string_view GetBuildHostName() {
    return detail::GetInfraBuildInfoSnapshot().host;
}

inline std::string_view GetBuildPlatform() {
    return detail::GetInfraBuildInfoSnapshot().platform;
}

inline std::string_view GetBuildToolchain() {
    return detail::GetInfraBuildInfoSnapshot().toolchain;
}

inline std::string GetBuildInfo() {
    std::string out;
    out.reserve(256);
    out += "version=";
    out += GetBuildVersion();
    out += " vcs=";
    out += GetBuildVCS();
    out += " branch=";
    out += GetBuildBranch();
    out += " rev=";
    out += GetBuildRevision();
    out += " hash_short=";
    out += GetBuildRevisionHashShort();
    out += " hash=";
    out += GetBuildRevisionHash();
    out += " dirty=";
    out += GetBuildDirty();
    out += " host=";
    out += GetBuildHostName();
    out += " platform=";
    out += GetBuildPlatform();
    out += " toolchain=";
    out += GetBuildToolchain();
    return out;
}

inline std::string_view GetVersion() {
    return GetBuildVersion();
}

} // namespace kano::backlog
