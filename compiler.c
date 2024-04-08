#include "compiler.h"

#include "common.h"
#include "object.h"
#include "scanner.h"
#include "value.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

struct parser {
        struct token current;
        struct token previous;
        bool had_error;
        bool panic_mode;
};

struct parser parser;
struct compiler *current = NULL;
struct chunk *compiling_chunk;

enum precedence {
        PREC_NONE,
        PREC_ASSIGNMENT, // =
        PREC_OR, // or
        PREC_AND, // and
        PREC_EQUALITY, // == !=
        PREC_COMPARISON, // < > <= >=
        PREC_TERM, // + -
        PREC_FACTOR, // * /
        PREC_UNARY, // ! -
        PREC_CALL, // . ()
        PREC_PRIMARY
};

typedef void (*parse_fn)(bool can_assign);

struct parse_rule {
        parse_fn prefix;
        parse_fn infix;
        enum precedence precedence;
};

struct local {
        struct token name;
        int depth;
};

struct compiler {
        struct local locals[UINT8_COUNT];
        int local_count;
        int scope_depth;
};

static struct chunk *current_chunk(void)
{
        return compiling_chunk;
}

static void error_at(const struct token *tok, const char *message)
{
        if (parser.panic_mode)
                return;

        parser.panic_mode = true;
        fprintf(stderr, "[line %d] Error", tok->line);

        if (tok->type == TOKEN_EOF) {
                fprintf(stderr, " at end");
        } else if (tok->type == TOKEN_ERROR) {
                // Nothing.
        } else {
                fprintf(stderr, " at '%.*s'", tok->length, tok->start);
        }

        fprintf(stderr, ": %s\n", message);
        parser.had_error = true;
}

static void error(const char *message)
{
        error_at(&parser.previous, message);
}

static void error_at_current(const char *message)
{
        error_at(&parser.current, message);
}

static void advance(void)
{
        parser.previous = parser.current;

        for (;;) {
                parser.current = scan_token();
                if (parser.current.type != TOKEN_ERROR)
                        break;

                error_at_current(parser.current.start);
        }
}

static void consume(enum token_type type, const char *message)
{
        if (parser.current.type == type) {
                advance();
                return;
        }

        error_at_current(message);
}

static bool check(enum token_type type)
{
        return parser.current.type == type;
}

static bool match(enum token_type type)
{
        if (!check(type))
                return false;

        advance();
        return true;
}

