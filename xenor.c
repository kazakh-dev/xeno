#define _CRT_SECURE_NO_WARNINGS

#include "xenor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* ============================================================
   Utilities
   ============================================================ */

static void *xalloc(size_t n)
{
    void *p = malloc(n ? n : 1);

    if (!p) {
        fprintf(stderr, "Xeno: out of memory\n");
        exit(2);
    }

    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);

    if (!q) {
        fprintf(stderr, "Xeno: out of memory\n");
        exit(2);
    }

    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xalloc(n);

    memcpy(p, s, n);
    return p;
}

static char *xformat(const char *a, const char *b)
{
    size_t na = strlen(a);
    size_t nb = strlen(b);
    char *r = xalloc(na + nb + 1);

    memcpy(r, a, na);
    memcpy(r + na, b, nb + 1);

    return r;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buffer;

    if (!f) {
        fprintf(stderr, "Xeno: cannot open '%s'\n", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    size = ftell(f);

    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buffer = xalloc((size_t)size + 1);

    if (fread(buffer, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "Xeno: cannot read '%s'\n", path);
        free(buffer);
        fclose(f);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(f);

    return buffer;
}

/* ============================================================
   Diagnostics
   ============================================================ */

typedef struct {
    int line;
    int column;
} Position;

static void diagnostic(Position p, const char *message)
{
    fprintf(stderr, "Xeno:%d:%d: error: %s\n",
            p.line, p.column, message);
}

static void diagnostic_at(int line, int column, const char *message)
{
    Position p = { line, column };
    diagnostic(p, message);
}

/* ============================================================
   Lexer
   ============================================================ */

typedef enum {
    T_EOF,
    T_IDENT,
    T_NUMBER,
    T_STRING,

    T_FN,
    T_LET,
    T_CONST,
    T_IF,
    T_ELSE,
    T_WHILE,
    T_RETURN,
    T_TRUE,
    T_FALSE,

    T_LPAREN,
    T_RPAREN,
    T_LBRACE,
    T_RBRACE,
    T_COMMA,
    T_COLON,
    T_SEMICOLON,
    T_DOT,
    T_ARROW,

    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_PERCENT,

    T_ASSIGN,
    T_EQ,
    T_NE,
    T_LT,
    T_LE,
    T_GT,
    T_GE,

    T_AND,
    T_OR,
    T_NOT
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    Position pos;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

typedef struct {
    const char *source;
    size_t index;
    int line;
    int column;
    int failed;
    TokenList tokens;
} Lexer;

static char lex_peek(Lexer *l)
{
    return l->source[l->index];
}

static char lex_peek_next(Lexer *l)
{
    if (!l->source[l->index])
        return '\0';

    return l->source[l->index + 1];
}

static char lex_advance(Lexer *l)
{
    char c = l->source[l->index];

    if (c) {
        l->index++;

        if (c == '\n') {
            l->line++;
            l->column = 1;
        } else {
            l->column++;
        }
    }

    return c;
}

static int lex_match(Lexer *l, char expected)
{
    if (lex_peek(l) != expected)
        return 0;

    lex_advance(l);
    return 1;
}

static void token_push(Lexer *l, TokenKind kind,
                       const char *start, size_t length,
                       Position pos)
{
    TokenList *list = &l->tokens;

    if (list->count == list->capacity) {
        list->capacity = list->capacity
            ? list->capacity * 2
            : 128;

        list->items = xrealloc(
            list->items,
            list->capacity * sizeof(Token)
        );
    }

    Token *t = &list->items[list->count++];

    t->kind = kind;
    t->pos = pos;
    t->text = xalloc(length + 1);

    memcpy(t->text, start, length);
    t->text[length] = '\0';
}

static TokenKind keyword_kind(const char *s)
{
    if (!strcmp(s, "fn"))     return T_FN;
    if (!strcmp(s, "let"))    return T_LET;
    if (!strcmp(s, "const"))  return T_CONST;
    if (!strcmp(s, "if"))     return T_IF;
    if (!strcmp(s, "else"))   return T_ELSE;
    if (!strcmp(s, "while"))  return T_WHILE;
    if (!strcmp(s, "return")) return T_RETURN;
    if (!strcmp(s, "true"))   return T_TRUE;
    if (!strcmp(s, "false"))  return T_FALSE;

    return T_IDENT;
}

static void lex_error(Lexer *l, Position p, const char *message)
{
    diagnostic(p, message);
    l->failed = 1;
}

static TokenList lex_source(const char *source, int *failed)
{
    Lexer l = {0};

    l.source = source;
    l.line = 1;
    l.column = 1;

    while (lex_peek(&l)) {
        char c = lex_peek(&l);
        Position pos = { l.line, l.column };

        if (isspace((unsigned char)c)) {
            lex_advance(&l);
            continue;
        }

        if (c == '/' && lex_peek_next(&l) == '/') {
            while (lex_peek(&l) && lex_peek(&l) != '\n')
                lex_advance(&l);

            continue;
        }

        if (c == '/' && lex_peek_next(&l) == '*') {
            lex_advance(&l);
            lex_advance(&l);

            while (lex_peek(&l) &&
                   !(lex_peek(&l) == '*' &&
                     lex_peek_next(&l) == '/')) {
                lex_advance(&l);
            }

            if (!lex_peek(&l)) {
                lex_error(&l, pos, "unterminated block comment");
                break;
            }

            lex_advance(&l);
            lex_advance(&l);
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = l.index;

            while (isalnum((unsigned char)lex_peek(&l)) ||
                   lex_peek(&l) == '_') {
                lex_advance(&l);
            }

            size_t len = l.index - start;
            char *word = xalloc(len + 1);

            memcpy(word, source + start, len);
            word[len] = '\0';

            token_push(
                &l,
                keyword_kind(word),
                word,
                len,
                pos
            );

            free(word);
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t start = l.index;
            int has_dot = 0;

            while (isdigit((unsigned char)lex_peek(&l)))
                lex_advance(&l);

            if (lex_peek(&l) == '.' &&
                isdigit((unsigned char)lex_peek_next(&l))) {
                has_dot = 1;
                lex_advance(&l);

                while (isdigit((unsigned char)lex_peek(&l)))
                    lex_advance(&l);
            }

            (void)has_dot;

            token_push(
                &l, T_NUMBER,
                source + start, l.index - start, pos
            );

            continue;
        }

        if (c == '"') {
            size_t start = l.index;

            lex_advance(&l);
            int closed = 0;

            while (lex_peek(&l)) {
                char ch = lex_advance(&l);

                if (ch == '\\') {
                    if (lex_peek(&l))
                        lex_advance(&l);

                    continue;
                }

                if (ch == '"') {
                    closed = 1;
                    break;
                }

                if (ch == '\n')
                    break;
            }

            if (!closed) {
                lex_error(&l, pos, "unterminated string literal");
                continue;
            }

            token_push(
                &l, T_STRING,
                source + start, l.index - start, pos
            );

            continue;
        }

        lex_advance(&l);

        switch (c) {
        case '(': token_push(&l,T_LPAREN,"(",1,pos); break;
        case ')': token_push(&l,T_RPAREN,")",1,pos); break;
        case '{': token_push(&l,T_LBRACE,"{",1,pos); break;
        case '}': token_push(&l,T_RBRACE,"}",1,pos); break;
        case ',': token_push(&l,T_COMMA,",",1,pos); break;
        case ':': token_push(&l,T_COLON,":",1,pos); break;
        case ';': token_push(&l,T_SEMICOLON,";",1,pos); break;
        case '.': token_push(&l,T_DOT,".",1,pos); break;
        case '+': token_push(&l,T_PLUS,"+",1,pos); break;
        case '*': token_push(&l,T_STAR,"*",1,pos); break;
        case '/': token_push(&l,T_SLASH,"/",1,pos); break;
        case '%': token_push(&l,T_PERCENT,"%",1,pos); break;

        case '-':
            if (lex_match(&l, '>'))
                token_push(&l,T_ARROW,"->",2,pos);
            else
                token_push(&l,T_MINUS,"-",1,pos);
            break;

        case '=':
            if (lex_match(&l, '='))
                token_push(&l,T_EQ,"==",2,pos);
            else
                token_push(&l,T_ASSIGN,"=",1,pos);
            break;

        case '!':
            if (lex_match(&l, '='))
                token_push(&l,T_NE,"!=",2,pos);
            else
                token_push(&l,T_NOT,"!",1,pos);
            break;

        case '<':
            if (lex_match(&l, '='))
                token_push(&l,T_LE,"<=",2,pos);
            else
                token_push(&l,T_LT,"<",1,pos);
            break;

        case '>':
            if (lex_match(&l, '='))
                token_push(&l,T_GE,">=",2,pos);
            else
                token_push(&l,T_GT,">",1,pos);
            break;

        case '&':
            if (lex_match(&l, '&'))
                token_push(&l,T_AND,"&&",2,pos);
            else
                lex_error(&l, pos, "expected '&' after '&'");
            break;

        case '|':
            if (lex_match(&l, '|'))
                token_push(&l,T_OR,"||",2,pos);
            else
                lex_error(&l, pos, "expected '|' after '|'");
            break;

        default:
            lex_error(&l, pos, "unexpected character");
            break;
        }
    }

    Position end = { l.line, l.column };
    token_push(&l, T_EOF, "", 0, end);

    *failed = l.failed;
    return l.tokens;
}

/* ============================================================
   AST
   ============================================================ */

typedef enum {
    TY_UNKNOWN,
    TY_VOID,
    TY_I32,
    TY_I64,
    TY_F64,
    TY_BOOL,
    TY_STRING
} Type;

static Type type_from_name(const char *name)
{
    if (!strcmp(name, "void"))   return TY_VOID;
    if (!strcmp(name, "i32"))    return TY_I32;
    if (!strcmp(name, "i64"))    return TY_I64;
    if (!strcmp(name, "f64"))    return TY_F64;
    if (!strcmp(name, "bool"))   return TY_BOOL;
    if (!strcmp(name, "string")) return TY_STRING;

    return TY_UNKNOWN;
}

static const char *type_name(Type t)
{
    switch (t) {
    case TY_VOID:   return "void";
    case TY_I32:    return "i32";
    case TY_I64:    return "i64";
    case TY_F64:    return "f64";
    case TY_BOOL:   return "bool";
    case TY_STRING: return "string";
    default:        return "unknown";
    }
}

static const char *c_type(Type t)
{
    switch (t) {
    case TY_VOID:   return "void";
    case TY_I32:    return "int32_t";
    case TY_I64:    return "int64_t";
    case TY_F64:    return "double";
    case TY_BOOL:   return "bool";
    case TY_STRING: return "const char *";
    default:        return "int";
    }
}

typedef enum {
    EX_NUMBER,
    EX_STRING,
    EX_BOOL,
    EX_NAME,
    EX_UNARY,
    EX_BINARY,
    EX_CALL
} ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    Position pos;
    Type type;

    union {
        struct {
            char *text;
        } literal;

        struct {
            char *name;
        } name;

        struct {
            char *op;
            Expr *value;
        } unary;

        struct {
            char *op;
            Expr *left;
            Expr *right;
        } binary;

        struct {
            char *name;
            Expr **args;
            size_t count;
        } call;
    } as;
};

typedef struct Stmt Stmt;

typedef struct {
    char *name;
    Type type;
    Position pos;
} Param;

typedef struct {
    Param *items;
    size_t count;
    size_t capacity;
} ParamList;

typedef struct {
    Stmt **items;
    size_t count;
    size_t capacity;
} StmtList;

typedef struct {
    char *name;
    Type return_type;
    ParamList params;
    Stmt *body;
    Position pos;
} Function;

typedef struct {
    Function *items;
    size_t count;
    size_t capacity;
} FunctionList;

typedef enum {
    ST_BLOCK,
    ST_VAR,
    ST_IF,
    ST_WHILE,
    ST_RETURN,
    ST_EXPR
} StmtKind;

struct Stmt {
    StmtKind kind;
    Position pos;

    union {
        struct {
            StmtList statements;
        } block;

        struct {
            char *name;
            Type declared_type;
            int is_const;
            Expr *initializer;
        } var;

        struct {
            Expr *condition;
            Stmt *then_branch;
            Stmt *else_branch;
        } if_stmt;

        struct {
            Expr *condition;
            Stmt *body;
        } while_stmt;

        struct {
            Expr *value;
        } ret;

        struct {
            Expr *value;
        } expr;
    } as;
};

typedef struct {
    FunctionList functions;
} Program;

static Expr *new_expr(ExprKind kind, Position pos)
{
    Expr *e = xalloc(sizeof(*e));
    memset(e, 0, sizeof(*e));

    e->kind = kind;
    e->pos = pos;
    e->type = TY_UNKNOWN;

    return e;
}

static Stmt *new_stmt(StmtKind kind, Position pos)
{
    Stmt *s = xalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    s->kind = kind;
    s->pos = pos;

    return s;
}

static void stmt_push(StmtList *list, Stmt *stmt)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity
            ? list->capacity * 2
            : 8;

        list->items = xrealloc(
            list->items,
            list->capacity * sizeof(Stmt *)
        );
    }

    list->items[list->count++] = stmt;
}

static void param_push(ParamList *list, Param p)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity
            ? list->capacity * 2
            : 4;

        list->items = xrealloc(
            list->items,
            list->capacity * sizeof(Param)
        );
    }

    list->items[list->count++] = p;
}

