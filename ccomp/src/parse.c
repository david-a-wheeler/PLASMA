/*
 * Stage 1: the parser. Recursive descent for declarations and
 * statements, precedence climbing for expressions (approach.md, issue
 * 5). It emits IR ops as it goes (ir_put) and builds no syntax tree.
 *
 * Milestone 1 language:
 *   - function definitions and prototypes returning int or void, with
 *     int parameters;
 *   - statements: return, expression statements, empty statements and
 *     nested blocks;
 *   - expressions: integer and character constants, string literals,
 *     parameters, function calls, binary +, parentheses, and the pstr()
 *     builtin.
 * A function called but not defined in the file must be one of the
 * CMDSYS functions in cmdsys_funcs[] below; it is imported from CMDSYS.
 *
 * Like PLASMA, a function returns exactly as many values as it is
 * declared to: one for int, none for void. So a void call used as a
 * statement needs no DROP, and a void value can't be used.
 */
#include <string.h>
#include "ccish.h"

Sym syms[MAX_SYMS];         /* functions */
int nsyms;

static Sym locals[MAX_PARAMS];
static int nlocals;
static Sym *cur_fn;
static int nest;            /* current nesting depth */

static const char *quote(const char *name)
{
    static char buf[MAX_NAME + 3];
    strcpy(buf, "'");
    strcat(buf, name);
    strcat(buf, "'");
    return buf;
}

static void not_yet(void)
{
    error("not supported yet:", tok_desc());
}

static void expect(int t, const char *what)
{
    if (tok != t)
        error(what, tok_desc());
    next();
}

static void enter_nest(void)
{
    if (++nest > MAX_NEST)
        fatal("nesting too deep", NULL);
}

static Sym *find_func(const char *name)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name))
            return &syms[i];
    return NULL;
}

