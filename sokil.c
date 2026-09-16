/*
 * Sokil (Сокіл) — дуже проста мова програмування.
 * Одна реалізація на чистих C. Нуль залежностей.
 *
 * Збірка:
 *   Linux/macOS:  cc -O2 -std=c99 -o sokil sokil.c
 *   Windows:      gcc -O2 -std=c99 -o sokil.exe sokil.c
 *
 * Запуск:
 *   ./sokil                 # REPL
 *   ./sokil файл.sokil      # виконати програму
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#else
#include <unistd.h>
#endif

/* аргументи командного рядка — видимі як args у програмі */
static char **g_argv;
static int g_argc;
static int g_args_start = 2;  /* де починаються користувацькі аргументи */

#if defined(__GNUC__) || defined(__clang__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

/* ═══════════ Арена (список блоків — вказівники ніколи не рухаються) ═══════════ */
typedef struct Blk { struct Blk *next; size_t used, cap; char data[]; } Blk;
typedef struct { Blk *head; } Arena;

static Blk *blk_new(size_t cap) {
    Blk *b = (Blk *)malloc(sizeof(Blk) + cap);
    if (!b) { fprintf(stderr, "Не вистачає пам'яті\n"); exit(1); }
    b->next = NULL; b->used = 0; b->cap = cap;
    return b;
}

static void *a_alloc(Arena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;                      /* вирівнювання */
    if (!a->head) a->head = blk_new(1 << 16);
    Blk *b = a->head;
    if (n > b->cap) {
        Blk *nb = blk_new(n);                        /* величезний запит */
        nb->next = b->next; b->next = nb; b = nb;
    } else if (b->used + n > b->cap) {
        size_t nc = b->cap * 2;
        while (nc < b->used + n) nc *= 2;
        Blk *nb = blk_new(nc);
        nb->next = b->next; b->next = nb; b = nb;
    }
    void *p = b->data + b->used;
    b->used += n;
    return p;
}

static char *a_strdup(Arena *a, const char *s) {
    size_t l = strlen(s) + 1;
    char *p = (char *)a_alloc(a, l);
    memcpy(p, s, l);
    return p;
}

static char *a_strndup(Arena *a, const char *s, size_t l) {
    char *p = (char *)a_alloc(a, l + 1);
    memcpy(p, s, l);
    p[l] = '\0';
    return p;
}

/* ═══════════ Глобальна обробка помилок лексера/парсера ═══════════
   (однопотокова програма — один глобальний jmp_buf) */
static jmp_buf lex_err_jmp;
static char lex_err_msg[512];

static NORETURN void fatal(const char *msg) {
    snprintf(lex_err_msg, sizeof lex_err_msg, "%s", msg);
    longjmp(lex_err_jmp, 1);
}

static NORETURN void fatal_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lex_err_msg, sizeof lex_err_msg, fmt, ap);
    va_end(ap);
    longjmp(lex_err_jmp, 1);
}

/* ═══════════ Токени ═══════════ */
enum {
    T_NUM, T_STR, T_IDENT,
    T_TRUE, T_FALSE, T_NIL,
    T_LET, T_IF, T_ELIF, T_ELSE, T_WHILE, T_FN, T_RETURN,
    T_FOR, T_BREAK, T_CONTINUE,
    T_IMPORT, T_IN, T_TRY, T_CATCH,
    T_AND, T_OR, T_NOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_PLUSPLUS, T_MINUSMINUS,
    T_EQ, T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ, T_PERCENTEQ,
    T_EQEQ, T_NEQ, T_LT, T_GT, T_LTE, T_GTE, T_BANG, T_QUESTION,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_COLON, T_SEMICOLON,
    T_NEWLINE, T_EOF
};

typedef struct { int type; double num; char *text; int line; } Token;

static const char *tok_names[] = {
    "NUM","STR","IDENT","TRUE","FALSE","NIL","LET","IF","ELIF","ELSE",
    "WHILE","FN","RETURN","FOR","BREAK","CONTINUE","AND","OR","NOT",
    "IMPORT","IN","TRY","CATCH",
    "PLUS","MINUS","STAR","SLASH","PERCENT","PLUSPLUS","MINUSMINUS",
    "EQ","PLUSEQ","MINUSEQ","STAREQ","SLASHEQ","PERCENTEQ","EQEQ","NEQ","LT","GT","LTE","GTE","BANG","QUESTION","LPAREN",
    "RPAREN","LBRACE","RBRACE","LBRACKET","RBRACKET","COMMA","COLON","SEMICOLON","NEWLINE","EOF"
};

/* ═══════════ Лексер ═══════════ */
typedef struct {
    Arena *a;
    const char *src;
    size_t p, total;
    int line;
    Token *toks;
    int n, cap;
} Lexer;

static const struct { const char *kw; int t; } keywords[] = {
    {"true",T_TRUE},{"false",T_FALSE},{"nil",T_NIL},
    {"let",T_LET},{"if",T_IF},{"elif",T_ELIF},{"else",T_ELSE},
    {"while",T_WHILE},{"fn",T_FN},{"return",T_RETURN},
    {"for",T_FOR},{"break",T_BREAK},{"continue",T_CONTINUE},
    {"and",T_AND},{"or",T_OR},{"not",T_NOT},
    {"import",T_IMPORT},{"in",T_IN},{"try",T_TRY},{"catch",T_CATCH},
    {NULL,0}
};

static char lx_peek(Lexer *lx, int off) {
    size_t i = lx->p + (size_t)off;
    return i < lx->total ? lx->src[i] : '\0';
}

static char lx_adv(Lexer *lx) {
    char c = lx->src[lx->p++];
    if (c == '\n') lx->line++;
    return c;
}

static Token lx_tok(Lexer *lx, int type, double num, char *text) {
    Token t; t.type = type; t.num = num; t.text = text; t.line = lx->line;
    return t;
}

static void lx_push(Lexer *lx, Token t) {
    if (lx->n == lx->cap) {
        lx->cap = lx->cap ? lx->cap * 2 : 64;
        Token *nt = (Token *)a_alloc(lx->a, (size_t)lx->cap * sizeof(Token));
        memcpy(nt, lx->toks, (size_t)lx->n * sizeof(Token));
        lx->toks = nt;
    }
    lx->toks[lx->n++] = t;
}

static int lx_keyword(const char *s) {
    for (int i = 0; keywords[i].kw; i++)
        if (!strcmp(keywords[i].kw, s)) return keywords[i].t;
    return 0;
}

static Token lx_string(Lexer *lx, char open) {
    int line = lx->line;
    lx_adv(lx);                               /* відкриваюча лапка */
    size_t start = lx->p;
    size_t len = 0;
    while (1) {
        char c = lx_adv(lx);
        if (c == open) break;
        if (c == '\0' || c == '\n')
            fatal_fmt("Незакритий рядок (рядок %d)", line);
        if (c == '\\') {
            char e = lx_adv(lx);
            if (e == 'n' || e == 't' || e == '\\' || e == '"' || e == '\'') len++;
            else len += 2;
        } else len++;
    }
    size_t p = start, o = 0;
    char *out = (char *)a_alloc(lx->a, len + 1);
    while (p < lx->p - 1) {
        char c = lx->src[p++];
        if (c == '\\') {
            char e = lx->src[p++];
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == '\\') c = '\\';
            else if (e == '"') c = '"';
            else if (e == '\'') c = '\'';
            else { out[o++] = '\\'; out[o++] = e; continue; }
            out[o++] = c;
        } else out[o++] = c;
    }
    out[o] = '\0';
    return lx_tok(lx, T_STR, 0, out);
}

static Token lx_number(Lexer *lx) {
    size_t start = lx->p;
    int has_dot = 0;
    while (1) {
        char c = lx_peek(lx, 0);
        if (isdigit((unsigned char)c)) { lx_adv(lx); continue; }
        if (c == '.' && !has_dot) { has_dot = 1; lx_adv(lx); continue; }
        break;
    }
    char *buf = a_strndup(lx->a, lx->src + start, lx->p - start);
    return lx_tok(lx, T_NUM, strtod(buf, NULL), buf);
}

static Token lx_ident(Lexer *lx) {
    size_t start = lx->p;
    while (isalnum((unsigned char)lx_peek(lx, 0)) || lx_peek(lx, 0) == '_')
        lx_adv(lx);
    char *s = a_strndup(lx->a, lx->src + start, lx->p - start);
    int kw = lx_keyword(s);
    return lx_tok(lx, kw ? kw : T_IDENT, 0, s);
}

static Token *lex(Arena *a, const char *src, int *out_n) {
    Lexer lx;
    memset(&lx, 0, sizeof lx);
    lx.a = a; lx.src = src; lx.total = strlen(src); lx.line = 1;

    while (lx.p < lx.total) {
        char c = lx_peek(&lx, 0);
        if (c == ' ' || c == '\t' || c == '\r') { lx_adv(&lx); continue; }
        if (c == '\n') { lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_NEWLINE, 0, NULL)); continue; }
        if (c == '/' && lx_peek(&lx, 1) == '/') {          /* коментар // */
            while (lx_peek(&lx, 0) != '\n' && lx_peek(&lx, 0) != '\0') lx_adv(&lx);
            continue;
        }
        if (c == '#') {                             /* коментар # (python-style) */
            while (lx_peek(&lx, 0) != '\n' && lx_peek(&lx, 0) != '\0') lx_adv(&lx);
            continue;
        }
        if (c == '/' && lx_peek(&lx, 1) == '*') {   // блоковий коментар
            lx_adv(&lx); lx_adv(&lx);
            while (1) {
                if (lx_peek(&lx, 0) == '\0')
                    fatal_fmt("Незакритий коментар /* (рядок %d)", lx.line);
                if (lx_peek(&lx, 0) == '*' && lx_peek(&lx, 1) == '/') { lx_adv(&lx); lx_adv(&lx); break; }
                lx_adv(&lx);
            }
            continue;
        }
        if (c == '"' || c == '\'') { lx_push(&lx, lx_string(&lx, c)); continue; }
        if (isdigit((unsigned char)c)) { lx_push(&lx, lx_number(&lx)); continue; }
        if (isalpha((unsigned char)c) || c == '_') { lx_push(&lx, lx_ident(&lx)); continue; }

        if (c == '=' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_EQEQ, 0, NULL)); continue; }
        if (c == '!' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_NEQ, 0, NULL)); continue; }
        if (c == '<' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_LTE, 0, NULL)); continue; }
        if (c == '>' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_GTE, 0, NULL)); continue; }
        if (c == '+' && lx_peek(&lx, 1) == '+') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_PLUSPLUS, 0, NULL)); continue; }
        if (c == '-' && lx_peek(&lx, 1) == '-') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_MINUSMINUS, 0, NULL)); continue; }
        if (c == '+' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_PLUSEQ, 0, NULL)); continue; }
        if (c == '-' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_MINUSEQ, 0, NULL)); continue; }
        if (c == '*' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_STAREQ, 0, NULL)); continue; }
        if (c == '/' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_SLASHEQ, 0, NULL)); continue; }
        if (c == '%' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_PERCENTEQ, 0, NULL)); continue; }

        int t = 0;
        switch (c) {
            case '+': t = T_PLUS; break;
            case '-': t = T_MINUS; break;
            case '*': t = T_STAR; break;
            case '/': t = T_SLASH; break;
            case '%': t = T_PERCENT; break;
            case '=': t = T_EQ; break;
            case '<': t = T_LT; break;
            case '>': t = T_GT; break;
            case '!': t = T_BANG; break;
            case '?': t = T_QUESTION; break;
            case '(': t = T_LPAREN; break;
            case ')': t = T_RPAREN; break;
            case '{': t = T_LBRACE; break;
            case '}': t = T_RBRACE; break;
            case '[': t = T_LBRACKET; break;
            case ']': t = T_RBRACKET; break;
            case ',': t = T_COMMA; break;
            case ':': t = T_COLON; break;
            case ';': t = T_SEMICOLON; break;
            default:
                fatal_fmt("Невідомий символ '%c' (рядок %d)", c, lx.line);
        }
        lx_adv(&lx);
        lx_push(&lx, lx_tok(&lx, t, 0, NULL));
    }
    lx_push(&lx, lx_tok(&lx, T_EOF, 0, NULL));
    *out_n = lx.n;
    return lx.toks;
}

/* ═══════════ AST ═══════════ */
enum {
    N_NUM, N_STR, N_VAR, N_BOOL, N_NIL, N_ARRAY, N_INDEX, N_IDXASSIGN,
    N_BINOP, N_UNARY, N_ASSIGN, N_LET, N_IF, N_WHILE, N_FN, N_CALL,
    N_RETURN, N_BLOCK, N_FOR, N_BREAK, N_CONTINUE,
    N_IMPORT, N_FOREACH, N_TERNARY, N_TRY
};

typedef struct Node Node;
typedef struct Value Value;
typedef struct Env Env;
typedef struct Interp Interp;

struct Node {
    int kind;
    int line;
    double num;
    char *str;
    Node *a, *b, *c;           /* операнди / умова / гілки */
    Node **items;              /* тіло блоку, аргументи, елементи масиву, параметри */
    int n;
};

