#include "vm.h"
#include <stdio.h>
#include <stdlib.h>

static void repl(void)
{
    for (;;) {
        char line[1024];
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        vm_interpret(line);
        printf("\n");
    }
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    const i64 file_size = ftell(file);
    rewind(file);

    if (file_size <= 0) {
        fprintf(stderr, "File \"%s\" is empty.\n", path);
        fclose(file);
        exit(74);
    }

    char *buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        exit(74);
    }

    const u64 bytes_read = fread(buffer, sizeof(char), (size_t)file_size, file);
    if (bytes_read < (u32)file_size) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        fclose(file);
        exit(74);
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static void run_file(const char *path)
{
    char *source = read_file(path);
    const enum interpret_result result = vm_interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR)
        exit(65);
    if (result == INTERPRET_RUNTIME_ERROR)
        exit(70);
}

i32 main(i32 argc, char *argv[])
{
    vm_init();
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        run_file(argv[1]);
    } else {
        fprintf(stderr, "Usage: clox [path]\n");
        exit(64);
    }
    vm_free();
    return 0;
}
