#include "chunk.h"

#include "memory.h"
#include "vm.h"
#include <stdlib.h>

void chunk_init(struct chunk *chunk)
{
    chunk->size = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->line_count = 0;
    chunk->line_capacity = 0;
    value_array_init(&chunk->constants);
}

void chunk_write(struct chunk *chunk, u8 byte, size_t line)
{
    if (chunk->capacity < (chunk->size + 1)) {
        const size_t old_capacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(old_capacity);
        chunk->code =
            GROW_ARRAY(u8, chunk->code, old_capacity, chunk->capacity);
    }
    chunk->code[chunk->size] = byte;
    chunk->size++;

    if (chunk->line_count > 0 &&
        chunk->lines[chunk->line_count - 1].line == line)
        return;

    if (chunk->line_capacity < chunk->line_count + 1) {
        size_t old_capacity = chunk->line_capacity;
        chunk->line_capacity = GROW_CAPACITY(old_capacity);
        chunk->lines = GROW_ARRAY(struct line_start, chunk->lines, old_capacity,
                                  chunk->line_capacity);
    }

    struct line_start *line_start = &chunk->lines[chunk->line_count++];
    line_start->offset = chunk->size - 1;
    line_start->line = line;
}

void chunk_free(struct chunk *chunk)
{
    FREE_ARRAY(u8, chunk->code, chunk->capacity);
    FREE_ARRAY(struct line_start, chunk->lines, chunk->line_capacity);
    value_array_free(&chunk->constants);
    chunk_init(chunk);
}

size_t chunk_getline(const struct chunk *chunk, size_t instruction)
{
    size_t start = 0;
    size_t end = chunk->line_count - 1;

    for (;;) {
        size_t mid = (start + end) / 2;
        struct line_start *line = &chunk->lines[mid];

        if (instruction < line->offset) {
            end = mid - 1;
        } else if (mid == chunk->line_count - 1 ||
                   instruction < chunk->lines[mid + 1].offset) {
            return line->line;
        } else {
            start = mid + 1;
        }
    }
}

size_t chunk_add_constant(struct chunk *chunk, value_t v)
{
    push(v);
    value_array_write(&chunk->constants, v);
    pop();
    return chunk->constants.count - 1;
}
