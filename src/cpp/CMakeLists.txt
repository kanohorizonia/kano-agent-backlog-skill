cmake_minimum_required(VERSION 3.20)
project(kano_backlog VERSION 0.1.0 DESCRIPTION "Kano Agent Backlog — ticket-first AI agent collaboration" LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Fix MSVC path length warning (object file paths exceed 250-char MAX_PATH)
# ---------------------------------------------------------------------------
set(CMAKE_OBJECT_PATH_MAX 500)

# ---------------------------------------------------------------------------
# Compiler safety defaults
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>"
        "$<$<COMPILE_LANGUAGE:CXX>:/GR>"
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
    )
    # Tell CMake to pass /I before system include paths for TryCompile tests.
    # Without this, external projects (yaml-cpp, sqlite3) can't find STL headers.
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/I")
    set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/I")
endif()

# =============================================================================
# Build Infrastructure Options
# =============================================================================
# These mirror the kano-git-master-skill conventions.
# Override on CMake command line or in CMakePresets.json cacheVariables.

option(KB_ENABLE_MODULES "Enable C++23 modules" OFF)
option(KB_ENABLE_UNITY_BUILD "Enable unity build for eligible targets" OFF)
set(KB_UNITY_BUILD_MODE "full" CACHE STRING "Unity build mode: full|changed")
set_property(CACHE KB_UNITY_BUILD_MODE PROPERTY STRINGS full changed)
set(KB_UNITY_CHANGED_FILES "" CACHE STRING "Semicolon-separated repo-relative changed files for selective unity")
set(KB_UNITY_BATCH_SIZE "0" CACHE STRING "Unity batch size override (0 uses generator default)")
set(KB_COMPILER_LAUNCHER "auto" CACHE STRING "Compiler launcher: auto|none|ccache|sccache|<path>")
set_property(CACHE KB_COMPILER_LAUNCHER PROPERTY STRINGS auto none ccache sccache)

set(KB_UNITY_EXCLUDED_TARGETS "" CACHE STRING "Semicolon-separated targets excluded from unity build")
list(REMOVE_DUPLICATES KB_UNITY_EXCLUDED_TARGETS)

if(KB_ENABLE_MODULES)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()

if(KB_ENABLE_UNITY_BUILD)
    string(TOLOWER "${KB_UNITY_BUILD_MODE}" KB_UNITY_BUILD_MODE_EFFECTIVE)
    if(NOT KB_UNITY_BUILD_MODE_EFFECTIVE STREQUAL "full" AND NOT KB_UNITY_BUILD_MODE_EFFECTIVE STREQUAL "changed")
        message(FATAL_ERROR "Unsupported KB_UNITY_BUILD_MODE: ${KB_UNITY_BUILD_MODE} (expected full|changed)")
    endif()
else()
    set(KB_UNITY_BUILD_MODE_EFFECTIVE "off")
endif()

# =============================================================================
# Compiler Launcher
# =============================================================================
set(_kb_cl_request "${KB_COMPILER_LAUNCHER}")
if(_kb_cl_request STREQUAL "auto" AND DEFINED ENV{KB_COMPILER_LAUNCHER} AND NOT "$ENV{KB_COMPILER_LAUNCHER}" STREQUAL "")
    set(_kb_cl_request "$ENV{KB_COMPILER_LAUNCHER}")
endif()

set(_kb_cl_path "")
if(_kb_cl_request STREQUAL "none")
    message(STATUS "Compiler launcher disabled (KB_COMPILER_LAUNCHER=none)")
elseif(_kb_cl_request STREQUAL "auto")
    if(WIN32)
        find_program(_kb_cl_path NAMES sccache ccache)
    else()
        find_program(_kb_cl_path NAMES ccache sccache)
    endif()
elseif(_kb_cl_request STREQUAL "ccache" OR _kb_cl_request STREQUAL "sccache")
    find_program(_kb_cl_path NAMES "${_kb_cl_request}")
    if(NOT _kb_cl_path)
        message(FATAL_ERROR "Requested compiler launcher not found: ${_kb_cl_request}")
    endif()
else()
    if(IS_ABSOLUTE "${_kb_cl_request}" AND EXISTS "${_kb_cl_request}")
        set(_kb_cl_path "${_kb_cl_request}")
    else()
        find_program(_kb_cl_path NAMES "${_kb_cl_request}")
        if(NOT _kb_cl_path)
            message(FATAL_ERROR "Requested compiler launcher not found: ${_kb_cl_request}")
        endif()
    endif()
endif()

if(_kb_cl_path)
    set(CMAKE_C_COMPILER_LAUNCHER "${_kb_cl_path}" CACHE STRING "C compiler launcher" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_kb_cl_path}" CACHE STRING "CXX compiler launcher" FORCE)
    message(STATUS "Using compiler launcher: ${_kb_cl_path}")
