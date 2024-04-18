#ifndef CLOX__VM_H_
#define CLOX__VM_H_

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include <stddef.h>
#include <stdint.h>

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

struct call_frame {
        struct obj_function *fn;
        uint8_t *ip;
        struct value *slots;
};

struct vm {
        struct call_frame frames[FRAMES_MAX];
        size_t frame_count;

        struct value stack[STACK_MAX];
        struct value *stack_top;
        struct table globals;
        struct table strings;
        struct obj *objects;
};

enum interpret_result {
        INTERPRET_OK,
        INTERPRET_COMPILE_ERROR,
        INTERPRET_RUNTIME_ERROR,
};

extern struct vm vm;

void vm_init(void);
void vm_free(void);
enum interpret_result vm_interpret(const char *source);

#endif // CLOX__VM_H_
