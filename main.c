#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, char *argv[])
{
        vm_init();
        struct chunk chunk;
        chunk_init(&chunk);

        const size_t constant = chunk_add_constant(&chunk, 1.2);
        chunk_write(&chunk, OP_CONSTANT, 123);
        chunk_write(&chunk, (uint8_t)constant, 123);
        chunk_write(&chunk, OP_NEGATE, 123);

        chunk_write(&chunk, OP_RETURN, 123);
        disassemble_chunk(&chunk, "test chunk");
        vm_interpret(&chunk);
        vm_free();
        chunk_free(&chunk);
}