static void function_push(FunctionList *list, Function f)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity
            ? list->capacity * 2
            : 8;

        list->items = xrealloc(
            list->items,
            list->capacity * sizeof(Function)
        );
    }

    list->items[list->count++] = f;
}

/* ============================================================
   Parser
   ============================================================ */

typedef struct {
    TokenList *tokens;
    size_t current;
    int failed;
} Parser;

static Token *peek(Parser *p)
{
    return &p->tokens->items[p->current];
}

static Token *previous(Parser *p)
{
    return &p->tokens->items[p->current - 1];
}

static int check(Parser *p, TokenKind kind)
{
    return peek(p)->kind == kind;
}

static Token *advance(Parser *p)
{
    if (!check(p, T_EOF))
        p->current++;

    return previous(p);
}

static int match(Parser *p, TokenKind kind)
{
    if (!check(p, kind))
        return 0;

    advance(p);
    return 1;
}

static void parse_error(Parser *p, Position pos, const char *message)
{
    diagnostic(pos, message);
    p->failed = 1;
}

static Token *expect(Parser *p, TokenKind kind, const char *message)
{
    if (check(p, kind))
        return advance(p);

    parse_error(p, peek(p)->pos, message);

    if (!check(p, T_EOF))
        advance(p);

    return previous(p);
}