static Node *new_node(Arena *a, int kind, int line) {
    Node *n = (Node *)a_alloc(a, sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->kind = kind; n->line = line;
    return n;
}

static void n_add(Arena *a, Node *n, Node *item) {
    Node **ni = (Node **)a_alloc(a, sizeof(Node *) * (size_t)(n->n + 1));
    if (n->items) memcpy(ni, n->items, sizeof(Node *) * (size_t)n->n);
    ni[n->n++] = item;
    n->items = ni;
}

/* ═══════════ Парсер ═══════════ */
typedef struct { Token *toks; int i, n; Arena *a; } Parser;

static Token pr_peek(Parser *p) { return p->toks[p->i]; }
static Token pr_adv(Parser *p) { return p->toks[p->i++]; }

static void pr_err(Parser *p, const char *fmt, ...) {
    Token t = pr_peek(p);
    char buf[512];
    snprintf(buf, sizeof buf, "рядок %d: ", t.line);
    size_t bl = strlen(buf);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + bl, sizeof buf - bl, fmt, ap);
    va_end(ap);
    fatal(buf);
}

static Token pr_expect(Parser *p, int type) {
    Token t = pr_peek(p);
    if (t.type != type)
        pr_err(p, "очікувався %s, але знайдено %s", tok_names[type], tok_names[t.type]);
    return pr_adv(p);
}

static void pr_skip_nl(Parser *p) {
    /* Пропускаємо переходи рядка І крапки з комою (роздільники операторів) */
    while (pr_peek(p).type == T_NEWLINE || pr_peek(p).type == T_SEMICOLON) p->i++;
}

static Node *pr_expr(Parser *p);
static Node *pr_stmt(Parser *p);
static Node *pr_for(Parser *p);

static Node *pr_block(Parser *p) {
    pr_expect(p, T_LBRACE);
    pr_skip_nl(p);
    Node *b = new_node(p->a, N_BLOCK, pr_peek(p).line);
    while (pr_peek(p).type != T_RBRACE) {
        n_add(p->a, b, pr_stmt(p));
        pr_skip_nl(p);
    }
    pr_expect(p, T_RBRACE);
    return b;
}

static Node *pr_let(Parser *p) {
    int line = pr_adv(p).line;                    /* let */
    Token name = pr_expect(p, T_IDENT);
    pr_expect(p, T_EQ);
    Node *n = new_node(p->a, N_LET, line);
    n->str = name.text;
    n->a = pr_expr(p);
    return n;
}

static Node *pr_import(Parser *p) {
    int line = pr_adv(p).line;                    /* import */
    Token path = pr_expect(p, T_STR);
    Node *n = new_node(p->a, N_IMPORT, line);
    n->str = path.text;
    return n;
}

static Node *pr_try(Parser *p) {
    int line = pr_adv(p).line;                    /* try */
    Node *n = new_node(p->a, N_TRY, line);
    n->a = pr_block(p);
    pr_expect(p, T_CATCH);
    if (pr_peek(p).type == T_IDENT) {
        Token v = pr_adv(p);
        n->str = v.text;
    }
    n->b = pr_block(p);
    return n;
}

static Node *pr_if(Parser *p) {
    int line = pr_adv(p).line;                    /* if */
    Node *n = new_node(p->a, N_IF, line);
    n->a = pr_expr(p);
    n->b = pr_block(p);
    if (pr_peek(p).type == T_ELSE) {
        pr_adv(p);
        if (pr_peek(p).type == T_IF || pr_peek(p).type == T_ELIF)
            n->c = pr_if(p);
        else
            n->c = pr_block(p);
    }
    return n;
}

static Node *pr_while(Parser *p) {
    int line = pr_adv(p).line;                    /* while */
    Node *n = new_node(p->a, N_WHILE, line);
    n->a = pr_expr(p);
    n->b = pr_block(p);
    return n;
}

static Node *pr_fn(Parser *p) {
    int line = pr_adv(p).line;                    /* fn */
    Token name = pr_expect(p, T_IDENT);
    Node *n = new_node(p->a, N_FN, line);
    n->str = name.text;
    pr_expect(p, T_LPAREN);
    if (pr_peek(p).type != T_RPAREN) {
        /* імена параметрів зберігаємо як покажчики на рядки у items */
        n_add(p->a, n, (Node *)(uintptr_t)pr_expect(p, T_IDENT).text);
        while (pr_peek(p).type == T_COMMA) {
            pr_adv(p);
            n_add(p->a, n, (Node *)(uintptr_t)pr_expect(p, T_IDENT).text);
        }
    }
    pr_expect(p, T_RPAREN);
    n->c = pr_block(p);                           /* тіло — N_BLOCK */
    return n;
}

static Node *pr_return(Parser *p) {
    int line = pr_adv(p).line;                    /* return */
    Node *n = new_node(p->a, N_RETURN, line);
    int t = pr_peek(p).type;
    if (t != T_NEWLINE && t != T_EOF && t != T_RBRACE)
        n->a = pr_expr(p);
    return n;
}

static Node *pr_expr_stmt(Parser *p) {
    /* складені присвоєння: x += 1, x -= 2, x *= 3, x /= 4, x %= 5 */
    if (pr_peek(p).type == T_IDENT && p->i + 1 < p->n) {
        int t2 = p->toks[p->i + 1].type;
        const char *op = t2 == T_PLUSEQ ? "+" : t2 == T_MINUSEQ ? "-" :
                         t2 == T_STAREQ ? "*" : t2 == T_SLASHEQ ? "/" :
                         t2 == T_PERCENTEQ ? "%" : NULL;
        if (op) {
            Token name = p->toks[p->i];
            p->i += 2;
            Node *n = new_node(p->a, N_ASSIGN, name.line);
            n->str = name.text;
            Node *b = new_node(p->a, N_BINOP, name.line);
            b->str = (char *)op;
            b->a = new_node(p->a, N_VAR, name.line);
            b->a->str = name.text;
            b->b = pr_expr(p);
            n->a = b;
            return n;
        }
    }
    Node *e = pr_expr(p);
    if (pr_peek(p).type == T_EQ) {
        if (e->kind == N_VAR) {
            pr_adv(p);
            Node *n = new_node(p->a, N_ASSIGN, e->line);
            n->str = e->str;
            n->a = pr_expr(p);
            return n;
        }
        if (e->kind == N_INDEX) {
            pr_adv(p);
            Node *n = new_node(p->a, N_IDXASSIGN, e->line);
            n->a = e->a;                          /* об'єкт */
            n->b = e->b;                          /* індекс */
            n->c = pr_expr(p);                    /* значення */
            return n;
        }
        pr_err(p, "неможливе присвоєння");
    }
    return e;
}

static Node *pr_stmt(Parser *p) {
    switch (pr_peek(p).type) {
        case T_LET:    return pr_let(p);
        case T_IF:     return pr_if(p);
        case T_WHILE:  return pr_while(p);
        case T_FOR:    return pr_for(p);
        case T_FN:     return pr_fn(p);
        case T_RETURN: return pr_return(p);
        case T_BREAK:  { int line = pr_adv(p).line; return new_node(p->a, N_BREAK, line); }
        case T_CONTINUE: { int line = pr_adv(p).line; return new_node(p->a, N_CONTINUE, line); }
        case T_IMPORT: return pr_import(p);
        case T_TRY:    return pr_try(p);
        case T_LBRACE: return pr_block(p);
        case T_NEWLINE:
        case T_SEMICOLON: p->i++; return pr_stmt(p);
        default:       return pr_expr_stmt(p);
    }
}

/* for i = 0; i < 10; i = i + 1 { ... } */
static Node *pr_for(Parser *p) {
    int line = pr_adv(p).line;                    /* for */
    if (pr_peek(p).type == T_IDENT && p->i + 1 < p->n && p->toks[p->i + 1].type == T_IN) {
        Token name = p->toks[p->i];
        p->i += 2;
        Node *n = new_node(p->a, N_FOREACH, line);
        n->str = name.text;
        n->a = pr_expr(p);                        /* колекція */
        n->c = pr_block(p);                       /* тіло */
        return n;
    }
    Node *n = new_node(p->a, N_FOR, line);
    n->a = pr_expr_stmt(p);                       /* ініціалізація */
    pr_expect(p, T_SEMICOLON);
    n->b = pr_expr(p);                            /* умова */
    pr_expect(p, T_SEMICOLON);
    n->items = (Node **)a_alloc(p->a, sizeof(Node *));
    n->items[0] = pr_expr_stmt(p);                /* крок */
    n->n = 1;
    n->c = pr_block(p);                           /* тіло */
    return n;
}

/* вирази — прецедентне сходження */
static Node *pr_or(Parser *p);
static Node *pr_and(Parser *p);
static Node *pr_not(Parser *p);
static Node *pr_cmp(Parser *p);
static Node *pr_add(Parser *p);
static Node *pr_mul(Parser *p);
static Node *pr_unary(Parser *p);
static Node *pr_call(Parser *p);
static Node *pr_primary(Parser *p);

static Node *pr_expr(Parser *p) {
    Node *l = pr_or(p);
    if (pr_peek(p).type == T_QUESTION) {
        int line = pr_adv(p).line;                /* ? */
        Node *n = new_node(p->a, N_TERNARY, line);
        n->a = l;
        n->b = pr_expr(p);
        pr_expect(p, T_COLON);
        n->c = pr_expr(p);
        return n;
    }
    return l;
}

static Node *pr_or(Parser *p) {
    Node *l = pr_and(p);
    while (pr_peek(p).type == T_OR) {
        pr_adv(p);
        Node *n = new_node(p->a, N_BINOP, l->line);
        n->str = "or"; n->a = l; n->b = pr_and(p);
        l = n;
    }
    return l;
}

static Node *pr_and(Parser *p) {
    Node *l = pr_not(p);
    while (pr_peek(p).type == T_AND) {
        pr_adv(p);
        Node *n = new_node(p->a, N_BINOP, l->line);
        n->str = "and"; n->a = l; n->b = pr_not(p);
        l = n;
    }
    return l;
}

static Node *pr_not(Parser *p) {
    if (pr_peek(p).type == T_NOT || pr_peek(p).type == T_BANG) {
        int line = pr_adv(p).line;
        Node *n = new_node(p->a, N_UNARY, line);
        n->str = "not"; n->a = pr_not(p);
        return n;
    }
    return pr_cmp(p);
}

static const char *cmp_op(int t) {
    return t == T_EQEQ ? "==" : t == T_NEQ ? "!=" : t == T_LT ? "<" :
           t == T_GT ? ">" : t == T_LTE ? "<=" : t == T_GTE ? ">=" : NULL;
}

static Node *pr_cmp(Parser *p) {
    Node *l = pr_add(p);
    while (1) {
        const char *op = cmp_op(pr_peek(p).type);
        if (!op) break;
        pr_adv(p);
        Node *n = new_node(p->a, N_BINOP, l->line);
        n->str = (char *)op; n->a = l; n->b = pr_add(p);
        l = n;
    }
    return l;
}

static const char *add_op(int t) {
    return t == T_PLUS ? "+" : t == T_MINUS ? "-" : NULL;
}

static Node *pr_add(Parser *p) {
    Node *l = pr_mul(p);
    while (1) {
        const char *op = add_op(pr_peek(p).type);
        if (!op) break;
        pr_adv(p);
        Node *n = new_node(p->a, N_BINOP, l->line);
        n->str = (char *)op; n->a = l; n->b = pr_mul(p);
        l = n;
    }
    return l;
}

static const char *mul_op(int t) {
    return t == T_STAR ? "*" : t == T_SLASH ? "/" : t == T_PERCENT ? "%" : NULL;
}

static Node *pr_mul(Parser *p) {
    Node *l = pr_unary(p);
    while (1) {
        const char *op = mul_op(pr_peek(p).type);
        if (!op) break;
        pr_adv(p);
        Node *n = new_node(p->a, N_BINOP, l->line);
        n->str = (char *)op; n->a = l; n->b = pr_unary(p);
        l = n;
    }
    return l;
}

static Node *pr_unary(Parser *p) {
    if (pr_peek(p).type == T_MINUS) {
        int line = pr_adv(p).line;
        Node *n = new_node(p->a, N_UNARY, line);
        n->str = "-"; n->a = pr_unary(p);
        return n;
    }
    return pr_call(p);
}

static Node *pr_call(Parser *p) {
    Node *e = pr_primary(p);
    while (1) {
        if (pr_peek(p).type == T_LPAREN && e->kind == N_VAR) {
            pr_adv(p);
            Node *n = new_node(p->a, N_CALL, e->line);
            n->str = e->str;
            if (pr_peek(p).type != T_RPAREN) {
                n_add(p->a, n, pr_expr(p));
                while (pr_peek(p).type == T_COMMA) {
                    pr_adv(p);
                    n_add(p->a, n, pr_expr(p));
                }
            }
            pr_expect(p, T_RPAREN);
            e = n;
        } else if (pr_peek(p).type == T_LBRACKET) {
            pr_adv(p);
            Node *n = new_node(p->a, N_INDEX, e->line);
            n->a = e;
            n->b = pr_expr(p);
            pr_expect(p, T_RBRACKET);
            e = n;
        } else if (pr_peek(p).type == T_PLUSPLUS || pr_peek(p).type == T_MINUSMINUS) {
            int op = pr_peek(p).type;                       /* i++ / i-- */
            int line = pr_adv(p).line;
            if (e->kind != N_VAR) pr_err(p, "++/-- лише для змінних");
            Node *one = new_node(p->a, N_NUM, line);
            one->num = 1.0;
            Node *bin = new_node(p->a, N_BINOP, line);
            bin->str = (char *)(op == T_PLUSPLUS ? "+" : "-");
            bin->a = e; bin->b = one;
            Node *as = new_node(p->a, N_ASSIGN, line);
            as->str = e->str; as->a = bin;
            e = as;
        } else break;
    }
    return e;
}

