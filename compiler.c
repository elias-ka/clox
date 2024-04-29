#include "compiler.h"

#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"
#include <limits.h>
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
    i32 depth;
    bool is_captured;
};

struct upvalue {
    u8 index;
    bool is_local;
};

enum function_type {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
};

struct compiler {
    struct compiler *enclosing;
    struct obj_function *fn;
    enum function_type fn_type;

    struct local locals[UINT8_COUNT];
    i32 local_count;
    struct upvalue upvalues[UINT8_COUNT];
    i32 scope_depth;
};

struct class_compiler {
    struct class_compiler *enclosing;
    bool has_superclass;
};

struct compiler *current = NULL;
struct class_compiler *current_class = NULL;

static struct chunk *current_chunk(void)
{
    return &current->fn->chunk;
}

static void error_at(const struct token *tok, const char *message)
{
    if (parser.panic_mode)
        return;

    parser.panic_mode = true;
    fprintf(stderr, "[line %zu] Error", tok->line);

    if (tok->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (tok->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", (i32)tok->length, tok->start);
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

static void emit_byte(u8 byte)
{
    chunk_write(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(u8 byte1, u8 byte2)
{
    emit_byte(byte1);
    emit_byte(byte2);
}

static void emit_loop(size_t loop_start)
{
    emit_byte(OP_LOOP);

    const size_t offset = current_chunk()->size - loop_start + 2;
    if (offset > UINT16_MAX)
        error("Loop body too large.");

    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static size_t emit_jump(u8 instruction)
{
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->size - 2;
}

static void emit_return(void)
{
    // In an initializer, return the instance implicitly.
    if (current->fn_type == TYPE_INITIALIZER) {
        emit_bytes(OP_GET_LOCAL, 0);
    } else {
        emit_byte(OP_NIL);
    }

    emit_byte(OP_RETURN);
}

static u8 make_constant(value_t value)
{
    const size_t constant = chunk_add_constant(current_chunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (u8)constant;
}

static void emit_constant(value_t value)
{
    emit_bytes(OP_CONSTANT, make_constant(value));
}

static void patch_jump(size_t offset)
{
    const struct chunk *cc = current_chunk();
    const size_t jump = cc->size - offset - 2;

    if (jump > UINT16_MAX)
        error("Too much code to jump over.");

    cc->code[offset] = (jump >> 8) & 0xff;
    cc->code[offset + 1] = jump & 0xff;
}

static void compiler_init(struct compiler *compiler, enum function_type type)
{
    compiler->enclosing = current;
    compiler->fn = NULL;
    compiler->fn_type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->fn = new_function();
    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->fn->name =
            copy_string(parser.previous.start, parser.previous.length);
    }

    struct local *local = &current->locals[current->local_count++];
    local->depth = 0;
    local->is_captured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static struct obj_function *end_compiler(void)
{
    emit_return();
    struct obj_function *fn = current->fn;

#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error) {
        disassemble_chunk(current_chunk(),
                          fn->name ? fn->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return fn;
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
        if (current->locals[current->local_count - 1].is_captured) {
            emit_byte(OP_CLOSE_UPVALUE);
        } else {
            // TODO: Instead of popping one by one, we could have an OP_POPN instruction,
            // that pops N values from the stack.
            emit_byte(OP_POP);
        }
        current->local_count--;
    }
}

static const struct parse_rule *get_rule(enum token_type type);
static void parse_precedence(enum precedence prec);
static void expression(void);
static void statement(void);
static void declaration(void);
static u8 identifier_constant(const struct token *name);
static i32 resolve_local(const struct compiler *compiler,
                         const struct token *name);
static u8 argument_list(void);

static void binary(bool can_assign)
{
    (void)can_assign;
    const enum token_type operator_type = parser.previous.type;
    const struct parse_rule *rule = get_rule(operator_type);
    parse_precedence(rule->precedence + 1);

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

static void call(bool can_assign)
{
    (void)can_assign;
    const u8 n_args = argument_list();
    emit_bytes(OP_CALL, n_args);
}

static void dot(bool can_assign)
{
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    const u8 name = identifier_constant(&parser.previous);

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        u8 n_args = argument_list();
        emit_bytes(OP_INVOKE, name);
        emit_byte(n_args);
    } else {
        emit_bytes(OP_GET_PROPERTY, name);
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
    const f64 value = strtod(parser.previous.start, NULL);
    emit_constant(NUMBER_VAL(value));
}

static void or_(bool can_assign)
{
    (void)can_assign;
    const size_t else_jump = emit_jump(OP_JUMP_IF_FALSE);
    const size_t end_jump = emit_jump(OP_JUMP);

    patch_jump(else_jump);
    emit_byte(OP_POP);

    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static void string(bool can_assign)
{
    (void)can_assign;
    emit_constant(OBJ_VAL(
        copy_string(parser.previous.start + 1, parser.previous.length - 2)));
}

static i32 resolve_upvalue(struct compiler *compiler, struct token *name);

static void named_variable(struct token name, bool can_assign)
{
    u8 get_op;
    u8 set_op;
    i32 arg = resolve_local(current, &name);

    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    } else {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(set_op, (u8)arg);
    } else {
        emit_bytes(get_op, (u8)arg);
    }
}

static void variable(bool can_assign)
{
    named_variable(parser.previous, can_assign);
}

static struct token synthetic_token(const char *text)
{
    return (struct token){.start = text, .length = strlen(text)};
}

static void super_(bool can_assign)
{
    (void)can_assign;

    if (current_class == NULL) {
        error("Cannot use 'super' outside of a class.");
    } else if (!current_class->has_superclass) {
        error("Cannot use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    const u8 name = identifier_constant(&parser.previous);

    named_variable(synthetic_token("this"), /*can_assign=*/false);

    if (match(TOKEN_LEFT_PAREN)) {
        const u8 n_args = argument_list();
        named_variable(synthetic_token("super"), /*can_assign=*/false);
        emit_bytes(OP_SUPER_INVOKE, name);
        emit_byte(n_args);
    } else {
        named_variable(synthetic_token("super"), /*can_assign=*/false);
        emit_bytes(OP_GET_SUPER, name);
    }
}

static void this_(bool can_assign)
{
    (void)can_assign;
    if (current_class == NULL) {
        error("Cannot use 'this' outside of a class.");
        return;
    }

    variable(false);
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
        break;
    }
}

static void and_(bool can_assign);

struct parse_rule rules[40] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void parse_precedence(enum precedence prec)
{
    advance();
    const parse_fn prefix_rule = get_rule(parser.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return;
    }

    const bool can_assign = prec <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (prec <= get_rule(parser.current.type)->precedence) {
        advance();
        const parse_fn infix_rule = get_rule(parser.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static u8 identifier_constant(const struct token *name)
{
    return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
}

static bool identifiers_equal(const struct token *a, const struct token *b)
{
    if (a->length != b->length)
        return false;

    return memcmp(a->start, b->start, a->length) == 0;
}

static i32 resolve_local(const struct compiler *compiler,
                         const struct token *name)
{
    for (i32 i = compiler->local_count - 1; i >= 0; i--) {
        const struct local *local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name)) {
            if (local->depth == -1) {
                error("Cannot read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static i32 add_upvalue(struct compiler *compiler, u8 index, bool is_local)
{
    const i32 upvalue_count = compiler->fn->upvalue_count;

    for (i32 i = 0; i < upvalue_count; i++) {
        struct upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local) {
            return i;
        }
    }

    if (upvalue_count == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->fn->upvalue_count++;
}

static i32 resolve_upvalue(struct compiler *compiler, struct token *name)
{
    if (compiler->enclosing == NULL)
        return -1;

    // Look for a matching local variable in the enclosing function.
    i32 local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (u8)local, true);
    }

    // The local variable wasn't found in the immediate enclosing function.
    // It must be an upvalue, recursively search through the enclosing functions.
    i32 upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(current, (u8)upvalue, false);
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
    local->is_captured = false;
}

static void declare_variable(void)
{
    if (current->scope_depth == 0)
        return;

    const struct token *name = &parser.previous;
    for (i32 i = current->local_count - 1; i >= 0; i--) {
        const struct local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth)
            break;

        if (identifiers_equal(name, &local->name))
            error("Variable with this name already declared in this scope.");
    }
    add_local(*name);
}

static u8 parse_variable(const char *error_msg)
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
    if (current->scope_depth == 0)
        return;

    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(u8 global)
{
    // Local scope, no need to define a global variable
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }

    emit_bytes(OP_DEFINE_GLOBAL, global);
}

static u8 argument_list(void)
{
    u8 n_args = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (n_args == 255) {
                error("Cannot have more than 255 arguments.");
            }
            n_args++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return n_args;
}

static void and_(bool can_assign)
{
    (void)can_assign;
    const size_t end_jump = emit_jump(OP_JUMP_IF_FALSE);

    emit_byte(OP_POP);
    parse_precedence(PREC_AND);

    patch_jump(end_jump);
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

static void function(enum function_type type)
{
    struct compiler compiler;
    compiler_init(&compiler, type);
    begin_scope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->fn->arity++;
            if (current->fn->arity > 255) {
                error("Cannot have more than 255 parameters.");
            }
            const u8 param_constant = parse_variable("Expect parameter name.");
            define_variable(param_constant);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    struct obj_function *fn = end_compiler();
    emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(fn)));

    for (i32 i = 0; i < fn->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void method(void)
{
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    const u8 constant = identifier_constant(&parser.previous);
    enum function_type type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    function(type);
    emit_bytes(OP_METHOD, constant);
}

static void class_declaration(void)
{
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    const struct token class_name = parser.previous;
    const u8 name_constant = identifier_constant(&parser.previous);
    declare_variable();

    emit_bytes(OP_CLASS, name_constant);
    define_variable(name_constant);

    struct class_compiler class_compiler = {
        .enclosing = current_class,
        .has_superclass = false,
    };
    current_class = &class_compiler;

    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);

        if (identifiers_equal(&class_name, &parser.previous))
            error("A class cannot inherit from itself.");

        begin_scope();
        add_local(synthetic_token("super"));
        define_variable(0);

        named_variable(class_name, /*can_assign=*/false);
        emit_byte(OP_INHERIT);
        class_compiler.has_superclass = true;
    }

    named_variable(class_name, /*can_assign=*/false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emit_byte(OP_POP);

    if (class_compiler.has_superclass) {
        end_scope();
    }

    current_class = current_class->enclosing;
}

static void fun_declaration(void)
{
    const u8 global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void var_declaration(void)
{
    const u8 global = parse_variable("Expect variable name.");

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

static void for_statement(void)
{
    begin_scope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {
        // no initializer
    } else if (match(TOKEN_VAR)) {
        var_declaration();
    } else {
        expression_statement();
    }

    size_t loop_start = current_chunk()->size;
    size_t exit_jump = SIZE_MAX;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // jump out of the loop if the condition is false
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP); // condition
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        const size_t body_jump = emit_jump(OP_JUMP);
        const size_t increment_start = current_chunk()->size;
        expression();
        emit_byte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != SIZE_MAX) {
        patch_jump(exit_jump);
        emit_byte(OP_POP); // condition
    }

    end_scope();
}

static void if_statement(void)
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    const size_t then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    const size_t else_jump = emit_jump(OP_JUMP);

    patch_jump(then_jump);
    emit_byte(OP_POP);

    if (match(TOKEN_ELSE))
        statement();

    patch_jump(else_jump);
}

static void print_statement(void)
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(OP_PRINT);
}

static void return_statement(void)
{
    if (current->fn_type == TYPE_SCRIPT)
        error("Cannot return from top-level code.");

    if (match(TOKEN_SEMICOLON)) {
        emit_return();
    } else {
        if (current->fn_type == TYPE_INITIALIZER)
            error("Cannot return a value from an initializer.");

        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(OP_RETURN);
    }
}

static void while_statement(void)
{
    const size_t loop_start = current_chunk()->size;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    const size_t exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
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
    if (match(TOKEN_CLASS))
        class_declaration();
    else if (match(TOKEN_FUN))
        fun_declaration();
    else if (match(TOKEN_VAR))
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
    } else if (match(TOKEN_FOR)) {
        for_statement();
    } else if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_RETURN)) {
        return_statement();
    } else if (match(TOKEN_WHILE)) {
        while_statement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        begin_scope();
        block();
        end_scope();
    } else {
        expression_statement();
    }
}

struct obj_function *compile(const char *source)
{
    scanner_init(source);
    struct compiler compiler;
    compiler_init(&compiler, TYPE_SCRIPT);

    parser.had_error = false;
    parser.panic_mode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    struct obj_function *fn = end_compiler();
    return parser.had_error ? NULL : fn;
}

void mark_compiler_roots(void)
{
    struct compiler *compiler = current;

    while (compiler != NULL) {
        mark_object((struct obj *)compiler->fn);
        compiler = compiler->enclosing;
    }
}
