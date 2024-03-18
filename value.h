#ifndef CLOX__VALUE_H_
#define CLOX__VALUE_H_

#include "common.h"

typedef double value;

struct value_array {
  int capacity;
  int count;
  value *values;
};

void value_array_init(struct value_array *array);
void value_array_write(struct value_array *array, value v);
void value_array_free(struct value_array *array);
void value_print(value v);

#endif //CLOX__VALUE_H_