static Node *pr_primary(Parser *p) {
    Token t = pr_peek(p);
    Node *n;
    switch (t.type) {
        case T_NUM:
            pr_adv(p);
            n = new_node(p->a, N_NUM, t.line);
            n->num = t.num;
            return n;
        case T_STR:
            pr_adv(p);
            n = new_node(p->a, N_STR, t.line);
            n->str = t.text;
            return n;
        case T_TRUE:
            pr_adv(p);
            n = new_node(p->a, N_BOOL, t.line);
            n->num = 1;
            return n;
        case T_FALSE:
            pr_adv(p);
            n = new_node(p->a, N_BOOL, t.line);
            n->num = 0;
            return n;
        case T_NIL:
            pr_adv(p);
            return new_node(p->a, N_NIL, t.line);
        case T_IDENT:
            pr_adv(p);
            n = new_node(p->a, N_VAR, t.line);
            n->str = t.text;
            return n;
        case T_LPAREN:
            pr_adv(p);
            n = pr_expr(p);
            pr_expect(p, T_RPAREN);
            return n;
        case T_LBRACKET: {
            pr_adv(p);
            n = new_node(p->a, N_ARRAY, t.line);
            if (pr_peek(p).type != T_RBRACKET) {
                n_add(p->a, n, pr_expr(p));
                while (pr_peek(p).type == T_COMMA) {
                    pr_adv(p);
                    n_add(p->a, n, pr_expr(p));
                }
            }
            pr_expect(p, T_RBRACKET);
            return n;
        }
        case T_LBRACE: {
            /* об'єкт {key: value, ...} → плоский парний масив ['key', value, ...] */
            pr_adv(p);
            n = new_node(p->a, N_ARRAY, t.line);
            if (pr_peek(p).type != T_RBRACE) {
                for (;;) {
                    Token k = pr_peek(p);
                    if (k.type != T_STR && k.type != T_IDENT)
                        pr_err(p, "очікувався ключ об'єкта");
                    pr_adv(p);
                    Node *kn = new_node(p->a, N_STR, k.line);
                    kn->str = k.text;
                    n_add(p->a, n, kn);
                    pr_expect(p, T_COLON);
                    n_add(p->a, n, pr_expr(p));
                    if (pr_peek(p).type != T_COMMA) break;
                    pr_adv(p);
                }
            }
            pr_expect(p, T_RBRACE);
            return n;
        }
        default:
            pr_err(p, "несподівана лексема %s", tok_names[t.type]);
            return NULL;
    }
}

static Node *parse(Arena *a, Token *toks, int n) {
    Parser p;
    p.toks = toks; p.i = 0; p.n = n; p.a = a;
    pr_skip_nl(&p);
    Node *prog = new_node(a, N_BLOCK, 1);
    while (pr_peek(&p).type != T_EOF) {
        n_add(a, prog, pr_stmt(&p));
        pr_skip_nl(&p);
    }
    return prog;
}

/* ═══════════ Значення ═══════════ */
enum { V_NUM, V_BOOL, V_NIL, V_STR, V_ARR, V_FUNC, V_NATIVE };

typedef Value (*NativeFn)(Interp *I, Value *args, int n);

struct Value {
    int type;
    double num;
    int boolean;
    char *str;             /* V_STR */
    Value *items;          /* V_ARR */
    int len;               /* V_ARR */
    Node *fn;              /* V_FUNC */
    Env *closure;          /* V_FUNC */
    char *fnname;          /* V_FUNC / V_NATIVE ім'я */
    NativeFn native;       /* V_NATIVE */
};

struct Env {
    Env *parent;
    char **names;
    Value *values;
    int n, cap;
    Arena *a;
};

/* ═══════════ Інтерпретатор ═══════════ */
struct Interp {
    Arena *a;
    Env *env;
    jmp_buf *ret_jmp;      /* поточний кадр `return` (ланцюжок викликів) */
    Value ret_val;
    jmp_buf *loop_jmp;     /* поточний кадр циклу (break/continue) */
    int loop_kind;         /* 1 = break, 2 = continue */
    jmp_buf err_jmp;       /* помилка → верхній рівень */
    char errmsg[512];
};

static Env *env_new(Arena *a, Env *parent) {
    Env *e = (Env *)a_alloc(a, sizeof(Env));
    memset(e, 0, sizeof(Env));
    e->a = a; e->parent = parent;
    return e;
}

static void env_set(Env *e, const char *name, Value v) {
    for (int i = 0; i < e->n; i++)
        if (!strcmp(e->names[i], name)) { e->values[i] = v; return; }
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 16;
        char **nn = (char **)a_alloc(e->a, (size_t)e->cap * sizeof(char *));
        Value *nv = (Value *)a_alloc(e->a, (size_t)e->cap * sizeof(Value));
        memcpy(nn, e->names, (size_t)e->n * sizeof(char *));
        memcpy(nv, e->values, (size_t)e->n * sizeof(Value));
        e->names = nn; e->values = nv;
    }
    e->names[e->n] = (char *)name;
    e->values[e->n] = v;
    e->n++;
}

static int env_get(Env *e, const char *name, Value *out) {
    for (Env *s = e; s; s = s->parent)
        for (int i = 0; i < s->n; i++)
            if (!strcmp(s->names[i], name)) { *out = s->values[i]; return 1; }
    return 0;
}

static int env_assign(Env *e, const char *name, Value v) {
    for (Env *s = e; s; s = s->parent)
        for (int i = 0; i < s->n; i++)
            if (!strcmp(s->names[i], name)) { s->values[i] = v; return 1; }
    return 0;
}

static NORETURN void err_raise(Interp *I, const char *msg) {
    snprintf(I->errmsg, sizeof I->errmsg, "%s", msg);
    longjmp(I->err_jmp, 1);
}

static Value mk_num(double d) { Value v; v.type = V_NUM; v.num = d; return v; }
static Value mk_bool(int b)   { Value v; v.type = V_BOOL; v.boolean = b; return v; }
static Value mk_nil(void)     { Value v; v.type = V_NIL; return v; }
static Value mk_str(char *s)  { Value v; v.type = V_STR; v.str = s; return v; }

static int truthy(Value v) {
    switch (v.type) {
        case V_NIL:  return 0;
        case V_BOOL: return v.boolean;
        case V_NUM:  return v.num != 0;
        case V_STR:  return v.str[0] != '\0';
        case V_ARR:  return v.len > 0;
        default:     return 1;
    }
}

static char *fmt_num(Arena *a, double d) {
    char buf[64];
    if (d == (double)(long long)d)
        snprintf(buf, sizeof buf, "%lld", (long long)d);
    else
        snprintf(buf, sizeof buf, "%g", d);
    return a_strdup(a, buf);
}

static char *fmt_into(Arena *a, Value v, int depth);

static char *fmt_into(Arena *a, Value v, int depth) {
    switch (v.type) {
        case V_NIL:  return a_strdup(a, "nil");
        case V_BOOL: return a_strdup(a, v.boolean ? "true" : "false");
        case V_NUM:  return fmt_num(a, v.num);
        case V_STR:  return v.str;
        case V_ARR: {
            if (depth > 32) return a_strdup(a, "[...]");
            size_t total = 1;                     /* '[' */
            char **parts = (char **)a_alloc(a, (size_t)(v.len ? v.len : 1) * sizeof(char *));
            for (int i = 0; i < v.len; i++) {
                parts[i] = fmt_into(a, v.items[i], depth + 1);
                total += strlen(parts[i]);
                if (i) total += 2;                /* ", " */
            }
            total += 2;                           /* ']' + '\0' */
            char *buf = (char *)a_alloc(a, total);
            size_t o = 0;
            buf[o++] = '[';
            for (int i = 0; i < v.len; i++) {
                if (i) { buf[o++] = ','; buf[o++] = ' '; }
                size_t sl = strlen(parts[i]);
                memcpy(buf + o, parts[i], sl); o += sl;
            }
            buf[o++] = ']';
            buf[o] = '\0';
            return buf;
        }
        default: {
            char buf[128];
            snprintf(buf, sizeof buf, "<fn %s>", v.fnname ? v.fnname : "?");
            return a_strdup(a, buf);
        }
    }
}

static char *fmt_val(Arena *a, Value v) { return fmt_into(a, v, 0); }

static Value eval(Interp *I, Node *n);
static void exec(Interp *I, Node *n);

static void exec_block(Interp *I, Node *b) {
    Env *old = I->env;
    I->env = env_new(I->a, old);
    for (int i = 0; i < b->n; i++)
        exec(I, b->items[i]);
    I->env = old;
}

static int value_eq(Value l, Value r) {
    if (l.type == V_NUM && r.type == V_NUM) return l.num == r.num;
    if (l.type == V_STR && r.type == V_STR) return !strcmp(l.str, r.str);
    if (l.type == V_BOOL && r.type == V_BOOL) return l.boolean == r.boolean;
    if (l.type == V_NIL && r.type == V_NIL) return 1;
    if (l.type != r.type) return 0;
    if (l.type == V_ARR) {
        if (l.len != r.len) return 0;
        for (int i = 0; i < l.len; i++)
            if (!value_eq(l.items[i], r.items[i])) return 0;
        return 1;
    }
    return l.boolean == r.boolean && l.num == r.num;
}

static Value binop(Interp *I, const char *op, Value l, Value r) {
    if (!strcmp(op, "+")) {
        if (l.type == V_NUM && r.type == V_NUM) return mk_num(l.num + r.num);
        if (l.type == V_STR && r.type == V_STR) {
            size_t ll = strlen(l.str), rl = strlen(r.str);
            char *s = (char *)a_alloc(I->a, ll + rl + 1);
            memcpy(s, l.str, ll); memcpy(s + ll, r.str, rl + 1);
            return mk_str(s);
        }
        if ((l.type == V_STR && r.type == V_NUM) || (l.type == V_NUM && r.type == V_STR)) {
            /* "рядок" + число ⇒ конкатенація */
            char numbuf[32];
            double d = l.type == V_NUM ? l.num : r.num;
            if (d == (double)(long long)d) snprintf(numbuf, sizeof numbuf, "%lld", (long long)d);
            else snprintf(numbuf, sizeof numbuf, "%g", d);
            const char *sa = l.type == V_STR ? l.str : numbuf;
            const char *sb = r.type == V_STR ? r.str : numbuf;
            size_t la = strlen(sa), lb = strlen(sb);
            char *s = (char *)a_alloc(I->a, la + lb + 1);
            memcpy(s, sa, la); memcpy(s + la, sb, lb + 1);
            return mk_str(s);
        }
        if (l.type == V_ARR && r.type == V_ARR) {
            Value v; v.type = V_ARR; v.len = l.len + r.len;
            v.items = (Value *)a_alloc(I->a, (size_t)v.len * sizeof(Value));
            memcpy(v.items, l.items, (size_t)l.len * sizeof(Value));
            memcpy(v.items + l.len, r.items, (size_t)r.len * sizeof(Value));
            return v;
        }
        err_raise(I, "Операція '+' несумісна з типами");
    }
    if (!strcmp(op, "-")) {
        if (l.type == V_NUM && r.type == V_NUM) return mk_num(l.num - r.num);
        err_raise(I, "Операція '-' лише для чисел");
    }
    if (!strcmp(op, "*")) {
        if (l.type == V_NUM && r.type == V_NUM) return mk_num(l.num * r.num);
        if (l.type == V_STR && r.type == V_NUM) {
            int times = (int)r.num;
            if (times < 0) times = 0;
            size_t sl = strlen(l.str), total = sl * (size_t)times;
            char *buf = (char *)a_alloc(I->a, total + 1);
            buf[0] = '\0';
            for (int i = 0; i < times; i++) memcpy(buf + i * sl, l.str, sl);
            buf[total] = '\0';
            return mk_str(buf);
        }
        err_raise(I, "Операція '*' лише для чисел");
    }
    if (!strcmp(op, "/")) {
        if (l.type == V_NUM && r.type == V_NUM) {
            if (r.num == 0) err_raise(I, "Ділення на нуль");
            return mk_num(l.num / r.num);
        }
        err_raise(I, "Операція '/' лише для чисел");
    }
    if (!strcmp(op, "%")) {
        if (l.type == V_NUM && r.type == V_NUM) {
            if (r.num == 0) err_raise(I, "Ділення на нуль");
            return mk_num((double)((long long)l.num % (long long)r.num));
        }
        err_raise(I, "Операція '%' лише для чисел");
    }
    if (!strcmp(op, "==")) return mk_bool(value_eq(l, r));
    if (!strcmp(op, "!=")) return mk_bool(!value_eq(l, r));
    if (!strcmp(op, "and")) return mk_bool(truthy(l) && truthy(r));
    if (!strcmp(op, "or"))  return mk_bool(truthy(l) || truthy(r));
    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        int c;
        if (l.type == V_NUM && r.type == V_NUM) c = (l.num > r.num) - (l.num < r.num);
        else if (l.type == V_STR && r.type == V_STR) c = strcmp(l.str, r.str);
        else err_raise(I, "Порівняння лише для чисел або рядків");
        if (!strcmp(op, "<"))  return mk_bool(c < 0);
        if (!strcmp(op, ">"))  return mk_bool(c > 0);
        if (!strcmp(op, "<=")) return mk_bool(c <= 0);
        return mk_bool(c >= 0);
    }
    err_raise(I, "Невідома операція");
    return mk_nil();
}

static Value call_func(Interp *I, Node *fndef, Env *closure, Value *args, int n);

