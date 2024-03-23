#include "value.h"

#include <stdio.h>

#include "memutil.h"

void value_array_init(struct value_array *array)
{
        array->capacity = 0;
        array->count = 0;
        array->values = NULL;
}

void value_array_write(struct value_array *array, value v)
{
        if (array->capacity < array->count + 1) {
                const int old_capacity = array->capacity;
                array->capacity = GROW_CAPACITY(old_capacity);
                array->values = GROW_ARRAY(value, array->values, old_capacity,
                                           array->capacity);
        }
        array->values[array->count] = v;
        array->count++;
}

void value_array_free(struct value_array *array)
{
        FREE_ARRAY(value, array->values, array->capacity);
        value_array_init(array);
}

void value_print(value v)
{
        printf("%g", v);
}
