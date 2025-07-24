include(FetchContent)

set(URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.12.0.tar.gz")
set(HASH "SHA256=4dccf2d10f410c1e2feaff89966bfc49a1abb29ef6f08246335b110e001e09a9")

# CMake ≥ 3.24 使用新特性，旧版本跳过
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    FetchContent_Declare(spdlog
            URL ${URL}
            URL_HASH ${HASH}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
else ()
    FetchContent_Declare(spdlog
            URL ${URL}
            URL_HASH ${HASH}
    )
endif ()

unset(URL)
unset(HASH)

# 设置spdlog构建选项
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)  # 使用外部fmt
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)

# 设置目标属性
if (TARGET spdlog)
    set_target_properties(spdlog PROPERTIES
            FOLDER "third_party"
            POSITION_INDEPENDENT_CODE ON
    )
endif ()