static Value eval(Interp *I, Node *n) {
    switch (n->kind) {
        case N_NUM:  return mk_num(n->num);
        case N_STR:  return mk_str(n->str);
        case N_BOOL: return mk_bool(n->num != 0);
        case N_NIL:  return mk_nil();
        case N_VAR: {
            Value v;
            if (!env_get(I->env, n->str, &v)) {
                char buf[256];
                snprintf(buf, sizeof buf, "Невідома змінна '%s' (рядок %d)", n->str, n->line);
                err_raise(I, buf);
            }
            return v;
        }
        case N_ARRAY: {
            Value v; v.type = V_ARR; v.len = n->n;
            v.items = (Value *)a_alloc(I->a, (size_t)(n->n ? n->n : 1) * sizeof(Value));
            for (int i = 0; i < n->n; i++) v.items[i] = eval(I, n->items[i]);
            return v;
        }
        case N_INDEX: {
            Value obj = eval(I, n->a);
            Value iv = eval(I, n->b);
            if (iv.type != V_NUM) err_raise(I, "Індекс має бути числом");
            long long i = (long long)iv.num;
            if (obj.type == V_ARR) {
                long long L = obj.len;
                if (i < 0) i += L;
                if (i < 0 || i >= L) {
                    char buf[128];
                    snprintf(buf, sizeof buf, "Індекс %lld поза межами (рядок %d)", i, n->line);
                    err_raise(I, buf);
                }
                return obj.items[i];
            }
            if (obj.type == V_STR) {
                long long L = (long long)strlen(obj.str);
                if (i < 0) i += L;
                if (i < 0 || i >= L) {
                    char buf[128];
                    snprintf(buf, sizeof buf, "Індекс %lld поза межами (рядок %d)", i, n->line);
                    err_raise(I, buf);
                }
                char *s = (char *)a_alloc(I->a, 2);
                s[0] = obj.str[i]; s[1] = '\0';
                return mk_str(s);
            }
            err_raise(I, "Індексація лише для масивів і рядків");
        }
        case N_UNARY: {
            if (!strcmp(n->str, "-")) return mk_num(-eval(I, n->a).num);
            if (!strcmp(n->str, "not")) return mk_bool(!truthy(eval(I, n->a)));
            err_raise(I, "Невідома унарна операція");
        }
        case N_TERNARY:
            return truthy(eval(I, n->a)) ? eval(I, n->b) : eval(I, n->c);
        case N_BINOP:
            return binop(I, n->str, eval(I, n->a), eval(I, n->b));
        case N_CALL: {
            Value args[64];
            if (n->n > 64) err_raise(I, "Забагато аргументів");
            int argc = n->n;
            for (int i = 0; i < argc; i++) args[i] = eval(I, n->items[i]);
            Value f;
            if (!env_get(I->env, n->str, &f)) {
                char buf[256];
                snprintf(buf, sizeof buf, "Невідома функція '%s' (рядок %d)", n->str, n->line);
                err_raise(I, buf);
            }
            if (f.type == V_NATIVE) return f.native(I, args, argc);
            if (f.type == V_FUNC) return call_func(I, f.fn, f.closure, args, argc);
            err_raise(I, "'не-функція' викликана як функція");
        }
        default:
            err_raise(I, "Вираз не можна обчислити тут");
            return mk_nil();
    }
}

static Value call_func(Interp *I, Node *fndef, Env *closure, Value *args, int n) {
    if (fndef->n != n) {
        char buf[256];
        snprintf(buf, sizeof buf, "Функція '%s' очікує %d аргументів, отримано %d",
                 fndef->str, fndef->n, n);
        err_raise(I, buf);
    }
    Env *e = env_new(I->a, closure);
    for (int i = 0; i < n; i++) {
        char *pname = (char *)(uintptr_t)fndef->items[i];
        env_set(e, pname, args[i]);
    }
    jmp_buf jb;
    jmp_buf *prev = I->ret_jmp;
    I->ret_jmp = &jb;
    Env *old = I->env;
    I->env = e;
    Value r = mk_nil();
    if (setjmp(jb) == 0) {
        for (int i = 0; i < fndef->c->n; i++)
            exec(I, fndef->c->items[i]);
    } else {
        r = I->ret_val;
    }
    I->env = old;
    I->ret_jmp = prev;
    return r;
}

static void exec(Interp *I, Node *n) {
    switch (n->kind) {
        case N_LET: {
            Value v = eval(I, n->a);
            env_set(I->env, n->str, v);
            return;
        }
        case N_ASSIGN: {
            Value v = eval(I, n->a);
            if (!env_assign(I->env, n->str, v))
                env_set(I->env, n->str, v);        /* легкість: авто-оголошення */
            return;
        }
        case N_IDXASSIGN: {
            Value obj = eval(I, n->a);
            Value iv = eval(I, n->b);
            if (obj.type != V_ARR) err_raise(I, "Присвоєння за індексом лише для масивів");
            long long i = (long long)iv.num;
            long long L = obj.len;
            if (i < 0) i += L;
            if (i < 0 || i >= L) {
                char buf[128];
                snprintf(buf, sizeof buf, "Індекс %lld поза межами (рядок %d)", i, n->line);
                err_raise(I, buf);
            }
            obj.items[i] = eval(I, n->c);
            return;
        }
        case N_IF: {
            if (truthy(eval(I, n->a)))
                exec_block(I, n->b);
            else if (n->c)
                exec_block(I, n->c);
            return;
        }
        case N_WHILE: {
            jmp_buf jb; jmp_buf *prev_loop = I->loop_jmp;
            I->loop_jmp = &jb;
            while (truthy(eval(I, n->a))) {
                int sig = setjmp(jb);
                if (sig == 0) {
                    exec_block(I, n->b);
                } else if (I->loop_kind == 1) {
                    I->loop_kind = 0; break;
                } else { I->loop_kind = 0; }
            }
            I->loop_jmp = prev_loop;
            return;
        }
        case N_FOR: {
            jmp_buf jb; jmp_buf *prev_loop = I->loop_jmp;
            I->loop_jmp = &jb;
            if (n->a->kind == N_ASSIGN) {        /* init: автооголошення змінної */
            Value v = eval(I, n->a->a);
            if (!env_assign(I->env, n->a->str, v))
                env_set(I->env, n->a->str, v);
        } else {
            exec(I, n->a);
        }
            while (1) {
                if (!truthy(eval(I, n->b))) break; /* cond */
                int sig = setjmp(jb);
                if (sig == 0) {
                    exec_block(I, n->c);           /* body */
                    exec(I, n->items[0]);          /* step */
                } else if (I->loop_kind == 1) {
                    I->loop_kind = 0; break;
                } else { I->loop_kind = 0; }      /* continue → step */
            }
            I->loop_jmp = prev_loop;
            return;
        }
        case N_BREAK: {
            if (!I->loop_jmp) err_raise(I, "break поза циклом");
            I->loop_kind = 1;
            longjmp(*I->loop_jmp, 1);
        }
        case N_CONTINUE: {
            if (!I->loop_jmp) err_raise(I, "continue поза циклом");
            I->loop_kind = 2;
            longjmp(*I->loop_jmp, 1);
        }
        case N_FN: {
            Value v;
            v.type = V_FUNC;
            v.fn = n;
            v.fnname = n->str;
            v.closure = I->env;
            env_set(I->env, n->str, v);
            return;
        }
        case N_RETURN: {
            if (!I->ret_jmp)
                err_raise(I, "return поза функцією");
            I->ret_val = n->a ? eval(I, n->a) : mk_nil();
            longjmp(*I->ret_jmp, 1);
        }
        case N_FOREACH: {
            Value col = eval(I, n->a);
            if (col.type != V_ARR) {
                char buf[256];
                snprintf(buf, sizeof buf, "for ... in: очікується масив, а не '%s' (рядок %d)",
                         col.type == V_STR ? "рядок" : col.type == V_NUM ? "число" : col.type == V_BOOL ? "логічне" : "інше", n->line);
                err_raise(I, buf);
            }
            jmp_buf jb; jmp_buf *prev_loop = I->loop_jmp;
            I->loop_jmp = &jb;
            for (long long i = 0; i < col.len; i++) {
                env_set(I->env, n->str, col.items[i]);
                int sig = setjmp(jb);
                if (sig == 0) {
                    exec_block(I, n->c);
                } else if (I->loop_kind == 1) {
                    I->loop_kind = 0; break;
                } else { I->loop_kind = 0; }
            }
            I->loop_jmp = prev_loop;
            return;
        }
        case N_IMPORT: {
            const char *path = n->str;
            char full[MAX_PATH];
            /* якщо відносний шлях — шукаємо відносно поточного файлу або CWD */
            if (path[0] != '/' && path[0] != '\\' && (path[1] != ':' || path[0] == '\0')) {
                GetModuleFileNameA(NULL, full, MAX_PATH);
                char *sl = strrchr(full, '\\');
                if (sl) { sl[1] = '\0'; strcat(full, path); }
                else strcpy(full, path);
            } else {
                strcpy(full, path);
            }
            FILE *f = fopen(full, "rb");
            if (!f) {
                char buf[512];
                snprintf(buf, sizeof buf, "import: не вдалося відкрити '%s' (рядок %d)", full, n->line);
                err_raise(I, buf);
                return;
            }
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *src = (char *)a_alloc(I->a, (size_t)sz + 1);
            fread(src, 1, (size_t)sz, f);
            src[sz] = '\0';
            fclose(f);
            int tok_n = 0;
            Token *toks = lex(I->a, src, &tok_n);
            Parser p; p.a = I->a; p.toks = toks; p.n = tok_n; p.i = 0;
            while (pr_peek(&p).type != T_EOF) {
                Node *stmt = pr_stmt(&p);
                if (stmt) exec(I, stmt);
                if (pr_peek(&p).type == T_NEWLINE || pr_peek(&p).type == T_SEMICOLON) pr_adv(&p);
            }
            return;
        }
        case N_TRY: {
            jmp_buf saved;
            memcpy(saved, I->err_jmp, sizeof(jmp_buf));
            if (setjmp(I->err_jmp) == 0) {
                exec_block(I, n->a);
                memcpy(I->err_jmp, saved, sizeof(jmp_buf));
            } else {
                memcpy(I->err_jmp, saved, sizeof(jmp_buf));
                if (n->str) env_set(I->env, n->str, mk_str(a_strdup(I->a, I->errmsg)));
                exec_block(I, n->b);
            }
            return;
        }
        case N_BLOCK:
            exec_block(I, n);
            return;
        default:
            eval(I, n);                       /* вираз-інструкція */
            return;
    }
}

/* ═══════════ Вбудовані функції ═══════════ */
static Value b_print(Interp *I, Value *args, int n) {
    for (int i = 0; i < n; i++) {
        if (i) printf(" ");
        printf("%s", fmt_val(I->a, args[i]));
    }
    printf("\n");
    return mk_nil();
}

static Value b_input(Interp *I, Value *args, int n) {
    if (n > 0 && args[0].type == V_STR) {
        printf("%s", args[0].str);
        fflush(stdout);
    }
    char buf[2048];
    if (!fgets(buf, sizeof buf, stdin)) return mk_str(a_strdup(I->a, ""));
    size_t l = strlen(buf);
    while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) buf[--l] = '\0';
    return mk_str(a_strdup(I->a, buf));
}

static Value b_len(Interp *I, Value *args, int n) {
    if (n != 1) err_raise(I, "len() очікує 1 аргумент");
    if (args[0].type == V_STR) return mk_num((double)strlen(args[0].str));
    if (args[0].type == V_ARR) return mk_num((double)args[0].len);
    err_raise(I, "len() лише для рядків і масивів");
    return mk_nil();
}

static Value b_type(Interp *I, Value *args, int n) {
    if (n != 1) err_raise(I, "type() очікує 1 аргумент");
    const char *t = args[0].type == V_NUM ? "число" :
                    args[0].type == V_STR ? "рядок" :
                    args[0].type == V_BOOL ? "логічне" :
                    args[0].type == V_ARR ? "масив" :
                    args[0].type == V_FUNC || args[0].type == V_NATIVE ? "функція" : "nil";
    return mk_str(a_strdup(I->a, t));
}

static Value b_num(Interp *I, Value *args, int n) {
    if (n != 1 || args[0].type == V_ARR || args[0].type == V_BOOL)
        err_raise(I, "num() очікує число або рядок");
    if (args[0].type == V_NUM) return args[0];
    char *end;
    double d = strtod(args[0].str, &end);
    if (end == args[0].str) err_raise(I, "num(): не число");
    return mk_num(d);
}

static Value b_str(Interp *I, Value *args, int n) {
    if (n != 1) err_raise(I, "str() очікує 1 аргумент");
    return mk_str(fmt_val(I->a, args[0]));
}

/* ── математика ── */
static double need_num(Interp *I, Value v) {
    if (v.type != V_NUM) err_raise(I, "очікується число");
    return v.num;
}
static Value b_abs(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "abs() очікує 1 аргумент");
    return mk_num(fabs(need_num(I, a[0])));
}
static Value b_min(Interp *I, Value *a, int n) {
    if (n != 2) err_raise(I, "min() очікує 2 аргументи");
    double x = need_num(I, a[0]), y = need_num(I, a[1]);
    return mk_num(x < y ? x : y);
}
static Value b_max(Interp *I, Value *a, int n) {
    if (n != 2) err_raise(I, "max() очікує 2 аргументи");
    double x = need_num(I, a[0]), y = need_num(I, a[1]);
    return mk_num(x > y ? x : y);
}
static Value b_floor(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "floor() очікує 1 аргумент");
    return mk_num(floor(need_num(I, a[0])));
}
static Value b_ceil(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "ceil() очікує 1 аргумент");
    return mk_num(ceil(need_num(I, a[0])));
}
static Value b_round(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "round() очікує 1 аргумент");
    return mk_num(round(need_num(I, a[0])));
}
static Value b_sqrt(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "sqrt() очікує 1 аргумент");
    double x = need_num(I, a[0]);
    if (x < 0) err_raise(I, "sqrt(): від'ємне число");
    return mk_num(sqrt(x));
}
static Value b_pow(Interp *I, Value *a, int n) {
    if (n != 2) err_raise(I, "pow() очікує 2 аргументи");
    return mk_num(pow(need_num(I, a[0]), need_num(I, a[1])));
}