else()
    unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
    unset(CMAKE_CXX_COMPILER_LAUNCHER CACHE)
endif()

# =============================================================================
# Build Metadata
# =============================================================================
set(KB_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if(EXISTS "${KB_REPO_ROOT}/VERSION")
    file(READ "${KB_REPO_ROOT}/VERSION" KB_VERSION_FILE_CONTENT)
    string(STRIP "${KB_VERSION_FILE_CONTENT}" KB_VERSION_STR)
else()
    set(KB_VERSION_STR "0.0.0-dev")
endif()
add_compile_definitions(KB_VERSION="${KB_VERSION_STR}")

# =============================================================================
# Platform Detection
# =============================================================================
if(WIN32)
    add_compile_definitions(KB_PLATFORM_WINDOWS=1)
    add_compile_definitions(NOMINMAX)
else()
    add_compile_definitions(KB_PLATFORM_UNIX=1)
endif()

# =============================================================================
# Output Directories  (mirrors kano-git-master-skill layout)
# =============================================================================
set(KB_BUILD_ROOT "${CMAKE_SOURCE_DIR}/out")
set(KB_PRESET_NAME "unknown-preset" CACHE STRING "CMake preset name for artifact layout")

set(KB_BIN_ROOT "${KB_BUILD_ROOT}/bin/${KB_PRESET_NAME}")
set(KB_LIB_ROOT "${KB_BUILD_ROOT}/lib/${KB_PRESET_NAME}")
set(KB_OBJ_ROOT "${KB_BUILD_ROOT}/obj/${KB_PRESET_NAME}")
set(KB_SYMBOL_ROOT "${KB_BUILD_ROOT}/obj/${KB_PRESET_NAME}/symbols")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${KB_BIN_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${KB_LIB_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${KB_LIB_ROOT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${KB_SYMBOL_ROOT}")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${KB_SYMBOL_ROOT}")

foreach(cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    string(TOLOWER "${cfg}" cfg_lower)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${KB_BIN_ROOT}/${cfg_lower}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${KB_LIB_ROOT}/${cfg_lower}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${KB_LIB_ROOT}/${cfg_lower}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${cfg_upper} "${KB_SYMBOL_ROOT}/${cfg_lower}")
    set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_${cfg_upper} "${KB_SYMBOL_ROOT}/${cfg_lower}")
endforeach()

# =============================================================================
# Coverage and PGO Options
# =============================================================================
option(KB_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
set(KB_PGO_MODE "off" CACHE STRING "PGO mode: off|collect|use")
set_property(CACHE KB_PGO_MODE PROPERTY STRINGS off collect use)
set(KB_PGO_LTCG "auto" CACHE STRING "PGO LTCG/LTO: auto|off")
set_property(CACHE KB_PGO_LTCG PROPERTY STRINGS auto off)

if(KB_ENABLE_COVERAGE)
    if(MSVC)
        add_link_options(/PROFILE)
        set(KB_COVERAGE_OUTPUT_DIR "${KB_BUILD_ROOT}/coverage" CACHE PATH "Coverage output directory")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate -fcoverage-mapping)
        set(KB_COVERAGE_OUTPUT_DIR "${KB_BUILD_ROOT}/coverage" CACHE PATH "Coverage output directory")
        message(STATUS "Coverage: Clang PGO-style (llvm-cov reporting, unified with PGO collect)")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-fprofile-arcs -ftest-coverage -fno-exceptions)
        add_link_options(-fprofile-arcs -ftest-coverage)
        set(KB_COVERAGE_OUTPUT_DIR "${KB_BUILD_ROOT}/coverage" CACHE PATH "Coverage output directory")
        message(STATUS "Coverage: GCC traditional (gcov reporting)")
    else()
        message(WARNING "Coverage not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
    message(STATUS "Coverage enabled (output: ${KB_COVERAGE_OUTPUT_DIR})")
endif()

if(NOT KB_PGO_MODE STREQUAL "off")
    if(MSVC)
        if(KB_PGO_MODE STREQUAL "collect")
            add_compile_options(/GL)
            add_link_options(/LTCG /GENPROFILE)
            message(STATUS "PGO: collect mode (MSVC /GL /LTCG /GENPROFILE)")
        elseif(KB_PGO_MODE STREQUAL "use")
            add_compile_options(/GL)
            if(NOT KB_PGO_LTCG STREQUAL "off")
                add_link_options(/LTCG /USEPROFILE)
                message(STATUS "PGO: use mode with LTCG (MSVC /GL /LTCG /USEPROFILE)")
            else()
                add_link_options(/LTCG /USEPROFILE)
            endif()
        endif()
        set(KB_PGO_PROFILE_DIR "${KB_BUILD_ROOT}/pgo" CACHE PATH "PGO profile data directory")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        if(KB_PGO_MODE STREQUAL "collect")
            add_compile_options(-fprofile-generate)
            add_link_options(-fprofile-generate)
            message(STATUS "PGO: collect mode (GCC -fprofile-generate)")
        elseif(KB_PGO_MODE STREQUAL "use")
            if(NOT KB_PGO_LTCG STREQUAL "off")
                add_compile_options(-fprofile-use -flto -fno-peel-loops -fno-tracer)
                add_link_options(-fprofile-use -flto)
                message(STATUS "PGO: use mode with LTO (GCC -fprofile-use -flto)")
            else()
                add_compile_options(-fprofile-use -fno-peel-loops -fno-tracer)
                add_link_options(-fprofile-use)
            endif()
        endif()
        set(KB_PGO_PROFILE_DIR "${KB_BUILD_ROOT}/pgo" CACHE PATH "PGO profile data directory")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(KB_PGO_MODE STREQUAL "collect")
            add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
            add_link_options(-fprofile-instr-generate -fcoverage-mapping)
            message(STATUS "PGO: collect mode (Clang -fprofile-instr-generate)")
        elseif(KB_PGO_MODE STREQUAL "use")
            set(KB_PGO_PROFILE_DIR "${KB_BUILD_ROOT}/pgo" CACHE PATH "PGO profile data directory")
            set(LLVM_PROFILE_FILE "${KB_PGO_PROFILE_DIR}/merged.profdata")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-use=${LLVM_PROFILE_FILE}")
            if(NOT KB_PGO_LTCG STREQUAL "off")
                add_compile_options(-flto=thin)
                add_link_options(-flto=thin)
                message(STATUS "PGO: use mode with thin LTO (Clang)")
            endif()
        endif()
    else()
        message(WARNING "PGO not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# =============================================================================
# Third-party Dependencies
# =============================================================================

# CLI11 v2.4 uses <codecvt> which is removed in MSVC 19.50 (VS 2026)
add_compile_definitions(CLI11_HAS_CODECVT=0)

set(KB_CLI11_SOURCE_MODE "fetch" CACHE STRING "CLI11 source mode: fetch|submodule|system")
set_property(CACHE KB_CLI11_SOURCE_MODE PROPERTY STRINGS fetch submodule system)

if(KB_CLI11_SOURCE_MODE STREQUAL "fetch")
    include(FetchContent)
    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.6.2
    )
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLI11_PRECOMPILED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(cli11)
    set(cli11_SOURCE_DIR "${cli11_SOURCE_DIR}")
elseif(KB_CLI11_SOURCE_MODE STREQUAL "submodule")
    set(KB_CLI11_SUBMODULE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/code/thirdparty/cli11")
    if(EXISTS "${KB_CLI11_SUBMODULE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KB_CLI11_SUBMODULE_SOURCE_DIR}" EXCLUDE_FROM_ALL)
        set(cli11_SOURCE_DIR "${KB_CLI11_SUBMODULE_SOURCE_DIR}")
    else()
        message(FATAL_ERROR "CLI11 submodule missing: ${KB_CLI11_SUBMODULE_SOURCE_DIR}")
    endif()
elseif(KB_CLI11_SOURCE_MODE STREQUAL "system")
    find_package(CLI11 CONFIG REQUIRED)
else()
    message(FATAL_ERROR "Unsupported KB_CLI11_SOURCE_MODE: ${KB_CLI11_SOURCE_MODE}")
endif()

# tomlplusplus
set(KB_TOMLPP_SOURCE_MODE "fetch" CACHE STRING "toml++ source mode: fetch|submodule|system")
set_property(CACHE KB_TOMLPP_SOURCE_MODE PROPERTY STRINGS fetch submodule system)

if(KB_TOMLPP_SOURCE_MODE STREQUAL "fetch")
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
elseif(KB_TOMLPP_SOURCE_MODE STREQUAL "submodule")
    set(KB_TOMLPP_SUBMODULE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/code/thirdparty/tomlplusplus")
    if(EXISTS "${KB_TOMLPP_SUBMODULE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KB_TOMLPP_SUBMODULE_SOURCE_DIR}" EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "toml++ submodule missing: ${KB_TOMLPP_SUBMODULE_SOURCE_DIR}")
    endif()
elseif(KB_TOMLPP_SOURCE_MODE STREQUAL "system")
    find_package(tomlplusplus CONFIG REQUIRED)
else()
    message(FATAL_ERROR "Unsupported KB_TOMLPP_SOURCE_MODE: ${KB_TOMLPP_SOURCE_MODE}")
endif()

# yaml-cpp — FetchContent (no good find_package on Windows)
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

# SQLite3 — azadkuh/sqlite-amalgamation (CMake-compatible fork)
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
FetchContent_MakeAvailable(jsoncpp)
if(TARGET jsoncpp_static)
    set(_jsoncpp_target jsoncpp_static)
elseif(TARGET jsoncpp_lib)
    set(_jsoncpp_target jsoncpp_lib)
elseif(TARGET JsonCpp::JsonCpp)
    set(_jsoncpp_target JsonCpp::JsonCpp)
endif()
set(_jsoncpp_include_dir ${jsoncpp_SOURCE_DIR}/include)

# =============================================================================
# Test infrastructure
# =============================================================================
enable_testing()

# =============================================================================
# Shared infra submodule
# =============================================================================
set(KB_INFRA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared/infra")
if(EXISTS "${KB_INFRA_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${KB_INFRA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/shared/infra")
    if(TARGET kano_infra_build_info)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "")
            set(_kb_infra_compiler "unknown")
        else()
            set(_kb_infra_compiler "${CMAKE_CXX_COMPILER_ID}")
        endif()

        if(CMAKE_BUILD_TYPE STREQUAL "")
            set(_kb_infra_build_type "unknown")
        else()
            set(_kb_infra_build_type "${CMAKE_BUILD_TYPE}")
        endif()

        target_compile_definitions(kano_infra_build_info PRIVATE
            KANO_INFRA_BUILD_VERSION="${KB_VERSION_STR}"
            KANO_INFRA_BUILD_REVISION="unknown"
            KANO_INFRA_BUILD_BRANCH="unknown"
            KANO_INFRA_BUILD_TYPE="${_kb_infra_build_type}"
            KANO_INFRA_BUILD_COMPILER="${_kb_infra_compiler}"
            KANO_INFRA_BUILD_TIMESTAMP="unknown"
            KANO_INFRA_BUILD_VCS_STATUS="unknown"
        )
    endif()
endif()

# =============================================================================
# Add subsystems (order matters: core before ops before cli)
# =============================================================================
add_subdirectory(code/systems/kano_backlog_core)
add_subdirectory(code/systems/kano_backlog_ops)

option(KB_BUILD_WEBVIEW "Build kano-backlog webview targets (requires drogon)" OFF)
if(KB_BUILD_WEBVIEW)
    add_subdirectory(code/systems/kano_backlog_webview_core)
endif()

# =============================================================================
# CLI executable
# =============================================================================
add_subdirectory(code/apps/kano_backlog_cli)

# =============================================================================
# Test targets
# =============================================================================
option(KB_BUILD_TESTS "Build kano-backlog C++ test targets" OFF)
if(KB_BUILD_TESTS)
    add_subdirectory(code/tests)
endif()

# =============================================================================
# Unity Build Configuration (must be last)
# =============================================================================
function(kb_collect_buildsystem_targets directory_path output_var)
    get_property(local_targets DIRECTORY "${directory_path}" PROPERTY BUILDSYSTEM_TARGETS)
    set(all_targets ${local_targets})
    get_property(subdirs DIRECTORY "${directory_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        kb_collect_buildsystem_targets("${subdir}" child_targets)
        list(APPEND all_targets ${child_targets})
    endforeach()
    list(REMOVE_DUPLICATES all_targets)
    set(${output_var} "${all_targets}" PARENT_SCOPE)
endfunction()

function(kb_configure_unity_target target_name)
    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY" OR target_type STREQUAL "UTILITY")
        return()
    endif()
    get_target_property(target_source_dir ${target_name} SOURCE_DIR)
    if(target_source_dir MATCHES "/thirdparty(/|$)" OR target_source_dir MATCHES "/_deps(/|$)")
        set_target_properties(${target_name} PROPERTIES UNITY_BUILD OFF)
        return()
    endif()
    list(FIND KB_UNITY_EXCLUDED_TARGETS "${target_name}" unity_excluded_target_index)
    if(NOT unity_excluded_target_index EQUAL -1)
        set_target_properties(${target_name} PROPERTIES UNITY_BUILD OFF)
        return()
    endif()
    if(KB_UNITY_BUILD_MODE_EFFECTIVE STREQUAL "off")
        set_target_properties(${target_name} PROPERTIES UNITY_BUILD OFF)
        return()
    endif()
    set_target_properties(${target_name} PROPERTIES UNITY_BUILD ON)
endfunction()

function(kb_apply_unity_configuration)
    kb_collect_buildsystem_targets("${CMAKE_CURRENT_SOURCE_DIR}" all_targets)
    foreach(target_name IN LISTS all_targets)
        kb_configure_unity_target("${target_name}")
    endforeach()
endfunction()

kb_apply_unity_configuration()
