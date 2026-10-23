cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(IveyBot VERSION 1.0.0)

# Explicit list: stray/old files left in the repo (e.g. src/render/IveyMenu.cpp) are ignored.
set(SOURCES
    src/main.cpp
    src/bot/Assist.cpp
    src/bot/BotCore.cpp
    src/bot/Import.cpp
    src/bot/Macro.cpp
    src/bot/Trajectory.cpp
    src/clickbot/Clickbot.cpp
    src/render/Renderer.cpp
    src/ui/IveyMenu.cpp
    src/ui/ThemeFx.cpp
)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Define the GEODE_SDK environment variable.")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# The prebuilt Codegen is the newest one; it must match the pinned bindings, so build it from them.
set(SKIP_BUILDING_CODEGEN OFF CACHE BOOL "" FORCE)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