/* ── масиви ── */
static Value b_range(Interp *I, Value *a, int n) {
    long long start = 0, end;
    if (n == 1) end = (long long)need_num(I, a[0]);
    else if (n == 2) { start = (long long)need_num(I, a[0]); end = (long long)need_num(I, a[1]); }
    else err_raise(I, "range() очікує 1–2 аргументи");
    if (end < start) err_raise(I, "range(): кінець менший за початок");
    Value v; v.type = V_ARR; v.len = (int)(end - start);
    v.items = (Value *)a_alloc(I->a, (size_t)(v.len ? v.len : 1) * sizeof(Value));
    for (long long i = start; i < end; i++) v.items[i - start] = mk_num((double)i);
    return v;
}
static Value b_push(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_ARR) err_raise(I, "push() очікує (масив, значення)");
    Value v; v.type = V_ARR; v.len = a[0].len + 1;
    v.items = (Value *)a_alloc(I->a, (size_t)v.len * sizeof(Value));
    memcpy(v.items, a[0].items, (size_t)a[0].len * sizeof(Value));
    v.items[a[0].len] = a[1];
    return v;
}
static Value b_pop(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_ARR) err_raise(I, "pop() очікує масив");
    if (a[0].len == 0) err_raise(I, "pop(): масив порожній");
    Value v; v.type = V_ARR; v.len = a[0].len - 1;
    v.items = (Value *)a_alloc(I->a, (size_t)(v.len ? v.len : 1) * sizeof(Value));
    memcpy(v.items, a[0].items, (size_t)v.len * sizeof(Value));
    return v;
}

/* ── рядки ── */
static Value b_join(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_ARR || a[1].type != V_STR)
        err_raise(I, "join() очікує (масив, розділювач)");
    size_t total = 1;
    char **parts = (char **)a_alloc(I->a, (size_t)(a[0].len ? a[0].len : 1) * sizeof(char *));
    for (int i = 0; i < a[0].len; i++) {
        parts[i] = fmt_val(I->a, a[0].items[i]);
        total += strlen(parts[i]);
        if (i < a[0].len - 1) total += strlen(a[1].str);
    }
    char *out = (char *)a_alloc(I->a, total);
    out[0] = '\0';
    for (int i = 0; i < a[0].len; i++) {
        if (i) strcat(out, a[1].str);
        strcat(out, parts[i]);
    }
    return mk_str(out);
}
static Value b_split(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_STR || a[1].type != V_STR)
        err_raise(I, "split() очікує (рядок, розділювач)");
    const char *s = a[0].str, *sep = a[1].str;
    size_t sl = strlen(s), sepl = strlen(sep);
    if (sepl == 0) err_raise(I, "split(): порожній розділювач");
    int cnt = 1;
    for (size_t i = 0; i + sepl <= sl; i++)
        if (!strncmp(s + i, sep, sepl)) { cnt++; i += sepl - 1; }
    Value v; v.type = V_ARR; v.len = cnt;
    v.items = (Value *)a_alloc(I->a, (size_t)cnt * sizeof(Value));
    const char *p = s; int k = 0;
    while (1) {
        const char *f = strstr(p, sep);
        if (!f) { v.items[k++] = mk_str(a_strdup(I->a, p)); break; }
        v.items[k++] = mk_str(a_strndup(I->a, p, (size_t)(f - p)));
        p = f + sepl;
    }
    return v;
}
static Value b_exit(Interp *I, Value *a, int n) {
    (void)I;
    int code = 0;
    if (n >= 1 && a[0].type == V_NUM) code = (int)a[0].num;
    exit(code);
}

static Value b_sleep(Interp *I, Value *a, int n) {
    (void)I;
    if (n != 1) err_raise(I, "sleep() очікує 1 аргумент — мілісекунди");
    double ms = need_num(I, a[0]);
    if (ms < 0) err_raise(I, "sleep(): від'ємний час");
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
    return mk_nil();
}

static Value b_contains(Interp *I, Value *a, int n) {
    if (n != 2) err_raise(I, "contains() очікує 2 аргументи (рядок, підрядок)");
    if (a[0].type != V_STR || a[1].type != V_STR)
        err_raise(I, "contains(): обидва аргументи — рядки");
    return mk_bool(strstr(a[0].str, a[1].str) != NULL);
}

static Value b_now(Interp *I, Value *a, int n) {
    (void)I; (void)a; (void)n;
    return mk_num((double)time(NULL));
}

static Value b_sort(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_ARR) err_raise(I, "sort() очікує масив");
    double *arr = (double *)a_alloc(I->a, a[0].len * sizeof(double));
    int cnt = 0;
    for (int i = 0; i < a[0].len; i++)
        if (a[0].items[i].type == V_NUM) arr[cnt++] = a[0].items[i].num;
    for (int i = 1; i < cnt; i++) {
        double k = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > k) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = k;
    }
    for (int i = 0; i < cnt; i++) { a[0].items[i].type = V_NUM; a[0].items[i].num = arr[i]; }
    return a[0];
}

static Value b_random(Interp *I, Value *a, int n) {
    double r = rand() / ((double)RAND_MAX + 1.0);
    if (n == 0) return mk_num(r);
    if (n == 1) {
        double m = need_num(I, a[0]);
        if (m <= 0) err_raise(I, "random(): має бути додатне");
        return mk_num(floor(r * m));
    }
    if (n == 2) {
        double lo = need_num(I, a[0]), hi = need_num(I, a[1]);
        if (hi <= lo) err_raise(I, "random(): кінець має бути більший");
        return mk_num(lo + floor(r * (hi - lo)));
    }
    err_raise(I, "random() очікує 0–2 аргументи");
}

/* ===== Математичні ===== */
static Value b_sin(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"sin() 1 arg"); return mk_num(sin(need_num(I,a[0]))); }
static Value b_cos(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"cos() 1 arg"); return mk_num(cos(need_num(I,a[0]))); }
static Value b_tan(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"tan() 1 arg"); return mk_num(tan(need_num(I,a[0]))); }
static Value b_log(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"log() 1 arg"); double x=need_num(I,a[0]); if(x<=0)err_raise(I,"log() >0"); return mk_num(log(x)); }
static Value b_exp(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"exp() 1 arg"); return mk_num(exp(need_num(I,a[0]))); }
static Value b_pi(Interp *I, Value *a, int n) { (void)I;(void)a;(void)n; return mk_num(3.14159265358979323846); }
static Value b_e(Interp *I, Value *a, int n) { (void)I;(void)a;(void)n; return mk_num(2.71828182845904523536); }
static Value b_deg(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"deg() 1 arg"); return mk_num(need_num(I,a[0])*180.0/3.141592653589793); }
static Value b_rad(Interp *I, Value *a, int n) { if (n!=1) err_raise(I,"rad() 1 arg"); return mk_num(need_num(I,a[0])*3.141592653589793/180.0); }

/* ===== Масив: map, filter, reduce, reverse, shuffle, sum, slice ===== */
static Value b_map(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_ARR || a[1].type != V_FUNC && a[1].type != V_NATIVE) err_raise(I, "map(arr, fn) → масив");
    Value src = a[0]; Value fn = a[1]; Value dst; dst.type = V_ARR; dst.len = src.len; dst.items = (Value *)a_alloc(I->a, (size_t)dst.len * sizeof(Value));
    for (int i = 0; i < src.len; i++) {
        Value args[1] = { src.items[i] };
        dst.items[i] = (fn.type == V_NATIVE) ? fn.native(I, args, 1) : call_func(I, fn.fn, fn.closure, args, 1);
    }
    return dst;
}
static Value b_filter(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_ARR || a[1].type != V_FUNC && a[1].type != V_NATIVE) err_raise(I, "filter(arr, fn) → масив");
    Value src = a[0]; Value fn = a[1]; Value *tmp = (Value *)a_alloc(I->a, (size_t)src.len * sizeof(Value)); int cnt = 0;
    for (int i = 0; i < src.len; i++) {
        Value args[1] = { src.items[i] };
        Value r = (fn.type == V_NATIVE) ? fn.native(I, args, 1) : call_func(I, fn.fn, fn.closure, args, 1);
        int truthy = 0;
        if (r.type == V_BOOL) truthy = r.num; else if (r.type == V_NUM) truthy = r.num != 0; else if (r.type == V_STR) truthy = strlen(r.str) > 0; else if (r.type == V_ARR) truthy = r.len > 0;
        if (truthy) tmp[cnt++] = src.items[i];
    }
    Value dst; dst.type = V_ARR; dst.len = cnt; dst.items = (Value *)a_alloc(I->a, (size_t)cnt * sizeof(Value));
    for (int i = 0; i < cnt; i++) dst.items[i] = tmp[i];
    return dst;
}
static Value b_reduce(Interp *I, Value *a, int n) {
    if (n != 3 || a[0].type != V_ARR || a[1].type != V_FUNC && a[1].type != V_NATIVE) err_raise(I, "reduce(arr, fn, init) → значення");
    Value src = a[0]; Value fn = a[1]; Value acc = a[2];
    for (int i = 0; i < src.len; i++) {
        Value args[2] = { acc, src.items[i] };
        acc = (fn.type == V_NATIVE) ? fn.native(I, args, 2) : call_func(I, fn.fn, fn.closure, args, 2);
    }
    return acc;
}
static Value b_reverse(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_ARR) err_raise(I, "reverse(arr) → масив");
    Value src = a[0]; Value dst; dst.type = V_ARR; dst.len = src.len; dst.items = (Value *)a_alloc(I->a, (size_t)dst.len * sizeof(Value));
    for (int i = 0; i < src.len; i++) dst.items[i] = src.items[src.len - 1 - i];
    return dst;
}
static Value b_shuffle(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_ARR) err_raise(I, "shuffle(arr) → масив");
    Value src = a[0]; Value dst; dst.type = V_ARR; dst.len = src.len; dst.items = (Value *)a_alloc(I->a, (size_t)dst.len * sizeof(Value));
    for (int i = 0; i < src.len; i++) dst.items[i] = src.items[i];
    for (int i = src.len - 1; i > 0; i--) {
        int j = (int)(rand() / ((double)RAND_MAX + 1.0) * (i + 1));
        Value t = dst.items[i]; dst.items[i] = dst.items[j]; dst.items[j] = t;
    }
    return dst;
}
static Value b_sum(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_ARR) err_raise(I, "sum(arr) → число");
    double s = 0; for (int i = 0; i < a[0].len; i++) if (a[0].items[i].type == V_NUM) s += a[0].items[i].num;
    return mk_num(s);
}
static Value b_slice(Interp *I, Value *a, int n) {
    if (n < 2 || n > 3 || a[0].type != V_ARR) err_raise(I, "slice(arr, start[, end]) → масив");
    Value src = a[0]; int start = (int)need_num(I, a[1]); int end = (n==3) ? (int)need_num(I, a[2]) : src.len;
    if (start < 0) start = src.len + start; if (start < 0) start = 0;
    if (end < 0) end = src.len + end; if (end > src.len) end = src.len; if (end < start) end = start;
    Value dst; dst.type = V_ARR; dst.len = end - start; dst.items = (Value *)a_alloc(I->a, (size_t)dst.len * sizeof(Value));
    for (int i = 0; i < dst.len; i++) dst.items[i] = src.items[start + i];
    return dst;
}

