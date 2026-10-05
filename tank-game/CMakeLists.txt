cmake_minimum_required(VERSION 3.20)
project(SteelCurtain LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Visual Studio: link the C++ runtime statically (game and raylib alike) so the .exe runs on
# PCs without the Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# raylib is downloaded automatically. To build offline, point CMake at a local copy:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

add_executable(steel_curtain
  src/main.cpp
  src/game.cpp
  src/terrain.cpp
  src/vehicles.cpp
  src/sprites.cpp
  src/audio.cpp)
target_link_libraries(steel_curtain PRIVATE raylib)

if(MSVC)
  target_compile_options(steel_curtain PRIVATE /W4 /utf-8)
else()
  target_compile_options(steel_curtain PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(WIN32)
  # A normal Windows app: no console window behind the game.
  set_target_properties(steel_curtain PROPERTIES WIN32_EXECUTABLE ON)
  if(MSVC)
    target_link_options(steel_curtain PRIVATE /ENTRY:mainCRTStartup)
  else()
    # MinGW: ship a single self-contained .exe.
    target_link_options(steel_curtain PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