static int is_type_token(Token *t)
{
    if (t->kind != T_IDENT)
        return 0;

    return type_from_name(t->text) != TY_UNKNOWN;
}

static Type parse_type(Parser *p)
{
    Token *t = expect(p, T_IDENT, "expected a type name");
    Type type = type_from_name(t->text);

    if (type == TY_UNKNOWN)
        parse_error(p, t->pos, "unknown type");

    return type;
}

static Expr *parse_expression(Parser *p);

static Expr *parse_primary(Parser *p)
{
    Token *t = peek(p);

    if (match(p, T_NUMBER)) {
        Expr *e = new_expr(EX_NUMBER, t->pos);
        e->as.literal.text = xstrdup(t->text);
        return e;
    }

    if (match(p, T_STRING)) {
        Expr *e = new_expr(EX_STRING, t->pos);
        e->as.literal.text = xstrdup(t->text);
        return e;
    }

    if (match(p, T_TRUE) || match(p, T_FALSE)) {
        Expr *e = new_expr(EX_BOOL, t->pos);
        e->as.literal.text = xstrdup(t->text);
        return e;
    }

    if (match(p, T_IDENT)) {
        char *name = xstrdup(t->text);

        if (!strcmp(name, "ConsoleOut") && match(p, T_DOT)) {
            Token *member = expect(
                p, T_IDENT, "expected a member after 'ConsoleOut.'"
            );

            if (strcmp(member->text, "write") != 0)
                parse_error(p, member->pos, "unknown ConsoleOut member");

            free(name);
            name = xstrdup("ConsoleOut.write");
        }

        if (match(p, T_LPAREN)) {
            Expr *e = new_expr(EX_CALL, t->pos);
            e->as.call.name = name;

            while (!check(p, T_RPAREN) && !check(p, T_EOF)) {
                Expr *arg = parse_expression(p);

                if (e->as.call.count % 4 == 0) {
                    size_t cap = e->as.call.count + 4;
                    e->as.call.args = xrealloc(
                        e->as.call.args,
                        cap * sizeof(Expr *)
                    );
                }

                e->as.call.args[e->as.call.count++] = arg;

                if (!match(p, T_COMMA))
                    break;
            }

            expect(p, T_RPAREN, "expected ')' after arguments");
            return e;
        }

        Expr *e = new_expr(EX_NAME, t->pos);
        e->as.name.name = name;
        return e;
    }

    if (match(p, T_LPAREN)) {
        Expr *e = parse_expression(p);
        expect(p, T_RPAREN, "expected ')' after expression");
        return e;
    }

    parse_error(p, t->pos, "expected an expression");

    if (!check(p, T_EOF))
        advance(p);

    Expr *e = new_expr(EX_NUMBER, t->pos);
    e->as.literal.text = xstrdup("0");
    return e;
}

