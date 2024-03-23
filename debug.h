#ifndef CLOX__DEBUG_H_
#define CLOX__DEBUG_H_

#include "chunk.h"

void disassemble_chunk(struct chunk *chunk, const char *name);
int disassemble_instruction(struct chunk *chunk, int offset);

#endif // CLOX__DEBUG_H_
