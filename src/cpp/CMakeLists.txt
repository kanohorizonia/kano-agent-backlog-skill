cmake_minimum_required(VERSION 3.20)
project(kano_backlog CXX)

# ---------------------------------------------------------------------------
# Fix MSVC path length warning (object file paths exceed 250-char MAX_PATH)
# ---------------------------------------------------------------------------
set(CMAKE_OBJECT_PATH_MAX 500)

# ---------------------------------------------------------------------------
# Version metadata (mirrors git-master convention)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" KANO_VERSION_FILE_CONTENT)
    string(STRIP "${KANO_VERSION_FILE_CONTENT}" KANO_VERSION_STR)
else()
    set(KANO_VERSION_STR "0.0.0-dev")
endif()
add_compile_definitions(KANO_VERSION="${KANO_VERSION_STR}")
add_compile_definitions(KANO_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Map KANO_VERSION to KB_BUILD_VERSION for the version module
add_compile_definitions(KB_BUILD_VERSION="${KANO_VERSION_STR}")
add_compile_definitions(KB_BUILD_VCS="git")
add_compile_definitions(KB_BUILD_DIRTY="unknown")

# ---------------------------------------------------------------------------
# Compiler safety defaults
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        "$<$<COMPILE_LANGUAGE:CXX>:/GR>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
    )
    # Workaround MSVC SDK bug: <string>/<optional> missing from SYSTEM includes
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/I ")
endif()

# ---------------------------------------------------------------------------
# CLI11 — FetchContent (default) or system
# ---------------------------------------------------------------------------
set(KANO_CLI11_SOURCE_MODE "fetch" CACHE STRING "CLI11 source mode: fetch|system")
set_property(CACHE KANO_CLI11_SOURCE_MODE PROPERTY STRINGS fetch system)

if(KANO_CLI11_SOURCE_MODE STREQUAL "fetch")
    include(FetchContent)
    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.4.2
    )
    FetchContent_MakeAvailable(cli11)
