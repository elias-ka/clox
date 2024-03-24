#ifndef CLOX__VM_H_
#define CLOX__VM_H_

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

struct vm {
        struct chunk *chunk;
        uint8_t *ip;
        struct value stack[STACK_MAX];
        struct value *stack_top;
};

enum interpret_result {
        INTERPRET_OK,
        INTERPRET_COMPILE_ERROR,
        INTERPRET_RUNTIME_ERROR,
};

void vm_init(void);
void vm_free(void);
enum interpret_result vm_interpret(const char *source);

#endif // CLOX__VM_H_
