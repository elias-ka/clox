#include "value.h"
#include "object.h"
#include "memutil.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

void value_array_init(struct value_array *array)
{
        array->capacity = 0;
        array->count = 0;
        array->values = NULL;
}

void value_array_write(struct value_array *array, struct value v)
{
        if (array->capacity < (array->count + 1)) {
                const int old_capacity = array->capacity;
                array->capacity = GROW_CAPACITY(old_capacity);
                array->values = GROW_ARRAY(struct value, array->values,
                                           old_capacity, array->capacity);
        }
        array->values[array->count] = v;
        array->count++;
}

void value_array_free(struct value_array *array)
{
        FREE_ARRAY(struct value, array->values, array->capacity);
        value_array_init(array);
}

void value_print(struct value v)
{
        switch (v.type) {
        case VAL_BOOL:
                printf(AS_BOOL(v) ? "true" : "false");
                break;
        case VAL_NIL:
                printf("nil");
                break;
        case VAL_NUMBER:
                printf("%g", AS_NUMBER(v));
                break;
        case VAL_OBJ:
                object_print(v);
                break;
        }
}

bool values_equal(struct value a, struct value b)
{
        if (a.type != b.type) {
                return false;
        }

        switch (a.type) {
        case VAL_BOOL:
                return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
                return true;
        case VAL_NUMBER:
                return (AS_NUMBER(a) - AS_NUMBER(b)) < DBL_EPSILON;
        case VAL_OBJ: {
                const struct obj_string *a_str = AS_STRING(a);
                const struct obj_string *b_str = AS_STRING(b);

                return (a_str->length == b_str->length) &&
                       (memcmp(a_str->chars, b_str->chars, a_str->length) == 0);
        }
        default:
                return false;
        }
}
