#include <stdio.h>

#include "vm.h"
#include "debug.h"
#include "common.h"
#include "compiler.h"

struct vm vm;

static void reset_stack(void)
{
        vm.stack_top = vm.stack;
}

void vm_init(void)
{
        reset_stack();
}

void vm_free(void)
{
}

void vm_stack_push(value v)
{
        *vm.stack_top = v;
        vm.stack_top++;
}

value vm_stack_pop(void)
{
        vm.stack_top--;
        return *vm.stack_top;
}

static enum interpret_result run(void)
{
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op)                           \
        do {                                    \
                const value b = vm_stack_pop(); \
                const value a = vm_stack_pop(); \
                vm_stack_push(a op b);          \
        } while (false)

        for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
                printf("          ");
                for (const value *slot = vm.stack; slot < vm.stack_top;
                     slot++) {
                        printf("[ ");
                        value_print(*slot);
                        printf(" ]");
                }
                printf("\n");
                disassemble_instruction(vm.chunk,
                                        (int)(vm.ip - vm.chunk->code));
#endif
                uint8_t instruction;
                switch (instruction = READ_BYTE()) {
                case OP_CONSTANT: {
                        const value constant = READ_CONSTANT();
                        vm_stack_push(constant);
                        value_print(constant);
                        printf("\n");
                        break;
                }
                case OP_ADD: {
                        BINARY_OP(+);
                        break;
                }
                case OP_SUBTRACT: {
                        BINARY_OP(-);
                        break;
                }
                case OP_MULTIPLY: {
                        BINARY_OP(*);
                        break;
                }
                case OP_DIVIDE: {
                        BINARY_OP(/);
                        break;
                }
                case OP_NEGATE: {
                        vm_stack_push(-vm_stack_pop());
                        break;
                }
                case OP_RETURN: {
                        value_print(vm_stack_pop());
                        printf("\n");
                        return INTERPRET_OK;
                }
                }
        }
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

enum interpret_result vm_interpret(const char *source)
{
        compile(source);
        return INTERPRET_OK;
}
