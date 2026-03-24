add_library(kano_backlog_ops STATIC)

target_sources(kano_backlog_ops
  PRIVATE
    workitem/public/kano/backlog_ops/workitem/workitem_ops.hpp
    workitem/private/workitem_ops.cpp
    view/public/kano/backlog_ops/view/view_ops.hpp
    view/private/view_ops.cpp
    index/public/kano/backlog_ops/index/backlog_index.hpp
    index/private/backlog_index.cpp
    templates/public/kano/backlog_ops/templates/template_ops.hpp
    templates/private/template_ops.cpp
    topic/public/kano/backlog_ops/topic/topic_ops.hpp
    topic/private/topic_ops.cpp
    workset/public/kano/backlog_ops/workset/workset_ops.hpp
    workset/private/workset_ops.cpp
    orchestration/public/kano/backlog_ops/orchestration/orchestration_ops.hpp
    orchestration/private/orchestration_ops.cpp
    config/public/kano/backlog_ops/config/config_ops.hpp
    config/private/config_ops.cpp
    doctor/public/kano/backlog_ops/doctor/doctor_ops.hpp
    doctor/private/doctor_ops.cpp
)

add_library(kano::backlog_ops ALIAS kano_backlog_ops)

target_compile_features(kano_backlog_ops PUBLIC cxx_std_23)
set_target_properties(kano_backlog_ops PROPERTIES CXX_SCAN_FOR_MODULES OFF)

target_include_directories(kano_backlog_ops
    PUBLIC
      workitem/public
      view/public
      index/public
      templates/public
      topic/public
      workset/public
      orchestration/public
      config/public
      doctor/public
    PRIVATE
      workitem/private
      view/private
      index/private
      templates/private
      topic/private
      workset/private
      orchestration/private
      config/private
      doctor/private
)

target_link_libraries(kano_backlog_ops 
    PUBLIC 
      kano::backlog_core
      ${_sqlite3_target}
      ${_jsoncpp_target}
)

target_include_directories(kano_backlog_ops
    PRIVATE
      ${_jsoncpp_include_dir}
)
