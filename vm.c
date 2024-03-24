#include <stdio.h>
#include <stdarg.h>

#include "vm.h"
#include "debug.h"
#include "common.h"
#include "compiler.h"

static struct vm vm;

static void reset_stack(void)
{
        vm.stack_top = vm.stack;
}

static void runtime_error(const char *format, ...)
{
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fputs("\n", stderr);

        const size_t instruction = vm.ip - vm.chunk->code - 1;
        const int line = vm.chunk->lines[instruction];
        fprintf(stderr, "[line %d] in script\n", line);
        reset_stack();
}

void vm_init(void)
{
        reset_stack();
}

void vm_free(void)
{
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

static bool is_falsey(struct value v)
{
        return IS_NIL(v) || (IS_BOOL(v) && !AS_BOOL(v));
}

static enum interpret_result run(void)
{
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
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
                disassemble_instruction(vm.chunk,
                                        (int)(vm.ip - vm.chunk->code));
#endif
                uint8_t instruction;
                switch (instruction = READ_BYTE()) {
                case OP_CONSTANT: {
                        const struct value constant = READ_CONSTANT();
                        push(constant);
                        value_print(constant);
                        printf("\n");
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
                        BINARY_OP(NUMBER_VAL, +);
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
                case OP_RETURN: {
                        value_print(pop());
                        printf("\n");
                        return INTERPRET_OK;
                }
                }
        }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

enum interpret_result vm_interpret(const char *source)
{
        struct chunk chunk;
        chunk_init(&chunk);

        if (!compile(source, &chunk)) {
                chunk_free(&chunk);
                return INTERPRET_COMPILE_ERROR;
        }

        vm.chunk = &chunk;
        vm.ip = vm.chunk->code;
        const enum interpret_result result = run();
        chunk_free(&chunk);
        return result;
}
