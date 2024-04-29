#ifndef CLOX__CHUNK_H_
#define CLOX__CHUNK_H_

#include "common.h"
#include "value.h"

enum op_code {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
};

struct line_start {
    size_t offset;
    size_t line;
};

struct chunk {
    size_t size;
    size_t capacity;
    u8 *code;
    struct value_array constants;
    struct line_start *lines;
    size_t line_count;
    size_t line_capacity;
};

void chunk_init(struct chunk *chunk);
void chunk_write(struct chunk *chunk, u8 byte, size_t line);
void chunk_free(struct chunk *chunk);
size_t chunk_getline(const struct chunk *chunk, size_t instruction);

/**
 * Add a constant to the chunk.
 * @return The index of the added constant in the constants array.
 */
size_t chunk_add_constant(struct chunk *chunk, struct value v);

#endif // CLOX__CHUNK_H_
