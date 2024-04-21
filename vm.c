#include "vm.h"

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "table.h"
#include "value.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include "memutil.h"
#include "object.h"
#include <string.h>
#include <time.h>

struct vm vm;

static struct value clock_native(i32 n_args, struct value *args)
{
        (void)n_args;
        (void)args;
        return NUMBER_VAL((f64)clock() / CLOCKS_PER_SEC);
}

static void reset_stack(void)
{
        vm.stack_top = vm.stack;
        vm.frame_count = 0;
}

__attribute__((__format__(__printf__, 1, 0))) static void
runtime_error(const char *format, ...)
{
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fputs("\n", stderr);

        for (i32 i = (i32)vm.frame_count - 1; i >= 0; i--) {
                const struct call_frame *frame = &vm.frames[i];
                const struct obj_function *fn = frame->closure->fn;
                const size_t instruction =
                        (size_t)(frame->ip - fn->chunk.code - 1);
                fprintf(stderr, "[line %zu] in ", fn->chunk.lines[instruction]);

                if (fn->name) {
                        fprintf(stderr, "%s()\n", fn->name->chars);
                } else {
                        fprintf(stderr, "script\n");
                }
        }

        reset_stack();
}

void push(struct value v);
struct value pop(void);

static void define_native(const char *name, native_fn fn)
{
        push(OBJ_VAL(copy_string(name, strlen(name))));
        push(OBJ_VAL(new_native(fn)));
        table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
        pop();
        pop();
}

void vm_init(void)
{
        reset_stack();
        vm.objects = NULL;
        table_init(&vm.globals);
        table_init(&vm.strings);

        define_native("clock", clock_native);
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

static struct value peek(i32 distance)
{
        return vm.stack_top[-1 - distance];
}

static bool call(struct obj_closure *closure, i32 n_args)
{
        if (n_args != closure->fn->arity) {
                runtime_error("Expected %d arguments but got %d.",
                              closure->fn->arity, n_args);
                return false;
        }

        if (vm.frame_count == FRAMES_MAX) {
                runtime_error("Stack overflow.");
                return false;
        }

        struct call_frame *frame = &vm.frames[vm.frame_count++];
        frame->closure = closure;
        frame->ip = closure->fn->chunk.code;
        frame->slots = vm.stack_top - n_args - 1;
        return true;
}

static bool call_value(struct value callee, i32 n_args)
{
        if (IS_OBJ(callee)) {
                switch (OBJ_TYPE(callee)) {
                case OBJ_CLOSURE:
                        return call(AS_CLOSURE(callee), n_args);
                case OBJ_NATIVE: {
                        native_fn fn = AS_NATIVE(callee);
                        struct value result = fn(n_args, vm.stack_top - n_args);
                        vm.stack_top -= n_args + 1;
                        push(result);
                        return true;
                }
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
#define READ_CONSTANT() \
        (frame->closure->fn->chunk.constants.values[READ_BYTE()])
#define READ_SHORT() \
        (frame->ip += 2, (u16)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(value_type, op)                                   \
        do {                                                        \
                if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {   \
                        runtime_error("Operands must be numbers."); \
                        return INTERPRET_RUNTIME_ERROR;             \
                }                                                   \
                f64 b = AS_NUMBER(pop());                           \
                f64 a = AS_NUMBER(pop());                           \
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
                        &frame->closure->fn->chunk,
                        (size_t)(frame->ip - frame->closure->fn->chunk.code));
#endif
                const u8 instruction = READ_BYTE();
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
                        u8 slot = READ_BYTE();
                        frame->slots[slot] = peek(0);
                        break;
                }
                case OP_GET_LOCAL: {
                        u8 slot = READ_BYTE();
                        push(frame->slots[slot]);
                        break;
                }
                case OP_GET_GLOBAL: {
                        const struct obj_string *name = READ_STRING();
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
                                const f64 b = AS_NUMBER(pop());
                                const f64 a = AS_NUMBER(pop());
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
                        const u16 offset = READ_SHORT();
                        frame->ip += offset;
                        break;
                }
                case OP_JUMP_IF_FALSE: {
                        const u16 offset = READ_SHORT();
                        if (is_falsey(peek(0))) {
                                frame->ip += offset;
                        }
                        break;
                }
                case OP_LOOP: {
                        const u16 offset = READ_SHORT();
                        frame->ip -= offset;
                        break;
                }
                case OP_CALL: {
                        const i32 n_args = READ_BYTE();
                        if (!call_value(peek(n_args), n_args)) {
                                return INTERPRET_RUNTIME_ERROR;
                        }
                        frame = &vm.frames[vm.frame_count - 1];
                        break;
                }
                case OP_CLOSURE: {
                        struct obj_function *fn = AS_FUNCTION(READ_CONSTANT());
                        const struct obj_closure *closure = new_closure(fn);
                        push(OBJ_VAL(closure));
                        break;
                }
                case OP_RETURN: {
                        const struct value result = pop();
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
        if (fn == NULL)
                return INTERPRET_COMPILE_ERROR;

        push(OBJ_VAL(fn));

        struct obj_closure *closure = new_closure(fn);
        pop();
        push(OBJ_VAL(closure));
        call(closure, 0);

        return run();
}
