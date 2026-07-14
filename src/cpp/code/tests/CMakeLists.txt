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
set(KB_TEST_BACKLOG_CORE "[bdd][featured][feature:backlog-core][scenario:KOB-BDD-CORE-001] Backlog core loads config, state, and frontmatter contracts")
add_test(NAME "${KB_TEST_BACKLOG_CORE}" COMMAND backlog_core_smoke_test)
set_tests_properties("${KB_TEST_BACKLOG_CORE}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(workitem_ops_smoke_test
    workitem_ops_smoke_test.cpp
)
target_compile_features(workitem_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(workitem_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_WORKITEM_OPS "[bdd][featured][feature:workitem-ops][scenario:KOB-BDD-WORKITEM-001] Workitem operations create canonical backlog items and templates")
add_test(NAME "${KB_TEST_WORKITEM_OPS}" COMMAND workitem_ops_smoke_test)
set_tests_properties("${KB_TEST_WORKITEM_OPS}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(integrity_ops_smoke_test
    integrity_ops_smoke_test.cpp
)
target_compile_features(integrity_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(integrity_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_INTEGRITY_OPS "[bdd][featured][feature:integrity-ops][scenario:KOB-BDD-INTEGRITY-001] Integrity operations detect stale drift duplicate metadata without writes")
add_test(NAME "${KB_TEST_INTEGRITY_OPS}" COMMAND integrity_ops_smoke_test)
set_tests_properties("${KB_TEST_INTEGRITY_OPS}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(relation_ops_smoke_test
    relation_ops_smoke_test.cpp
)
target_compile_features(relation_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(relation_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_RELATION_OPS "[bdd][featured][feature:relation-ops][scenario:KOB-BDD-RELATION-001] Relation operations mutate and query bounded cross-product graphs")
add_test(NAME "${KB_TEST_RELATION_OPS}" COMMAND relation_ops_smoke_test)
set_tests_properties("${KB_TEST_RELATION_OPS}" PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(topic_ops_smoke_test
    topic_ops_smoke_test.cpp
)
target_compile_features(topic_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(topic_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_TOPIC_OPS "[bdd][featured][feature:topic-ops][scenario:KOB-BDD-TOPIC-001] Topic audit reports lifecycle hygiene without writes")
add_test(NAME "${KB_TEST_TOPIC_OPS}" COMMAND topic_ops_smoke_test)
set_tests_properties("${KB_TEST_TOPIC_OPS}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(cli_repo_smoke_test
    cli_repo_smoke_test.cpp
)
target_compile_features(cli_repo_smoke_test PRIVATE cxx_std_23)
target_compile_definitions(cli_repo_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
target_link_libraries(cli_repo_smoke_test PRIVATE kano::backlog_core)
set(KB_TEST_CLI_REPO "[bdd][featured][feature:cli-workflow][scenario:KOB-BDD-CLI-001] KOB CLI discovers repo roots and runs backlog workflows")
add_test(NAME "${KB_TEST_CLI_REPO}" COMMAND cli_repo_smoke_test)
set_tests_properties("${KB_TEST_CLI_REPO}" PROPERTIES
    TIMEOUT 120
    LABELS long
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(cli_quick_smoke_test
    cli_quick_smoke_test.cpp
)
target_compile_features(cli_quick_smoke_test PRIVATE cxx_std_23)
target_compile_definitions(cli_quick_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
target_link_libraries(cli_quick_smoke_test PRIVATE kano::backlog_core)
set(KB_TEST_CLI_QUICK "[bdd][featured][feature:cli-workflow][scenario:KOB-BDD-CLI-QUICK-001] KOB CLI quick smoke covers bounded core commands")
add_test(NAME "${KB_TEST_CLI_QUICK}" COMMAND cli_quick_smoke_test)
set_tests_properties("${KB_TEST_CLI_QUICK}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

if(TARGET kano::kano_backlog_webview_core)
    add_executable(webview_service_smoke_test
        webview_service_smoke_test.cpp
    )
    target_compile_features(webview_service_smoke_test PRIVATE cxx_std_23)
    target_link_libraries(webview_service_smoke_test PRIVATE
        kano::kano_backlog_webview_core
        kano::backlog_core
        kano::backlog_ops
    )
    set(KB_TEST_WEBVIEW_SERVICE "[bdd][feature:cli-workflow][scenario:KOB-BDD-WEBVIEW-001] Backlog webview service exposes reviewable project data")
    add_test(NAME "${KB_TEST_WEBVIEW_SERVICE}" COMMAND webview_service_smoke_test)
    set_tests_properties("${KB_TEST_WEBVIEW_SERVICE}" PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
    )
endif()
