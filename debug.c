#include "debug.h"

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "value.h"

void disassemble_chunk(const struct chunk *chunk, const char *name)
{
        printf("== %s ==\n", name);

        for (size_t offset = 0; offset < chunk->size;
             offset = disassemble_instruction(chunk, offset)) {
                // empty loop body
        }
}

static size_t constant_instruction(const char *name, const struct chunk *chunk,
                                   size_t offset)
{
        const u8 constant = chunk->code[offset + 1];
        printf("%-16s %4d '", name, constant);
        value_print(chunk->constants.values[constant]);
        printf("'\n");
        return offset + 2;
}

static size_t simple_instruction(const char *name, size_t offset)
{
        printf("%s\n", name);
        return offset + 1;
}

static size_t byte_instruction(const char *name, const struct chunk *chunk,
                               size_t offset)
{
        const u8 slot = chunk->code[offset + 1];
        printf("%-16s %4d\n", name, slot);
        return offset + 2;
}

static size_t jump_instruction(const char *name, i32 sign,
                               const struct chunk *chunk, size_t offset)
{
        u16 jump = (u16)(chunk->code[offset + 1] << 8);
        jump |= chunk->code[offset + 2];
        printf("%-16s %4zu -> %zu\n", name, offset,
               offset + 3 + (size_t)sign * jump);
        return offset + 3;
}

size_t disassemble_instruction(const struct chunk *chunk, size_t offset)
{
        printf("%04zu ", offset);
        if ((offset > 0) &&
            (chunk->lines[offset] == chunk->lines[offset - 1])) {
                printf("   | ");
        } else {
                printf("%4zu ", chunk->lines[offset]);
        }

        const u8 instruction = chunk->code[offset];
        switch (instruction) {
        case OP_CONSTANT:
                return constant_instruction("OP_CONSTANT", chunk, offset);
        case OP_NIL:
                return simple_instruction("OP_NIL", offset);
        case OP_TRUE:
                return simple_instruction("OP_TRUE", offset);
        case OP_FALSE:
                return simple_instruction("OP_FALSE", offset);
        case OP_POP:
                return simple_instruction("OP_POP", offset);
        case OP_GET_LOCAL:
                return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:
                return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_GET_GLOBAL:
                return constant_instruction("OP_GET_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL:
                return constant_instruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
                return constant_instruction("OP_SET_GLOBAL", chunk, offset);
        case OP_GET_UPVALUE:
                return byte_instruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_UPVALUE:
                return byte_instruction("OP_SET_UPVALUE", chunk, offset);
        case OP_EQUAL:
                return simple_instruction("OP_EQUAL", offset);
        case OP_GREATER:
                return simple_instruction("OP_GREATER", offset);
        case OP_LESS:
                return simple_instruction("OP_LESS", offset);
        case OP_ADD:
                return simple_instruction("OP_ADD", offset);
        case OP_SUBTRACT:
                return simple_instruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
                return simple_instruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
                return simple_instruction("OP_DIVIDE", offset);
        case OP_NOT:
                return simple_instruction("OP_NOT", offset);
        case OP_NEGATE:
                return simple_instruction("OP_NEGATE", offset);
        case OP_PRINT:
                return simple_instruction("OP_PRINT", offset);
        case OP_JUMP:
                return jump_instruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
                return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:
                return jump_instruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
                return byte_instruction("OP_CALL", chunk, offset);
        case OP_CLOSURE: {
                offset++;
                const u8 constant = chunk->code[offset++];
                printf("%-16s %4d ", "OP_CLOSURE", constant);
                value_print(chunk->constants.values[constant]);
                printf("\n");

                const struct obj_function *fn =
                        AS_FUNCTION(chunk->constants.values[constant]);

                for (i32 i = 0; i < fn->upvalue_count; i++) {
                        const u8 is_local = chunk->code[offset++];
                        const u8 index = chunk->code[offset++];
                        printf("%04zu      |                     %s %d\n",
                               offset - 2, is_local ? "local" : "upvalue",
                               index);
                }
                return offset;
        }
        case OP_CLOSE_UPVALUE:
                return simple_instruction("OP_CLOSE_UPVALUE", offset);
        case OP_RETURN:
                return simple_instruction("OP_RETURN", offset);
        default:
                printf("<Unknown opcode %d>\n", instruction);
                return offset + 1;
        }
}
