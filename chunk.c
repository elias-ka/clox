#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

void chunk_init(struct chunk *chunk) {
  chunk->size = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
}

void chunk_write(struct chunk *chunk, uint8_t byte) {
  if (chunk->capacity < chunk->size + 1) {
    const int old_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_capacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
                             old_capacity, chunk->capacity);
  }
  chunk->code[chunk->size] = byte;
  chunk->size++;
}

void chunk_free(struct chunk *chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  chunk_init(chunk);
}
