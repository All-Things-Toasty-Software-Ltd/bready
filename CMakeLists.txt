cmake_minimum_required(VERSION 3.16)
project(bready VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch D++ (Discord++)
include(FetchContent)
FetchContent_Declare(
        dpp
        GIT_REPOSITORY https://github.com/brainboxdotcc/DPP.git
        GIT_TAG        v10.1.4
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dpp)

# Fetch nlohmann/json
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# libcurl (system)
find_package(CURL REQUIRED)

# Source files
set(SOURCES
        src/main.cc
)

add_executable(bready ${SOURCES})

target_include_directories(bready PRIVATE include)

target_link_libraries(bready PRIVATE dpp nlohmann_json::nlohmann_json
        CURL::libcurl)

# Compiler warnings
target_compile_options(bready PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        >
)