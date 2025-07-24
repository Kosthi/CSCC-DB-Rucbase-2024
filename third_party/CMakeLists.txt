ADD_SUBDIRECTORY(googletest)

# 设置FetchContent策略
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# 包含各个库的配置文件
include(fmt/fmt.cmake)
include(spdlog/spdlog.cmake)

# 创建统一的接口库
#add_library(third_party INTERFACE)
#
## 链接所有启用的库
#if (TARGET fmt::fmt)
#    target_link_libraries(third_party INTERFACE fmt::fmt)
#endif ()
#
#if (TARGET spdlog::spdlog)
#    target_link_libraries(third_party INTERFACE spdlog::spdlog)
#endif ()
