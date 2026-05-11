# Benchmark utilities library
add_library(bench_utils STATIC
    util/bench_utils.c
    util/bench_utils.h
)
target_link_libraries(bench_utils nvme_kv_engine)
target_include_directories(bench_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Benchmark programs
add_executable(bench_throughput bench_throughput.c)
target_link_libraries(bench_throughput nvme_kv_engine bench_utils)

add_executable(test_memory_pool test_memory_pool.c)
target_link_libraries(test_memory_pool nvme_kv_engine bench_utils)

add_executable(bench_dma_pool bench_dma_pool.c)
target_link_libraries(bench_dma_pool nvme_kv_engine bench_utils)
target_include_directories(bench_dma_pool PRIVATE ${CMAKE_SOURCE_DIR}/src/utils)

# Capstone-grade KV stress benchmark (multi-threaded, configurable workloads)
add_executable(bench_kv_stress bench_kv_stress.c)
target_link_libraries(bench_kv_stress nvme_kv_engine bench_utils pthread)

# Max key capacity benchmark (key/value size sweep)
add_executable(bench_kv_capacity bench_kv_capacity.c)
target_link_libraries(bench_kv_capacity nvme_kv_engine bench_utils pthread)

# TODO: Add comparison benchmarks with RocksDB, LevelDB, Redis
