#pragma once

#include <kano_build_info.hpp>

#include <string>
#include <string_view>

namespace kano::backlog {

namespace detail {

using InfraBuildInfoSnapshot = kano::infra::build_info::Snapshot;

inline const InfraBuildInfoSnapshot& GetInfraBuildInfoSnapshot() {
    static const InfraBuildInfoSnapshot snapshot = [] {
        return kano::infra::build_info::discover_snapshot({
#ifdef KB_BUILD_VERSION
            .version = KB_BUILD_VERSION,
#elif defined(KB_VERSION)
            .version = KB_VERSION,
#endif
#ifdef KB_BUILD_VCS
            .vcs = KB_BUILD_VCS,
#endif
#ifdef KB_BUILD_BRANCH
            .branch = KB_BUILD_BRANCH,
#endif
#ifdef KB_BUILD_REVISION
            .revision = KB_BUILD_REVISION,
#endif
#ifdef KB_BUILD_REVISION_HASH_SHORT
            .revisionHashShort = KB_BUILD_REVISION_HASH_SHORT,
#endif
#ifdef KB_BUILD_REVISION_HASH
            .revisionHash = KB_BUILD_REVISION_HASH,
#endif
#ifdef KB_BUILD_DIRTY
            .dirty = KB_BUILD_DIRTY,
#endif
#ifdef KB_BUILD_HOST_NAME
            .host = KB_BUILD_HOST_NAME,
#endif
#ifdef KB_BUILD_PLATFORM
            .platform = KB_BUILD_PLATFORM,
#endif
#ifdef KB_BUILD_TOOLCHAIN
            .toolchain = KB_BUILD_TOOLCHAIN,
#endif
        });
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
    return detail::GetInfraBuildInfoSnapshot().revisionHashShort;
}

inline std::string_view GetBuildRevisionHash() {
    return detail::GetInfraBuildInfoSnapshot().revisionHash;
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
