#include "vm.h"

#include "common.h"
#include "compiler.h"
#include "memutil.h"
#include "object.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct vm vm;

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
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
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
                disassemble_instruction(vm.chunk,
                                        (int)(vm.ip - vm.chunk->code));
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
                case OP_RETURN: {
                        return INTERPRET_OK;
                }
                default:
                        runtime_error("Unknown opcode %d\n", instruction);
                        return INTERPRET_RUNTIME_ERROR;
                }
        }
#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
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
