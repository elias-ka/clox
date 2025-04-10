#ifndef CLOX__TABLE_H_
#define CLOX__TABLE_H_

#include "common.h"
#include "value.h"

struct entry {
    struct obj_string *key;
    value_ty value;
};

struct table {
    size_t len;
    size_t capacity;
    struct entry *entries;
};

void table_init(struct table *t);
void table_free(struct table *t);
bool table_set(struct table *table, struct obj_string *key, value_ty value);
bool table_get(const struct table *table, const struct obj_string *key,
               value_ty *value);
bool table_delete(const struct table *table, const struct obj_string *key);
void table_add_all(const struct table *source, struct table *dest);
const struct obj_string *table_find_string(const struct table *table,
                                           const char *chars, size_t length,
                                           u32 hash);

void table_remove_unreachable(const struct table *table);
void table_mark(const struct table *table);

#endif // CLOX__TABLE_H_