static Sym *add_func(const char *name)
{
    Sym *s;
    if (nsyms >= MAX_SYMS)
        fatal("too many functions", NULL);
    s = &syms[nsyms++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_FUNC;
    s->nparams = -1;
    s->returns_value = 1;   /* C89 implicit declarations return int */
    s->import_idx = -1;
    return s;
}

/*
 * The CMDSYS functions a program may call, from src/inc/cmdsys.plh,
 * with their parameter counts and whether they return a value. Their
 * names and meanings are PLASMA's, not C's: puts() takes a PLASMA
 * string and adds no newline. divmod() returns two values and is left
 * out.
 */
static const struct { const char *name; int nparams, returns_value; }
cmdsys_funcs[] = {
    {"putc", 1, 0}, {"putln", 0, 0}, {"puts", 1, 0}, {"puti", 1, 0},
    {"putb", 1, 0}, {"puth", 1, 0}, {"getc", 0, 1}, {"gets", 1, 1},
    {"call", 5, 1}, {"syscall", 2, 1}, {"heapmark", 0, 1},
    {"heapallocalign", 3, 1}, {"heapalloc", 1, 1}, {"heaprelease", 1, 1},
    {"heapavail", 0, 1}, {"memset", 3, 0}, {"memcpy", 3, 0},
    {"strcpy", 2, 1}, {"strcat", 2, 1}, {"toupper", 1, 1}, {"sext", 1, 1},
    {"isugt", 2, 1}, {"isuge", 2, 1}, {"isult", 2, 1}, {"isule", 2, 1},
    {NULL, 0, 0}
};

static int find_cmdsys(const char *name)
{
    int i;
    for (i = 0; cmdsys_funcs[i].name; i++)
        if (!strcmp(cmdsys_funcs[i].name, name))
            return i;
    return -1;
}

static Sym *find_local(const char *name)
{
    int i;
    for (i = 0; i < nlocals; i++)
        if (!strcmp(locals[i].name, name))
            return &locals[i];
    return NULL;
}

/* ---------- Expressions ---------- */

static int parse_expr(int min_prec);

/* Parse an expression that must produce a value. */
static void parse_value(void)
{
    if (parse_expr(1))
        error("void value used in an expression", NULL);
}

/*
 * Binary operator precedence (higher binds tighter), or 0 if tok is not
 * a binary operator. Operators C has but we don't support yet return -1.
 */
static int binary_prec(void)
{
    switch (tok) {
    case '+':
        return 12;
    case '-': case '*': case '/': case '%': case T_SHL: case T_SHR:
    case '<': case '>': case T_LE: case T_GE: case T_EQ: case T_NE:
    case '&': case '^': case '|': case T_ANDAND: case T_OROR: case '?':
    case '=': case T_ADDEQ: case T_SUBEQ: case T_MULEQ: case T_DIVEQ:
    case T_MODEQ: case T_ANDEQ: case T_OREQ: case T_XOREQ: case T_SHLEQ:
    case T_SHREQ:
        return -1;
    }
    return 0;
}

/* Returns 1 if the call produces no value. */
static int parse_call(Sym *fn)
{
    int nargs = 0;
    next();                         /* '(' */
    if (tok != ')') {
        for (;;) {
            parse_value();
            nargs++;
            if (tok != ',')
                break;
            next();
        }
    }
    expect(')', "expected ')' after arguments, not");
    if (fn->nparams < 0) {
        fn->nparams = nargs;        /* first use fixes the count */
    } else if (fn->nparams != nargs) {
        error("wrong number of arguments in call to", quote(fn->name));
    }
    fn->called = 1;
    ir_put(IR_CALL, nargs, fn);
    return !fn->returns_value;
}

/* pstr("literal"): the address of the literal's PLASMA (length-prefixed)
 * form, which is the byte just before its first character. */
static void parse_pstr(void)
{
    int off;
    next();                         /* '(' */
    if (tok != T_STR)
        error("pstr() needs a string literal, not", tok_desc());
    if (tok_len > 255)
        error("string too long for pstr() (over 255 characters)", NULL);
    off = data_string(tok_text, tok_len);
    next();
    expect(')', "expected ')' after pstr argument, not");
    ir_put(IR_DATAADDR, off - 1, NULL);
}

/* Returns 1 if the expression produces no value (a void call). */
static int parse_primary(void)
{
    char name[MAX_NAME + 1];
    Sym *s;
    int is_void, c;

    switch (tok) {
    case T_NUM:
        ir_put(IR_CONST, tok_val, NULL);
        next();
        return 0;
    case T_STR:
        ir_put(IR_DATAADDR, data_string(tok_text, tok_len), NULL);
        next();
        return 0;
    case '(':
        enter_nest();
        next();
        is_void = parse_expr(1);
        expect(')', "expected ')', not");
        nest--;
        return is_void;
    case T_ID:
        strcpy(name, tok_text);
        next();
        if (tok == '(') {
            if (!strcmp(name, "pstr")) {
                parse_pstr();
                return 0;
            }
            s = find_local(name);
            if (s)
                error("called object is not a function:", quote(name));
            s = find_func(name);
            if (!s) {
                s = add_func(name);
                c = find_cmdsys(name);
                if (c >= 0) {
                    /* Declared by the CMDSYS table. */
                    s->nparams = cmdsys_funcs[c].nparams;
                    s->returns_value = cmdsys_funcs[c].returns_value;
                    s->declared = 1;
                }
                /* Otherwise an implicit declaration: returns int, and
                 * this call fixes the parameter count. */
            }
            return parse_call(s);
        }
        s = find_local(name);
        if (s) {
            ir_put(IR_LOCAL, s->offset, NULL);
            return 0;
        }
        if (find_func(name))
            error("function pointers are not supported yet:", quote(name));
        error("undeclared identifier", quote(name));
        return 0;
    case T_KEYWORD:
        not_yet();
        return 0;
    }
    error("expected an expression, not", tok_desc());
    return 0;
}

static int parse_unary(void)
{
    int is_void;
    switch (tok) {
    case '-': case '+': case '!': case '~': case '*': case '&':
    case T_INC: case T_DEC:
        not_yet();
    }
    is_void = parse_primary();
    switch (tok) {
    case '[': case '.': case T_ARROW: case T_INC: case T_DEC:
        not_yet();
    }
    return is_void;
}

/* Precedence climbing: parse operands and binary operators whose
 * precedence is at least min_prec. All supported operators are
 * left-associative. Returns 1 if the expression produces no value. */
static int parse_expr(int min_prec)
{
    int is_void;
    enter_nest();
    is_void = parse_unary();
    for (;;) {
        int prec = binary_prec();
        int op = tok;
        if (prec < 0)
            not_yet();
        if (prec == 0 || prec < min_prec)
            break;
        if (is_void)
            error("void value used in an expression", NULL);
        next();
        if (parse_expr(prec + 1))
            error("void value used in an expression", NULL);
        if (op == '+')
            ir_put(IR_ADD, 0, NULL);
    }
    nest--;
    return is_void;
}

/* ---------- Statements ---------- */

static void parse_block(void);

static void parse_statement(void)
{
    switch (tok) {
    case '{':
        parse_block();
        return;
    case ';':
        next();
        return;
    case T_RETURN:
        next();
        if (tok == ';') {
            if (cur_fn->returns_value)
                ir_put(IR_CONST, 0, NULL);  /* C89 allows this */
        } else {
            if (!cur_fn->returns_value)
                error("void function returns a value:", quote(cur_fn->name));
            parse_value();
        }
        expect(';', "expected ';' after return value, not");
        ir_put(IR_RETURN, 0, NULL);
        return;
    case T_INT:
    case T_VOID:
        error("local variables are not supported yet", NULL);
        return;
    case T_KEYWORD:
        not_yet();
        return;
    }
    if (!parse_expr(1))
        ir_put(IR_DROP, 0, NULL);
    expect(';', "expected ';' after expression, not");
}

static void parse_block(void)
{
    enter_nest();
    next();                         /* '{' */
    while (tok != '}') {
        if (tok == T_EOF)
            error("missing '}' at end of file", NULL);
        parse_statement();
    }
    next();
    nest--;
}

/* ---------- Declarations ---------- */

/* Parse a parameter list starting at '('. Returns the parameter count,
 * or -1 for "()" (unspecified in a prototype, none in a definition).
 * Names go into locals[]. */
static int parse_params(void)
{
    int n = 0;
    nlocals = 0;
    next();                         /* '(' */
    if (tok == ')') {
        next();
        return -1;
    }
    if (tok == T_VOID) {
        next();
        if (tok == ')') {
            next();
            return 0;
        }
        error("a parameter can't have type void", NULL);
    }
    for (;;) {
        if (tok == T_ELLIPSIS)
            error("variable argument lists are not supported yet", NULL);
        if (tok == T_KEYWORD)
            not_yet();
        expect(T_INT, "expected a parameter type, not");
        if (tok == '*' || tok == '[')
            error("pointer parameters are not supported yet", NULL);
        if (n >= MAX_PARAMS)
            fatal("too many parameters", NULL);
        locals[n].name[0] = 0;
        if (tok == T_ID) {
            if (find_local(tok_text))
                error("duplicate parameter", quote(tok_text));
            strcpy(locals[n].name, tok_text);
            next();
        }
        locals[n].kind = S_PARAM;
        locals[n].offset = 2 * n;   /* where ENTER puts parameter n */
        n++;
        nlocals = n;
        if (tok != ',')
            break;
        next();
    }
    expect(')', "expected ')' after parameters, not");
    return n;
}

static void parse_toplevel(void)
{
    int returns_value, nparams, i;
    Sym *fn;

    if (tok == T_INT)
        returns_value = 1;
    else if (tok == T_VOID)
        returns_value = 0;
    else if (tok == T_KEYWORD) {
        not_yet();
        return;
    } else {
        error("expected a function definition, not", tok_desc());
        return;
    }
    next();
    if (tok == '*')
        error("pointer return types are not supported yet", NULL);
    if (tok != T_ID)
        error("expected a function name, not", tok_desc());
    fn = find_func(tok_text);
    if (!fn)
        fn = add_func(tok_text);
    next();
    if (tok != '(')
        error("global variables are not supported yet", NULL);

    nparams = parse_params();
    if (tok == ';') {
        /* A prototype. "()" leaves the parameter count unspecified. */
        /* A call before any declaration assumed int, and its code
         * already depends on that. */
        if ((fn->declared || fn->called)
            && fn->returns_value != returns_value)
            error("conflicting declaration of", quote(fn->name));
        fn->returns_value = returns_value;
        if (nparams >= 0) {
            if (fn->nparams >= 0 && fn->nparams != nparams)
                error("conflicting parameter count for", quote(fn->name));
            fn->nparams = nparams;
        }
        fn->declared = 1;
        next();
        return;
    }
    if (nparams < 0)
        nparams = 0;
    if (tok != '{')
        error("expected '{' or ';' after function header, not", tok_desc());
    for (i = 0; i < nlocals; i++)
        if (!locals[i].name[0])
            error("parameter name missing in definition of",
                  quote(fn->name));
    if (fn->defined)
        error("redefinition of", quote(fn->name));
    if ((fn->declared || fn->called) && fn->returns_value != returns_value)
        error("conflicting declaration of", quote(fn->name));
    if (fn->nparams >= 0 && fn->nparams != nparams)
        error("parameter count differs from earlier use of",
              quote(fn->name));
    if (!strcmp(fn->name, "main") && nparams != 0)
        error("main with parameters is not supported yet", NULL);
    fn->returns_value = returns_value;
    fn->nparams = nparams;
    fn->declared = 1;
    fn->defined = 1;

    cur_fn = fn;
    ir_put(IR_FUNC, nparams, fn);
    parse_block();
    /* Falling off the end of an int function returns 0. */
    if (returns_value)
        ir_put(IR_CONST, 0, NULL);
    ir_put(IR_RETURN, 0, NULL);
    ir_put(IR_ENDFUNC, 2 * nparams, fn);
    cur_fn = NULL;
    nlocals = 0;
}

void parse_program(void)
{
    int i, c;
    while (tok != T_EOF)
        parse_toplevel();

    /* Every function called but not defined here must come from
     * CMDSYS, with the signature CMDSYS has. */
    src_line = 0;
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
        if (s->defined || !s->called)
            continue;
        c = find_cmdsys(s->name);
        if (c < 0)
            error("call to undefined function", quote(s->name));
        if (s->nparams != cmdsys_funcs[c].nparams
            || s->returns_value != cmdsys_funcs[c].returns_value)
            error("declaration doesn't match the CMDSYS function",
                  quote(s->name));
    }
}
