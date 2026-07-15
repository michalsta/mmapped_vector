cmake_minimum_required(VERSION 3.20)
project(mmapped_vector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── header-only library ───────────────────────────────────────────────────
add_library(mmapped_vector INTERFACE)
target_include_directories(mmapped_vector INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(mmapped_vector INTERFACE -Wall -Wextra)

# ── doctest ───────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

# ── correctness tests ─────────────────────────────────────────────────────
add_executable(correctness correctness.cpp)
target_link_libraries(correctness PRIVATE mmapped_vector doctest::doctest)
target_compile_definitions(correctness PRIVATE MMAPPED_VECTOR_TESTING)
target_compile_options(correctness PRIVATE
    $<$<CONFIG:Debug>:-Og -g -fsanitize=address -fno-omit-frame-pointer>
    $<$<CONFIG:Release>:-O2>
)
target_link_options(correctness PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

include(CTest)
add_test(NAME correctness COMMAND correctness)

# ── single-threaded performance benchmark ─────────────────────────────────
add_executable(performance performance.cpp)
target_link_libraries(performance PRIVATE mmapped_vector)
target_compile_options(performance PRIVATE -O3)

# ── multi-threaded performance benchmark (requires TBB) ───────────────────
find_package(TBB QUIET)
if(TBB_FOUND)
    add_executable(performance_threaded performance_threaded.cpp)
    target_link_libraries(performance_threaded PRIVATE mmapped_vector TBB::tbb)
    target_compile_options(performance_threaded PRIVATE -O3)
else()
    message(STATUS "TBB not found — skipping performance_threaded target")
endif()
