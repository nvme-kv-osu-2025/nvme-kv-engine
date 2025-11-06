cmake_minimum_required(VERSION 3.10)
project(nvme_kv_engine C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# KVSSD library should be built separately first
# See README for build instructions

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src/core)
include_directories(${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/include)
include_directories(${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/src/api/include)

# Source files for the library
set(LIBRARY_SOURCES
    src/core/kv_engine.c
    src/utils/memory_pool.c
    src/utils/thread_pool.c
    src/async/async_ops.c
)

# Add KVSSD library directories
link_directories(${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/build)
link_directories(${CMAKE_SOURCE_DIR}/lib/KVSSD/PDK/core/build/deps)

# Build shared library
add_library(nvme_kv_engine SHARED ${LIBRARY_SOURCES})
target_link_libraries(nvme_kv_engine pthread kvapi)

# Build static library
add_library(nvme_kv_engine_static STATIC ${LIBRARY_SOURCES})
target_link_libraries(nvme_kv_engine_static pthread kvapi)

# Set output directory
set_target_properties(nvme_kv_engine nvme_kv_engine_static
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add subdirectories
add_subdirectory(examples)
add_subdirectory(benchmarks)
