# Benchmark programs

add_executable(bench_throughput bench_throughput.c)
target_link_libraries(bench_throughput nvme_kv_engine)

# TODO: Add comparison benchmarks with RocksDB, LevelDB, Redis
