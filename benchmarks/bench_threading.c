/**
 * Basic Threading Test Suite for KV Engine
 * Tests thread pool initialization and basic thread operations
 */

#include "kv_engine_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

/* ============================================================================
 * Test Configuration
 * ============================================================================ */

#define NUM_THREADS 8
#define NUM_SIMPLE_TASKS 100

/* Test results */
static int tests_passed = 0;
static int tests_failed = 0;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void test_pass(const char *message) {
    printf("  ✓ %s\n", message);
    tests_passed++;
}

static void test_fail(const char *message) {
    printf("  ✗ %s\n", message);
    tests_failed++;
}

static void test_assert(int condition, const char *message) {
    if (condition) {
        test_pass(message);
    } else {
        test_fail(message);
    }
}

/* ============================================================================
 * Test 1: Thread Pool Creation and Destruction
 * ============================================================================ */

static void test_thread_pool_lifecycle(void) {
    printf("\n[Test 1] Thread Pool Lifecycle\n");
    printf("================================\n");
    
    /* Test creation with different sizes */
    thread_pool_t *pool1 = thread_pool_create(1);
    test_assert(pool1 != NULL, "Create thread pool with 1 thread");
    if (pool1) {
        test_assert(pool1->total_threads == 1, "Pool has correct thread count (1)");
        thread_pool_destroy(pool1);
        test_pass("Destroy thread pool with 1 thread");
    }
    
    thread_pool_t *pool4 = thread_pool_create(4);
    test_assert(pool4 != NULL, "Create thread pool with 4 threads");
    if (pool4) {
        test_assert(pool4->total_threads == 4, "Pool has correct thread count (4)");
        thread_pool_destroy(pool4);
        test_pass("Destroy thread pool with 4 threads");
    }
    
    thread_pool_t *pool16 = thread_pool_create(16);
    test_assert(pool16 != NULL, "Create thread pool with 16 threads");
    if (pool16) {
        test_assert(pool16->total_threads == 16, "Pool has correct thread count (16)");
        thread_pool_destroy(pool16);
        test_pass("Destroy thread pool with 16 threads");
    }
    
    printf("\n");
}

/* ============================================================================
 * Test 2: Memory Pool Thread Safety
 * ============================================================================ */

typedef struct {
    memory_pool_t *pool;
    int thread_id;
    int num_allocs;
    int successful_allocs;
    int failed_allocs;
} mem_pool_test_args_t;

static void *memory_pool_thread_test(void *arg) {
    mem_pool_test_args_t *args = (mem_pool_test_args_t *)arg;
    
    for (int i = 0; i < args->num_allocs; i++) {
        size_t size = 64 + (rand() % 192); /* Random size 64-256 bytes */
        void *ptr = memory_pool_alloc(args->pool, size);
        
        if (ptr) {
            args->successful_allocs++;
            /* Write thread_id to verify no corruption */
            memset(ptr, args->thread_id & 0xFF, size);
        } else {
            args->failed_allocs++;
        }
        
        /* Small delay to increase chance of race conditions */
        if (i % 10 == 0) {
            usleep(1);
        }
    }
    
    return NULL;
}

static void test_memory_pool_threading(void) {
    printf("[Test 2] Memory Pool Thread Safety\n");
    printf("===================================\n");
    
    /* Create a decent-sized pool */
    size_t pool_size = 512 * 1024; /* 512KB */
    memory_pool_t *pool = memory_pool_create(pool_size);
    test_assert(pool != NULL, "Memory pool created");
    
    if (!pool) {
        printf("\n");
        return;
    }
    
    pthread_t threads[NUM_THREADS];
    mem_pool_test_args_t args[NUM_THREADS];
    
    /* Launch threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].pool = pool;
        args[i].thread_id = i;
        args[i].num_allocs = 50;
        args[i].successful_allocs = 0;
        args[i].failed_allocs = 0;
        
        int ret = pthread_create(&threads[i], NULL, memory_pool_thread_test, &args[i]);
        if (ret != 0) {
            test_fail("Failed to create thread");
            memory_pool_destroy(pool);
            return;
        }
    }
    
    /* Wait for all threads to complete */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Check results */
    int total_successful = 0;
    int total_failed = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_successful += args[i].successful_allocs;
        total_failed += args[i].failed_allocs;
    }
    
    printf("  Allocations: %d successful, %d failed\n", 
           total_successful, total_failed);
    test_assert(total_successful > 0, "Some allocations succeeded");
    test_assert(pool->used <= pool->size, "Pool didn't overflow");
    
    memory_pool_destroy(pool);
    test_pass("Memory pool destroyed cleanly");
    
    printf("\n");
}

