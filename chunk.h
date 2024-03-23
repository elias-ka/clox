#ifndef CLOX__CHUNK_H_
#define CLOX__CHUNK_H_

#include "common.h"
#include "value.h"

enum op_code {
        OP_CONSTANT,
        OP_NEGATE,
        OP_RETURN,
};

struct chunk {
        int size;
        int capacity;
        uint8_t *code;
        // TODO: Storing the line number for each instruction is not really necessary
        //       and wastes some memory.
        int *lines;
        struct value_array constants;
};

void chunk_init(struct chunk *chunk);
void chunk_write(struct chunk *chunk, uint8_t byte, int line);
void chunk_free(struct chunk *chunk);

/**
 * Add a constant to the chunk.
 * @return The index of the added constant in the constants array.
 */
size_t chunk_add_constant(struct chunk *chunk, value v);

#endif // CLOX__CHUNK_H_