static void emit_byte(uint8_t byte)
{
        chunk_write(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2)
{
        emit_byte(byte1);
        emit_byte(byte2);
}

static void emit_return(void)
{
        emit_byte(OP_RETURN);
}

static uint8_t make_constant(struct value value)
{
        const size_t constant = chunk_add_constant(current_chunk(), value);
        if (constant > UINT8_MAX) {
                error("Too many constants in one chunk.");
                return 0;
        }

        return (uint8_t)constant;
}

static void emit_constant(struct value value)
{
        emit_bytes(OP_CONSTANT, make_constant(value));
}

static void compiler_init(struct compiler *compiler)
{
        compiler->local_count = 0;
        compiler->scope_depth = 0;
        current = compiler;
}

static void end_compiler(void)
{
        emit_return();
#ifdef DEBUG_PRINT_CODE
        if (!parser.had_error) {
                disassemble_chunk(current_chunk(), "bytecode");
        }
#endif
}

static void begin_scope(void)
{
        current->scope_depth++;
}

static void end_scope(void)
{
        current->scope_depth--;

        // Pop the locals that were declared in this scope
        while (current->local_count > 0 &&
               current->locals[current->local_count - 1].depth >
                       current->scope_depth) {
                // TODO: Instead of popping one by one, we could have an OP_POPN instruction,
                // that pops N values from the stack.
                emit_byte(OP_POP);
                current->local_count--;
        }
}

static const struct parse_rule *get_rule(enum token_type type);
static void parse_precedence(enum precedence prec);
static void expression(void);
static void statement(void);
static void declaration(void);
static uint8_t identifier_constant(const struct token *name);
static int resolve_local(struct compiler *compiler, struct token *name);

static void binary(bool can_assign)
{
        (void)can_assign;
        const enum token_type operator_type = parser.previous.type;
        const struct parse_rule *rule = get_rule(operator_type);
        parse_precedence((enum precedence)(rule->precedence + 1));

        switch (operator_type) {
        case TOKEN_BANG_EQUAL:
                emit_bytes(OP_EQUAL, OP_NOT);
                break;
        case TOKEN_EQUAL_EQUAL:
                emit_byte(OP_EQUAL);
                break;
        case TOKEN_GREATER:
                emit_byte(OP_GREATER);
                break;
        case TOKEN_GREATER_EQUAL:
                emit_bytes(OP_LESS, OP_NOT);
                break;
        case TOKEN_LESS:
                emit_byte(OP_LESS);
                break;
        case TOKEN_LESS_EQUAL:
                emit_bytes(OP_GREATER, OP_NOT);
                break;
        case TOKEN_PLUS:
                emit_byte(OP_ADD);
                break;
        case TOKEN_MINUS:
                emit_byte(OP_SUBTRACT);
                break;
        case TOKEN_STAR:
                emit_byte(OP_MULTIPLY);
                break;
        case TOKEN_SLASH:
                emit_byte(OP_DIVIDE);
                break;
        default:
                return;
        }
}

static void literal(bool can_assign)
{
        (void)can_assign;
        switch (parser.previous.type) {
        case TOKEN_FALSE:
                emit_byte(OP_FALSE);
                break;
        case TOKEN_NIL:
                emit_byte(OP_NIL);
                break;
        case TOKEN_TRUE:
                emit_byte(OP_TRUE);
                break;
        default:
                return;
        }
}

static void grouping(bool can_assign)
{
        (void)can_assign;
        expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool can_assign)
{
        (void)can_assign;
        const double value = strtod(parser.previous.start, NULL);
        emit_constant(NUMBER_VAL(value));
}

static void string(bool can_assign)
{
        (void)can_assign;
        emit_constant(OBJ_VAL(copy_string(parser.previous.start + 1,
                                          parser.previous.length - 2)));
}

static void named_variable(struct token name, bool can_assign)
{
        uint8_t get_op;
        uint8_t set_op;
        int arg = resolve_local(current, &name);

        if (arg != -1) {
                get_op = OP_GET_LOCAL;
                set_op = OP_SET_LOCAL;
        } else {
                arg = identifier_constant(&name);
                get_op = OP_GET_GLOBAL;
                set_op = OP_SET_GLOBAL;
        }

        if (can_assign && match(TOKEN_EQUAL)) {
                expression();
                emit_bytes(set_op, (uint8_t)arg);
        } else {
                emit_bytes(get_op, (uint8_t)arg);
        }
}

static void variable(bool can_assign)
{
        named_variable(parser.previous, can_assign);
}

static void unary(bool can_assign)
{
        (void)can_assign;
        const enum token_type operator_type = parser.previous.type;

        parse_precedence(PREC_UNARY);

        switch (operator_type) {
        case TOKEN_BANG:
                emit_byte(OP_NOT);
                break;
        case TOKEN_MINUS:
                emit_byte(OP_NEGATE);
                break;
        default:
                return;
        }
}

struct parse_rule rules[40] = {
        [TOKEN_LEFT_PAREN] = { grouping, NULL, PREC_NONE },
        [TOKEN_RIGHT_PAREN] = { NULL, NULL, PREC_NONE },
        [TOKEN_LEFT_BRACE] = { NULL, NULL, PREC_NONE },
        [TOKEN_RIGHT_BRACE] = { NULL, NULL, PREC_NONE },
        [TOKEN_COMMA] = { NULL, NULL, PREC_NONE },
        [TOKEN_DOT] = { NULL, NULL, PREC_NONE },
        [TOKEN_MINUS] = { unary, binary, PREC_TERM },
        [TOKEN_PLUS] = { NULL, binary, PREC_TERM },
        [TOKEN_SEMICOLON] = { NULL, NULL, PREC_NONE },
        [TOKEN_SLASH] = { NULL, binary, PREC_FACTOR },
        [TOKEN_STAR] = { NULL, binary, PREC_FACTOR },
        [TOKEN_BANG] = { unary, NULL, PREC_NONE },
        [TOKEN_BANG_EQUAL] = { NULL, binary, PREC_EQUALITY },
        [TOKEN_EQUAL] = { NULL, NULL, PREC_NONE },
        [TOKEN_EQUAL_EQUAL] = { NULL, binary, PREC_EQUALITY },
        [TOKEN_GREATER] = { NULL, binary, PREC_COMPARISON },
        [TOKEN_GREATER_EQUAL] = { NULL, binary, PREC_COMPARISON },
        [TOKEN_LESS] = { NULL, binary, PREC_COMPARISON },
        [TOKEN_LESS_EQUAL] = { NULL, binary, PREC_COMPARISON },
        [TOKEN_IDENTIFIER] = { variable, NULL, PREC_NONE },
        [TOKEN_STRING] = { string, NULL, PREC_NONE },
        [TOKEN_NUMBER] = { number, NULL, PREC_NONE },
        [TOKEN_AND] = { NULL, NULL, PREC_NONE },
        [TOKEN_CLASS] = { NULL, NULL, PREC_NONE },
        [TOKEN_ELSE] = { NULL, NULL, PREC_NONE },
        [TOKEN_FALSE] = { literal, NULL, PREC_NONE },
        [TOKEN_FOR] = { NULL, NULL, PREC_NONE },
        [TOKEN_FUN] = { NULL, NULL, PREC_NONE },
        [TOKEN_IF] = { NULL, NULL, PREC_NONE },
        [TOKEN_NIL] = { literal, NULL, PREC_NONE },
        [TOKEN_OR] = { NULL, NULL, PREC_NONE },
        [TOKEN_PRINT] = { NULL, NULL, PREC_NONE },
        [TOKEN_RETURN] = { NULL, NULL, PREC_NONE },
        [TOKEN_SUPER] = { NULL, NULL, PREC_NONE },
        [TOKEN_THIS] = { NULL, NULL, PREC_NONE },
        [TOKEN_TRUE] = { literal, NULL, PREC_NONE },
        [TOKEN_VAR] = { NULL, NULL, PREC_NONE },
        [TOKEN_WHILE] = { NULL, NULL, PREC_NONE },
        [TOKEN_ERROR] = { NULL, NULL, PREC_NONE },
        [TOKEN_EOF] = { NULL, NULL, PREC_NONE },
};

static void parse_precedence(enum precedence prec)
{
        advance();
        parse_fn prefix_rule = get_rule(parser.previous.type)->prefix;
        if (prefix_rule == NULL) {
                error("Expect expression.");
                return;
        }

        bool can_assign = prec <= PREC_ASSIGNMENT;
        prefix_rule(can_assign);

        while (prec <= get_rule(parser.current.type)->precedence) {
                advance();
                parse_fn infix_rule = get_rule(parser.previous.type)->infix;
                infix_rule(can_assign);
        }

        if (can_assign && match(TOKEN_EQUAL)) {
                error("Invalid assignment target.");
        }
}

static uint8_t identifier_constant(const struct token *name)
{
        return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
}

static bool identifiers_equal(const struct token *a, const struct token *b)
{
        if (a->length != b->length)
                return false;

        return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(struct compiler *compiler, struct token *name)
{
        for (int i = compiler->local_count - 1; i >= 0; i--) {
                struct local *local = &compiler->locals[i];
                if (identifiers_equal(name, &local->name)) {
                        if (local->depth == -1) {
                                error("Cannot read local variable in its own initializer.");
                        }
                        return i;
                }
        }

        return -1;
}

static void add_local(struct token name)
{
        if (current->local_count == UINT8_COUNT) {
                error("Too many local variables in function. Maximum is 256.");
                return;
        }

        struct local *local = &current->locals[current->local_count++];
        local->name = name;
        local->depth = -1;
}

static void declare_variable(void)
{
        if (current->scope_depth == 0)
                return;

        const struct token *name = &parser.previous;
        for (int i = current->local_count - 1; i >= 0; i--) {
                const struct local *local = &current->locals[i];
                if (local->depth != -1 && local->depth < current->scope_depth)
                        break;

                if (identifiers_equal(name, &local->name))
                        error("Variable with this name already declared in this scope.");
        }
        add_local(*name);
}

static uint8_t parse_variable(const char *error_msg)
{
        consume(TOKEN_IDENTIFIER, error_msg);

        declare_variable();
        // At runtime, locals aren’t looked up by name.
        // There’s no need to stuff the variable’s name into the constant table,
        // so if the declaration is inside a local scope, we return a dummy table index instead.
        if (current->scope_depth > 0)
                return 0;

        return identifier_constant(&parser.previous);
}

static void mark_initialized(void)
{
        current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global)
{
        // Local scope, no need to define a global variable
        if (current->scope_depth > 0) {
                mark_initialized();
                return;
        }

        emit_bytes(OP_DEFINE_GLOBAL, global);
}

static const struct parse_rule *get_rule(enum token_type type)
{
        return &rules[type];
}

static void expression(void)
{
        parse_precedence(PREC_ASSIGNMENT);
}

static void block(void)
{
        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
                declaration();
        }

        consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void var_declaration(void)
{
        uint8_t global = parse_variable("Expect variable name.");

        if (match(TOKEN_EQUAL))
                expression();
        else
                emit_byte(OP_NIL);

        consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        define_variable(global);
}

static void expression_statement(void)
{
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
        emit_byte(OP_POP);
}

static void print_statement(void)
{
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after value.");
        emit_byte(OP_PRINT);
}

static void synchronize(void)
{
        parser.panic_mode = false;

        while (!check(TOKEN_EOF)) {
                if (parser.previous.type == TOKEN_SEMICOLON)
                        return;

                switch (parser.current.type) {
                case TOKEN_CLASS:
                case TOKEN_FUN:
                case TOKEN_VAR:
                case TOKEN_FOR:
                case TOKEN_IF:
                case TOKEN_WHILE:
                case TOKEN_PRINT:
                case TOKEN_RETURN:
                        return;
                default: {
                        // do nothing
                }
                }

                advance();
        }
}

static void declaration(void)
{
        if (match(TOKEN_VAR))
                var_declaration();
        else
                statement();

        if (parser.panic_mode)
                synchronize();
}

static void statement(void)
{
        if (match(TOKEN_PRINT)) {
                print_statement();
        } else if (match(TOKEN_LEFT_BRACE)) {
                begin_scope();
                block();
                end_scope();
        } else {
                expression_statement();
        }
}

bool compile(const char *source, struct chunk *chunk)
{
        scanner_init(source);
        struct compiler compiler;
        compiler_init(&compiler);

        compiling_chunk = chunk;
        parser.had_error = false;
        parser.panic_mode = false;

        advance();

        while (!match(TOKEN_EOF)) {
                declaration();
        }

        end_compiler();
        return !parser.had_error;
}
