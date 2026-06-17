cmake_minimum_required(VERSION 3.20)

project(YoutubeDownloader
    VERSION 0.1.0
    DESCRIPTION "Portable Win32 YouTube downloader"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)
endif()

set(CORE_SOURCES
    src/AppPaths.cpp
    src/Config.cpp
    src/ToolManagers.cpp
    src/Version.cpp
    src/YtDlpClient.cpp
)

set(CORE_HEADERS
    src/AppPaths.h
    src/Config.h
    src/ToolManagers.h
    src/Version.h
    src/YtDlpClient.h
)

add_library(YoutubeDownloaderCore STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(YoutubeDownloaderCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(YoutubeDownloaderCore PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(YoutubeDownloader WIN32
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/DialogWindows.cpp
    src/DialogWindows.h
    src/resource.h
    src/resource.rc
    src/UiRenderer.cpp
    src/UiRenderer.h
)

target_link_libraries(YoutubeDownloader PRIVATE
    YoutubeDownloaderCore
    comctl32
    dwmapi
    gdiplus
    gdi32
    user32
    shell32
)

if(MSVC)
    target_compile_options(YoutubeDownloader PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(YoutubeDownloader PROPERTIES
    OUTPUT_NAME "YoutubeDownloader"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(YoutubeDownloaderTests
    tests/test_core.cpp
)

target_link_libraries(YoutubeDownloaderTests PRIVATE YoutubeDownloaderCore)

if(MSVC)
    target_compile_options(YoutubeDownloaderTests PRIVATE /W4 /permissive- /utf-8)
endif()

enable_testing()
add_test(NAME YoutubeDownloaderCoreTests COMMAND YoutubeDownloaderTests)
