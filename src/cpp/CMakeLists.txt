cmake_minimum_required(VERSION 3.20)
project(kano_backlog CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(KANO_REPO_ROOT "${CMAKE_SOURCE_DIR}/../../../../../../" ABSOLUTE)

enable_testing()

# Core library
add_library(backlog_core
    systems/backlog_core/frontmatter/parser.cpp
    systems/backlog_core/state/state_machine.cpp
    systems/backlog_core/validation/validator.cpp
)
target_include_directories(backlog_core PUBLIC systems)

add_library(backlog_ops
    systems/backlog_ops/workitem/workitem_ops.cpp
)
target_include_directories(backlog_ops PUBLIC systems)
target_link_libraries(backlog_ops PUBLIC backlog_core backlog_adapters)

# Adapters library
add_library(backlog_adapters
    systems/backlog_adapters/fs/filesystem.cpp
)
target_include_directories(backlog_adapters PUBLIC systems)
target_link_libraries(backlog_adapters PUBLIC backlog_core)

# CLI executable
add_executable(kano-backlog
    apps/kano_backlog_cli/main.cpp
)
target_link_libraries(kano-backlog PRIVATE backlog_core backlog_adapters backlog_ops)

add_executable(backlog_core_smoke_test
    tests/backlog_core_smoke_test.cpp
)
target_link_libraries(backlog_core_smoke_test PRIVATE backlog_core)
add_test(NAME backlog_core_smoke_test COMMAND backlog_core_smoke_test)

add_executable(workitem_ops_smoke_test
    tests/workitem_ops_smoke_test.cpp
)
target_link_libraries(workitem_ops_smoke_test PRIVATE backlog_ops)
add_test(NAME workitem_ops_smoke_test COMMAND workitem_ops_smoke_test)

add_executable(cli_repo_smoke_test
    tests/cli_repo_smoke_test.cpp
)
target_compile_definitions(cli_repo_smoke_test PRIVATE KANO_REPO_ROOT="${KANO_REPO_ROOT}")
add_test(NAME cli_repo_smoke_test COMMAND cli_repo_smoke_test)