/* ===== Рядки: upper, lower, trim, replace, startswith, endswith, substr ===== */
static Value b_upper(Interp *I, Value *a, int n) { if (n!=1 || a[0].type!=V_STR) err_raise(I,"upper(str)"); char *s=a_strdup(I->a,a[0].str); for(char*p=s;*p;p++) if(*p>='a'&&*p<='z')*p=*p-'a'+'A'; return mk_str(s); }
static Value b_lower(Interp *I, Value *a, int n) { if (n!=1 || a[0].type!=V_STR) err_raise(I,"lower(str)"); char *s=a_strdup(I->a,a[0].str); for(char*p=s;*p;p++) if(*p>='A'&&*p<='Z')*p=*p-'A'+'a'; return mk_str(s); }
static Value b_trim(Interp *I, Value *a, int n) { if (n!=1 || a[0].type!=V_STR) err_raise(I,"trim(str)"); char *s=a[0].str; while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r')s++; char *e=s+strlen(s)-1; while(e>=s&&(*e==' '||*e=='\t'||*e=='\n'||*e=='\r'))e--; size_t l=e-s+1; char *r=(char*)a_alloc(I->a,l+1); memcpy(r,s,l); r[l]=0; return mk_str(r); }
static Value b_replace(Interp *I, Value *a, int n) {
    if (n!=3 || a[0].type!=V_STR || a[1].type!=V_STR || a[2].type!=V_STR) err_raise(I,"replace(str,from,to)");
    const char *s=a[0].str, *from=a[1].str, *to=a[2].str; size_t fl=strlen(from); if(fl==0) return a[0];
    size_t cap=strlen(s)*2+1; char *buf=(char*)a_alloc(I->a,cap); size_t o=0;
    for(const char *p=s; *p; ) {
        if(strncmp(p,from,fl)==0) { memcpy(buf+o,to,strlen(to)); o+=strlen(to); p+=fl; }
        else buf[o++]=*p++;
    } buf[o]=0; return mk_str(buf);
}
static Value b_startswith(Interp *I, Value *a, int n) { if(n!=2||a[0].type!=V_STR||a[1].type!=V_STR) err_raise(I,"startswith(str,pref)"); return mk_num(strncmp(a[0].str,a[1].str,strlen(a[1].str))==0); }
static Value b_endswith(Interp *I, Value *a, int n) { if(n!=2||a[0].type!=V_STR||a[1].type!=V_STR) err_raise(I,"endswith(str,suf)"); size_t sl=strlen(a[0].str),pl=strlen(a[1].str); return mk_num(sl>=pl&&strcmp(a[0].str+sl-pl,a[1].str)==0); }
static Value b_substr(Interp *I, Value *a, int n) {
    if(n<2||n>3||a[0].type!=V_STR) err_raise(I,"substr(str,start[,len])");
    const char *s=a[0].str; int start=(int)need_num(I,a[1]); int len=(n==3)?(int)need_num(I,a[2]):strlen(s);
    if(start<0) start=strlen(s)+start; if(start<0) start=0; if(start>(int)strlen(s)) return mk_str(a_strdup(I->a,""));
    if(start+len>strlen(s)) len=strlen(s)-start; if(len<0) len=0;
    char *r=(char*)a_alloc(I->a,len+1); memcpy(r,s+start,len); r[len]=0; return mk_str(r);
}

/* ===== Типи ===== */
static Value b_is_num(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&a[0].type==V_NUM); }
static Value b_is_str(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&a[0].type==V_STR); }
static Value b_is_arr(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&a[0].type==V_ARR); }
static Value b_is_bool(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&a[0].type==V_BOOL); }
static Value b_is_nil(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&a[0].type==V_NIL); }
static Value b_is_func(Interp *I, Value *a, int n) { (void)I; return mk_num(n==1&&(a[0].type==V_FUNC||a[0].type==V_NATIVE)); }

/* ===== Файли ===== */
static Value b_read_file(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"read_file(path)");
    FILE *f=fopen(a[0].str,"rb"); if(!f) err_raise(I,"read_file: не вдалося відкрити");
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=(char*)a_alloc(I->a,sz+1); fread(buf,1,sz,f); buf[sz]=0; fclose(f);
    return mk_str(buf);
}
static Value b_write_file(Interp *I, Value *a, int n) {
    if (n!=2 || a[0].type!=V_STR || a[1].type!=V_STR) err_raise(I,"write_file(path,content)");
    FILE *f=fopen(a[0].str,"wb"); if(!f) err_raise(I,"write_file: не вдалося відкрити");
    fwrite(a[1].str,1,strlen(a[1].str),f); fclose(f); return mk_nil();
}
static Value b_append_file(Interp *I, Value *a, int n) {
    if (n!=2 || a[0].type!=V_STR || a[1].type!=V_STR) err_raise(I,"append_file(path,content)");
    FILE *f=fopen(a[0].str,"ab"); if(!f) err_raise(I,"append_file: не вдалося відкрити");
    fwrite(a[1].str,1,strlen(a[1].str),f); fclose(f); return mk_nil();
}
static Value b_exists(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"exists(path)");
    DWORD attr=GetFileAttributesA(a[0].str); return mk_num(attr!=INVALID_FILE_ATTRIBUTES);
}
static Value b_is_file(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"is_file(path)");
    DWORD attr=GetFileAttributesA(a[0].str); return mk_num(attr!=INVALID_FILE_ATTRIBUTES && !(attr&FILE_ATTRIBUTE_DIRECTORY));
}
static Value b_is_dir(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"is_dir(path)");
    DWORD attr=GetFileAttributesA(a[0].str); return mk_num(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY));
}
static Value b_list_dir(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"list_dir(path)");
    char pattern[MAX_PATH]; snprintf(pattern,sizeof pattern,"%s\\*",a[0].str);
    WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(pattern,&fd);
    if(h==INVALID_HANDLE_VALUE) err_raise(I,"list_dir: не вдалося");
    Value *tmp=(Value*)a_alloc(I->a,256*sizeof(Value)); int cnt=0;
    do {
        if(strcmp(fd.cFileName,".")!=0 && strcmp(fd.cFileName,"..")!=0) {
            tmp[cnt++] = mk_str(a_strdup(I->a,fd.cFileName));
            if(cnt>=256) break;
        }
    } while(FindNextFileA(h,&fd) && cnt<256); FindClose(h);
    Value dst; dst.type=V_ARR; dst.len=cnt; dst.items=(Value*)a_alloc(I->a,(size_t)cnt*sizeof(Value));
    for(int i=0;i<cnt;i++) dst.items[i]=tmp[i]; return dst;
}

/* ===== Час ===== */
static Value b_date(Interp *I, Value *a, int n) {
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    char buf[64]; strftime(buf,sizeof buf,"%Y-%m-%d %H:%M:%S",tm); return mk_str(a_strdup(I->a,buf));
}
static Value b_format_time(Interp *I, Value *a, int n) {
    if (n!=2 || a[0].type!=V_STR || a[1].type!=V_NUM) err_raise(I,"format_time(fmt,ts)");
    time_t t = (time_t)need_num(I,a[1]); struct tm *tm = localtime(&t);
    char buf[128]; strftime(buf,sizeof buf,a[0].str,tm); return mk_str(a_strdup(I->a,buf));
}

/* ===== Система ===== */
static Value b_system(Interp *I, Value *a, int n) {
    if (n!=1 || a[0].type!=V_STR) err_raise(I,"system(cmd)");
    int r = system(a[0].str); return mk_num(r);
}
static Value b_pid(Interp *I, Value *a, int n) { (void)I;(void)a;(void)n; return mk_num((double)GetCurrentProcessId()); }

/* ===== JSON (простий парсер) ===== */
static char *json_skip_ws(char *p) { while (*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++; return p; }
static Value json_parse_val(Interp *I, char **pp) {
    char *p = json_skip_ws(*pp);
    if (*p == '{') {
        p++; Value obj; obj.type = V_ARR; obj.len = 0; obj.items = (Value *)a_alloc(I->a, 32 * sizeof(Value)); int cap = 32;
        while (1) {
            p = json_skip_ws(p);
            if (*p == '}') { p++; *pp = p; return obj; }
            if (*p != '"') err_raise(I, "JSON: очікувався ключ");
            char *key_start = ++p; while (*p && *p != '"') p++; if (!*p) err_raise(I, "JSON: незакритий ключ");
            char *key = a_strndup(I->a, key_start, p - key_start); p++;
            p = json_skip_ws(p); if (*p != ':') err_raise(I, "JSON: очікувалася ':'"); p++;
            Value v = json_parse_val(I, &p);
            if (obj.len >= cap) { cap *= 2; Value *new_items = (Value *)a_alloc(I->a, cap * sizeof(Value)); for (int i = 0; i < obj.len; i++) new_items[i] = obj.items[i]; obj.items = new_items; }
            if (obj.len + 1 >= cap) { cap *= 2; Value *new_items = (Value *)a_alloc(I->a, cap * sizeof(Value)); for (int i = 0; i < obj.len; i++) new_items[i] = obj.items[i]; obj.items = new_items; }
            obj.items[obj.len++] = mk_str(key); obj.items[obj.len++] = v;
            p = json_skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; *pp = p; return obj; }
            err_raise(I, "JSON: очікувалася ',' або '}'");
        }
    }
    if (*p == '[') {
        p++; Value arr; arr.type = V_ARR; arr.len = 0; arr.items = (Value *)a_alloc(I->a, 32 * sizeof(Value)); int cap = 32;
        while (1) {
            p = json_skip_ws(p);
            if (*p == ']') { p++; *pp = p; return arr; }
            Value v = json_parse_val(I, &p);
            if (arr.len >= cap) { cap *= 2; Value *new_items = (Value *)a_alloc(I->a, cap * sizeof(Value)); for (int i = 0; i < arr.len; i++) new_items[i] = arr.items[i]; arr.items = new_items; }
            arr.items[arr.len++] = v;
            p = json_skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; *pp = p; return arr; }
            err_raise(I, "JSON: очікувалася ',' або ']'");
        }
    }
    if (*p == '"') {
        p++; char *s = p; while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        char *val = a_strndup(I->a, s, p - s); if (*p == '"') p++; *pp = p; return mk_str(val);
    }
    if (*p == 't' && strncmp(p, "true", 4) == 0) { p += 4; *pp = p; return mk_num(1); }
    if (*p == 'f' && strncmp(p, "false", 5) == 0) { p += 5; *pp = p; return mk_num(0); }
    if (*p == 'n' && strncmp(p, "null", 4) == 0) { p += 4; *pp = p; return mk_nil(); }
    char *start = p; while (*p && (isdigit(*p) || *p == '-' || *p == '.' || *p == 'e' || *p == 'E')) p++;
    char *num = a_strndup(I->a, start, p - start); *pp = p; return mk_num(strtod(num, NULL));
}
static Value b_json_parse(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "json_parse(str)");
    char *p = a[0].str; return json_parse_val(I, &p);
}
static void json_write_val(Interp *I, Value v, char **out, size_t *len, size_t *cap) {
    auto void append(const char *s) {
        size_t l = strlen(s);
        if (*len + l + 1 >= *cap) { *cap = (*cap ? *cap * 2 : 256); char *new_out = (char *)a_alloc(I->a, *cap); if (*out) memcpy(new_out, *out, *len); *out = new_out; }
        memcpy(*out + *len, s, l); *len += l; (*out)[*len] = '\0';
    }
    if (v.type == V_NIL) { append("null"); return; }
    if (v.type == V_BOOL) { append(v.num ? "true" : "false"); return; }
    if (v.type == V_NUM) { char buf[64]; snprintf(buf, sizeof buf, "%g", v.num); append(buf); return; }
    if (v.type == V_STR) { char *esc = (char *)a_alloc(I->a, strlen(v.str) * 2 + 3); char *d = esc; *d++ = '"'; for (char *s = v.str; *s; s++) { if (*s == '"' || *s == '\\') { *d++ = '\\'; *d++ = *s; } else if (*s == '\n') { *d++ = '\\'; *d++ = 'n'; } else if (*s == '\t') { *d++ = '\\'; *d++ = 't'; } else { *d++ = *s; } } *d++ = '"'; *d = '\0'; append(esc); return; }
    if (v.type == V_ARR) {
        if (v.len > 0 && v.items[0].type == V_STR && v.len % 2 == 0) {
            append("{"); for (int i = 0; i < v.len; i += 2) { if (i) append(","); json_write_val(I, v.items[i], out, len, cap); append(":"); json_write_val(I, v.items[i+1], out, len, cap); } append("}"); return;
        }
        append("["); for (int i = 0; i < v.len; i++) { if (i) append(","); json_write_val(I, v.items[i], out, len, cap); } append("]"); return;
    }
    append("null");
}
static Value b_json_stringify(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "json_stringify(val)");
    char *buf = NULL; size_t len = 0, cap = 0;
    json_write_val(I, a[0], &buf, &len, &cap);
    Value r = mk_str(buf ? buf : "null");
    return r;
}

/* ===== Regex (Windows) ===== */
static Value b_regex_match(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_STR || a[1].type != V_STR) err_raise(I, "regex_match(pattern, str)");
    /* simple regex using Windows FindPattern - fallback to strstr for now */
    const char *pat = a[0].str, *str = a[1].str;
    if (strstr(str, pat)) return mk_num(1);
    return mk_num(0);
}
static Value b_regex_replace(Interp *I, Value *a, int n) {
    if (n != 3 || a[0].type != V_STR || a[1].type != V_STR || a[2].type != V_STR) err_raise(I, "regex_replace(pattern, str, repl)");
    const char *pat = a[0].str, *str = a[1].str, *repl = a[2].str;
    /* simple strstr replace */
    size_t cap = strlen(str) * 2 + 1; char *buf = (char *)a_alloc(I->a, cap); size_t o = 0;
    for (const char *p = str; *p; ) {
        if (strncmp(p, pat, strlen(pat)) == 0) { memcpy(buf + o, repl, strlen(repl)); o += strlen(repl); p += strlen(pat); }
        else buf[o++] = *p++;
    } buf[o] = '\0'; return mk_str(buf);
}

