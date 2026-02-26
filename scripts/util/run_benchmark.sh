source /user/scripts/setup_emulated_ssds.sh 4 /user && \
cd /user/build/benchmarks && \
./bench_throughput /dev/kvemul0
