#include "vm.h"

#include "chunk.h"
#include "compiler.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include "memutil.h"
#include "object.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct vm vm;

static void reset_stack(void)
{
        vm.stack_top = vm.stack;
        vm.frame_count = 0;
}

static void runtime_error(const char *format, ...)
{
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fputs("\n", stderr);

        for (size_t i = vm.frame_count - 1; i >= 0; i--) {
                const struct call_frame *frame = &vm.frames[i];
                const struct obj_function *fn = frame->fn;
                const size_t instruction = frame->ip - fn->chunk.code - 1;
                fprintf(stderr, "[line %d] in ", fn->chunk.lines[instruction]);

                if (fn->name) {
                        fprintf(stderr, "%s()\n", fn->name->chars);
                } else {
                        fprintf(stderr, "script\n");
                }
        }

        reset_stack();
}

void vm_init(void)
{
        reset_stack();
        vm.objects = NULL;
        table_init(&vm.globals);
        table_init(&vm.strings);
}

void vm_free(void)
{
        free_objects();
        table_free(&vm.globals);
        table_free(&vm.strings);
}

void push(struct value v)
{
        *vm.stack_top = v;
        vm.stack_top++;
}

struct value pop(void)
{
        vm.stack_top--;
        return *vm.stack_top;
}

static struct value peek(int distance)
{
        return vm.stack_top[-1 - distance];
}

static bool call(struct obj_function *fn, int n_args)
{
        if (n_args != fn->arity) {
                runtime_error("Expected %d arguments but got %d.", fn->arity,
                              n_args);
                return false;
        }

        if (vm.frame_count == FRAMES_MAX) {
                runtime_error("Stack overflow.");
                return false;
        }

        struct call_frame *frame = &vm.frames[vm.frame_count++];
        frame->fn = fn;
        frame->ip = fn->chunk.code;
        frame->slots = vm.stack_top - n_args - 1;
        return true;
}

static bool call_value(struct value callee, int n_args)
{
        if (IS_OBJ(callee)) {
                switch (OBJ_TYPE(callee)) {
                case OBJ_FUNCTION:
                        return call(AS_FUNCTION(callee), n_args);
                default:
                        // Non-callable object type.
                        break;
                }
        }
        runtime_error("Can only call functions and classes.");
        return false;
}

static bool is_falsey(struct value v)
{
        return IS_NIL(v) || (IS_BOOL(v) && (!AS_BOOL(v)));
}

static void concatenate(void)
{
        const struct obj_string *b = AS_STRING(pop());
        const struct obj_string *a = AS_STRING(pop());

        const size_t length = a->length + b->length;
        char *chars = ALLOCATE(char, length + 1);
        memcpy(chars, a->chars, a->length);
        memcpy(chars + a->length, b->chars, b->length);
        chars[length] = '\0';

        struct obj_string *result = take_string(chars, length);
        push(OBJ_VAL(result));
}

