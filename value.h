#ifndef CLOX__VALUE_H_
#define CLOX__VALUE_H_

#include "common.h"

enum value_type { VAL_BOOL, VAL_NIL, VAL_NUMBER };

struct value {
        enum value_type type;
        union {
                bool boolean;
                double number;
        } as;
};

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(v) ((v).type == VAL_NUMBER)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(v) ((v).as.number)

#define BOOL_VAL(v) ((struct value){ VAL_BOOL, { .boolean = v } })
#define NIL_VAL ((struct value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(v) ((struct value){ VAL_NUMBER, { .number = v } })

struct value_array {
        int capacity;
        int count;
        struct value *values;
};

bool values_equal(struct value a, struct value b);
void value_array_init(struct value_array *array);
void value_array_write(struct value_array *array, struct value v);
void value_array_free(struct value_array *array);
void value_print(struct value v);

#endif // CLOX__VALUE_H_
