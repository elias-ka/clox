#include "vm.h"

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "table.h"
#include "value.h"
#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif
#include "memory.h"
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
    vm.open_upvalues = NULL;
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
        const size_t instruction = (size_t)(frame->ip - fn->chunk.code - 1);
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
    vm.bytes_allocated = 0;
    vm.next_gc = 1024 * 1024;
    vm.gray_count = 0;
    vm.gray_capacity = 0;
    vm.gray_stack = NULL;
    table_init(&vm.globals);
    table_init(&vm.strings);

    vm.init_string = NULL;
    vm.init_string = copy_string("init", 4);

    define_native("clock", clock_native);
}

void vm_free(void)
{
    free_objects();
    table_free(&vm.globals);
    table_free(&vm.strings);
    vm.init_string = NULL;
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
        runtime_error("Expected %d arguments but got %d.", closure->fn->arity,
                      n_args);
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
        case OBJ_BOUND_METHOD: {
            const struct obj_bound_method *bound = AS_BOUND_METHOD(callee);
            vm.stack_top[-n_args - 1] = bound->receiver;
            return call(bound->method, n_args);
        }
        case OBJ_CLASS: {
            struct obj_class *klass = AS_CLASS(callee);
            vm.stack_top[-n_args - 1] = OBJ_VAL(new_instance(klass));
            struct value initializer;
            if (table_get(&klass->methods, vm.init_string, &initializer)) {
                return call(AS_CLOSURE(initializer), n_args);
            } else if (n_args != 0) {
                runtime_error("Expected 0 arguments but got %d.", n_args);
                return false;
            }
            return true;
        }
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

static bool invoke_from_class(struct obj_class *klass, struct obj_string *name,
                              i32 n_args)
{
    struct value method;
    if (!table_get(&klass->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }

    return call(AS_CLOSURE(method), n_args);
}

static bool invoke(struct obj_string *name, i32 n_args)
{
    struct value receiver = peek(n_args);
    if (!IS_INSTANCE(receiver)) {
        runtime_error("Only instances have methods.");
        return false;
    }

    struct obj_instance *instance = AS_INSTANCE(receiver);

    struct value value;
    if (table_get(&instance->fields, name, &value)) {
        vm.stack_top[-n_args - 1] = value;
        return call_value(value, n_args);
    }

    return invoke_from_class(instance->klass, name, n_args);
}

static bool bind_method(struct obj_class *klass, struct obj_string *name)
{
    struct value method;
    if (!table_get(&klass->methods, name, &method)) {
        runtime_error("Undefined property '%s'.", name->chars);
        return false;
    }

    struct obj_bound_method *bound =
        new_bound_method(peek(0), AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
}

static struct obj_upvalue *capture_upvalue(struct value *local)
{
    struct obj_upvalue *prev_upvalue = NULL;
    struct obj_upvalue *upvalue = vm.open_upvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prev_upvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    struct obj_upvalue *created_upvalue = new_upvalue(local);
    created_upvalue->next = upvalue;

    if (prev_upvalue == NULL) {
        vm.open_upvalues = created_upvalue;
    } else {
        prev_upvalue->next = created_upvalue;
    }

    return created_upvalue;
}

static void close_upvalues(const struct value *last)
{
    while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
        struct obj_upvalue *upvalue = vm.open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.open_upvalues = upvalue->next;
    }
}

static void define_method(struct obj_string *name)
{
    struct value method = peek(0);
    struct obj_class *klass = AS_CLASS(peek(1));
    table_set(&klass->methods, name, method);
    pop();
}

static bool is_falsey(struct value v)
{
    return IS_NIL(v) || (IS_BOOL(v) && (!AS_BOOL(v)));
}

static void concatenate(void)
{
    const struct obj_string *b = AS_STRING(peek(0));
    const struct obj_string *a = AS_STRING(peek(1));

    const size_t length = a->length + b->length;
    char *chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    struct obj_string *result = take_string(chars, length);
    pop();
    pop();
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
#define BINARY_OP(value_type, op)                         \
    do {                                                  \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtime_error("Operands must be numbers.");   \
            return INTERPRET_RUNTIME_ERROR;               \
        }                                                 \
        f64 b = AS_NUMBER(pop());                         \
        f64 a = AS_NUMBER(pop());                         \
        push(value_type(a op b));                         \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (const struct value *slot = vm.stack; slot < vm.stack_top; slot++) {
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
                runtime_error("Undefined variable '%s'.", name->chars);
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
                runtime_error("Undefined variable '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        case OP_GET_UPVALUE: {
            const u8 slot = READ_BYTE();
            push(*frame->closure->upvalues[slot]->location);
            break;
        }
        case OP_SET_UPVALUE: {
            const u8 slot = READ_BYTE();
            *frame->closure->upvalues[slot]->location = peek(0);
            break;
        }
        case OP_GET_PROPERTY: {
            if (!IS_INSTANCE(peek(0))) {
                runtime_error("Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            const struct obj_instance *instance = AS_INSTANCE(peek(0));
            struct obj_string *name = READ_STRING();

            struct value value;
            if (table_get(&instance->fields, name, &value)) {
                pop(); // Instance.
                push(value);
                break;
            }

            if (!bind_method(instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }

            break;
        }
        case OP_SET_PROPERTY: {
            if (!IS_INSTANCE(peek(1))) {
                runtime_error("Only instances have fields.");
                return INTERPRET_RUNTIME_ERROR;
            }

            struct obj_instance *instance = AS_INSTANCE(peek(1));
            table_set(&instance->fields, READ_STRING(), peek(0));
            const struct value value = pop();
            pop();
            push(value);
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
                runtime_error("Operands must be two numbers or two strings.");
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
        case OP_INVOKE: {
            struct obj_string *method = READ_STRING();
            const u8 n_args = READ_BYTE();
            if (!invoke(method, n_args)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm.frames[vm.frame_count - 1];
            break;
        }
        case OP_CLOSURE: {
            struct obj_function *fn = AS_FUNCTION(READ_CONSTANT());
            const struct obj_closure *closure = new_closure(fn);
            push(OBJ_VAL(closure));
            for (i32 i = 0; i < closure->upvalue_count; i++) {
                const u8 is_local = READ_BYTE();
                const u8 index = READ_BYTE();
                if (is_local) {
                    closure->upvalues[i] =
                        capture_upvalue(frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            break;
        }
        case OP_CLOSE_UPVALUE: {
            close_upvalues(vm.stack_top - 1);
            pop();
            break;
        }
        case OP_RETURN: {
            const struct value result = pop();
            close_upvalues(frame->slots);
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
        case OP_CLASS: {
            push(OBJ_VAL(new_class(READ_STRING())));
            break;
        }
        case OP_METHOD: {
            define_method(READ_STRING());
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
