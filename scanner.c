#include "scanner.h"

#include <string.h>

struct scanner {
    const char *start;
    const char *current;
    size_t line;
};

static struct scanner scanner;

void scanner_init(const char *source)
{
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool is_alpha(char c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
           (c == '_');
}

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

static bool is_at_end(void)
{
    return *scanner.current == '\0';
}

static char advance(void)
{
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void)
{
    return *scanner.current;
}

static char peek_next(void)
{
    if (is_at_end())
        return '\0';

    return scanner.current[1];
}

static bool match(char expected)
{
    if (is_at_end())
        return false;

    if (*scanner.current != expected)
        return false;

    scanner.current++;
    return true;
}

static struct token make_token(enum token_type type)
{
    return (struct token){
        .type = type,
        .start = scanner.start,
        .length = (size_t)(scanner.current - scanner.start),
        .line = scanner.line,
    };
}

static struct token error_token(const char *message)
{
    return (struct token){
        .type = TOKEN_ERROR,
        .start = message,
        .length = strlen(message),
        .line = scanner.line,
    };
}

static void skip_whitespace(void)
{
    for (;;) {
        const char c = peek();
        switch (c) {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '\n':
            scanner.line++;
            advance();
            break;
        case '/':
            if (peek_next() == '/') {
                while ((peek() != '\n') && (!is_at_end()))
                    advance();
            } else {
                return;
            }
            break;
        default:
            return;
        }
    }
}

static enum token_type check_keyword(size_t start, size_t length,
                                     const char *rest, enum token_type type)
{
    const bool is_keyword =
            ((size_t)(scanner.current - scanner.start) == (start + length)) &&
            (memcmp(scanner.start + start, rest, length) == 0);

    if (is_keyword) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static enum token_type identifier_type(void)
{
    switch (scanner.start[0]) {
    case 'a':
        return check_keyword(1, 2, "nd", TOKEN_AND);
    case 'c':
        return check_keyword(1, 4, "lass", TOKEN_CLASS);
    case 'e':
        return check_keyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
        if (scanner.current - scanner.start > 1) {
            switch (scanner.start[1]) {
            case 'a':
                return check_keyword(2, 3, "lse", TOKEN_FALSE);
            case 'o':
                return check_keyword(2, 1, "r", TOKEN_FOR);
            case 'u':
                return check_keyword(2, 1, "n", TOKEN_FUN);
            default:
                break;
            }
        }
        break;
    case 'i':
        return check_keyword(1, 1, "f", TOKEN_IF);
    case 'n':
        return check_keyword(1, 2, "il", TOKEN_NIL);
    case 'o':
        return check_keyword(1, 1, "r", TOKEN_OR);
    case 'p':
        return check_keyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
        return check_keyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
        return check_keyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
        if (scanner.current - scanner.start > 1) {
            switch (scanner.start[1]) {
            case 'h':
                return check_keyword(2, 2, "is", TOKEN_THIS);
            case 'r':
                return check_keyword(2, 2, "ue", TOKEN_TRUE);
            default:
                break;
            }
        }
        break;
    case 'v':
        return check_keyword(1, 2, "ar", TOKEN_VAR);
    case 'w':
        return check_keyword(1, 4, "hile", TOKEN_WHILE);
    default:
        break;
    }
    return TOKEN_IDENTIFIER;
}

static struct token identifier(void)
{
    while (is_alpha(peek()) || is_digit(peek()))
        advance();

    return make_token(identifier_type());
}

static struct token number(void)
{
    while (is_digit(peek()))
        advance();

    // Look for a fractional part
    if ((peek() == '.') && is_digit(peek_next())) {
        // Consume the "."
        advance();

        while (is_digit(peek()))
            advance();
    }

    return make_token(TOKEN_NUMBER);
}

static struct token string(void)
{
    while ((peek() != '"') && (!is_at_end())) {
        if (peek() == '\n')
            scanner.line++;

        advance();
    }

    if (is_at_end())
        return error_token("Unterminated string.");

    // The closing quote
    advance();
    return make_token(TOKEN_STRING);
}

struct token scan_token(void)
{
    skip_whitespace();
    scanner.start = scanner.current;
    if (is_at_end())
        return make_token(TOKEN_EOF);

    const char c = advance();
    if (is_alpha(c))
        return identifier();

    if (is_digit(c))
        return number();

    switch (c) {
    case '(':
        return make_token(TOKEN_LEFT_PAREN);
    case ')':
        return make_token(TOKEN_RIGHT_PAREN);
    case '{':
        return make_token(TOKEN_LEFT_BRACE);
    case '}':
        return make_token(TOKEN_RIGHT_BRACE);
    case ';':
        return make_token(TOKEN_SEMICOLON);
    case ',':
        return make_token(TOKEN_COMMA);
    case '.':
        return make_token(TOKEN_DOT);
    case '-':
        return make_token(TOKEN_MINUS);
    case '+':
        return make_token(TOKEN_PLUS);
    case '/':
        return make_token(TOKEN_SLASH);
    case '*':
        return make_token(TOKEN_STAR);
    case '!':
        return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
        return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
        return make_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
        return string();
    default:
        break;
    }
    return error_token("Unexpected character.");
}