static Expr *parse_unary(Parser *p)
{
    if (check(p, T_MINUS) || check(p, T_NOT)) {
        Token *op = advance(p);
        Expr *e = new_expr(EX_UNARY, op->pos);

        e->as.unary.op = xstrdup(op->text);
        e->as.unary.value = parse_unary(p);

        return e;
    }

    return parse_primary(p);
}

static Expr *make_binary(Expr *left, Token *op, Expr *right)
{
    Expr *e = new_expr(EX_BINARY, op->pos);

    e->as.binary.left = left;
    e->as.binary.right = right;
    e->as.binary.op = xstrdup(op->text);

    return e;
}

static Expr *parse_factor(Parser *p)
{
    Expr *e = parse_unary(p);

    while (check(p, T_STAR) ||
           check(p, T_SLASH) ||
           check(p, T_PERCENT)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_unary(p));
    }

    return e;
}

static Expr *parse_term(Parser *p)
{
    Expr *e = parse_factor(p);

    while (check(p, T_PLUS) || check(p, T_MINUS)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_factor(p));
    }

    return e;
}

static Expr *parse_comparison(Parser *p)
{
    Expr *e = parse_term(p);

    while (check(p, T_LT) || check(p, T_LE) ||
           check(p, T_GT) || check(p, T_GE)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_term(p));
    }

    return e;
}

static Expr *parse_equality(Parser *p)
{
    Expr *e = parse_comparison(p);

    while (check(p, T_EQ) || check(p, T_NE)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_comparison(p));
    }

    return e;
}

static Expr *parse_and(Parser *p)
{
    Expr *e = parse_equality(p);

    while (check(p, T_AND)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_equality(p));
    }

    return e;
}

static Expr *parse_or(Parser *p)
{
    Expr *e = parse_and(p);

    while (check(p, T_OR)) {
        Token *op = advance(p);
        e = make_binary(e, op, parse_and(p));
    }

    return e;
}

static Expr *parse_assignment(Parser *p)
{
    Expr *e = parse_or(p);

    if (match(p, T_ASSIGN)) {
        Token *op = previous(p);
        Expr *right = parse_assignment(p);

        return make_binary(e, op, right);
    }

    return e;
}

static Expr *parse_expression(Parser *p)
{
    return parse_assignment(p);
}

static Stmt *parse_statement(Parser *p);

static Stmt *parse_block(Parser *p, Position pos)
{
    Stmt *s = new_stmt(ST_BLOCK, pos);

    while (!check(p, T_RBRACE) && !check(p, T_EOF)) {
        size_t old = p->current;

        stmt_push(&s->as.block.statements, parse_statement(p));

        if (p->current == old && !check(p, T_EOF))
            advance(p);
    }

    expect(p, T_RBRACE, "expected '}' after block");

    return s;
}

static Stmt *parse_statement(Parser *p)
{
    Token *t = peek(p);

    if (match(p, T_LBRACE))
        return parse_block(p, t->pos);

    if (match(p, T_LET) || match(p, T_CONST)) {
        int is_const = t->kind == T_CONST;
        Token *name = expect(p, T_IDENT, "expected variable name");

        Stmt *s = new_stmt(ST_VAR, t->pos);

        s->as.var.name = xstrdup(name->text);
        s->as.var.is_const = is_const;
        s->as.var.declared_type = TY_UNKNOWN;

        if (match(p, T_COLON))
            s->as.var.declared_type = parse_type(p);

        expect(p, T_ASSIGN, "expected '=' in variable declaration");

        s->as.var.initializer = parse_expression(p);

        expect(p, T_SEMICOLON, "expected ';' after declaration");

        return s;
    }

    if (match(p, T_IF)) {
        Stmt *s = new_stmt(ST_IF, t->pos);

        expect(p, T_LPAREN, "expected '(' after if");
        s->as.if_stmt.condition = parse_expression(p);
        expect(p, T_RPAREN, "expected ')' after condition");

        s->as.if_stmt.then_branch = parse_statement(p);

        if (match(p, T_ELSE))
            s->as.if_stmt.else_branch = parse_statement(p);

        return s;
    }

    if (match(p, T_WHILE)) {
        Stmt *s = new_stmt(ST_WHILE, t->pos);

        expect(p, T_LPAREN, "expected '(' after while");
        s->as.while_stmt.condition = parse_expression(p);
        expect(p, T_RPAREN, "expected ')' after condition");

        s->as.while_stmt.body = parse_statement(p);

        return s;
    }

    if (match(p, T_RETURN)) {
        Stmt *s = new_stmt(ST_RETURN, t->pos);

        if (!check(p, T_SEMICOLON))
            s->as.ret.value = parse_expression(p);

        expect(p, T_SEMICOLON, "expected ';' after return");

        return s;
    }

    Stmt *s = new_stmt(ST_EXPR, t->pos);

    s->as.expr.value = parse_expression(p);
    expect(p, T_SEMICOLON, "expected ';' after expression");

    return s;
}

static Function parse_function(Parser *p)
{
    Token *start = expect(p, T_FN, "expected 'fn'");
    Token *name = expect(p, T_IDENT, "expected function name");

    Function f = {0};

    f.name = xstrdup(name->text);
    f.return_type = TY_VOID;
    f.pos = start->pos;

    expect(p, T_LPAREN, "expected '(' after function name");

    while (!check(p, T_RPAREN) && !check(p, T_EOF)) {
        Token *param_name = expect(p, T_IDENT, "expected parameter name");

        expect(p, T_COLON, "expected ':' after parameter name");

        Param param = {0};

        param.name = xstrdup(param_name->text);
        param.type = parse_type(p);
        param.pos = param_name->pos;

        param_push(&f.params, param);

        if (!match(p, T_COMMA))
            break;
    }

    expect(p, T_RPAREN, "expected ')' after parameters");

    if (match(p, T_ARROW))
        f.return_type = parse_type(p);

    expect(p, T_LBRACE, "expected '{' before function body");

    f.body = parse_block(p, start->pos);

    return f;
}

