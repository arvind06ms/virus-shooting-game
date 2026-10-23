cmake_minimum_required(VERSION 3.20)
project(zombie_waves_game LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib)

add_executable(zombie_waves_game
  src/main.cpp
)

target_link_libraries(zombie_waves_game PRIVATE raylib)

if (WIN32)
  target_compile_definitions(zombie_waves_game PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
