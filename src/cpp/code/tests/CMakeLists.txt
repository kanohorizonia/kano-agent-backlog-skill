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
target_compile_features(backlog_core_smoke_test PRIVATE cxx_std_23)
target_link_libraries(backlog_core_smoke_test PRIVATE kano::backlog_core)
add_test(NAME backlog_core_smoke_test COMMAND backlog_core_smoke_test)
set_tests_properties(backlog_core_smoke_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(workitem_ops_smoke_test
    workitem_ops_smoke_test.cpp
)
target_compile_features(workitem_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(workitem_ops_smoke_test PRIVATE kano::backlog_ops)
add_test(NAME workitem_ops_smoke_test COMMAND workitem_ops_smoke_test)
set_tests_properties(workitem_ops_smoke_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(cli_repo_smoke_test
    cli_repo_smoke_test.cpp
)
target_compile_features(cli_repo_smoke_test PRIVATE cxx_std_23)
target_compile_definitions(cli_repo_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
target_link_libraries(cli_repo_smoke_test PRIVATE kano::backlog_core)
add_test(NAME cli_repo_smoke_test COMMAND cli_repo_smoke_test)
set_tests_properties(cli_repo_smoke_test PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)
