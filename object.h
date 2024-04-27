#ifndef CLOX__OBJECT_H_
#define CLOX__OBJECT_H_

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_CLOSURE(value) is_obj_type(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNCTION)
#define IS_NATIVE(value) is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_CLOSURE(value) ((struct obj_closure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((struct obj_function *)AS_OBJ(value))
#define AS_NATIVE(value) (((struct obj_native *)AS_OBJ(value))->fn)
#define AS_STRING(value) ((struct obj_string *)AS_OBJ(value))
#define AS_CSTRING(value) (((struct obj_string *)AS_OBJ(value))->chars)

enum obj_type {
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
};

struct obj {
    enum obj_type type;
    bool is_marked;
    struct obj *next;
};

struct obj_function {
    struct obj obj;
    i32 arity;
    i32 upvalue_count;
    struct chunk chunk;
    struct obj_string *name;
};

typedef struct value (*native_fn)(i32 arg_count, struct value *args);

struct obj_native {
    struct obj obj;
    native_fn fn;
};

struct obj_string {
    struct obj obj;
    size_t length;
    char *chars;
    u32 hash;
};

struct obj_upvalue {
    struct obj obj;
    struct value *location;
    struct value closed;
    struct obj_upvalue *next;
};

struct obj_closure {
    struct obj obj;
    struct obj_function *fn;
    struct obj_upvalue **upvalues;
    i32 upvalue_count;
};

struct obj_closure *new_closure(struct obj_function *fn);
struct obj_function *new_function();
struct obj_native *new_native(native_fn fn);
struct obj_string *take_string(char *chars, size_t length);
struct obj_string *copy_string(const char *chars, size_t length);
struct obj_upvalue *new_upvalue(struct value *slot);
void object_print(struct value value);

static inline bool is_obj_type(struct value v, enum obj_type type)
{
    return IS_OBJ(v) && (AS_OBJ(v)->type == type);
}

#endif // CLOX__OBJECT_H_
