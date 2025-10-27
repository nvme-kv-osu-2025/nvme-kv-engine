cmake_minimum_required(VERSION 3.10)
project(nvme-kv-engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g -O2")

# Find dependencies
find_package(Threads REQUIRED)

# Samsung OpenMPDK paths (adjust as needed)
set(KVSSD_ROOT "${CMAKE_SOURCE_DIR}/../KVSSD" CACHE PATH "Path to KVSSD repository")
set(KVSSD_INCLUDE "${KVSSD_ROOT}/PDK/core/include")
set(KVSSD_LIB "${KVSSD_ROOT}/PDK/core")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core
    ${KVSSD_INCLUDE}
)

# Source files
set(ENGINE_SOURCES
    src/core/kv_engine.c
    src/utils/memory_pool.c
    src/utils/thread_pool.c
    src/async/async_ops.c
)

# Build shared library
add_library(nvme_kv_engine SHARED ${ENGINE_SOURCES})
target_link_libraries(nvme_kv_engine
    Threads::Threads
    ${KVSSD_LIB}/libkvapi.so
)

# Build static library
add_library(nvme_kv_engine_static STATIC ${ENGINE_SOURCES})
target_link_libraries(nvme_kv_engine_static
    Threads::Threads
    ${KVSSD_LIB}/libkvapi.so
)

# Examples
add_subdirectory(examples)

# Tests (optional)
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Benchmarks (optional)
option(BUILD_BENCHMARKS "Build benchmark programs" ON)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Install targets
install(TARGETS nvme_kv_engine nvme_kv_engine_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES include/kv_engine.h
    DESTINATION include
)
