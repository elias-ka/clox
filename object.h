#ifndef CLOX__OBJECT_H_
#define CLOX__OBJECT_H_

#include "chunk.h"
#include "value.h"
#include <stdint.h>

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNCTION)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_FUNCTION(value) ((struct obj_function *)AS_OBJ(value))
#define AS_STRING(value) ((struct obj_string *)AS_OBJ(value))
#define AS_CSTRING(value) (((struct obj_string *)AS_OBJ(value))->chars)

enum obj_type {
        OBJ_FUNCTION,
        OBJ_STRING,
};

struct obj {
        enum obj_type type;
        struct obj *next;
};

struct obj_function {
        struct obj obj;
        int arity;
        struct chunk chunk;
        struct obj_string *name;
};

struct obj_string {
        struct obj obj;
        size_t length;
        char *chars;
        uint32_t hash;
};

struct obj_function *new_function();
struct obj_string *take_string(char *chars, size_t length);
struct obj_string *copy_string(const char *chars, size_t length);
void object_print(struct value value);

inline bool is_obj_type(struct value v, enum obj_type type)
{
        return IS_OBJ(v) && (AS_OBJ(v)->type == type);
}

#endif // CLOX__OBJECT_H_
