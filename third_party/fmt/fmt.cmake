include(FetchContent)

set(URL "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.1.tar.gz")
set(HASH "SHA256=78b8c0a72b1c35e4443a7e308df52498252d1cefc2b08c9a97bc9ee6cfe61f8b")

# CMake ≥ 3.24 使用新特性，旧版本跳过
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    FetchContent_Declare(fmt
            URL ${URL}
            URL_HASH ${HASH}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
else ()
    FetchContent_Declare(fmt
            URL ${URL}
            URL_HASH ${HASH}
    )
endif ()

unset(URL)
unset(HASH)

# 设置fmt构建选项
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(fmt)

# 设置目标属性
if (TARGET fmt)
    set_target_properties(fmt PROPERTIES
            FOLDER "third_party"
            POSITION_INDEPENDENT_CODE ON
    )
endif ()
