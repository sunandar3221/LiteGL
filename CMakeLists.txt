cmake_minimum_required(VERSION 3.12)
project(LiteGL VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(LITEGL_BUILD_EXAMPLES "Build LiteGL examples and benchmarks" ON)
option(LITEGL_BUILD_TESTS "Build LiteGL unit tests" ON)

include_directories(include)

# LiteGL Library Sources
set(LITEGL_SOURCES
    src/litegl_core.c
    src/litegl_state.c
    src/litegl_buffer.c
    src/litegl_gl_loader.c
)

add_library(litegl STATIC ${LITEGL_SOURCES})
target_include_directories(litegl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compiler warnings and optimizations
if (MSVC)
    target_compile_options(litegl PRIVATE /W4 /O2)
else()
    target_compile_options(litegl PRIVATE -Wall -Wextra -O3)
endif()

# Unit Tests (Mock GL loader - runs anywhere headless!)
if (LITEGL_BUILD_TESTS)
    enable_testing()
    add_executable(test_state_cache tests/test_state_cache.c)
    target_link_libraries(test_state_cache PRIVATE litegl)
    add_test(NAME StateCacheTest COMMAND test_state_cache)
endif()

# Benchmark / Examples (Requires SDL2)
if (LITEGL_BUILD_EXAMPLES)
    find_package(SDL2 QUIET)
    if (NOT SDL2_FOUND)
        find_package(PkgConfig QUIET)
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(SDL2 QUIET sdl2)
        endif()
    endif()

    find_package(OpenGL QUIET)

    if (SDL2_FOUND)
        add_executable(bench_compare examples/bench_compare.c)
        target_link_libraries(bench_compare PRIVATE litegl)

        if (TARGET SDL2::SDL2)
            target_link_libraries(bench_compare PRIVATE SDL2::SDL2)
        elseif(SDL2_LIBRARIES)
            target_link_libraries(bench_compare PRIVATE ${SDL2_LIBRARIES})
            target_include_directories(bench_compare PRIVATE ${SDL2_INCLUDE_DIRS})
        else()
            target_link_libraries(bench_compare PRIVATE SDL2)
        endif()

        if (OPENGL_FOUND)
            target_link_libraries(bench_compare PRIVATE OpenGL::GL)
        endif()

        # Math library on Unix
        if (UNIX AND NOT APPLE)
            target_link_libraries(bench_compare PRIVATE m dl)
        endif()
    else()
        message(STATUS "SDL2 not found, skipping bench_compare executable")
    endif()
endif()
