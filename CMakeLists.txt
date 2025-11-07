cmake_minimum_required(VERSION 3.10)
project(nvme_kv_engine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# Source files for the library
set(LIBRARY_SOURCES
    src/core/kv_engine.c
    src/utils/memory_pool.c
    src/utils/thread_pool.c
    src/async/async_ops.c
)

set(WITH_EMU ON CACHE BOOL "Build KVSSD with emulation")

add_subdirectory(lib/KVSSD/PDK/core KVSSD_BUILD)

add_library(nvme_kv_engine SHARED ${LIBRARY_SOURCES})

# Include directories
target_include_directories(nvme_kv_engine
    PUBLIC
    ${CMAKE_SOURCE_DIR}/build/KVSSD_BUILD/deps
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/include
    ${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/src/api/include
)

# Link to kvssd api
target_link_libraries(nvme_kv_engine pthread kvapi)

# Set output directory
set_target_properties(nvme_kv_engine
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add subdirectories
add_subdirectory(examples)
add_subdirectory(benchmarks)
