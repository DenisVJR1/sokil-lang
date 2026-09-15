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
    T_AND, T_OR, T_NOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_PLUSPLUS, T_MINUSMINUS,
    T_EQ, T_EQEQ, T_NEQ, T_LT, T_GT, T_LTE, T_GTE, T_BANG,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_COMMA, T_SEMICOLON,
    T_NEWLINE, T_EOF
};

typedef struct { int type; double num; char *text; int line; } Token;

static const char *tok_names[] = {
    "NUM","STR","IDENT","TRUE","FALSE","NIL","LET","IF","ELIF","ELSE",
    "WHILE","FN","RETURN","FOR","BREAK","CONTINUE","AND","OR","NOT",
    "PLUS","MINUS","STAR","SLASH","PERCENT","PLUSPLUS","MINUSMINUS",
    "EQ","EQEQ","NEQ","LT","GT","LTE","GTE","BANG","LPAREN",
    "RPAREN","LBRACE","RBRACE","LBRACKET","RBRACKET","COMMA","SEMICOLON","NEWLINE","EOF"
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

static Token lx_string(Lexer *lx) {
    int line = lx->line;
    lx_adv(lx);                               /* " */
    size_t start = lx->p;
    size_t len = 0;
    while (1) {
        char c = lx_adv(lx);
        if (c == '"') break;
        if (c == '\0' || c == '\n')
            fatal_fmt("Незакритий рядок (рядок %d)", line);
        if (c == '\\') {
            char e = lx_adv(lx);
            if (e == 'n' || e == 't' || e == '\\' || e == '"') len++;
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
        if (c == '"') { lx_push(&lx, lx_string(&lx)); continue; }
        if (isdigit((unsigned char)c)) { lx_push(&lx, lx_number(&lx)); continue; }
        if (isalpha((unsigned char)c) || c == '_') { lx_push(&lx, lx_ident(&lx)); continue; }

        if (c == '=' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_EQEQ, 0, NULL)); continue; }
        if (c == '!' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_NEQ, 0, NULL)); continue; }
        if (c == '<' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_LTE, 0, NULL)); continue; }
        if (c == '>' && lx_peek(&lx, 1) == '=') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_GTE, 0, NULL)); continue; }
        if (c == '+' && lx_peek(&lx, 1) == '+') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_PLUSPLUS, 0, NULL)); continue; }
        if (c == '-' && lx_peek(&lx, 1) == '-') { lx_adv(&lx); lx_adv(&lx); lx_push(&lx, lx_tok(&lx, T_MINUSMINUS, 0, NULL)); continue; }

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
            case '(': t = T_LPAREN; break;
            case ')': t = T_RPAREN; break;
            case '{': t = T_LBRACE; break;
            case '}': t = T_RBRACE; break;
            case '[': t = T_LBRACKET; break;
            case ']': t = T_RBRACKET; break;
            case ',': t = T_COMMA; break;
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
    N_RETURN, N_BLOCK, N_FOR, N_BREAK, N_CONTINUE
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
    while (pr_peek(p).type == T_NEWLINE) p->i++;
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
        case T_LBRACE: return pr_block(p);
        case T_NEWLINE:
        case T_SEMICOLON: p->i++; return pr_stmt(p);
        default:       return pr_expr_stmt(p);
    }
}

/* for i = 0; i < 10; i = i + 1 { ... } */
static Node *pr_for(Parser *p) {
    int line = pr_adv(p).line;                    /* for */
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

static Node *pr_expr(Parser *p) { return pr_or(p); }

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
            if (!env_assign(I->env, n->str, v)) {
                char buf[256];
                snprintf(buf, sizeof buf, "Невідома змінна '%s' (рядок %d)", n->str, n->line);
                err_raise(I, buf);
            }
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
    return mk_nil();
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
"  ███████╗ ██████╗ ██╗  ██╗██╗██╗\n"
"  ██╔════╝██╔═══██╗██║ ██╔╝██║██║\n"
"  ███████╗██║   ██║█████╔╝ ██║██║\n"
"  ╚════██║██║   ██║██╔═██╗ ██║██║\n"
"  ███████║╚██████╔╝██║  ██╗██║███████╗\n"
"  ╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝╚══════╝\n";

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
    printf("Сокіл v2.0 — REPL (exit щоб вийти)\n");
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

int main(int argc, char **argv) {
    g_argc = argc; g_argv = argv;
    srand((unsigned)time(NULL));
    if (argc > 1) {
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