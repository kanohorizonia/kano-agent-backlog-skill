add_executable(kano_backlog_webview main.cpp)

target_link_libraries(kano_backlog_webview
  PRIVATE
    kano::backlog_core
    kano::kano_backlog_webview_core
)

set_target_properties(kano_backlog_webview PROPERTIES
  OUTPUT_NAME kano_backlog_webview
)
