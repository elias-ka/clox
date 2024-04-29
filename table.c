#include "table.h"

#include "memory.h"
#include "object.h"
#include "value.h"
#include <string.h>

#define TABLE_MAX_LOAD 0.75

void table_init(struct table *t)
{
    t->len = 0;
    t->capacity = 0;
    t->entries = NULL;
}

void table_free(struct table *t)
{
    FREE_ARRAY(struct entry, t->entries, t->capacity);
    table_init(t);
}

static struct entry *find_entry(struct entry *entries, size_t capacity,
                                const struct obj_string *key)
{
    size_t index = key->hash & (capacity - 1);
    struct entry *tombstone = NULL;

    for (;;) {
        struct entry *entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // bucket is empty
                return tombstone ? tombstone : entry;
            }
            if (tombstone == NULL) {
                // bucket had a tombstone
                tombstone = entry;
            }
        } else if (entry->key == key) {
            // found the key
            return entry;
        }

        // probe linearly if the bucket is occupied
        index = (index + 1) & (capacity - 1);
    }
}

static void adjust_capacity(struct table *table, size_t capacity)
{
    struct entry *entries = ALLOCATE(struct entry, capacity);
    for (size_t i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->len = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        const struct entry *entry = &table->entries[i];
        if (entry->key == NULL)
            continue;

        struct entry *dest = find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->len++;
    }

    FREE_ARRAY(struct entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool table_set(struct table *table, struct obj_string *key, value_t value)
{
    if (((f64)(table->len + 1)) > ((f64)table->capacity * TABLE_MAX_LOAD)) {
        const size_t capacity = GROW_CAPACITY(table->capacity);
        adjust_capacity(table, capacity);
    }

    struct entry *entry = find_entry(table->entries, table->capacity, key);
    const bool is_new_key = entry->key == NULL;

    if (is_new_key && IS_NIL(entry->value))
        table->len++;

    entry->key = key;
    entry->value = value;
    return is_new_key;
}

bool table_get(const struct table *table, const struct obj_string *key,
               value_t *value)
{
    if (table->len == 0)
        return false;

    const struct entry *entry =
        find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    *value = entry->value;
    return true;
}

bool table_delete(const struct table *table, const struct obj_string *key)
{
    if (table->len == 0)
        return false;

    // find the entry
    struct entry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL)
        return false;

    // place a tombstone in the entry
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void table_add_all(const struct table *source, struct table *dest)
{
    for (size_t i = 0; i < source->capacity; i++) {
        const struct entry *entry = &source->entries[i];
        if (entry->key != NULL)
            table_set(dest, entry->key, entry->value);
    }
}

struct obj_string *table_find_string(const struct table *table,
                                     const char *chars, size_t length, u32 hash)
{
    if (table->len == 0)
        return NULL;

    size_t index = hash & (table->capacity - 1);
    for (;;) {
        const struct entry *entry = &table->entries[index];

        if (entry->key == NULL) {
            // stop if we find an empty non-tombstone entry
            if (IS_NIL(entry->value))
                return NULL;

        } else if ((entry->key->length == length) &&
                   (entry->key->hash == hash) &&
                   (memcmp(entry->key->chars, chars, length) == 0)) {
            // found the entry
            return entry->key;
        }

        // probe linearly if the bucket is occupied
        index = (index + 1) & (table->capacity - 1);
    }
}

void table_remove_unreachable(struct table *table)
{
    for (size_t i = 0; i < table->capacity; i++) {
        struct entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.is_marked) {
            table_delete(table, entry->key);
        }
    }
}

void mark_table(struct table *table)
{
    for (size_t i = 0; i < table->capacity; i++) {
        struct entry *entry = &table->entries[i];
        mark_object((struct obj *)entry->key);
        mark_value(entry->value);
    }
}