else()
    find_package(CLI11 CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# tomlplusplus — FetchContent (default), submodule, or system
# ---------------------------------------------------------------------------
set(KANO_TOMLPP_SOURCE_MODE "fetch" CACHE STRING "toml++ source mode: fetch|submodule|system")
set_property(CACHE KANO_TOMLPP_SOURCE_MODE PROPERTY STRINGS fetch submodule system)

set(KANO_TOMLPP_SUBMODULE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/code/thirdparty/tomlplusplus")
set(KANO_TOMLPP_BINARY_DIR "${CMAKE_BINARY_DIR}/thirdparty/tomlplusplus-build")

if(KANO_TOMLPP_SOURCE_MODE STREQUAL "submodule")
    if(EXISTS "${KANO_TOMLPP_SUBMODULE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KANO_TOMLPP_SUBMODULE_SOURCE_DIR}" "${KANO_TOMLPP_BINARY_DIR}" EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "toml++ submodule missing: ${KANO_TOMLPP_SUBMODULE_SOURCE_DIR}. Run: git submodule update --init --recursive")
    endif()
elseif(KANO_TOMLPP_SOURCE_MODE STREQUAL "fetch")
    include(FetchContent)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tomlplusplus)
else()
    find_package(tomlplusplus CONFIG REQUIRED)
endif()

# ---------------------------------------------------------------------------
# yaml-cpp — FetchContent (no good find_package on Windows)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.9.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

# ---------------------------------------------------------------------------
# SQLite3 — azadkuh/sqlite-amalgamation (CMake-compatible fork)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(sqlite3)
target_include_directories(SQLite3 INTERFACE $<BUILD_INTERFACE:${sqlite3_SOURCE_DIR}>)
set(_sqlite3_target SQLite3)

# ---------------------------------------------------------------------------
# jsoncpp — FetchContent with explicit option control
# ---------------------------------------------------------------------------
# jsoncpp CMakeLists.txt reads BUILD_SHARED_LIBS (global CMake variable),
# not JSONCPP_BUILD_SHARED_LIBS. Set it before add_subdirectory to ensure
# only the static library is built. jsoncpp defaults all three (shared,
# static, object) to ON, so we must explicitly disable shared here.
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(JSONCPP_WITH_TESTS OFF)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
FetchContent_Declare(
    jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG 1.9.7
)
FetchContent_Populate(jsoncpp)
add_subdirectory(${jsoncpp_SOURCE_DIR} ${jsoncpp_BINARY_DIR} EXCLUDE_FROM_ALL)
if(TARGET jsoncpp_static)
    set(_jsoncpp_target jsoncpp_static)
elseif(TARGET jsoncpp_lib)
    set(_jsoncpp_target jsoncpp_lib)
elseif(TARGET JsonCpp::JsonCpp)
    set(_jsoncpp_target JsonCpp::JsonCpp)
endif()
set(_jsoncpp_include_dir ${jsoncpp_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Output directories (unified out/ convention)
# ---------------------------------------------------------------------------
set(KANO_OUT_ROOT "${CMAKE_SOURCE_DIR}/out")
set(KANO_BIN_ROOT "${KANO_OUT_ROOT}/bin")
set(KANO_LIB_ROOT "${KANO_OUT_ROOT}/lib")
set(KANO_SYMBOL_ROOT "${KANO_OUT_ROOT}/symbols")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${KANO_BIN_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${KANO_LIB_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${KANO_LIB_ROOT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${KANO_SYMBOL_ROOT}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${KANO_SYMBOL_ROOT}")

foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    string(TOLOWER "${cfg}" cfg_lower)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${KANO_BIN_ROOT}/${cfg_lower}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${KANO_LIB_ROOT}/${cfg_lower}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${KANO_LIB_ROOT}/${cfg_lower}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${cfg_upper} "${KANO_SYMBOL_ROOT}/${cfg_lower}")
    set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_${cfg_upper} "${KANO_SYMBOL_ROOT}/${cfg_lower}")
endforeach()

# ---------------------------------------------------------------------------
# Optional: Catch2 test framework
# ---------------------------------------------------------------------------
option(KANO_BUILD_TESTS "Build kano-backlog C++ test targets" ON)

# ---------------------------------------------------------------------------
# Optional: Code coverage (MSVC /PROFILE flag — linker only, compiler instrumentation)
# NOTE: /PROFILE is a LINKER flag only. MSVC coverage requires:
#   1. /PROFILE at link time (enables profiling instrumentation)
#   2. Microsoft.CodeCoverage.Console collect at runtime
# See: https://learn.microsoft.com/en-us/visualstudio/profiling/
# ---------------------------------------------------------------------------
option(KANO_ENABLE_COVERAGE "Enable code coverage instrumentation (MSVC /PROFILE)" OFF)

if(KANO_ENABLE_COVERAGE AND MSVC)
    add_link_options("$<$<LINK_LANGUAGE:CXX>:/PROFILE>")
endif()

# ---------------------------------------------------------------------------
# Core layers (new code/ tree — replaces old systems/ layer)
# ---------------------------------------------------------------------------
add_subdirectory(code/systems/kano_backlog_core)
add_subdirectory(code/systems/kano_backlog_ops)

# CLI executable (code/apps/kano_backlog_cli has its own CMakeLists.txt)
add_subdirectory(code/apps/kano_backlog_cli)

# Smoke tests (migrated to new layer 2026-03-24)
if(KANO_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/backlog_core_smoke_test.cpp")
        add_executable(backlog_core_smoke_test tests/backlog_core_smoke_test.cpp)
        target_link_libraries(backlog_core_smoke_test PRIVATE kano::backlog_core)
        add_test(NAME backlog_core_smoke_test COMMAND backlog_core_smoke_test)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/workitem_ops_smoke_test.cpp")
        add_executable(workitem_ops_smoke_test tests/workitem_ops_smoke_test.cpp)
        target_link_libraries(workitem_ops_smoke_test PRIVATE kano::backlog_ops)
        add_test(NAME workitem_ops_smoke_test COMMAND workitem_ops_smoke_test)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_repo_smoke_test.cpp")
        add_executable(cli_repo_smoke_test tests/cli_repo_smoke_test.cpp)
        target_link_libraries(cli_repo_smoke_test PRIVATE kano::backlog_ops)
        target_compile_definitions(cli_repo_smoke_test PRIVATE KANO_REPO_ROOT="${CMAKE_SOURCE_DIR}")
        add_test(NAME cli_repo_smoke_test COMMAND cli_repo_smoke_test)
    endif()
endif()
