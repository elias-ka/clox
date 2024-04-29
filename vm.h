#ifndef CLOX__VM_H_
#define CLOX__VM_H_

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

struct call_frame {
    struct obj_closure *closure;
    u8 *ip;
    value_ty *slots;
};

struct vm {
    struct call_frame frames[FRAMES_MAX];
    size_t frame_count;

    value_ty stack[STACK_MAX];
    value_ty *stack_top;
    struct table globals;
    struct table strings;
    struct obj_string *init_string;
    struct obj_upvalue *open_upvalues;

    size_t bytes_allocated;
    size_t next_gc;

    struct obj *objects;
    size_t gray_count;
    size_t gray_capacity;
    struct obj **gray_stack;
};

enum interpret_result {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
};

extern struct vm vm;

void vm_init(void);
void vm_free(void);
void push(value_ty v);
value_ty pop(void);
enum interpret_result vm_interpret(const char *source);

#endif // CLOX__VM_H_
