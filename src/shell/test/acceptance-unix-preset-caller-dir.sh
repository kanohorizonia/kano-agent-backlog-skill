#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CASE_ROOT="$ROOT_DIR/.kano/tmp/unix-preset-caller-dir-$$"
export KOG_CPP_ROOT="$ROOT_DIR/src/cpp"
export KANO_CPP_ROOT="$KOG_CPP_ROOT"

cleanup() {
  rm -rf "$CASE_ROOT"
}
trap cleanup EXIT

verify_caller_dir_preserved() {
  local platform="$1"
  local prerequisite="$2"
  local caller_dir="$ROOT_DIR/src/cpp/scripts/$platform"

  (
    SCRIPT_DIR="$caller_dir"
    source "$ROOT_DIR/src/cpp/scripts/common/unix_preset_build.sh"

    [[ "$SCRIPT_DIR" == "$caller_dir" ]]
    [[ -f "$SCRIPT_DIR/$prerequisite" ]]
  )
}

verify_caller_dir_preserved macos prerequisite_macos.sh
verify_caller_dir_preserved linux prerequisite_linux.sh

mkdir -p "$CASE_ROOT/bin" "$CASE_ROOT/cpp"
cat > "$CASE_ROOT/bin/cmake" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$CASE_ROOT/cmake.args"
EOF
chmod +x "$CASE_ROOT/bin/cmake"

(
  export PATH="$CASE_ROOT/bin:$PATH"
  source "$ROOT_DIR/src/cpp/scripts/common/unix_preset_build.sh"
  export KOG_CPP_ROOT="$CASE_ROOT/cpp"
  export KANO_CPP_ROOT="$KOG_CPP_ROOT"
  export INF_CMAKE_CACHE_ARGS_JSON='{"KB_BUILD_TESTS":"ON"}'
  kog_apply_self_build_config() { :; }
  kog_collect_build_metadata() { :; }
  kano_cpp_infra_tool() { printf '%s\n' '-DKB_BUILD_TESTS=ON'; }
  kabld_run_unix_preset macos-ninja-clang-x64 macos-ninja-clang-x64-release
)

grep -q '^--preset macos-ninja-clang-x64 -DKB_PRESET_NAME=macos-ninja-clang-x64 -DKB_BUILD_TESTS=ON$' "$CASE_ROOT/cmake.args"
grep -q '^--build --preset macos-ninja-clang-x64-release$' "$CASE_ROOT/cmake.args"

echo "PASS: Unix preset helper preserves caller directories and preset artifact identity"
