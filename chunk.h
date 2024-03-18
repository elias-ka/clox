#ifndef CLOX__CHUNK_H_
#define CLOX__CHUNK_H_

#include "common.h"

enum op_code {
  OP_RETURN,
};

struct chunk {
  int size;
  int capacity;
  uint8_t *code;
};

void chunk_init(struct chunk *chunk);
void chunk_write(struct chunk *chunk, uint8_t byte);
void chunk_free(struct chunk *chunk);


#endif //CLOX__CHUNK_H_
