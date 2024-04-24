#include "object.h"

#include "memutil.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#include <stdio.h>
#include <string.h>

#define ALLOCATE_OBJ(type, obj_type) \
        (type *)allocate_object(sizeof(type), obj_type)

static struct obj *allocate_object(size_t size, enum obj_type type)
{
        struct obj *object = reallocate(NULL, 0, size);
        object->type = type;
        object->next = vm.objects;
        vm.objects = object;
        return object;
}

struct obj_closure *new_closure(struct obj_function *fn)
{
        struct obj_upvalue **upvalues =
                ALLOCATE(struct obj_upvalue *, (u64)fn->upvalue_count);

        for (i32 i = 0; i < fn->upvalue_count; i++) {
                upvalues[i] = NULL;
        }

        struct obj_closure *closure =
                ALLOCATE_OBJ(struct obj_closure, OBJ_CLOSURE);

        closure->fn = fn;
        closure->upvalues = upvalues;
        closure->upvalue_count = fn->upvalue_count;
        return closure;
}

struct obj_function *new_function()
{
        struct obj_function *fn =
                ALLOCATE_OBJ(struct obj_function, OBJ_FUNCTION);
        fn->arity = 0;
        fn->upvalue_count = 0;
        fn->name = NULL;
        chunk_init(&fn->chunk);
        return fn;
}

struct obj_native *new_native(native_fn fn)
{
        struct obj_native *native = ALLOCATE_OBJ(struct obj_native, OBJ_NATIVE);
        native->fn = fn;
        return native;
}

static struct obj_string *allocate_string(char *chars, size_t length, u32 hash)
{
        struct obj_string *string = ALLOCATE_OBJ(struct obj_string, OBJ_STRING);
        string->chars = chars;
        string->length = length;
        string->hash = hash;
        table_set(&vm.strings, string, NIL_VAL);
        return string;
}

static u32 hash_string(const char *key, size_t length)
{
        u32 hash = 2166136261u;
        for (size_t i = 0; i < length; i++) {
                hash ^= (u8)key[i];
                hash *= 16777619;
        }
        return hash;
}

struct obj_string *take_string(char *chars, size_t length)
{
        const u32 hash = hash_string(chars, length);
        struct obj_string *interned =
                table_find_string(&vm.strings, chars, length, hash);

        if (interned) {
                FREE_ARRAY(char, chars, length + 1);
                return interned;
        }
        return allocate_string(chars, length, hash);
}

struct obj_string *copy_string(const char *chars, size_t length)
{
        const u32 hash = hash_string(chars, length);
        struct obj_string *interned =
                table_find_string(&vm.strings, chars, length, hash);

        if (interned)
                return interned;

        char *heap_chars = ALLOCATE(char, length + 1);
        memcpy(heap_chars, chars, length);
        heap_chars[length] = '\0';
        return allocate_string(heap_chars, length, hash);
}

struct obj_upvalue *new_upvalue(struct value *slot)
{
        struct obj_upvalue *upvalue =
                ALLOCATE_OBJ(struct obj_upvalue, OBJ_UPVALUE);
        upvalue->location = slot;
        upvalue->closed = NIL_VAL;
        upvalue->next = NULL;
        return upvalue;
}

static void function_print(const struct obj_function *fn)
{
        if (fn->name == NULL) {
                printf("<script>");
                return;
        }
        printf("<fn %s>", fn->name->chars);
}

void object_print(struct value value)
{
        switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
                printf("%s", AS_CSTRING(value));
                break;
        case OBJ_FUNCTION:
                function_print(AS_FUNCTION(value));
                break;
        case OBJ_NATIVE:
                printf("<native fn>");
                break;
        case OBJ_CLOSURE:
                function_print(AS_CLOSURE(value)->fn);
                break;
        case OBJ_UPVALUE:
                printf("upvalue");
                break;
        }
}
