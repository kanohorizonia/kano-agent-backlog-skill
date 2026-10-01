#!/usr/bin/env bash
set -euo pipefail

# Reuse locally cached, pinned FetchContent sources in an isolated native lane.
# The caller owns the checkout and provides an offline dependency snapshot.
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
deps_root="${KOB_OFFLINE_DEPS_ROOT:?Set KOB_OFFLINE_DEPS_ROOT to the local dependency snapshot}"
args=()
for dependency in cli11 tomlplusplus yaml-cpp sqlite3 jsoncpp; do
  test -f "$deps_root/$dependency-src/CMakeLists.txt"
  name="${dependency^^}"
  args+=("-DFETCHCONTENT_SOURCE_DIR_${name}=$deps_root/$dependency-src")
done
cmake -S "$repo_root/src/cpp" -B "$repo_root/.kano/tmp/create-scan-build" \
  -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DKB_BUILD_TESTS=ON \
  -DKB_PRESET_NAME=create-scan-offline -DKB_COMPILER_LAUNCHER=none \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON "${args[@]}"
cmake --build "$repo_root/.kano/tmp/create-scan-build" --parallel 8 \
  --target create_request_scan_smoke_test workitem_ops_smoke_test backlog_core_smoke_test
