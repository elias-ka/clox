#include "chunk.h"

#include <stdlib.h>

#include "memutil.h"

void chunk_init(struct chunk *chunk)
{
        chunk->size = 0;
        chunk->capacity = 0;
        chunk->code = NULL;
        chunk->lines = NULL;
        value_array_init(&chunk->constants);
}

void chunk_write(struct chunk *chunk, uint8_t byte, int line)
{
        if (chunk->capacity < chunk->size + 1) {
                const int old_capacity = chunk->capacity;
                chunk->capacity = GROW_CAPACITY(old_capacity);
                chunk->code = GROW_ARRAY(uint8_t, chunk->code, old_capacity,
                                         chunk->capacity);
                chunk->lines = GROW_ARRAY(int, chunk->lines, old_capacity,
                                          chunk->capacity);
        }
        chunk->code[chunk->size] = byte;
        chunk->lines[chunk->size] = line;
        chunk->size++;
}

void chunk_free(struct chunk *chunk)
{
        FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
        FREE_ARRAY(int, chunk->lines, chunk->capacity);
        value_array_free(&chunk->constants);
        chunk_init(chunk);
}

size_t chunk_add_constant(struct chunk *chunk, struct value v)
{
        value_array_write(&chunk->constants, v);
        return chunk->constants.count - 1;
}
