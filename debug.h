#ifndef CLOX__DEBUG_H_
#define CLOX__DEBUG_H_

#include "chunk.h"

void disassemble_chunk(const struct chunk *chunk, const char *name);
size_t disassemble_instruction(const struct chunk *chunk, size_t offset);

#endif // CLOX__DEBUG_H_
