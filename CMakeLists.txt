cmake_minimum_required(VERSION 3.20)

file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_MAJOR_LINE REGEX "^#define YTD_APP_VERSION_MAJOR ")
file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_MINOR_LINE REGEX "^#define YTD_APP_VERSION_MINOR ")
file(STRINGS src/Core/AppVersion.h YTD_APP_VERSION_PATCH_LINE REGEX "^#define YTD_APP_VERSION_PATCH ")
string(REGEX REPLACE "^#define YTD_APP_VERSION_MAJOR ([0-9]+)$" "\\1" YTD_APP_VERSION_MAJOR "${YTD_APP_VERSION_MAJOR_LINE}")
string(REGEX REPLACE "^#define YTD_APP_VERSION_MINOR ([0-9]+)$" "\\1" YTD_APP_VERSION_MINOR "${YTD_APP_VERSION_MINOR_LINE}")
string(REGEX REPLACE "^#define YTD_APP_VERSION_PATCH ([0-9]+)$" "\\1" YTD_APP_VERSION_PATCH "${YTD_APP_VERSION_PATCH_LINE}")
set(YTD_APP_VERSION "${YTD_APP_VERSION_MAJOR}.${YTD_APP_VERSION_MINOR}.${YTD_APP_VERSION_PATCH}")

project(YoutubeDownloader
    VERSION ${YTD_APP_VERSION}
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
    src/Core/AppPaths.cpp
    src/Core/AppVersion.h
    src/Core/BackendText.cpp
    src/Core/Config.cpp
    src/Core/DownloadQueue.cpp
    src/Core/KeyboardShortcuts.cpp
    src/Core/Logger.cpp
    src/Core/ToolManagers.cpp
    src/Core/Version.cpp
    src/Core/YtDlpClient.cpp
    src/Platform/ProcessRunner.cpp
    src/Platform/WinHttpClient.cpp
)

set(CORE_HEADERS
    src/Core/AppPaths.h
    src/Core/AppVersion.h
    src/Core/BackendText.h
    src/Core/Config.h
    src/Core/DownloadQueue.h
    src/Core/KeyboardShortcuts.h
    src/Core/Logger.h
    src/Core/ToolManagers.h
    src/Core/Version.h
    src/Core/YtDlpClient.h
    src/Platform/ProcessRunner.h
    src/Platform/WinHttpClient.h
)

add_library(YoutubeDownloaderCore STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(YoutubeDownloaderCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Platform
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(YoutubeDownloaderCore PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(YoutubeDownloaderCore PUBLIC
    winhttp
)

add_executable(YoutubeDownloader WIN32
    src/App/main.cpp
    src/UI/Application.cpp
    src/UI/Application.h
    src/UI/DialogWindows.cpp
    src/UI/DialogWindows.h
    src/UI/resource.h
    src/UI/resource.rc
    src/UI/UiRenderer.cpp
    src/UI/UiRenderer.h
)

target_include_directories(YoutubeDownloader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UI
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