static Program parse_program(Parser *p)
{
    Program program = {0};

    while (!check(p, T_EOF)) {
        size_t old = p->current;

        if (!check(p, T_FN)) {
            parse_error(p, peek(p)->pos,
                        "expected a function declaration beginning with 'fn'");

            advance(p);
            continue;
        }

        function_push(&program.functions, parse_function(p));

        if (old == p->current)
            advance(p);
    }

    return program;
}

/* ============================================================
   Type checker and scopes
   ============================================================ */

typedef struct {
    char *name;
    Type type;
    int depth;
    int is_const;
} Symbol;

typedef struct {
    Symbol *items;
    size_t count;
    size_t capacity;
    int depth;
    int failed;
    Program *program;
    Function *function;
} Checker;

static Function *find_function(Program *p, const char *name)
{
    for (size_t i = 0; i < p->functions.count; i++) {
        if (!strcmp(p->functions.items[i].name, name))
            return &p->functions.items[i];
    }

    return NULL;
}

static Symbol *find_symbol(Checker *c, const char *name)
{
    for (size_t i = c->count; i > 0; i--) {
        if (!strcmp(c->items[i - 1].name, name))
            return &c->items[i - 1];
    }

    return NULL;
}

static void checker_error(Checker *c, Position pos, const char *message)
{
    diagnostic(pos, message);
    c->failed = 1;
}

static int is_numeric(Type t)
{
    return t == TY_I32 || t == TY_I64 || t == TY_F64;
}

static Type common_numeric(Type a, Type b)
{
    if (!is_numeric(a) || !is_numeric(b))
        return TY_UNKNOWN;

    if (a == TY_F64 || b == TY_F64)
        return TY_F64;

    if (a == TY_I64 || b == TY_I64)
        return TY_I64;

    return TY_I32;
}

static void declare_symbol(Checker *c, const char *name,
                           Type type, int is_const, Position pos)
{
    for (size_t i = c->count; i > 0; i--) {
        Symbol *s = &c->items[i - 1];

        if (s->depth < c->depth)
            break;

        if (!strcmp(s->name, name)) {
            checker_error(c, pos, "duplicate declaration in this scope");
            return;
        }
    }

    if (c->count == c->capacity) {
        c->capacity = c->capacity ? c->capacity * 2 : 16;
        c->items = xrealloc(
            c->items, c->capacity * sizeof(Symbol)
        );
    }

    Symbol *s = &c->items[c->count++];

    s->name = xstrdup(name);
    s->type = type;
    s->depth = c->depth;
    s->is_const = is_const;
}

static void enter_scope(Checker *c)
{
    c->depth++;
}

static void leave_scope(Checker *c)
{
    while (c->count && c->items[c->count - 1].depth == c->depth) {
        free(c->items[c->count - 1].name);
        c->count--;
    }

    c->depth--;
}

static Type check_expr(Checker *c, Expr *e);

static Type check_call(Checker *c, Expr *e)
{
    if (!strcmp(e->as.call.name, "ConsoleOut.write")) {
        for (size_t i = 0; i < e->as.call.count; i++) {
            Type t = check_expr(c, e->as.call.args[i]);

            if (t == TY_VOID || t == TY_UNKNOWN)
                checker_error(c, e->pos,
                              "ConsoleOut.write cannot print this value");
        }

        return e->type = TY_VOID;
    }

    Function *f = find_function(c->program, e->as.call.name);

    if (!f) {
        checker_error(c, e->pos, "call to an unknown function");
        return e->type = TY_UNKNOWN;
    }

    if (f->params.count != e->as.call.count)
        checker_error(c, e->pos, "incorrect number of function arguments");

    size_t count = f->params.count < e->as.call.count
        ? f->params.count
        : e->as.call.count;

    for (size_t i = 0; i < e->as.call.count; i++) {
        Type actual = check_expr(c, e->as.call.args[i]);

        if (i < count && actual != f->params.items[i].type)
            checker_error(c, e->as.call.args[i]->pos,
                          "function argument type mismatch");
    }

    return e->type = f->return_type;
}

static Type check_expr(Checker *c, Expr *e)
{
    switch (e->kind) {
    case EX_NUMBER:
        if (strchr(e->as.literal.text, '.'))
            return e->type = TY_F64;

        return e->type = TY_I32;

    case EX_STRING:
        return e->type = TY_STRING;

    case EX_BOOL:
        return e->type = TY_BOOL;

    case EX_NAME: {
        Symbol *s = find_symbol(c, e->as.name.name);

        if (!s) {
            checker_error(c, e->pos, "use of an undeclared variable");
            return e->type = TY_UNKNOWN;
        }

        return e->type = s->type;
    }

    case EX_CALL:
        return check_call(c, e);

    case EX_UNARY: {
        Type t = check_expr(c, e->as.unary.value);

        if (!strcmp(e->as.unary.op, "-")) {
            if (!is_numeric(t))
                checker_error(c, e->pos,
                              "unary '-' requires a numeric value");

            return e->type = t;
        }

        if (strcmp(e->as.unary.op, "!") == 0) {
            if (t != TY_BOOL)
                checker_error(c, e->pos,
                              "'!' requires a bool value");

            return e->type = TY_BOOL;
        }

        return e->type = TY_UNKNOWN;
    }

    case EX_BINARY: {
        Expr *left = e->as.binary.left;
        Expr *right = e->as.binary.right;
        const char *op = e->as.binary.op;

        Type a = check_expr(c, left);
        Type b = check_expr(c, right);

        if (!strcmp(op, "=")) {
            if (left->kind != EX_NAME) {
                checker_error(c, e->pos,
                              "assignment target must be a variable");
                return e->type = TY_UNKNOWN;
            }

            Symbol *s = find_symbol(c, left->as.name.name);

            if (!s) {
                checker_error(c, left->pos,
                              "assignment to an undeclared variable");
                return e->type = TY_UNKNOWN;
            }

            if (s->is_const)
                checker_error(c, left->pos,
                              "cannot assign to a constant");

            if (a != b)
                checker_error(c, e->pos,
                              "assignment type mismatch");

            return e->type = s->type;
        }

        if (!strcmp(op, "&&") || !strcmp(op, "||")) {
            if (a != TY_BOOL || b != TY_BOOL)
                checker_error(c, e->pos,
                              "logical operators require bool operands");

            return e->type = TY_BOOL;
        }

        if (!strcmp(op, "==") || !strcmp(op, "!=")) {
            if (a != b && common_numeric(a, b) == TY_UNKNOWN)
                checker_error(c, e->pos,
                              "incompatible equality operands");

            if (a == TY_VOID || b == TY_VOID)
                checker_error(c, e->pos,
                              "cannot compare void values");

            return e->type = TY_BOOL;
        }

        if (!strcmp(op, "<") || !strcmp(op, "<=") ||
            !strcmp(op, ">") || !strcmp(op, ">=")) {
            if (common_numeric(a, b) == TY_UNKNOWN)
                checker_error(c, e->pos,
                              "comparison requires numeric operands");

            return e->type = TY_BOOL;
        }

        if (common_numeric(a, b) == TY_UNKNOWN) {
            checker_error(c, e->pos,
                          "arithmetic requires numeric operands");
            return e->type = TY_UNKNOWN;
        }

        if (!strcmp(op, "%") &&
            (a == TY_F64 || b == TY_F64)) {
            checker_error(c, e->pos,
                          "operator '%' does not support f64");
        }

        return e->type = common_numeric(a, b);
    }
    }

    return TY_UNKNOWN;
}

