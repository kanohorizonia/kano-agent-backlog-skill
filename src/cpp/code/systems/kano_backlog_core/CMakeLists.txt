add_library(kano_backlog_core STATIC)

target_sources(kano_backlog_core
  PRIVATE
    version/public/version.hpp
    version/private/version.cpp
    models/public/kano/backlog_core/models/models.hpp
    models/public/kano/backlog_core/models/errors.hpp
    models/private/models.cpp
    models/private/errors.cpp
    config/public/kano/backlog_core/config/config.hpp
    config/private/config.cpp
    frontmatter/public/kano/backlog_core/frontmatter/frontmatter.hpp
    frontmatter/private/frontmatter.cpp
    frontmatter/public/kano/backlog_core/frontmatter/canonical_store.hpp
    frontmatter/private/canonical_store.cpp
    state/public/kano/backlog_core/state/state_machine.hpp
    state/private/state_machine.cpp
    validation/public/kano/backlog_core/validation/validator.hpp
    validation/private/validator.cpp
    refs/public/kano/backlog_core/refs/ref_parser.hpp
    refs/private/ref_parser.cpp
    refs/public/kano/backlog_core/refs/ref_resolver.hpp
    refs/private/ref_resolver.cpp
)

add_library(kano::backlog_core ALIAS kano_backlog_core)

# C++23 standard (matching kano-git convention)
target_compile_features(kano_backlog_core PUBLIC cxx_std_23)
set_target_properties(kano_backlog_core PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# CLI11 v2.4 uses <codecvt> which is removed in MSVC 19.50 (VS 2026)
target_compile_definitions(kano_backlog_core PRIVATE CLI11_HAS_CODECVT=0)

target_include_directories(kano_backlog_core
    PUBLIC
      version/public
      models/public
      config/public
      frontmatter/public
      state/public
      validation/public
      refs/public
    PRIVATE
      version/private
      models/private
      config/private
      frontmatter/private
      state/private
      validation/private
      refs/private
)

target_link_libraries(kano_backlog_core PUBLIC
    KanoInfra::config
    KanoInfra::build_info
    yaml-cpp
)

if(MSVC AND TARGET CLI11::CLI11)
    get_target_property(_cli11_inc CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)
    if(_cli11_inc)
        target_include_directories(kano_backlog_core PUBLIC "${_cli11_inc}")
    endif()
endif()
