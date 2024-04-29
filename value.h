#ifndef CLOX__VALUE_H_
#define CLOX__VALUE_H_

#include "common.h"
#include <string.h>

struct obj;
struct obj_string;

#ifdef NAN_BOXING
#define SIGN_BIT ((u64)0x8000000000000000)
#define QNAN ((u64)0x7ffc000000000000)

#define TAG_NIL 1 // 01
#define TAG_FALSE 2 // 10
#define TAG_TRUE 3 // 11

typedef u64 value_ty;

#define IS_BOOL(v) (((v) | 1) == TRUE_VAL)
#define IS_NIL(v) ((v) == NIL_VAL)
#define IS_NUMBER(v) (((v)&QNAN) != QNAN)
#define IS_OBJ(v) (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(v) ((v) == TRUE_VAL)
#define AS_NUMBER(v) value_to_num(v)
#define AS_OBJ(v) ((struct obj *)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL ((value_ty)(u64)(QNAN | TAG_FALSE))
#define TRUE_VAL ((value_ty)(u64)(QNAN | TAG_TRUE))
#define NIL_VAL ((value_ty)(u64)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) num_to_value(num)
#define OBJ_VAL(obj) (value_ty)(SIGN_BIT | QNAN | (u64)(uintptr_t)(obj))

static inline double value_to_num(value_ty value)
{
    double num;
    memcpy(&num, &value, sizeof(double));
    return num;
}

static inline value_ty num_to_value(double num)
{
    value_ty value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#else
enum value_type { VAL_BOOL, VAL_NIL, VAL_NUMBER, VAL_OBJ };

struct value {
    enum value_type type;
    union {
        bool boolean;
        f64 number;
        struct obj *obj;
    } as;
};

typedef struct value value_ty;

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(v) ((v).type == VAL_NUMBER)
#define IS_OBJ(v) ((v).type == VAL_OBJ)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(v) ((v).as.number)
#define AS_OBJ(v) ((v).as.obj)

#define BOOL_VAL(v) ((value_ty){VAL_BOOL, {.boolean = v}})
#define NIL_VAL ((value_ty){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(v) ((value_ty){VAL_NUMBER, {.number = v}})
#define OBJ_VAL(object) ((value_ty){VAL_OBJ, {.obj = (struct obj *)object}})
#endif

struct value_array {
    size_t capacity;
    size_t count;
    value_ty *values;
};

bool values_equal(value_ty a, value_ty b);
void value_array_init(struct value_array *array);
void value_array_write(struct value_array *array, value_ty v);
void value_array_free(struct value_array *array);
void value_print(value_ty v);

#endif // CLOX__VALUE_H_
