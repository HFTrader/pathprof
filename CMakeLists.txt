cmake_minimum_required(VERSION 3.16)
project(pathprof CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(PATHPROF_TIMING "read rdtsc per gate + keep per-segment histograms" ON)
option(PATHPROF_BUILD_PLUGIN "build the Clang pass plugin (needs LLVM dev)" ON)
option(PATHPROF_BUILD_EXAMPLES "build the ITCH example (RAII + baseline)" ON)

# header-only runtime ------------------------------------------------------
add_library(pathprof INTERFACE)
target_include_directories(pathprof INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(PATHPROF_TIMING)
    target_compile_definitions(pathprof INTERFACE PATHPROF_TIMING)
endif()
add_library(pathprof::pathprof ALIAS pathprof)

# external shared-memory reader -------------------------------------------
add_executable(pathprof-top tools/pathprof-top/pathprof-top.cpp)
target_link_libraries(pathprof-top PRIVATE pathprof rt)

# Clang pass plugin (front-end 4) -----------------------------------------
if(PATHPROF_BUILD_PLUGIN)
    find_package(LLVM CONFIG)
    if(LLVM_FOUND)
        message(STATUS "pathprof: building pass plugin against LLVM ${LLVM_PACKAGE_VERSION}")
        add_library(PathProfPass MODULE plugin/PathProfPass.cpp)
        target_include_directories(PathProfPass SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
        target_compile_definitions(PathProfPass PRIVATE ${LLVM_DEFINITIONS})
        set_target_properties(PathProfPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
    else()
        message(STATUS "pathprof: LLVM not found, skipping pass plugin")
    endif()
endif()

# example (RAII + baseline; the cyg/xray/plugin builds need front-end-specific
# flags - see bench/bench.sh and test/verify.sh which drive the full matrix) --
if(PATHPROF_BUILD_EXAMPLES)
    add_executable(itch-raii     examples/itch-sim/main_raii.cpp)
    add_executable(itch-baseline examples/itch-sim/main_baseline.cpp)
    target_link_libraries(itch-raii     PRIVATE pathprof rt)
    target_link_libraries(itch-baseline PRIVATE pathprof rt)
endif()

enable_testing()
add_test(NAME verify   COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test/verify.sh 500000)
