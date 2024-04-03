#ifndef CLOX__TABLE_H_
#define CLOX__TABLE_H_

#include "value.h"

struct entry {
        struct obj_string *key;
        struct value value;
};

struct table {
        int len;
        int capacity;
        struct entry *entries;
};

void table_init(struct table *t);
void table_free(struct table *t);
bool table_set(struct table *table, struct obj_string *key, struct value value);
bool table_get(struct table *table, const struct obj_string *key,
               struct value *value);
bool table_delete(struct table *table, const struct obj_string *key);
void table_add_all(struct table *source, struct table *dest);
struct obj_string *table_find_string(struct table *table, const char *chars,
                                     int length, uint32_t hash);

#endif // CLOX__TABLE_H_