static void check_stmt(Checker *c, Stmt *s);

static void check_block(Checker *c, Stmt *s)
{
    enter_scope(c);

    for (size_t i = 0; i < s->as.block.statements.count; i++)
        check_stmt(c, s->as.block.statements.items[i]);

    leave_scope(c);
}

static void check_stmt(Checker *c, Stmt *s)
{
    switch (s->kind) {
    case ST_BLOCK:
        check_block(c, s);
        break;

    case ST_VAR: {
        Type actual = check_expr(c, s->as.var.initializer);
        Type declared = s->as.var.declared_type;

        if (declared == TY_UNKNOWN)
            declared = actual;
        else if (declared != actual)
            checker_error(c, s->pos,
                          "variable initializer type mismatch");

        if (declared == TY_VOID || declared == TY_UNKNOWN)
            checker_error(c, s->pos,
                          "invalid variable type");

        declare_symbol(
            c,
            s->as.var.name,
            declared,
            s->as.var.is_const,
            s->pos
        );

        s->as.var.declared_type = declared;
        break;
    }

    case ST_IF: {
        Type t = check_expr(c, s->as.if_stmt.condition);

        if (t != TY_BOOL)
            checker_error(c, s->pos,
                          "if condition must have type bool");

        check_stmt(c, s->as.if_stmt.then_branch);

        if (s->as.if_stmt.else_branch)
            check_stmt(c, s->as.if_stmt.else_branch);

        break;
    }

    case ST_WHILE: {
        Type t = check_expr(c, s->as.while_stmt.condition);

        if (t != TY_BOOL)
            checker_error(c, s->pos,
                          "while condition must have type bool");

        check_stmt(c, s->as.while_stmt.body);
        break;
    }

    case ST_RETURN:
        if (!s->as.ret.value) {
            if (c->function->return_type != TY_VOID)
                checker_error(c, s->pos,
                              "non-void function must return a value");
        } else {
            Type t = check_expr(c, s->as.ret.value);

            if (c->function->return_type == TY_VOID)
                checker_error(c, s->pos,
                              "void function cannot return a value");
            else if (t != c->function->return_type)
                checker_error(c, s->pos,
                              "return type mismatch");
        }
        break;

    case ST_EXPR:
        check_expr(c, s->as.expr.value);
        break;
    }
}

static int check_program(Program *p)
{
    int failed = 0;
    int main_found = 0;

    for (size_t i = 0; i < p->functions.count; i++) {
        Function *f = &p->functions.items[i];

        for (size_t j = 0; j < i; j++) {
            if (!strcmp(f->name, p->functions.items[j].name)) {
                diagnostic(f->pos, "duplicate function declaration");
                failed = 1;
            }
        }

        if (!strcmp(f->name, "main")) {
            main_found = 1;

            if (f->params.count != 0)
                diagnostic(f->pos, "main must not have parameters"),
                failed = 1;

            if (f->return_type != TY_I32)
                diagnostic(f->pos, "main must return i32"),
                failed = 1;
        }
    }

    if (!main_found) {
        fprintf(stderr, "Xeno: error: program must define fn main() -> i32\n");
        failed = 1;
    }

    for (size_t i = 0; i < p->functions.count; i++) {
        Function *f = &p->functions.items[i];
        Checker c = {0};

        c.program = p;
        c.function = f;

        for (size_t j = 0; j < f->params.count; j++) {
            for (size_t k = 0; k < j; k++) {
                if (!strcmp(f->params.items[j].name,
                            f->params.items[k].name)) {
                    diagnostic(f->params.items[j].pos,
                               "duplicate parameter name");
                    c.failed = 1;
                }
            }

            declare_symbol(
                &c,
                f->params.items[j].name,
                f->params.items[j].type,
                0,
                f->params.items[j].pos
            );
        }

        check_stmt(&c, f->body);

        if (c.failed)
            failed = 1;

        for (size_t j = 0; j < c.count; j++)
            free(c.items[j].name);

        free(c.items);
    }

    return failed;
}

/* ============================================================
   C code generation
   ============================================================ */

typedef struct {
    FILE *out;
    int indent;
} Generator;

static void indent(Generator *g)
{
    for (int i = 0; i < g->indent; i++)
        fputs("    ", g->out);
}

