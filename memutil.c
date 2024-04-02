#include "memutil.h"
#include "object.h"
#include "vm.h"

#include <stdlib.h>

void *reallocate(void *ptr, size_t old_size, size_t new_size)
{
        if (new_size == 0) {
                free(ptr);
                return NULL;
        }

        void *new_ptr = realloc(ptr, new_size);
        if (!new_ptr) {
                exit(1);
        }

        return new_ptr;
}

void free_object(struct obj *object)
{
        switch (object->type) {
        case OBJ_STRING: {
                struct obj_string *string = (struct obj_string *)object;
                FREE_ARRAY(char, string->chars, string->length + 1);
                FREE(struct obj_string, object);
                break;
        }
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
