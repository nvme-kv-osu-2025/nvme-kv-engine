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

add_executable(bench_threading bench_threading.c)

target_link_libraries(bench_threading nvme_kv_engine bench_utils)

# TODO: Add comparison benchmarks with RocksDB, LevelDB, Redis
