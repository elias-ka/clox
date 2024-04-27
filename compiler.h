#ifndef CLOX__COMPILER_H_
#define CLOX__COMPILER_H_

#include "object.h"

struct obj_function *compile(const char *source);
void mark_compiler_roots(void);

#endif // CLOX__COMPILER_H_
