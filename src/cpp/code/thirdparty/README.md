# `src/cpp/code/thirdparty/` — Reserved for Forked Dependencies

## Policy

**FetchContent is the default** for all ordinary upstream C++ libraries.
Libraries are downloaded into CMake's `_deps` directory during the first configure.

**This directory is reserved** for dependencies that are intentionally forked or
customized in-repo. Ordinary upstream deps should NOT be cloned here.

## Currently Used Dependencies (via FetchContent)

| Library | Version | CMake Variable | Mode |
|---|---|---|---|
| CLI11 | v2.6.2 | `KB_CLI11_SOURCE_MODE` | fetch (default) |
| tomlplusplus | v3.4.0 | `KB_TOMLPP_SOURCE_MODE` | fetch (default) |

FTXUI is **not** used by kano-backlog (no TUI component).

## Adding a Forked Dependency

1. Clone the fork into the appropriate subdirectory:
   ```bash
   git clone https://github.com/kano/my-fork.git src/cpp/code/thirdparty/my-dep
   ```
2. Switch the corresponding `KB_*_SOURCE_MODE` cache variable to `submodule` in `CMakePresets.json` or on the CMake command line.
3. Ensure the `CMakeLists.txt` in that subdirectory exists and is self-contained.

## CMake Variables

- `KB_CLI11_SOURCE_MODE`: `fetch` (default) | `submodule` | `system`
- `KB_TOMLPP_SOURCE_MODE`: `fetch` (default) | `submodule` | `system`

Override at configure time:
```bash
cmake --preset linux-ninja-gcc -DKB_CLI11_SOURCE_MODE=submodule
```
