add_executable(kano_backlog_cli
    main.cpp
)

set_target_properties(kano_backlog_cli PROPERTIES OUTPUT_NAME kano-backlog)

target_link_libraries(kano_backlog_cli PRIVATE kano::backlog_core kano::backlog_ops CLI11::CLI11 ${_jsoncpp_target})

# CLI11 v2.4 uses <codecvt> which is removed in MSVC 19.50 (VS 2026)
target_compile_definitions(kano_backlog_cli PRIVATE CLI11_HAS_CODECVT=0)

if(MSVC)
    target_compile_options(kano_backlog_cli PRIVATE /bigobj)
endif()

# Workaround for MSVC SYSTEM include bug (<string> missing)
if(MSVC AND TARGET CLI11::CLI11)
    get_target_property(_cli11_inc CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)
    if(_cli11_inc)
        target_include_directories(kano_backlog_cli PRIVATE "${_cli11_inc}")
    endif()
endif()

target_include_directories(kano_backlog_cli PRIVATE ${_jsoncpp_include_dir})
