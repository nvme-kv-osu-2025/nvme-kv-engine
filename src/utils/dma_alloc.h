/**
 * DMA-aligned memory allocation
 *
 * provides 4096-byte aligned memory allocations
 * needed for NVMe DMA operations.
 */

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include <stddef.h>

// alignment required for nvme DMA buffers (4KB)
#define DMA_ALIGNMENT 4096

/**
 * allocate DMA-aligned memory
 * @param size Number of bytes to allocate
 * @return Pointer to aligned memory, or NULL on failure
 */
void* dma_alloc(size_t size);

/**
 * free DMA-aligned memory
 * @param ptr Pointer from dma_alloc (NULL is safe)
 */
void dma_free(void* ptr);

#endif /* DMA_ALLOC_H */