/* ===== Хакерські інструменти (легкі) ===== */
static Value b_exec(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "exec(cmd) → вивід команди");
    FILE *f = popen(a[0].str, "r");
    if (!f) err_raise(I, "exec: не вдалося запустити");
    size_t cap = 4096, len = 0;
    char *buf = (char *)a_alloc(I->a, cap);
    while (fgets(buf + len, (int)(cap - len), f)) {
        len += strlen(buf + len);
        if (cap - len < 256) { cap *= 2; char *nb = (char *)a_alloc(I->a, cap); memcpy(nb, buf, len); buf = nb; }
    }
    pclose(f);
    buf[len] = '\0';
    return mk_str(buf);
}
static Value b_http_get(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "http_get(url) → вміст сторінки");
    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp); strcat(tmp, "sokil_http.tmp");
    if (FAILED(URLDownloadToFileA(NULL, a[0].str, tmp, 0, NULL))) err_raise(I, "http_get: не вдалося завантажити");
    FILE *f = fopen(tmp, "rb");
    if (!f) err_raise(I, "http_get: не вдалося прочитати");
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = (char *)a_alloc(I->a, (size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f); buf[sz] = '\0'; fclose(f);
    remove(tmp);
    return mk_str(buf);
}
static Value b_download(Interp *I, Value *a, int n) {
    if (n != 2 || a[0].type != V_STR || a[1].type != V_STR) err_raise(I, "download(url, file)");
    HRESULT hr = URLDownloadToFileA(NULL, a[0].str, a[1].str, 0, NULL);
    return mk_num(SUCCEEDED(hr));
}
static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static Value b_b64_encode(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "b64_encode(str)");
    const char *s = a[0].str; size_t in = strlen(s);
    char *out = (char *)a_alloc(I->a, ((in + 2) / 3) * 4 + 1);
    size_t o = 0;
    for (size_t i = 0; i < in; i += 3) {
        unsigned x = (unsigned char)s[i] << 16;
        if (i + 1 < in) x |= (unsigned char)s[i + 1] << 8;
        if (i + 2 < in) x |= (unsigned char)s[i + 2];
        out[o++] = b64tab[(x >> 18) & 63];
        out[o++] = b64tab[(x >> 12) & 63];
        out[o++] = i + 1 < in ? b64tab[(x >> 6) & 63] : '=';
        out[o++] = i + 2 < in ? b64tab[x & 63] : '=';
    }
    out[o] = '\0';
    return mk_str(out);
}
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
static Value b_b64_decode(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "b64_decode(str)");
    const char *s = a[0].str; size_t in = strlen(s);
    char *out = (char *)a_alloc(I->a, in + 1);
    size_t o = 0; unsigned buf = 0; int bits = 0;
    for (size_t i = 0; i < in; i++) {
        if (s[i] == '=' || s[i] == '\n' || s[i] == '\r') continue;
        int v = b64val(s[i]);
        if (v < 0) err_raise(I, "b64_decode: некоректний символ");
        buf = (buf << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (char)((buf >> bits) & 0xff);
        }
    }
    out[o] = '\0';
    return mk_str(out);
}
static Value b_hex(Interp *I, Value *a, int n) {
    if (n != 1) err_raise(I, "hex(num|str)");
    if (a[0].type == V_NUM) {
        char buf[32]; snprintf(buf, sizeof buf, "%llx", (unsigned long long)a[0].num);
        return mk_str(a_strdup(I->a, buf));
    }
    if (a[0].type == V_STR) {
        const char *s = a[0].str; size_t l = strlen(s);
        char *out = (char *)a_alloc(I->a, l * 2 + 1);
        for (size_t i = 0; i < l; i++) snprintf(out + i * 2, 3, "%02x", (unsigned char)s[i]);
        return mk_str(out);
    }
    err_raise(I, "hex(num|str)");
    return mk_nil();
}
static Value b_uuid(Interp *I, Value *a, int n) {
    (void)I; (void)a; (void)n;
    char buf[40];
    snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx",
             (unsigned)rand() & 0xffffffffu, (unsigned)rand() & 0xffff,
             ((unsigned)rand() & 0x0fff) | 0x4000, ((unsigned)rand() & 0x3fff) | 0x8000,
             ((unsigned long long)rand() << 32) ^ (unsigned long long)rand());
    return mk_str(a_strdup(I->a, buf));
}
static Value b_env(Interp *I, Value *a, int n) {
    if (n != 1 || a[0].type != V_STR) err_raise(I, "env(name) → значення або nil");
    const char *v = getenv(a[0].str);
    return v ? mk_str(a_strdup(I->a, v)) : mk_nil();
}

static void install_builtins(Interp *I) {
    Env *g = I->env;
    Value p; p.type = V_NATIVE; p.native = b_print; p.fnname = "print"; env_set(g, "print", p);
    Value in; in.type = V_NATIVE; in.native = b_input; in.fnname = "input"; env_set(g, "input", in);
    Value l; l.type = V_NATIVE; l.native = b_len; l.fnname = "len"; env_set(g, "len", l);
    Value ty; ty.type = V_NATIVE; ty.native = b_type; ty.fnname = "type"; env_set(g, "type", ty);
    Value nm; nm.type = V_NATIVE; nm.native = b_num; nm.fnname = "num"; env_set(g, "num", nm);
    Value st; st.type = V_NATIVE; st.native = b_str; st.fnname = "str"; env_set(g, "str", st);
    Value by; by.type = V_NATIVE; by.native = b_abs; by.fnname = "abs"; env_set(g, "abs", by);
    Value mn; mn.type = V_NATIVE; mn.native = b_min; mn.fnname = "min"; env_set(g, "min", mn);
    Value mx; mx.type = V_NATIVE; mx.native = b_max; mx.fnname = "max"; env_set(g, "max", mx);
    Value fl; fl.type = V_NATIVE; fl.native = b_floor; fl.fnname = "floor"; env_set(g, "floor", fl);
    Value cl; cl.type = V_NATIVE; cl.native = b_ceil; cl.fnname = "ceil"; env_set(g, "ceil", cl);
    Value rd; rd.type = V_NATIVE; rd.native = b_round; rd.fnname = "round"; env_set(g, "round", rd);
    Value sq; sq.type = V_NATIVE; sq.native = b_sqrt; sq.fnname = "sqrt"; env_set(g, "sqrt", sq);
    Value pw; pw.type = V_NATIVE; pw.native = b_pow; pw.fnname = "pow"; env_set(g, "pow", pw);
    Value rg; rg.type = V_NATIVE; rg.native = b_range; rg.fnname = "range"; env_set(g, "range", rg);
    Value pu; pu.type = V_NATIVE; pu.native = b_push; pu.fnname = "push"; env_set(g, "push", pu);
    Value po; po.type = V_NATIVE; po.native = b_pop; po.fnname = "pop"; env_set(g, "pop", po);
    Value jn; jn.type = V_NATIVE; jn.native = b_join; jn.fnname = "join"; env_set(g, "join", jn);
    Value sp; sp.type = V_NATIVE; sp.native = b_split; sp.fnname = "split"; env_set(g, "split", sp);
    Value ex; ex.type = V_NATIVE; ex.native = b_exit; ex.fnname = "exit"; env_set(g, "exit", ex);
    Value rn; rn.type = V_NATIVE; rn.native = b_random; rn.fnname = "random"; env_set(g, "random", rn);
    Value sl; sl.type = V_NATIVE; sl.native = b_sleep; sl.fnname = "sleep"; env_set(g, "sleep", sl);
    Value ct; ct.type = V_NATIVE; ct.native = b_contains; ct.fnname = "contains"; env_set(g, "contains", ct);
    Value nw; nw.type = V_NATIVE; nw.native = b_now; nw.fnname = "now"; env_set(g, "now", nw);
    Value sr; sr.type = V_NATIVE; sr.native = b_sort; sr.fnname = "sort"; env_set(g, "sort", sr);
    /* math */
    Value s1; s1.type = V_NATIVE; s1.native = b_sin; s1.fnname = "sin"; env_set(g, "sin", s1);
    Value s2; s2.type = V_NATIVE; s2.native = b_cos; s2.fnname = "cos"; env_set(g, "cos", s2);
    Value s3; s3.type = V_NATIVE; s3.native = b_tan; s3.fnname = "tan"; env_set(g, "tan", s3);
    Value s4; s4.type = V_NATIVE; s4.native = b_log; s4.fnname = "log"; env_set(g, "log", s4);
    Value s5; s5.type = V_NATIVE; s5.native = b_exp; s5.fnname = "exp"; env_set(g, "exp", s5);
    Value s6; s6.type = V_NATIVE; s6.native = b_pi; s6.fnname = "pi"; env_set(g, "pi", s6);
    Value s7; s7.type = V_NATIVE; s7.native = b_e; s7.fnname = "e"; env_set(g, "e", s7);
    Value s8; s8.type = V_NATIVE; s8.native = b_deg; s8.fnname = "deg"; env_set(g, "deg", s8);
    Value s9; s9.type = V_NATIVE; s9.native = b_rad; s9.fnname = "rad"; env_set(g, "rad", s9);
    /* array */
    Value am; am.type = V_NATIVE; am.native = b_map; am.fnname = "map"; env_set(g, "map", am);
    Value af; af.type = V_NATIVE; af.native = b_filter; af.fnname = "filter"; env_set(g, "filter", af);
    Value ar; ar.type = V_NATIVE; ar.native = b_reduce; ar.fnname = "reduce"; env_set(g, "reduce", ar);
    Value av2; av2.type = V_NATIVE; av2.native = b_reverse; av2.fnname = "reverse"; env_set(g, "reverse", av2);
    Value as; as.type = V_NATIVE; as.native = b_shuffle; as.fnname = "shuffle"; env_set(g, "shuffle", as);
    Value asum; asum.type = V_NATIVE; asum.native = b_sum; asum.fnname = "sum"; env_set(g, "sum", asum);
    Value asl; asl.type = V_NATIVE; asl.native = b_slice; asl.fnname = "slice"; env_set(g, "slice", asl);
    /* string */
    Value su; su.type = V_NATIVE; su.native = b_upper; su.fnname = "upper"; env_set(g, "upper", su);
    Value slo; slo.type = V_NATIVE; slo.native = b_lower; slo.fnname = "lower"; env_set(g, "lower", slo);
    Value strim; strim.type = V_NATIVE; strim.native = b_trim; strim.fnname = "trim"; env_set(g, "trim", strim);
    Value srepl; srepl.type = V_NATIVE; srepl.native = b_replace; srepl.fnname = "replace"; env_set(g, "replace", srepl);
    Value ssw; ssw.type = V_NATIVE; ssw.native = b_startswith; ssw.fnname = "startswith"; env_set(g, "startswith", ssw);
    Value sew; sew.type = V_NATIVE; sew.native = b_endswith; sew.fnname = "endswith"; env_set(g, "endswith", sew);
    Value ssub; ssub.type = V_NATIVE; ssub.native = b_substr; ssub.fnname = "substr"; env_set(g, "substr", ssub);
    /* types */
    Value tn; tn.type = V_NATIVE; tn.native = b_is_num; tn.fnname = "is_num"; env_set(g, "is_num", tn);
    Value ts; ts.type = V_NATIVE; ts.native = b_is_str; ts.fnname = "is_str"; env_set(g, "is_str", ts);
    Value ta; ta.type = V_NATIVE; ta.native = b_is_arr; ta.fnname = "is_arr"; env_set(g, "is_arr", ta);
    Value tb; tb.type = V_NATIVE; tb.native = b_is_bool; tb.fnname = "is_bool"; env_set(g, "is_bool", tb);
    Value tni; tni.type = V_NATIVE; tni.native = b_is_nil; tni.fnname = "is_nil"; env_set(g, "is_nil", tni);
    Value tf; tf.type = V_NATIVE; tf.native = b_is_func; tf.fnname = "is_func"; env_set(g, "is_func", tf);
    /* files */
    Value fr; fr.type = V_NATIVE; fr.native = b_read_file; fr.fnname = "read_file"; env_set(g, "read_file", fr);
    Value fw; fw.type = V_NATIVE; fw.native = b_write_file; fw.fnname = "write_file"; env_set(g, "write_file", fw);
    Value fa; fa.type = V_NATIVE; fa.native = b_append_file; fa.fnname = "append_file"; env_set(g, "append_file", fa);
    Value fe; fe.type = V_NATIVE; fe.native = b_exists; fe.fnname = "exists"; env_set(g, "exists", fe);
    Value ffi; ffi.type = V_NATIVE; ffi.native = b_is_file; ffi.fnname = "is_file"; env_set(g, "is_file", ffi);
    Value fdi; fdi.type = V_NATIVE; fdi.native = b_is_dir; fdi.fnname = "is_dir"; env_set(g, "is_dir", fdi);
    Value fls; fls.type = V_NATIVE; fls.native = b_list_dir; fls.fnname = "list_dir"; env_set(g, "list_dir", fls);
    /* time */
    Value dt; dt.type = V_NATIVE; dt.native = b_date; dt.fnname = "date"; env_set(g, "date", dt);
    Value ft; ft.type = V_NATIVE; ft.native = b_format_time; ft.fnname = "format_time"; env_set(g, "format_time", ft);
    /* system */
    Value sys; sys.type = V_NATIVE; sys.native = b_system; sys.fnname = "system"; env_set(g, "system", sys);
    Value pid; pid.type = V_NATIVE; pid.native = b_pid; pid.fnname = "pid"; env_set(g, "pid", pid);
    /* json */
    Value jp; jp.type = V_NATIVE; jp.native = b_json_parse; jp.fnname = "json_parse"; env_set(g, "json_parse", jp);
    Value js; js.type = V_NATIVE; js.native = b_json_stringify; js.fnname = "json_stringify"; env_set(g, "json_stringify", js);
    /* regex */
    Value rm; rm.type = V_NATIVE; rm.native = b_regex_match; rm.fnname = "regex_match"; env_set(g, "regex_match", rm);
    Value rr; rr.type = V_NATIVE; rr.native = b_regex_replace; rr.fnname = "regex_replace"; env_set(g, "regex_replace", rr);
    /* хакерські */
    Value xe; xe.type = V_NATIVE; xe.native = b_exec; xe.fnname = "exec"; env_set(g, "exec", xe);
    Value xg; xg.type = V_NATIVE; xg.native = b_http_get; xg.fnname = "http_get"; env_set(g, "http_get", xg);
    Value xd; xd.type = V_NATIVE; xd.native = b_download; xd.fnname = "download"; env_set(g, "download", xd);
    Value xb1; xb1.type = V_NATIVE; xb1.native = b_b64_encode; xb1.fnname = "b64_encode"; env_set(g, "b64_encode", xb1);
    Value xb2; xb2.type = V_NATIVE; xb2.native = b_b64_decode; xb2.fnname = "b64_decode"; env_set(g, "b64_decode", xb2);
    Value xh; xh.type = V_NATIVE; xh.native = b_hex; xh.fnname = "hex"; env_set(g, "hex", xh);
    Value xu; xu.type = V_NATIVE; xu.native = b_uuid; xu.fnname = "uuid"; env_set(g, "uuid", xu);
    Value xv; xv.type = V_NATIVE; xv.native = b_env; xv.fnname = "env"; env_set(g, "env", xv);
    /* args — аргументи командного рядка */
    Value av; av.type = V_ARR;
    av.len = g_argc > g_args_start ? g_argc - g_args_start : 0;
    av.items = (Value *)a_alloc(I->a, (size_t)(av.len ? av.len : 1) * sizeof(Value));
    for (int i = g_args_start; i < g_argc; i++)
        av.items[i - g_args_start] = mk_str(a_strdup(I->a, g_argv[i]));
    env_set(g, "args", av);
}

