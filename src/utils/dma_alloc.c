/**
* DMA-Aligned Memory Allocation
*/

#include "dma_alloc.h"
#include <stdlib.h>

void* dma_alloc(size_t size) {
  if (size == 0) {
    // cannot pass size 0 to posix_memalign()
    return NULL;
  }

  void* ptr = NULL;
  int result = posix_memalign(&ptr, DMA_ALIGNMENT, size);

  if (result != 0) {
    // posix_memalign() does not set errno
    return NULL;
  }

  return ptr;
}

void dma_free(void* ptr) {
  free(ptr);
}


