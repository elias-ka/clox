#include "value.h"

#include "memory.h"
#include "object.h"
#include <math.h>
#include <string.h>

void value_array_init(struct value_array *array)
{
    array->capacity = 0;
    array->count = 0;
    array->values = NULL;
}

void value_array_write(struct value_array *array, value_ty v)
{
    if (array->capacity < (array->count + 1)) {
        const size_t old_capacity = array->capacity;
        array->capacity = GROW_CAPACITY(old_capacity);
        array->values =
            GROW_ARRAY(value_ty, array->values, old_capacity, array->capacity);
    }
    array->values[array->count] = v;
    array->count++;
}

void value_array_free(struct value_array *array)
{
    FREE_ARRAY(value_ty, array->values, array->capacity);
    value_array_init(array);
}

void value_print(value_ty v)
{
#ifdef NAN_BOXING
    if (IS_BOOL(v)) {
        printf(AS_BOOL(v) ? "true" : "false");
    } else if (IS_NIL(v)) {
        printf("nil");
    } else if (IS_NUMBER(v)) {
        printf("%g", AS_NUMBER(v));
    } else if (IS_OBJ(v)) {
        object_print(v);
    }
#else
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
#endif
}

bool values_equal(value_ty a, value_ty b)
{
#ifdef NAN_BOXING
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    return a == b;
#else

    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
    case VAL_BOOL:
        return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
        return true;
    case VAL_NUMBER:
        return fabs(AS_NUMBER(a) - AS_NUMBER(b)) < DBL_EPSILON;
    case VAL_OBJ:
        return AS_OBJ(a) == AS_OBJ(b);
    default:
        return false;
    }
#endif
}