static void gen_expr(Generator *g, Expr *e);

static void gen_print(Generator *g, Expr *e)
{
    fputs("(", g->out);

    for (size_t i = 0; i < e->as.call.count; i++) {
        Expr *arg = e->as.call.args[i];

        if (i)
            fputs(", ", g->out);

        if (arg->type == TY_STRING) {
            fputs("printf(\"%s\", ", g->out);
            gen_expr(g, arg);
            fputc(')', g->out);
        } else if (arg->type == TY_BOOL) {
            fputs("printf(\"%s\", (", g->out);
            fputs("(", g->out);
            gen_expr(g, arg);
            fputs(") ? \"true\" : \"false\"))", g->out);
        } else if (arg->type == TY_F64) {
            fputs("printf(\"%g\", (double)(", g->out);
            gen_expr(g, arg);
            fputs("))", g->out);
        } else if (arg->type == TY_I64) {
            fputs("printf(\"%lld\", (long long)(", g->out);
            gen_expr(g, arg);
            fputs("))", g->out);
        } else {
            fputs("printf(\"%d\", (int)(", g->out);
            gen_expr(g, arg);
            fputs("))", g->out);
        }
    }

    if (e->as.call.count)
        fputs(", ", g->out);

    fputs("printf(\"\\n\"), (void)0)", g->out);
}

static void gen_expr(Generator *g, Expr *e)
{
    switch (e->kind) {
    case EX_NUMBER:
    case EX_STRING:
        fputs(e->as.literal.text, g->out);
        break;

    case EX_BOOL:
        fputs(!strcmp(e->as.literal.text, "true") ? "true" : "false",
              g->out);
        break;

    case EX_NAME:
        fprintf(g->out, "xv_%s", e->as.name.name);
        break;

    case EX_UNARY:
        fprintf(g->out, "(%s", e->as.unary.op);
        gen_expr(g, e->as.unary.value);
        fputc(')', g->out);
        break;

    case EX_BINARY:
        fputc('(', g->out);
        gen_expr(g, e->as.binary.left);
        fprintf(g->out, " %s ", e->as.binary.op);
        gen_expr(g, e->as.binary.right);
        fputc(')', g->out);
        break;

    case EX_CALL:
        if (!strcmp(e->as.call.name, "ConsoleOut.write")) {
            gen_print(g, e);
            break;
        }

        if (!strcmp(e->as.call.name, "main"))
            fputs("main", g->out);
        else
            fprintf(g->out, "xf_%s", e->as.call.name);

        fputc('(', g->out);

        for (size_t i = 0; i < e->as.call.count; i++) {
            if (i)
                fputs(", ", g->out);

            gen_expr(g, e->as.call.args[i]);
        }

        fputc(')', g->out);
        break;
    }
}

static void gen_stmt(Generator *g, Stmt *s);

static void gen_block(Generator *g, Stmt *s)
{
    fputs("{\n", g->out);
    g->indent++;

    for (size_t i = 0; i < s->as.block.statements.count; i++)
        gen_stmt(g, s->as.block.statements.items[i]);

    g->indent--;
    indent(g);
    fputc('}', g->out);
}

static void gen_stmt(Generator *g, Stmt *s)
{
    indent(g);

    switch (s->kind) {
    case ST_BLOCK:
        gen_block(g, s);
        fputc('\n', g->out);
        break;

    case ST_VAR:
        fprintf(g->out, "%s xv_%s = ",
                c_type(s->as.var.declared_type),
                s->as.var.name);

        gen_expr(g, s->as.var.initializer);
        fputs(";\n", g->out);
        break;

    case ST_IF:
        fputs("if (", g->out);
        gen_expr(g, s->as.if_stmt.condition);
        fputs(") ", g->out);

        if (s->as.if_stmt.then_branch->kind == ST_BLOCK) {
            gen_block(g, s->as.if_stmt.then_branch);
        } else {
            fputs("{\n", g->out);
            g->indent++;
            gen_stmt(g, s->as.if_stmt.then_branch);
            g->indent--;
            indent(g);
            fputc('}', g->out);
        }

        if (s->as.if_stmt.else_branch) {
            fputs(" else ", g->out);

            if (s->as.if_stmt.else_branch->kind == ST_BLOCK) {
                gen_block(g, s->as.if_stmt.else_branch);
            } else {
                fputs("{\n", g->out);
                g->indent++;
                gen_stmt(g, s->as.if_stmt.else_branch);
                g->indent--;
                indent(g);
                fputc('}', g->out);
            }
        }

        fputc('\n', g->out);
        break;

    case ST_WHILE:
        fputs("while (", g->out);
        gen_expr(g, s->as.while_stmt.condition);
        fputs(") ", g->out);

        if (s->as.while_stmt.body->kind == ST_BLOCK) {
            gen_block(g, s->as.while_stmt.body);
        } else {
            fputs("{\n", g->out);
            g->indent++;
            gen_stmt(g, s->as.while_stmt.body);
            g->indent--;
            indent(g);
            fputc('}', g->out);
        }

        fputc('\n', g->out);
        break;

    case ST_RETURN:
        fputs("return", g->out);

        if (s->as.ret.value) {
            fputc(' ', g->out);
            gen_expr(g, s->as.ret.value);
        }

        fputs(";\n", g->out);
        break;

    case ST_EXPR:
        gen_expr(g, s->as.expr.value);
        fputs(";\n", g->out);
        break;
    }
}

