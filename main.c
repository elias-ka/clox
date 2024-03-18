#include "chunk.h"
#include "debug.h"

int main(int argc, char *argv[]) {
  struct chunk chunk;
  chunk_init(&chunk);
  chunk_write(&chunk, OP_RETURN);
  disassemble_chunk(&chunk, "test chunk");
  chunk_free(&chunk);
}
