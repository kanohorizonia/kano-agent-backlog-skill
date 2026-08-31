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

add_executable(metadata_index_smoke_test
    metadata_index_smoke_test.cpp
)
target_compile_features(metadata_index_smoke_test PRIVATE cxx_std_23)
target_link_libraries(metadata_index_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_METADATA_INDEX "[bdd][featured][feature:metadata-index][scenario:KOB-BDD-METADATA-INDEX-001] Derived metadata index remains disposable, invalidatable, and canonical-safe")
add_test(NAME "${KB_TEST_METADATA_INDEX}" COMMAND metadata_index_smoke_test)
set_tests_properties("${KB_TEST_METADATA_INDEX}" PROPERTIES
    TIMEOUT 90
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(change_probe_smoke_test
    change_probe_smoke_test.cpp
)
target_compile_features(change_probe_smoke_test PRIVATE cxx_std_23)
target_link_libraries(change_probe_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_CHANGE_PROBE "[bdd][featured][feature:metadata-index][scenario:KOB-BDD-CHANGE-PROBE-001] NTFS change proof filters unrelated writes and fails closed on invalid journal state")
add_test(NAME "${KB_TEST_CHANGE_PROBE}" COMMAND change_probe_smoke_test)
set_tests_properties("${KB_TEST_CHANGE_PROBE}" PROPERTIES
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

add_executable(migration_ops_smoke_test
    migration_ops_smoke_test.cpp
)
target_compile_features(migration_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(migration_ops_smoke_test PRIVATE kano::backlog_ops)
  set(KB_TEST_MIGRATION_OPS "[bdd][featured][feature:migration-ops][scenario:KOB-BDD-MIGRATION-001] Cross-product migration is deterministic atomic and recoverable")
add_test(NAME "${KB_TEST_MIGRATION_OPS}" COMMAND migration_ops_smoke_test)
set_tests_properties("${KB_TEST_MIGRATION_OPS}" PROPERTIES
    TIMEOUT 240
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(prefix_migration_ops_smoke_test
    prefix_migration_ops_smoke_test.cpp
)
target_compile_features(prefix_migration_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(prefix_migration_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_PREFIX_MIGRATION_OPS "[bdd][featured][feature:prefix-migration][scenario:KOB-BDD-PREFIX-MIGRATION-001] Product prefix migration is bounded identity-preserving and recoverable")
add_test(NAME "${KB_TEST_PREFIX_MIGRATION_OPS}" COMMAND prefix_migration_ops_smoke_test)
set_tests_properties("${KB_TEST_PREFIX_MIGRATION_OPS}" PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(product_relocation_ops_smoke_test
    product_relocation_ops_smoke_test.cpp
)
target_compile_features(product_relocation_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(product_relocation_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_PRODUCT_RELOCATION_OPS "[bdd][featured][feature:product-relocation][scenario:KOB-BDD-PRODUCT-RELOCATION-001] Product root relocation is bounded identity-preserving cross-volume-safe and recoverable")
add_test(NAME "${KB_TEST_PRODUCT_RELOCATION_OPS}" COMMAND product_relocation_ops_smoke_test)
set_tests_properties("${KB_TEST_PRODUCT_RELOCATION_OPS}" PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(product_registration_ops_smoke_test
    product_registration_ops_smoke_test.cpp
)
target_compile_features(product_registration_ops_smoke_test PRIVATE cxx_std_23)
target_link_libraries(product_registration_ops_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_PRODUCT_REGISTRATION_OPS "[bdd][featured][feature:product-registration][scenario:KOB-BDD-PRODUCT-REGISTRATION-001] Existing external product registration is config-only bounded and recoverable by exact apply")
add_test(NAME "${KB_TEST_PRODUCT_REGISTRATION_OPS}" COMMAND product_registration_ops_smoke_test)
set_tests_properties("${KB_TEST_PRODUCT_REGISTRATION_OPS}" PROPERTIES
    TIMEOUT 120
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

add_executable(list_compaction_smoke_test
    list_compaction_smoke_test.cpp
)
target_compile_features(list_compaction_smoke_test PRIVATE cxx_std_23)
target_compile_definitions(list_compaction_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
target_link_libraries(list_compaction_smoke_test PRIVATE kano::backlog_ops)
set(KB_TEST_LIST_COMPACTION "[bdd][featured][feature:list-compaction][scenario:KOB-BDD-LIST-COMPACTION-001] Large item lists compact old Done work and retain reversible retrieval")
add_test(NAME "${KB_TEST_LIST_COMPACTION}" COMMAND list_compaction_smoke_test)
set_tests_properties("${KB_TEST_LIST_COMPACTION}" PROPERTIES
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
target_link_libraries(cli_quick_smoke_test PRIVATE
    kano::backlog_core
    KanoInfra::process
    ${_jsoncpp_target}
)
target_include_directories(cli_quick_smoke_test PRIVATE ${_jsoncpp_include_dir})
set(KB_TEST_CLI_QUICK "[bdd][featured][feature:cli-workflow][scenario:KOB-BDD-CLI-QUICK-001] KOB CLI quick smoke covers bounded core commands")
add_test(NAME "${KB_TEST_CLI_QUICK}" COMMAND cli_quick_smoke_test)
set_tests_properties("${KB_TEST_CLI_QUICK}" PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1;GIT_TERMINAL_PROMPT=0"
)

add_executable(cli11_state_lifetime_smoke_test
    cli11_state_lifetime_smoke_test.cpp
)
target_compile_features(cli11_state_lifetime_smoke_test PRIVATE cxx_std_23)
target_compile_definitions(cli11_state_lifetime_smoke_test PRIVATE KANO_REPO_ROOT="${KB_REPO_ROOT}")
set(KB_TEST_CLI11_STATE_LIFETIME "[bdd][featured][feature:cli-workflow][scenario:KOB-BDD-CLI11-LIFETIME-001] CLI11 option bindings retain storage through parse and callback dispatch")
add_test(NAME "${KB_TEST_CLI11_STATE_LIFETIME}" COMMAND cli11_state_lifetime_smoke_test)
set_tests_properties("${KB_TEST_CLI11_STATE_LIFETIME}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1"
)

add_executable(infra_config_smoke_test
    infra_config_smoke_test.cpp
)
target_compile_features(infra_config_smoke_test PRIVATE cxx_std_23)
target_link_libraries(infra_config_smoke_test PRIVATE KanoInfra::config)
set(KB_TEST_INFRA_CONFIG "[bdd][featured][feature:infra-config][scenario:KOB-BDD-INFRA-CONFIG-001] Shared config safely loads empty arrays and quote-aware comments")
add_test(NAME "${KB_TEST_INFRA_CONFIG}" COMMAND infra_config_smoke_test)
set_tests_properties("${KB_TEST_INFRA_CONFIG}" PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "CI=true;KANO_BACKLOG_NONINTERACTIVE=1;KANO_TEST_NONINTERACTIVE=1"
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
