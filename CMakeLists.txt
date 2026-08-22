cmake_minimum_required(VERSION 3.20)

project(plugin_sysinfo
    VERSION 0.1.0
    DESCRIPTION "Example plugin for reversal-suite-mcp"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# nlohmann/json — header-only, fetched at configure time. The public
# plugin ABI stays JSON-over-C-strings; nlohmann is just what this
# plugin picks to build / parse those strings. Any JSON library (or
# hand-written writer) works — see README.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(plugin_sysinfo SHARED
    src/plugin_sysinfo.cpp
)

target_include_directories(plugin_sysinfo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(plugin_sysinfo PRIVATE
    nlohmann_json::nlohmann_json
)

# The host looks for a DLL named plugin_sysinfo.dll on Windows.
set_target_properties(plugin_sysinfo PROPERTIES
    OUTPUT_NAME "plugin_sysinfo"
    PREFIX ""
)

if(WIN32)
    # Use the modern process-snapshot APIs from kernel32; no extra libs needed
    # beyond the default C++ runtime.
endif()
