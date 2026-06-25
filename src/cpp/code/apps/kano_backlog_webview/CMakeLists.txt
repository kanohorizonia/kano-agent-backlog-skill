add_executable(kano_backlog_webview main.cpp)

target_sources(kano_backlog_webview
  PRIVATE
    assets/backboard_app_js.hpp
    assets/backboard_css.hpp
    assets/index_html.hpp
    assets/kob_ui_js.hpp
)

target_link_libraries(kano_backlog_webview
  PRIVATE
    kano::backlog_core
    kano::kano_backlog_webview_core
)

set_target_properties(kano_backlog_webview PROPERTIES
  OUTPUT_NAME kano_backlog_webview
)
