# =============================================================================
# kano-backlog C++ test targets
# =============================================================================
# Mirrors the test layout from kano-git-master-skill (code/tests/).
# Test executables are registered with CTest.
# =============================================================================

get_filename_component(KB_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

add_executable(backlog_core_smoke_test
    backlog_core_smoke_test.cpp
)
target_link_libraries(backlog_core_smoke_test PRIVATE kano::backlog_core)
add_test(NAME backlog_core_smoke_test COMMAND backlog_core_smoke_test)

add_executable(workitem_ops_smoke_test
    workitem_ops_smoke_test.cpp
)
target_link_libraries(workitem_ops_smoke_test PRIVATE kano::backlog_ops)
add_test(NAME workitem_ops_smoke_test COMMAND workitem_ops_smoke_test)

add_executable(cli_repo_smoke_test
    cli_repo_smoke_test.cpp
)
target_compile_definitions(cli_repo_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
add_test(NAME cli_repo_smoke_test COMMAND cli_repo_smoke_test)