static enum interpret_result run(void)
{
        struct call_frame *frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_CONSTANT() (frame->fn->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() \
        (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(value_type, op)                                   \
        do {                                                        \
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {   \
                        runtime_error("Operands must be numbers."); \
                        return INTERPRET_RUNTIME_ERROR;             \
                }                                                   \
                double b = AS_NUMBER(pop());                        \
                double a = AS_NUMBER(pop());                        \
                push(value_type(a op b));                           \
        } while (false)

        for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
                printf("          ");
                for (const struct value *slot = vm.stack; slot < vm.stack_top;
                     slot++) {
                        printf("[ ");
                        value_print(*slot);
                        printf(" ]");
                }
                printf("\n");
                disassemble_instruction(
                        &frame->fn->chunk,
                        (int)(frame->ip - frame->fn->chunk.code));
#endif
                const uint8_t instruction = READ_BYTE();
                switch (instruction) {
                case OP_CONSTANT: {
                        const struct value constant = READ_CONSTANT();
                        push(constant);
                        break;
                }
                case OP_NIL: {
                        push(NIL_VAL);
                        break;
                }
                case OP_TRUE: {
                        push(BOOL_VAL(true));
                        break;
                }
                case OP_FALSE: {
                        push(BOOL_VAL(false));
                        break;
                }
                case OP_POP: {
                        pop();
                        break;
                }
                case OP_SET_LOCAL: {
                        uint8_t slot = READ_BYTE();
                        frame->slots[slot] = peek(0);
                        break;
                }
                case OP_GET_LOCAL: {
                        uint8_t slot = READ_BYTE();
                        push(frame->slots[slot]);
                        break;
                }
                case OP_GET_GLOBAL: {
                        struct obj_string *name = READ_STRING();
                        struct value value;
                        if (!table_get(&vm.globals, name, &value)) {
                                runtime_error("Undefined variable '%s'.",
                                              name->chars);
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        push(value);
                        break;
                }
                case OP_DEFINE_GLOBAL: {
                        struct obj_string *name = READ_STRING();
                        table_set(&vm.globals, name, peek(0));
                        pop();
                        break;
                }
                case OP_SET_GLOBAL: {
                        struct obj_string *name = READ_STRING();
                        if (table_set(&vm.globals, name, peek(0))) {
                                table_delete(&vm.globals, name);
                                runtime_error("Undefined variable '%s'.",
                                              name->chars);
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        break;
                }
                case OP_EQUAL: {
                        const struct value b = pop();
                        const struct value a = pop();
                        push(BOOL_VAL(values_equal(a, b)));
                        break;
                }
                case OP_GREATER: {
                        BINARY_OP(BOOL_VAL, >);
                        break;
                }
                case OP_LESS: {
                        BINARY_OP(BOOL_VAL, <);
                        break;
                }
                case OP_ADD: {
                        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                                concatenate();
                        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                                double b = AS_NUMBER(pop());
                                double a = AS_NUMBER(pop());
                                push(NUMBER_VAL(a + b));
                        } else {
                                runtime_error(
                                        "Operands must be two numbers or two strings.");
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        break;
                }
                case OP_SUBTRACT: {
                        BINARY_OP(NUMBER_VAL, -);
                        break;
                }
                case OP_MULTIPLY: {
                        BINARY_OP(NUMBER_VAL, *);
                        break;
                }
                case OP_DIVIDE: {
                        BINARY_OP(NUMBER_VAL, /);
                        break;
                }
                case OP_NOT: {
                        push(BOOL_VAL(is_falsey(pop())));
                        break;
                }
                case OP_NEGATE: {
                        if (!IS_NUMBER(peek(0))) {
                                runtime_error("Operand must be a number.");
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        push(NUMBER_VAL(-AS_NUMBER(pop())));
                        break;
                }
                case OP_PRINT: {
                        value_print(pop());
                        printf("\n");
                        break;
                }
                case OP_JUMP: {
                        uint16_t offset = READ_SHORT();
                        frame->ip += offset;
                        break;
                }
                case OP_JUMP_IF_FALSE: {
                        uint16_t offset = READ_SHORT();
                        if (is_falsey(peek(0))) {
                                frame->ip += offset;
                        }
                        break;
                }
                case OP_LOOP: {
                        uint16_t offset = READ_SHORT();
                        frame->ip -= offset;
                        break;
                }
                case OP_CALL: {
                        int n_args = READ_BYTE();
                        if (!call_value(peek(n_args), n_args)) {
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        frame = &vm.frames[vm.frame_count - 1];
                        break;
                }
                case OP_RETURN: {
                        struct value result = pop();
                        vm.frame_count--;
                        if (vm.frame_count == 0) {
                                pop();
                                return INTERPRET_OK;
                        }

                        vm.stack_top = frame->slots;
                        push(result);
                        frame = &vm.frames[vm.frame_count - 1];
                        break;
                }
                default:
                        runtime_error("Unknown opcode %d\n", instruction);
                        return INTERPRET_RUNTIME_ERROR;
                }
        }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

enum interpret_result vm_interpret(const char *source)
{
        struct obj_function *fn = compile(source);
        if (!fn)
                return INTERPRET_COMPILE_ERROR;

        push(OBJ_VAL(fn));
        call(fn, 0);

        return run();
}