static int generate_c(Program *p, const char *path)
{
    FILE *out = fopen(path, "wb");

    if (!out) {
        fprintf(stderr, "Xeno: cannot write '%s'\n", path);
        return 0;
    }

    Generator g = {0};
    g.out = out;

    fputs(
        "#include <stdio.h>\n"
        "#include <stdint.h>\n"
        "#include <stdbool.h>\n\n",
        out
    );

    for (size_t i = 0; i < p->functions.count; i++) {
        Function *f = &p->functions.items[i];

        fprintf(out, "%s %s(",
                c_type(f->return_type),
                !strcmp(f->name, "main")
                    ? "main"
                    : xformat("xf_", f->name));

        if (!f->params.count) {
            fputs("void", out);
        } else {
            for (size_t j = 0; j < f->params.count; j++) {
                if (j)
                    fputs(", ", out);

                fprintf(out, "%s xv_%s",
                        c_type(f->params.items[j].type),
                        f->params.items[j].name);
            }
        }

        fputs(");\n", out);
    }

    fputc('\n', out);

    for (size_t i = 0; i < p->functions.count; i++) {
        Function *f = &p->functions.items[i];

        fprintf(out, "%s %s(",
                c_type(f->return_type),
                !strcmp(f->name, "main")
                    ? "main"
                    : xformat("xf_", f->name));

        if (!f->params.count) {
            fputs("void", out);
        } else {
            for (size_t j = 0; j < f->params.count; j++) {
                if (j)
                    fputs(", ", out);

                fprintf(out, "%s xv_%s",
                        c_type(f->params.items[j].type),
                        f->params.items[j].name);
            }
        }

        fputs(") ", out);
        gen_block(&g, f->body);
        fputs("\n\n", out);
    }

    fclose(out);
    return 1;
}

/* ============================================================
   Compiler pipeline and command line
   ============================================================ */

static int compile_source(const char *source_path,
                          const char *generated_path,
                          int emit_c)
{
    char *source = read_file(source_path);

    if (!source)
        return 0;

    int lex_failed = 0;
    TokenList tokens = lex_source(source, &lex_failed);

    if (lex_failed) {
        free(source);
        return 0;
    }

    Parser parser = {0};
    parser.tokens = &tokens;

    Program program = parse_program(&parser);

    if (parser.failed) {
        free(source);
        return 0;
    }

    if (check_program(&program)) {
        free(source);
        return 0;
    }

    if (emit_c && !generate_c(&program, generated_path)) {
        free(source);
        return 0;
    }

    free(source);
    return 1;
}

static char *default_output(const char *source)
{
    const char *slash1 = strrchr(source, '/');
    const char *slash2 = strrchr(source, '\\');
    const char *base = source;

    if (slash1 && slash1 + 1 > base)
        base = slash1 + 1;

    if (slash2 && slash2 + 1 > base)
        base = slash2 + 1;

    char *name = xstrdup(base);
    char *dot = strrchr(name, '.');

    if (dot)
        *dot = '\0';

    char *out = xformat(name, ".exe");
    free(name);

    return out;
}

static char *generated_name(const char *source)
{
    return xformat(source, ".generated.c");
}

static int build_program(const char *source,
                         const char *output,
                         int execute)
{
    char *generated = generated_name(source);

    if (!compile_source(source, generated, 1)) {
        free(generated);
        return 1;
    }

    size_t command_size =
        strlen(generated) + strlen(output) + 128;

    char *command = xalloc(command_size);

    snprintf(
        command,
        command_size,
        "gcc -std=c11 -O2 \"%s\" -o \"%s\"",
        generated,
        output
    );

    int result = system(command);

    free(command);
    free(generated);

    if (result != 0) {
        fprintf(stderr, "Xeno: GCC compilation failed\n");
        return 1;
    }

    printf("Built: %s\n", output);

    if (execute) {
        size_t n = strlen(output) + 8;
        char *run_command = xalloc(n);

        snprintf(run_command, n, "\"%s\"", output);

        result = system(run_command);
        free(run_command);

        return result == -1 ? 1 : result;
    }

    return 0;
}

static void print_help(void)
{
    puts("Xeno compiler " XENO_VERSION);
    puts("");
    puts("Usage:");
    puts("  xeno build <file.xeno> [-o output.exe]");
    puts("  xeno run <file.xeno>");
    puts("  xeno check <file.xeno>");
    puts("  xeno --make");
    puts("  xeno --version");
    puts("  xeno --help");
}

int xeno_main(int argc, char **argv)
{
    if (argc < 2) {
        print_help();
        return 0;
    }

    if (!strcmp(argv[1], "--version") ||
        !strcmp(argv[1], "-v")) {
        puts("Xeno " XENO_VERSION);
        return 0;
    }

    if (!strcmp(argv[1], "--help") ||
        !strcmp(argv[1], "-h")) {
        print_help();
        return 0;
    }

    if (!strcmp(argv[1], "--make")) {
        int result = system("make");
        return result == 0 ? 0 : 1;
    }

    if (!strcmp(argv[1], "build")) {
        if (argc < 3) {
            fprintf(stderr, "Usage: xeno build <file.xeno> [-o output.exe]\n");
            return 2;
        }

        char *output = default_output(argv[2]);

        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "-o") && i + 1 < argc) {
                free(output);
                output = xstrdup(argv[++i]);
            } else {
                fprintf(stderr, "Xeno: unknown build option '%s'\n", argv[i]);
                free(output);
                return 2;
            }
        }

        int result = build_program(argv[2], output, 0);
        free(output);
        return result;
    }

    if (!strcmp(argv[1], "run")) {
        if (argc < 3) {
            fprintf(stderr, "Usage: xeno run <file.xeno>\n");
            return 2;
        }

        char *output = default_output(argv[2]);
        int result = build_program(argv[2], output, 1);

        free(output);
        return result;
    }

    if (!strcmp(argv[1], "check")) {
        if (argc < 3) {
            fprintf(stderr, "Usage: xeno check <file.xeno>\n");
            return 2;
        }

        char *generated = generated_name(argv[2]);
        int result = compile_source(argv[2], generated, 0);

        if (result)
            puts("Xeno: no errors found");

        free(generated);
        return result ? 0 : 1;
    }

    fprintf(stderr, "Xeno: unknown command '%s'\n\n", argv[1]);
    print_help();

    return 2;
}