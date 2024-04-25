#ifndef CLOX__TABLE_H_
#define CLOX__TABLE_H_

#include "common.h"
#include "value.h"

struct entry {
    struct obj_string *key;
    struct value value;
};

struct table {
    size_t len;
    size_t capacity;
    struct entry *entries;
};

void table_init(struct table *t);
void table_free(struct table *t);
bool table_set(struct table *table, struct obj_string *key, struct value value);
bool table_get(const struct table *table, const struct obj_string *key,
               struct value *value);
bool table_delete(const struct table *table, const struct obj_string *key);
void table_add_all(const struct table *source, struct table *dest);
struct obj_string *table_find_string(const struct table *table,
                                     const char *chars, size_t length,
                                     u32 hash);

#endif // CLOX__TABLE_H_