/* ============================================================================
 * Test 3: Basic Thread Pool Task Submission
 * ============================================================================ */

typedef struct {
    int task_id;
    int *counter;
    pthread_mutex_t *lock;
    useconds_t delay_us;
} simple_task_t;

static void *simple_worker_task(void *arg) {
    simple_task_t *task = (simple_task_t *)arg;
    
    /* Simulate some work */
    if (task->delay_us > 0) {
        usleep(task->delay_us);
    }
    
    /* Update shared counter */
    pthread_mutex_lock(task->lock);
    (*task->counter)++;
    pthread_mutex_unlock(task->lock);
    
    free(task);
    return NULL;
}

static void test_thread_pool_task_submission(void) {
    printf("[Test 3] Thread Pool Task Submission\n");
    printf("=====================================\n");
    
    thread_pool_t *pool = thread_pool_create(4);
    test_assert(pool != NULL, "Thread pool created");
    
    if (!pool) {
        printf("\n");
        return;
    }
    
    int counter = 0;
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    
    /* Submit a batch of tasks */
    int num_tasks = 20;
    for (int i = 0; i < num_tasks; i++) {
        simple_task_t *task = malloc(sizeof(simple_task_t));
        if (!task) {
            test_fail("Failed to allocate task");
            continue;
        }
        
        task->task_id = i;
        task->counter = &counter;
        task->lock = &lock;
        task->delay_us = 1000; /* 1ms delay */
        
        int ret = thread_pool_submit(pool, simple_worker_task, task);
        test_assert(ret == 0, "Task submitted successfully");
    }
    
    /* Wait for tasks to complete */
    sleep(2);
    
    printf("  Tasks completed: %d/%d\n", counter, num_tasks);
    test_assert(counter == num_tasks, "All tasks executed");
    
    thread_pool_destroy(pool);
    pthread_mutex_destroy(&lock);
    test_pass("Thread pool destroyed cleanly");
    
    printf("\n");
}

/* ============================================================================
 * Test 4: Engine Initialization with Threading
 * ============================================================================ */

static void test_engine_init_with_threads(void) {
    printf("[Test 4] Engine Initialization with Threading\n");
    printf("==============================================\n");
    
    kv_engine_t *engine = NULL;
    kv_engine_config_t config = {
        .device_path = "/dev/kvemul",
        .emul_config_file = NULL,
        .num_worker_threads = 4,
        .memory_pool_size = 1024 * 1024, /* 1MB */
    };
    
    kv_result_t result = kv_engine_init(&engine, &config);
    
    /* Note: This will likely fail if device doesn't exist, which is OK for testing */
    if (result == KV_SUCCESS && engine != NULL) {
        test_pass("Engine initialized successfully");
        
        if (engine->workers) {
            test_assert(engine->workers->total_threads == 4, 
                       "Worker threads initialized");
        } else {
            test_fail("Worker threads not initialized");
        }
        
        if (engine->mem_pool) {
            test_pass("Memory pool initialized");
        } else {
            test_fail("Memory pool not initialized");
        }
        
        kv_engine_cleanup(engine);
        test_pass("Engine cleaned up successfully");
    } else {
        printf("  ℹ Engine init failed (expected if device unavailable)\n");
        printf("  ℹ Result code: %d\n", result);
    }
    
    printf("\n");
}

