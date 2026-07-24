cmake_minimum_required(VERSION 3.24)

project(RushGrid VERSION 0.1.0 LANGUAGES CXX)

option(RUSHGRID_BUILD_TESTS "Build the RushGrid test executable" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(rushgrid_core
    src/jobs/thread_pool.cpp
    src/navigation/navigation.cpp
    src/simulation/agent_storage.cpp
    src/simulation/simulation.cpp
    src/spatial/spatial_grid.cpp
    src/telemetry/statistics.cpp
    src/visualization/debug_image.cpp
)

target_include_directories(rushgrid_core PUBLIC include)
target_link_libraries(rushgrid_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(rushgrid_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rushgrid_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(rushgrid_demo src/app/demo_main.cpp)
target_link_libraries(rushgrid_demo PRIVATE rushgrid_core)

add_executable(rushgrid_benchmark src/app/benchmark_main.cpp)
target_link_libraries(rushgrid_benchmark PRIVATE rushgrid_core)

if(WIN32)
    add_executable(rushgrid_app WIN32 src/app/window_main.cpp)
    target_link_libraries(rushgrid_app PRIVATE rushgrid_core user32 gdi32)
endif()

if(RUSHGRID_BUILD_TESTS)
    enable_testing()
    add_executable(rushgrid_tests tests/rushgrid_tests.cpp)
    target_link_libraries(rushgrid_tests PRIVATE rushgrid_core)
    add_test(NAME rushgrid_tests COMMAND rushgrid_tests)
endif()
