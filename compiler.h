#ifndef CLOX__COMPILER_H_
#define CLOX__COMPILER_H_

#include "chunk.h"

bool compile(const char *source, struct chunk *chunk);

#endif // CLOX__COMPILER_H_