/* ============================================================================
 * Test 5: Statistics Thread Safety
 * ============================================================================ */

typedef struct {
    kv_engine_t *engine;
    int num_updates;
} stats_test_args_t;

static void *stats_updater_thread(void *arg) {
    stats_test_args_t *args = (stats_test_args_t *)arg;
    
    for (int i = 0; i < args->num_updates; i++) {
        /* Simulate different types of operations */
        update_stats(args->engine, 1, 0, 0, 1, 100);  
        update_stats(args->engine, 0, 1, 0, 1, 200);
        update_stats(args->engine, 0, 0, 1, 1, 0);

        if (i % 10 == 0) {
            update_stats(args->engine, 1, 0, 0, 0, 0);
        }
    }
    
    return NULL;
}

static void test_statistics_thread_safety(void) {
    printf("[Test 5] Statistics Thread Safety\n");
    printf("==================================\n");
    
    /* Create a minimal engine structure for testing */
    kv_engine_t test_engine;
    memset(&test_engine, 0, sizeof(kv_engine_t));
    pthread_mutex_init(&test_engine.stats_lock, NULL);
    memset(&test_engine.stats, 0, sizeof(kv_engine_stats_t));
    
    pthread_t threads[NUM_THREADS];
    stats_test_args_t args[NUM_THREADS];
    
    int updates_per_thread = 1000;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].engine = &test_engine;
        args[i].num_updates = updates_per_thread;
        pthread_create(&threads[i], NULL, stats_updater_thread, &args[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Verify results */
    uint64_t expected_reads = NUM_THREADS * updates_per_thread;
    uint64_t expected_writes = NUM_THREADS * updates_per_thread;
    uint64_t expected_deletes = NUM_THREADS * updates_per_thread;
    uint64_t expected_total = expected_reads + expected_writes + expected_deletes;
    
    printf("  Total ops: %lu (expected: %lu)\n", 
           test_engine.stats.total_ops, expected_total);
    printf("  Read ops: %lu (expected: %lu)\n", 
           test_engine.stats.read_ops, expected_reads);
    printf("  Write ops: %lu (expected: %lu)\n", 
           test_engine.stats.write_ops, expected_writes);
    printf("  Delete ops: %lu (expected: %lu)\n", 
           test_engine.stats.delete_ops, expected_deletes);
    
    test_assert(test_engine.stats.total_ops == expected_total, 
               "Total ops count is accurate");
    test_assert(test_engine.stats.read_ops == expected_reads, 
               "Read ops count is accurate");
    test_assert(test_engine.stats.write_ops == expected_writes, 
               "Write ops count is accurate");
    test_assert(test_engine.stats.delete_ops == expected_deletes, 
               "Delete ops count is accurate");
    test_assert(test_engine.stats.bytes_read == expected_reads * 100, 
               "Bytes read is accurate");
    test_assert(test_engine.stats.bytes_written == expected_writes * 200, 
               "Bytes written is accurate");
    
    pthread_mutex_destroy(&test_engine.stats_lock);
    
    printf("\n");
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║  KV Engine Basic Threading Tests         ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    
    srand(time(NULL));
    
    test_thread_pool_lifecycle();
    // test_memory_pool_threading();
    test_thread_pool_task_submission();
    test_engine_init_with_threads();
    test_statistics_thread_safety();
    
    /* Print summary */
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║  Test Summary                             ║\n");
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║  Passed: %-4d                            ║\n", tests_passed);
    printf("║  Failed: %-4d                            ║\n", tests_failed);
    printf("╠═══════════════════════════════════════════╣\n");
    
    if (tests_failed == 0) {
        printf("║  Status: ✓ ALL TESTS PASSED              ║\n");
    } else {
        printf("║  Status: ✗ SOME TESTS FAILED             ║\n");
    }
    
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");
    
    return (tests_failed == 0) ? 0 : 1;
}