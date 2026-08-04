// Memory pool operations
#pragma once

#include <stddef.h>

typedef struct Pool Pool;

/** Initialize a memory pool pre-allocated with nblocks of size block_size */
Pool* pool_create(size_t block_size, size_t nblocks);

/** Free memory from a memory pool */
void  pool_destroy(Pool* pool);

/** Get a block of memory from a pool */
void* pool_alloc(Pool* pool); 

/** Return block back to pool */
void pool_free(Pool* pool, void* block);
