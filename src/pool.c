#include "../include/pool.h"

#include <stdbool.h>
#include <stdlib.h>

struct Block
{
    struct Block* next;
};

struct Chunk
{
    struct Chunk* next;
    void*         buffer;
};

typedef struct Pool
{
    size_t        block_size;
    size_t        nblocks;
    struct Chunk* chunks;
    struct Block* free;
} Pool;

static struct Chunk* chunk_create(size_t block_size, size_t nblocks);
static void          chunk_destroy(struct Chunk* chunk);
static bool          expand_pool(Pool* pool);

Pool* pool_create(size_t block_size, size_t nblocks)
{
    Pool* pool = malloc(sizeof(Pool));
    if (!pool)
    {
	    return NULL;
    }
    if (nblocks == 0)
    {
        free(pool);
        return NULL;
    }
    if (block_size < sizeof(struct Block))
    {
        block_size = sizeof(struct Block);
    }
    struct Chunk* chunk = chunk_create(block_size, nblocks);
    if (!chunk)
    {
        free(pool);
        return NULL;
    }

    pool->free       = (struct Block*)chunk->buffer;
    pool->block_size = block_size;
    pool->nblocks    = nblocks;
    pool->chunks     = chunk;

    return pool;
}
    
void pool_destroy(Pool* pool)
{
    if (!pool)
    {
        return;
    }
    struct Chunk* chunk = pool->chunks;
    while (chunk)
    {
        struct Chunk* next = chunk->next;
        chunk_destroy(chunk);
        chunk = next;
    }
    free(pool);
}

void* pool_alloc(Pool* pool)
{
    if (!pool)
    {
        return NULL;
    }
    if (!pool->free)
    {
        if (!expand_pool(pool))
        {
            return NULL;
        }
    }
    struct Block* block = pool->free;
    pool->free = block->next;

    return (char*)block + sizeof(struct Block);
}

void pool_free(Pool* pool, void* ptr)
{
    if (!pool || !ptr)
    {
	    return;
    }

    struct Block* block = (struct Block*)((char*)ptr - sizeof(struct Block));
    block->next = pool->free;
    pool->free = block;
}

static struct Chunk* chunk_create(size_t block_size, size_t nblocks)
{
    struct Chunk* chunk = malloc(sizeof (struct Chunk));
    if (!chunk)
    {
        return NULL;
    }
    chunk->buffer = malloc((sizeof(struct Block) + block_size) * nblocks);
    if (!chunk->buffer)
    {
        free(chunk);
        return NULL;
    }

    struct Block* block = (struct Block*)chunk->buffer;
    for (size_t i = 0; i < nblocks-1; i++)
    {
        struct Block* next = (struct Block*)((char*)block +
                                              sizeof(struct Block) +
                                              block_size);
        block->next = next;
        block = next;
    }
    block->next = NULL;
    chunk->next = NULL;

    return chunk;
}

static void chunk_destroy(struct Chunk* chunk)
{
    free(chunk->buffer);
    free(chunk);
}

static bool expand_pool(Pool* pool)
{
    // Just linearly expand the memory size. I might change this to exponential
    // growth if it becomes warranted.
    struct Chunk* newchunk = chunk_create(pool->block_size, pool->nblocks);
    if (!newchunk)
    {
        return false;
    }
    newchunk->next = pool->chunks;
    pool->chunks = newchunk;

    // Get last block in new chunk
    struct Block* lastblock = (struct Block*)(
        (char*)newchunk->buffer + 
        (sizeof(struct Block) + pool->block_size) * (pool->nblocks - 1)
    );
    
    lastblock->next = pool->free;
    pool->free = (struct Block*)newchunk->buffer;

    return true;
}