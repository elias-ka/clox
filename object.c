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

struct obj_function *new_function()
{
        struct obj_function *fn =
                ALLOCATE_OBJ(struct obj_function, OBJ_FUNCTION);
        fn->arity = 0;
        fn->name = NULL;
        chunk_init(&fn->chunk);
        return fn;
}

static struct obj_string *allocate_string(char *chars, size_t length,
                                          uint32_t hash)
{
        struct obj_string *string = ALLOCATE_OBJ(struct obj_string, OBJ_STRING);
        string->chars = chars;
        string->length = length;
        string->hash = hash;
        table_set(&vm.strings, string, NIL_VAL);
        return string;
}

static uint32_t hash_string(const char *key, size_t length)
{
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < length; i++) {
                hash ^= (uint8_t)key[i];
                hash *= 16777619;
        }
        return hash;
}

struct obj_string *take_string(char *chars, size_t length)
{
        uint32_t hash = hash_string(chars, length);
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
        uint32_t hash = hash_string(chars, length);
        struct obj_string *interned =
                table_find_string(&vm.strings, chars, length, hash);

        if (interned)
                return interned;

        char *heap_chars = ALLOCATE(char, length + 1);
        memcpy(heap_chars, chars, length);
        heap_chars[length] = '\0';
        return allocate_string(heap_chars, length, hash);
}

static void function_print(struct obj_function *fn)
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
        }
}
