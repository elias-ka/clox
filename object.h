#ifndef CLOX__OBJECT_H_
#define CLOX__OBJECT_H_

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) is_obj_type(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) is_obj_type(value, OBJ_CLASS)
#define IS_CLOSURE(value) is_obj_type(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) is_obj_type(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) is_obj_type(value, OBJ_INSTANCE)
#define IS_NATIVE(value) is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value) is_obj_type(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((struct obj_bound_method *)AS_OBJ(value))
#define AS_CLASS(value) ((struct obj_class *)AS_OBJ(value))
#define AS_CLOSURE(value) ((struct obj_closure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((struct obj_function *)AS_OBJ(value))
#define AS_INSTANCE(value) ((struct obj_instance *)AS_OBJ(value))
#define AS_NATIVE(value) (((struct obj_native *)AS_OBJ(value))->fn)
#define AS_STRING(value) ((struct obj_string *)AS_OBJ(value))
#define AS_CSTRING(value) (((struct obj_string *)AS_OBJ(value))->chars)

enum obj_type {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
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

typedef value_ty (*native_fn)(i32 arg_count, value_ty *args);

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
    value_ty *location;
    value_ty closed;
    struct obj_upvalue *next;
};

struct obj_closure {
    struct obj obj;
    struct obj_function *fn;
    struct obj_upvalue **upvalues;
    i32 upvalue_count;
};

struct obj_class {
    struct obj obj;
    struct obj_string *name;
    value_ty initializer;
    struct table methods;
};

struct obj_instance {
    struct obj obj;
    struct obj_class *klass;
    struct table fields;
};

struct obj_bound_method {
    struct obj obj;
    value_ty receiver;
    struct obj_closure *method;
};

struct obj_bound_method *new_bound_method(value_ty receiver,
                                          struct obj_closure *method);
struct obj_class *new_class(struct obj_string *name);
struct obj_closure *new_closure(struct obj_function *fn);
struct obj_function *new_function();
struct obj_instance *new_instance(struct obj_class *klass);
struct obj_native *new_native(native_fn fn);
struct obj_string *take_string(char *chars, size_t length);
struct obj_string *copy_string(const char *chars, size_t length);
struct obj_upvalue *new_upvalue(value_ty *slot);
void object_print(value_ty value);

static inline bool __attribute__((unused))
is_obj_type(value_ty v, enum obj_type type)
{
    return IS_OBJ(v) && (AS_OBJ(v)->type == type);
}

#endif // CLOX__OBJECT_H_
