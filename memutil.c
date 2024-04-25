#include "memutil.h"

#include "object.h"
#include "vm.h"
#include <stdlib.h>

void *reallocate(void *ptr, size_t old_size, size_t new_size)
{
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    void *new_ptr = realloc(ptr, new_size);
    if (new_ptr == NULL) {
        exit(1);
    }

    return new_ptr;
}

void free_object(struct obj *object)
{
    switch (object->type) {
    case OBJ_STRING: {
        const struct obj_string *string = (const struct obj_string *)object;
        FREE_ARRAY(char, string->chars, string->length + 1);
        FREE(struct obj_string, object);
        break;
    }
    case OBJ_FUNCTION: {
        struct obj_function *fn = (struct obj_function *)object;
        chunk_free(&fn->chunk);
        FREE(struct obj_function, object);
        break;
    }
    case OBJ_NATIVE: {
        FREE(struct obj_native, object);
        break;
    }
    case OBJ_CLOSURE: {
        struct obj_closure *closure = (struct obj_closure *)object;
        FREE_ARRAY(struct obj_upvalue *, closure->upvalues,
                   (u64)closure->upvalue_count);
        FREE(struct obj_closure, object);
        break;
    }
    case OBJ_UPVALUE:
        FREE(struct obj_upvalue, object);
        break;
    }
}

void free_objects(void)
{
    struct obj *object = vm.objects;
    while (object) {
        struct obj *next = object->next;
        free_object(object);
        object = next;
    }
}
