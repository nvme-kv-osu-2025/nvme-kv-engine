/**
 * DMA-aligned memory allocation
 *
 * provides 4096-byte aligned memory allocations
 * needed for NVMe DMA operations.
 *
 * NOTE: we won't see any performance gains with this
 * until we move onto testing with the actual device.
 * When using the emulator, we only see the cost of
 * aligning the buffers to 4096-bytes and not any of
 * the benefits of replacing malloc with posix_memalign()
 */

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include <stddef.h>

/* alignment required for nvme DMA buffers (4KB) */
#define DMA_ALIGNMENT 4096

/* check if low 12 bits are zero to make sure buffer is 4096-byte aligned */
#define IS_DMA_ALIGNED(ptr) (((uintptr_t)(ptr) & (DMA_ALIGNMENT - 1)) == 0)

/**
 * allocate DMA-aligned memory
 * @param size Number of bytes to allocate
 * @return Pointer to aligned memory, or NULL on failure
 */
void *dma_alloc(size_t size);

/**
 * free DMA-aligned memory
 * @param ptr Pointer from dma_alloc (NULL is safe)
 */
void dma_free(void *ptr);

#endif /* DMA_ALLOC_H */
