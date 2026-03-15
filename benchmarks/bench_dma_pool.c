/**
 * DMA Buffer Pool Benchmark
 *
 * Compares raw DMA allocation (posix_memalign via dma_alloc) against
 * pool acquire/release to demonstrate the slab allocator's performance
 * benefit on the hot store/retrieve buffer allocation path.
 */

#include "dma_alloc.h"
#include "dma_pool.h"
#include "util/bench_utils.h"
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_NUM_OPS 100000
#define POOL_BUFFER_SIZE (2 * 1024 * 1024) // 2MB to match retrieve buffer
#define POOL_COUNT 16

static void print_results(const char *label, int num_ops, double elapsed_sec) {
  double ops_per_sec = num_ops / elapsed_sec;
  double latency_ns = (elapsed_sec * 1e9) / num_ops;
  printf("  %-26s  ops/sec: %10.0f   latency: %.1f ns\n", label, ops_per_sec,
         latency_ns);
}

static void bench_raw_alloc(int num_ops) {
  printf("\n[BEFORE] Raw dma_alloc / dma_free (%d ops)\n", num_ops);

  double start = get_time_seconds();
  for (int i = 0; i < num_ops; i++) {
    void *buf = dma_alloc(POOL_BUFFER_SIZE);
    dma_free(buf);
  }
  double elapsed = get_time_seconds() - start;

  print_results("dma_alloc + dma_free", num_ops, elapsed);
}

static void bench_pool_alloc(int num_ops) {
  printf("\n[AFTER] dma_pool_acquire / dma_pool_release (%d ops)\n", num_ops);

  dma_pool_t *pool = dma_pool_create(POOL_BUFFER_SIZE, POOL_COUNT);
  if (!pool) {
    fprintf(stderr, "Failed to create DMA pool\n");
    return;
  }

  double start = get_time_seconds();
  for (int i = 0; i < num_ops; i++) {
    void *buf = dma_pool_acquire(pool);
    if (!buf) {
      fprintf(stderr, "Pool exhausted at op %d\n", i);
      break;
    }
    dma_pool_release(pool, buf);
  }
  double elapsed = get_time_seconds() - start;

  print_results("pool acquire + release", num_ops, elapsed);
  dma_pool_destroy(pool);
}

int main(int argc, char **argv) {
  int num_ops = DEFAULT_NUM_OPS;
  if (argc >= 2) {
    num_ops = atoi(argv[1]);
    if (num_ops <= 0) {
      fprintf(stderr, "Invalid num_ops: %s\n", argv[1]);
      return 1;
    }
  }

  printf("=== DMA Buffer Pool Benchmark ===\n");
  printf("Buffer size: %d MB | Pool count: %d\n",
         POOL_BUFFER_SIZE / (1024 * 1024), POOL_COUNT);

  bench_raw_alloc(num_ops);
  bench_pool_alloc(num_ops);

  printf("\nDone.\n");
  return 0;
}
