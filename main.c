#include "vm.h"
#include <stdio.h>
#include <stdlib.h>

static void repl(void)
{
        char line[1024];
        for (;;) {
                printf("> ");
                if (!fgets(line, sizeof(line), stdin)) {
                        printf("\n");
                        break;
                }
                vm_interpret(line);
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
        const size_t file_size = ftell(file);
        rewind(file);

        if (file_size <= 0) {
                fprintf(stderr, "File \"%s\" is empty.\n", path);
                fclose(file);
                exit(74);
        }

        char *buffer = malloc(file_size + 1);
        if (!buffer) {
                fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
                fclose(file);
                exit(74);
        }

        const size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
        if (bytes_read < file_size) {
                fprintf(stderr, "Could not read file \"%s\".\n", path);
                fclose(file);
                exit(74);
        }

        buffer[bytes_read] = '\0';
        free(buffer);
        fclose(file);
        return buffer;
}

static void run_file(const char *path)
{
        char *source = read_file(path);
        enum interpret_result result = vm_interpret(source);
        free(source);

        if (result == INTERPRET_COMPILE_ERROR)
                exit(65);
        if (result == INTERPRET_RUNTIME_ERROR)
                exit(70);
}

int main(int argc, char *argv[])
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