/* ═══════════ Запуск ═══════════ */
static const char *BANNER =
"=================================\n"
"  Сокіл (Sokil) v2.13 — мова програмування\n"
"  sokil файл.sokil · sokil -e \"код\" · sokil --compile файл.sokil\n"
"  REPL: введи код, exit — вийти\n"
"=================================\n";

static int run_ast(Arena *a, Node *prog) {
    Interp I;
    memset(&I, 0, sizeof I);
    I.a = a;
    I.env = env_new(a, NULL);
    install_builtins(&I);
    if (setjmp(I.err_jmp) == 0) {
        for (int i = 0; i < prog->n; i++)
            exec(&I, prog->items[i]);
        return 0;
    }
    fprintf(stderr, "Помилка: %s\n", I.errmsg);
    return 1;
}

static int run_source(const char *src) {
    Arena a = {0};
    if (setjmp(lex_err_jmp) != 0) {
        fprintf(stderr, "Помилка: %s\n", lex_err_msg);
        return 1;
    }
    int n;
    Token *toks = lex(&a, src, &n);
    Node *prog = parse(&a, toks, n);
    return run_ast(&a, prog);
}

static void repl(void) {
    printf("%s", BANNER);
    printf("  REPL (exit — вийти)\n");
    Arena a = {0};
    Interp I;
    memset(&I, 0, sizeof I);
    I.a = &a;
    I.env = env_new(&a, NULL);
    install_builtins(&I);
    char line[4096];
    while (1) {
        printf("sokil> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) { printf("\n"); break; }
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        if (!strcmp(line, "exit") || !strcmp(line, "quit")) break;
        if (!l) continue;
        int lex_err = setjmp(lex_err_jmp);
        if (setjmp(I.err_jmp) == 0 && lex_err == 0) {
            int n;
            Token *toks = lex(&a, line, &n);
            Node *prog = parse(&a, toks, n);
            for (int i = 0; i < prog->n; i++)
                exec(&I, prog->items[i]);
        } else {
            printf("Помилка: %s\n", lex_err ? lex_err_msg : I.errmsg);
        }
    }
}

static char *read_file(Arena *a, const char *path, int *ok) {
    FILE *f = fopen(path, "rb");
    if (!f) { *ok = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)a_alloc(a, (size_t)sz + 2);
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    *ok = 1;
    return buf;
}

/* ── Самооновлення: sokil --update ── */
#ifdef _WIN32
static int cmd_update(void) {
    wchar_t cur[MAX_PATH];
    GetModuleFileNameW(NULL, cur, MAX_PATH);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + wcslen(tmp), L"sokil-new-%lu.exe", GetCurrentProcessId());
    printf("Сокіл: завантажую останню версію з GitHub...\n");
    fflush(stdout);
    HRESULT hr = URLDownloadToFileW(NULL,
        L"https://github.com/DenisVJR1/sokil-lang/releases/latest/download/sokil.exe",
        tmp, 0, NULL);
    if (FAILED(hr)) {
        printf("Помилка завантаження (%08lX)\n", (unsigned long)hr);
        return 1;
    }
    FILE *f = _wfopen(tmp, L"rb");
    unsigned char mz[2] = {0};
    if (f) { fread(mz, 1, 2, f); fclose(f); }
    if (mz[0] != 'M' || mz[1] != 'Z') {
        printf("Завантажений файл не схожий на програму — оновлення скасовано.\n");
        DeleteFileW(tmp);
        return 1;
    }
    wchar_t bat[MAX_PATH];
    GetTempPathW(MAX_PATH, bat);
    wsprintfW(bat + wcslen(bat), L"sokil-up-%lu.bat", GetCurrentProcessId());
    FILE *fb = _wfopen(bat, L"w");
    if (!fb) { DeleteFileW(tmp); return 1; }
    fwprintf(fb, L"@echo off\r\nping 127.0.0.1 -n 2 >nul\r\n"
                 L"move /y \"%s\" \"%s\"\r\ndel \"%%~f0\"\r\n", tmp, cur);
    fclose(fb);
    wchar_t cmdline[2048];
    wsprintfW(cmdline, L"cmd /c \"%s\"", bat);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi = {0};
    if (CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    printf("Оновлення встановлено. Перезапусти sokil.\n");
    return 0;
}
#else
static int cmd_update(void) {
    printf("Оновлення з репозиторію:\n"
           "  git pull\n"
           "  sudo ./install.sh\n"
           "або завантаж нову збірку з Releases.\n");
    return 0;
}
#endif

/* ═══════════ --compile: .sokil → .exe (без компілятора C) ═══════════ */
static const char EMBED_MARKER[] = "\n\x00SOKIL_EMBED\x00\n";

/* Запуск вбудованого коду з кінця .exe */
static int try_embedded(void) {
    FILE *f;
#ifdef _WIN32
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    f = _wfopen(self, L"rb");
#else
    char self[4096];
    ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
    if (r <= 0) return 0;
    self[r] = '\0';
    f = fopen(self, "rb");
#endif
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    size_t mlen = sizeof(EMBED_MARKER) - 1;
    if (sz < (long)(mlen + 4 + 10)) { fclose(f); return 0; }
    /* Читаємо останні ~512KB для пошуку маркера */
    long scan_from = sz > 524288 ? sz - 524288 : 0;
    fseek(f, scan_from, SEEK_SET);
    size_t chunk = (size_t)(sz - scan_from);
    char *buf = (char *)malloc(chunk);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, chunk, f);
    fclose(f);
    char *hits[8];
    int nh = 0;
    for (size_t i = 0; i + mlen + 4 <= got && nh < 8; i++)
        if (memcmp(buf + i, EMBED_MARKER, mlen) == 0) hits[nh++] = buf + i;
    /* Валідний вбудований код: код закінчується точно на кінці файлу */
    for (int hi = nh - 1; hi >= 0; hi--) {
        char *hit = hits[hi];
        unsigned int csz;
        memcpy(&csz, hit + mlen, 4);
        long code_abs = scan_from + (long)(hit - buf) + (long)mlen + 4;
        if (csz > 0 && csz < (1u << 30) && code_abs + (long)csz == sz) {
            char *src = (char *)malloc(csz + 1);
            if (!src) { free(buf); return 0; }
            memcpy(src, hit + mlen + 4, csz);
            src[csz] = '\0';
            free(buf);
            static char *eargv[2] = { "sokil-embedded", NULL };
            g_argc = 1; g_argv = eargv;
            g_args_start = 0;
            run_source(src);
            free(src);
            return 1;
        }
    }
    free(buf);
    return 0;
}

static int cmd_compile(const char *srcpath) {
    FILE *f = fopen(srcpath, "rb");
    if (!f) { fprintf(stderr, "Файл не знайдено: %s\n", srcpath); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)sz + 1);
    if (!src) { fclose(f); return 1; }
    size_t n = fread(src, 1, (size_t)sz, f);
    src[n] = '\0';
    fclose(f);
    /* Вихідний файл: замінюємо .sokil на .exe (якщо є) */
    char outpath[4096];
    strncpy(outpath, srcpath, sizeof outpath - 1);
    outpath[sizeof outpath - 1] = '\0';
    size_t l = strlen(outpath);
    if (l > 6 && !strcmp(outpath + l - 6, ".sokil"))
        strcpy(outpath + l - 6, ".exe");
    else
        strcpy(outpath + l, ".exe");
    /* Копіюємо ourselves + маркер + код */
    FILE *out = fopen(outpath, "wb");
    if (!out) { fprintf(stderr, "Не вдалося створити: %s\n", outpath); free(src); return 1; }
#ifdef _WIN32
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    FILE *self_f = _wfopen(self, L"rb");
#else
    char self[4096];
    ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
    self[r] = '\0';
    FILE *self_f = fopen(self, "rb");
#endif
    fseek(self_f, 0, SEEK_END);
    long self_sz = ftell(self_f);
    fseek(self_f, 0, SEEK_SET);
    char *buf = malloc((size_t)self_sz);
    fread(buf, 1, (size_t)self_sz, self_f);
    fclose(self_f);
    fwrite(buf, 1, (size_t)self_sz, out);
    free(buf);
    /* Маркер: \n + name + \n + 4 байти довжини коду */
    fwrite(EMBED_MARKER, 1, sizeof(EMBED_MARKER) - 1, out);
    unsigned int csz = (unsigned int)n;
    fwrite(&csz, 1, 4, out);
    fwrite(src, 1, n, out);
    fclose(out);
    free(src);
    printf("Готово: %s (%ld байт коду)\n", outpath, (long)n);
    return 0;
}

/* Самовідновлення PATH: якщо інстальована копія (з %LOCALAPPDATA%\Sokil),
   а запис у HKCU\Environment Path зник — дописуємо і сповіщаємо систему. */
static void path_guard(void) {
#ifdef _WIN32
    char dir[MAX_PATH], target[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char *sl = strrchr(dir, '\\');
    if (!sl) return;
    *sl = '\0';
    const char *la = getenv("LOCALAPPDATA");
    if (!la) return;
    snprintf(target, sizeof target, "%s\\Sokil", la);
    if (_stricmp(dir, target) != 0) return;      /* портативна копія — не чіпаємо */

    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    char cur[8192]; DWORD type = REG_EXPAND_SZ, size = sizeof cur; cur[0] = '\0';
    LONG r = RegQueryValueExA(hk, "Path", NULL, &type, (LPBYTE)cur, &size);
    if (r == ERROR_SUCCESS && type != REG_EXPAND_SZ && type != REG_SZ)
        { RegCloseKey(hk); return; }
    /* чи вже є entry (без врахування регістру, з ';' межами) */
    size_t tl = strlen(target);
    int found = 0;
    if (r == ERROR_SUCCESS) {
        char *start = cur;
        for (char *q = cur; ; q++) {
            if (*q == ';' || *q == '\0') {
                size_t len = (size_t)(q - start);
                if (len == tl && _strnicmp(start, target, tl) == 0) { found = 1; break; }
                if (*q == '\0') break;
                start = q + 1;
            }
        }
    }
    if (found) { RegCloseKey(hk); return; }
    if (r == ERROR_SUCCESS && size >= sizeof cur) { RegCloseKey(hk); return; }
    /* дописуємо */
    size_t cl = r == ERROR_SUCCESS ? strlen(cur) : 0;
    char *newp = (char *)malloc(cl + tl + 3);
    if (!newp) { RegCloseKey(hk); return; }
    size_t o = 0;
    /* копіюємо cur без провідних ';' */
    if (r == ERROR_SUCCESS) {
        size_t s = 0;
        while (cur[s] == ';') s++;
        for (size_t i = s; i < cl; i++) newp[o++] = cur[i];
    }
    if (o > 0 && newp[o - 1] != ';') newp[o++] = ';';
    memcpy(newp + o, target, tl); o += tl;
    while (o > 0 && newp[o - 1] == ';') o--;
    newp[o] = '\0';
    RegSetValueExA(hk, "Path", 0, REG_EXPAND_SZ, (const BYTE *)newp, (DWORD)o + 1);
    RegCloseKey(hk);
    free(newp);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 5000, NULL);
#endif
}

int main(int argc, char **argv) {
    g_argc = argc; g_argv = argv;
    srand((unsigned)time(NULL));
#ifdef _WIN32
    SetConsoleOutputCP(65001);   /* UTF-8 вивід без крякозябр */
    SetConsoleCP(65001);
    path_guard();
#endif
    if (argc > 1 && !strcmp(argv[1], "--compile"))
        return cmd_compile(argc > 2 ? argv[2] : NULL);
    if (try_embedded()) return 0;
    if (argc > 1) {
        if (!strcmp(argv[1], "--update")) return cmd_update();
        if (!strcmp(argv[1], "--version")) {
            printf("Sokil v2.13\n");
            return 0;
        }
        if (!strcmp(argv[1], "-e")) {             /* sokil -e "код" */
            g_args_start = 3;
            if (argc < 3) {
                fprintf(stderr, "Використання: sokil -e \"код\"\n");
                return 1;
            }
            return run_source(argv[2]);
        }
        g_args_start = 2;
        Arena a = {0};
        int ok;
        char *src = read_file(&a, argv[1], &ok);
        if (!ok && strlen(argv[1]) > 0) {          /* авто-розширення .sokil */
            char alt[MAX_PATH];
            snprintf(alt, sizeof alt, "%s.sokil", argv[1]);
            src = read_file(&a, alt, &ok);
            if (ok) argv[1] = alt;
        }
        if (!ok) {
            fprintf(stderr, "Файл не знайдено: %s\n", argv[1]);
            return 1;
        }
        return run_source(src);
    }
    g_args_start = 0;
    repl();
    return 0;
}