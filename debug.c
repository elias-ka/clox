#include <stdio.h>

#include "debug.h"

void disassemble_chunk(struct chunk *chunk, const char *name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->size;) {
    offset = disassemble_instruction(chunk, offset);
  }
}

static int simple_instruction(const char *name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

int disassemble_instruction(struct chunk *chunk, int offset) {
  printf("%04d ", offset);

  const uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_RETURN: {
      return simple_instruction("OP_RETURN", offset);
    }
    default: {
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
    }
  }
}

