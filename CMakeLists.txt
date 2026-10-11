cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(MiniBrowser VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Microsoft WebView2 SDK (the Edge engine that ships with Windows 10/11), downloaded from NuGet at build time
include(FetchContent)
FetchContent_Declare(webview2
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
    DOWNLOAD_NAME webview2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webview2)
target_include_directories(${PROJECT_NAME} PRIVATE ${webview2_SOURCE_DIR}/build/native/include)
target_link_libraries(${PROJECT_NAME} ${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib opengl32 version)

setup_geode_mod(${PROJECT_NAME})
