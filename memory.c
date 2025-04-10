#include "memory.h"

#include "compiler.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif
#include <stdlib.h>

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(void *ptr, size_t old_size, size_t new_size)
{
    vm.bytes_allocated += new_size - old_size;
    if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
        collect_garbage();
#endif
        if (vm.bytes_allocated > vm.next_gc) {
            collect_garbage();
        }
    }
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

void object_mark(struct obj *object)
{
    if (!object || object->is_marked)
        return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void *)object);
    value_print(OBJ_VAL(object));
    printf("\n");
#endif

    object->is_marked = true;

    if (vm.gray_capacity < vm.gray_count + 1) {
        vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
        vm.gray_stack =
            realloc(vm.gray_stack, sizeof(struct obj *) * vm.gray_capacity);

        if (!vm.gray_stack)
            exit(1);
    }

    vm.gray_stack[vm.gray_count++] = object;
}

void value_mark(value_ty value)
{
    if (IS_OBJ(value)) {
        object_mark(AS_OBJ(value));
    }
}

void array_mark(const struct value_array *array)
{
    for (size_t i = 0; i < array->count; i++) {
        value_mark(array->values[i]);
    }
}

static void blacken_object(struct obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void *)object);
    value_print(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
    case OBJ_BOUND_METHOD: {
        const struct obj_bound_method *bound =
            (struct obj_bound_method *)object;
        value_mark(bound->receiver);
        object_mark((struct obj *)bound->method);
        break;
    }
    case OBJ_CLASS: {
        const struct obj_class *klass = (struct obj_class *)object;
        object_mark((struct obj *)klass->name);
        table_mark(&klass->methods);
        break;
    }
    case OBJ_CLOSURE: {
        const struct obj_closure *closure = (struct obj_closure *)object;
        object_mark((struct obj *)closure->fn);
        for (i32 i = 0; i < closure->upvalue_count; i++) {
            object_mark((struct obj *)closure->upvalues[i]);
        }
        break;
    }
    case OBJ_FUNCTION: {
        const struct obj_function *function = (struct obj_function *)object;
        object_mark((struct obj *)function->name);
        array_mark(&function->chunk.constants);
        break;
    }
    case OBJ_INSTANCE: {
        const struct obj_instance *instance = (struct obj_instance *)object;
        object_mark((struct obj *)instance->klass);
        table_mark(&instance->fields);
        break;
    }
    case OBJ_UPVALUE:
        value_mark(((struct obj_upvalue *)object)->closed);
        break;
    case OBJ_NATIVE:
    case OBJ_STRING:
        break;
    }
}

void free_object(struct obj *object)
{
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void *)object, object->type);
#endif
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
        const struct obj_closure *closure = (struct obj_closure *)object;
        FREE_ARRAY(struct obj_upvalue *, closure->upvalues,
                   (u64)closure->upvalue_count);
        FREE(struct obj_closure, object);
        break;
    }
    case OBJ_UPVALUE:
        FREE(struct obj_upvalue, object);
        break;
    case OBJ_CLASS: {
        struct obj_class *klass = (struct obj_class *)object;
        table_free(&klass->methods);
        FREE(struct obj_class, object);
        break;
    }
    case OBJ_INSTANCE: {
        struct obj_instance *instance = (struct obj_instance *)object;
        table_free(&instance->fields);
        FREE(struct obj_instance, object);
        break;
    }
    case OBJ_BOUND_METHOD:
        FREE(struct obj_bound_method, object);
        break;
    }
}

static void mark_roots(void)
{
    for (const value_ty *slot = vm.stack; slot < vm.stack_top; slot++) {
        value_mark(*slot);
    }

    for (size_t i = 0; i < vm.frame_count; i++) {
        object_mark((struct obj *)vm.frames[i].closure);
    }

    for (struct obj_upvalue *upvalue = vm.open_upvalues; upvalue;
         upvalue = upvalue->next) {
        object_mark((struct obj *)upvalue);
    }

    table_mark(&vm.globals);
    mark_compiler_roots();
    object_mark((struct obj *)vm.init_string);
}

static void trace_references(void)
{
    while (vm.gray_count > 0) {
        struct obj *object = vm.gray_stack[--vm.gray_count];
        blacken_object(object);
    }
}

static void sweep(void)
{
    struct obj *previous = NULL;
    struct obj *object = vm.objects;

    while (object) {
        if (object->is_marked) {
            // Marked objects aren't freed but they're unmarked
            // for the next GC cycle.
            object->is_marked = false;
            previous = object;
            object = object->next;
        } else {
            // Object is unreachable so unlink and free it.
            struct obj *unreached = object;
            object = object->next;

            if (previous) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            free_object(unreached);
        }
    }
}

void collect_garbage(void)
{
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytes_allocated;
#endif
    mark_roots();
    trace_references();
    table_remove_unreachable(&vm.strings);
    sweep();
    vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytes_allocated, before, vm.bytes_allocated, vm.next_gc);
#endif
}

void free_objects(void)
{
    struct obj *object = vm.objects;
    while (object) {
        struct obj *next = object->next;
        free_object(object);
        object = next;
    }

    free(vm.gray_stack);
}
