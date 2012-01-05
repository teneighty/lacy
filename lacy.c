#include <dirent.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"
#include "markdown.h"

#define MAX_INHERIT 50

#define PACKAGE_NAME "lacy"
#define PACKAGE_VERSION "0.0.1"
#define PACKAGE_URL "http://teneighty.github.com/lacy"

#define env_get_page_top(env)  env->p_stack->stack[env->p_stack->size - 1]
#define env_get_page(env)      env->p_stack->stack[env->p_stack->pos]
#define env_get_next_page(env) env->p_stack->stack[env->p_stack->pos + 1]
#define env_has_next(env)      (env->p_stack->pos + 1 < env->p_stack->size)
#define env_inc(env)           env->p_stack->pos++
#define env_dec(env)           env->p_stack->pos--

struct ut_str {
    char *s;
    long def_size;
    long size;
    long len;
};

enum { NORM, REF, HEADER };
enum { NONE, MARKDOWN };

struct page_attr {
    struct ut_str name;
    struct ut_str value;
    struct page_attr *next;
};

struct page {
    struct page *inherits;
    char *file_path;
    char *code;
    int page_type;

    struct page_attr *attr_top;

    struct page *next;
    struct page *prev;
};

static struct page * page_list;

enum TOKENS { IDENT = 0, MEMBER, 
              BLOCK,
              EXP_START, EXP_END, 
              SH_START, SH_BLOCK, SH_END, 
              VAR_START, VAR_END, 
              FOR, IN, DO, DONE, INCLUDE };

struct appconf {
    struct ut_str shell;
    struct ut_str output_dir;
    struct ut_str static_dir;
};

struct tree_node {
    int token;
    int scope;
    struct ut_str buffer;
    struct tree_node *next;
};

struct page_stack {
    struct page **stack;
    int size;
    int pos;
};

struct lacy_env {
    int depth;
    struct page_stack *p_stack;
    struct page_attr *sym_tbl;
};

/* function declarations */
static int build_depth(char *file_path);
static int copy_dir(char *src, char *dest);
static int copy_file(char *src, char *dest);
static bool file_exists(char *s);
static bool flook_ahead(FILE *f, char *s, int n);
static bool slook_ahead(char *f, char *s, int n);
static int iswhitespace(char c);
static int isnewline(char c);
static void fatal(const char *s, ...);
static void setup();
static void breakdown();
static void render(struct page *p);
static void env_build(struct page *p, struct lacy_env *env);
static void env_free(struct lacy_env *env);
static void env_set(struct lacy_env *env, char *ident, char *value);
static void tree_push(int tok, char *buffer);
static void build_tree(struct lacy_env *env);
static char *do_build_tree(char *s, struct lacy_env *env);
static void parse_header(FILE *f, struct page *p);
static struct page * parse_page(FILE *f, char *file_path);
static void parse_filepath(const char *file_path, struct page *p);
static void page_attr_free(struct page *p);
static void page_add(struct page *np);
static struct page * page_find(char *file_path);
static struct page * page_slurp(char *file_path);
static struct page_attr * page_attr_lookup(struct page *e, char *s);
static void page_list_init();
static void page_list_free();
static void page_free();
static char *parse_var(char *s, struct page *p, struct lacy_env *env);
static char *parse_expression(char *s, struct lacy_env *env);
static char *parse_include(char *s, struct lacy_env *env);
static char *parse_foreach(char *s, struct lacy_env *env);
static char *parse_sh_exp(char *s, struct lacy_env *env);
static void str_resize(struct ut_str *u, long ns);
static void str_init(struct ut_str *u);
static void str_append(struct ut_str *u, char c);
static void str_append_str(struct ut_str *u, char *s);
static void str_append_long(struct ut_str *u, long l);
static void str_trim(struct ut_str *u);
static int str_is_empty(struct ut_str *u);
static void str_clear(struct ut_str *u);
static void str_free(struct ut_str *u);
static void write_tree(FILE *out, struct lacy_env *env);
static void do_write_tree(FILE *out, struct lacy_env *env, struct tree_node *top);
static struct tree_node * write_include(FILE *out, struct tree_node *t, struct lacy_env *env);
static struct tree_node * write_var(FILE *out, struct tree_node *t, struct lacy_env *env);
static void write_sh_block(FILE *out, struct tree_node *t, struct lacy_env *env);
static struct tree_node * write_for(FILE *out, struct tree_node *t, struct lacy_env *env);
static struct tree_node * write_member(FILE *out, struct tree_node *t, 
                                       struct page *p, struct lacy_env *env) ;
static int next_token(char **s);
struct page_attr * env_attr_lookup(struct lacy_env *e, char *s);
static void usage();
static void version();
static void write_depth(FILE *out, struct lacy_env *env);

/* variables */
static struct appconf conf;
static int token;
static struct ut_str curtok;
static struct tree_node *tree_top;
static bool quiet_flag = 0;
static int verbosity = 1;


void
setup()
{
    str_init(&conf.shell);
    str_init(&conf.output_dir);
    str_init(&conf.static_dir);

    str_append_str(&conf.shell, "/bin/sh");
    str_append_str(&conf.output_dir, "_output");
    str_append_str(&conf.static_dir, "_static");

    mkdir(conf.output_dir.s, 0777);

    if (file_exists(conf.output_dir.s)) {
        copy_dir(conf.static_dir.s, conf.output_dir.s);
    }
}

void
breakdown()
{
    str_free(&conf.shell);
    str_free(&conf.output_dir);
    str_free(&conf.static_dir);
}

void
env_build(struct page *p, struct lacy_env *env)
{
    env->p_stack->size++; 
    if (NULL == p || NULL == p->inherits 
     || MAX_INHERIT <= env->p_stack->size) {
        env->p_stack->stack = 
            calloc(env->p_stack->size, sizeof (struct page *));
    }
    else if (NULL != p->inherits) {
        env_build(p->inherits, env);
    }
    else {
        return;
    }
    env->p_stack->stack[env->p_stack->pos] = p;
    env->p_stack->pos++;

    struct page_attr *t = p->attr_top;
    while (t != NULL) {
        env_set(env, t->name.s, t->value.s);
        t = t->next;
    }
}

void 
env_free(struct lacy_env *env)
{
    struct page_attr *tmp;
    struct page_attr *t = env->sym_tbl;

    while (t != NULL) {
        tmp = t->next;
        free(t);
        t = tmp;
    }

    free(env->p_stack->stack);
}

void 
env_set(struct lacy_env *env, char *ident, char *value)
{
    struct page_attr *e = env_attr_lookup(env, ident);
    if (NULL == e) {

        e = malloc(sizeof(struct page_attr));
        e->next = NULL;
        str_init(&(e->name));
        str_init(&(e->value));

        str_append_str(&(e->name), ident);
        str_append_str(&(e->value), value);

        if (env->sym_tbl == NULL) {
            env->sym_tbl = e;
        }
        else {
            struct page_attr *t = env->sym_tbl;
            while (t->next != NULL) 
                t = t->next;
            
            t->next = e;
        }
    }
    else {
        str_clear(&(e->value));
        str_append_str(&(e->value), value);
    }
}

void
page_add(struct page *np)
{
    struct page *p;
    if (NULL == page_list) {
        page_list = np;
        np->next = NULL;
        np->prev = NULL;
        return;
    }
    p = page_list;
    while (NULL != p->next) 
        p = p->next;

    p->next = np;
    np->prev = p;
}

struct page *
page_find(char *file_path)
{
    struct page *p = page_list;
    while (NULL != p) {
        if (0 == strcmp(p->file_path, file_path))
            return p;

        p = p->next;
    }
    p = page_slurp(file_path);
    page_add(p);

    return p;
}

struct page * 
page_slurp(char *file_path)
{
    struct page *p = NULL;

    FILE *f = fopen(file_path, "r");

    if (NULL == f) {
        fatal("Unable to open: %s\n", file_path);
    }

    p = parse_page(f, file_path); 
    p->next = NULL;
    p->prev = NULL;

    if (0 != fclose(f)) {
        fatal("Unabled to close: %s\n", file_path);
    }
    return p;
}

struct page *
parse_page(FILE *f, char *file_path)
{
    char c;
    long pos = 0;
    long len = 0;
    struct ut_str buffer;

    /* markdown vars */

    struct page *p = malloc(sizeof(struct page));

    parse_filepath(file_path, p);

    p->inherits = NULL;
    p->attr_top = NULL;
    str_init(&buffer);

    while ((c = fgetc(f)) != EOF) {
        if ('-' == c && flook_ahead(f, "--", 2)) {
            parse_header(f, p);
        }
        else {
            str_append(&buffer, c);
        }
    } 
    while (c != EOF);

    p->code = malloc(sizeof(char) * buffer.size + 1);
    memset(p->code, '\0', buffer.size + 1);

    if (MARKDOWN == p->page_type) {
        Document *doc = mkd_string(buffer.s, buffer.size + 1, 0);
	    if (NULL != doc && mkd_compile(doc, 0) ) {
            char *html = NULL;
            int szdoc = mkd_document(doc, &html);
            strncpy(p->code, html, szdoc);
            mkd_cleanup(doc);

        }
    } else {
        strncpy(p->code, buffer.s, buffer.size);
    }
    str_free(&buffer);


    return p;
}

void 
parse_filepath(const char *file_path, struct page *p) {
    int len;
    char *start;

    len = strlen(file_path) + 1;
    if (NULL != (start = strstr(file_path, ".mkd"))) {
        len = len - strlen(start) - 1;
        p->file_path = malloc(sizeof(char) * (len + 6));
        memset(p->file_path, '\0', len);
        strncpy(p->file_path, file_path, len);
        strncat(p->file_path, ".html", 5);
        p->page_type = MARKDOWN;
    } else {
        p->page_type = NONE;
        p->file_path = malloc(sizeof(char) * len);
        memset(p->file_path, '\0', len);
        strncpy(p->file_path, file_path, len - 1);
    }
}

void
parse_header(FILE *f, struct page *p) 
{
    char c;
    struct ut_str val, var;
    int state = NORM;
    int whitespace = 0;

    str_init(&val);
    str_init(&var);

    do {
        c = fgetc(f);
        if ('-' == c && flook_ahead(f, "--", 2)) {
            str_free(&var);
            str_free(&val);
            return;
        }
        switch (c) {
        case '\n':
        case '\r':
            if (!str_is_empty(&var)) {
                str_trim(&var);
                str_trim(&val);
                if (0 == strcmp("inherits", var.s)) {
                    p->inherits = page_find(val.s);
                }
                else {
                    struct page_attr a;
                    struct page_attr *ap = malloc(sizeof(struct page_attr));

                    memcpy(ap, &a, sizeof(struct page_attr));

                    ap->next = NULL;
                    str_init(&ap->name);
                    str_init(&ap->value);

                    str_append_str(&ap->name, var.s);
                    str_append_str(&ap->value, val.s);

                    if (NULL == p->attr_top) {
                        p->attr_top = ap;
                    }
                    else {
                        struct page_attr *c = p->attr_top;
                        while (c->next != NULL) 
                            c = c->next;

                        c->next = ap;
                    }
                }
            }
            str_clear(&var);
            str_clear(&val);
            state = NORM;
            break;
        case ':':
            state = REF;
            break;
        default:
            if (REF == state) {
                str_append(&val, c);
            }
            else {
                str_append(&var, c);
            }
        }
    }
    while (c != EOF);

    str_free(&var);
    str_free(&val);
}

void
page_free(struct page* p)
{
    if (NULL == p)
        return;

    page_attr_free(p);

    p->inherits = NULL;
    p->next = NULL;
    p->prev = NULL;
    p->attr_top = NULL;

    if (NULL != p->file_path)
        free(p->file_path);
    if (NULL != p->code)
        free(p->code);

    free(p);
}

void
page_list_init() { }

void
page_list_free()
{
    struct page *tmp;
    struct page *p = page_list;
    while (NULL != p) {
        tmp = p->next;
        page_free(p);
        p = tmp;
    }
    page_list = NULL;
}

void
page_attr_free(struct page *p)
{
    struct page_attr *tmp;
    struct page_attr *t = p->attr_top;

    while (t != NULL) {
        tmp = t->next;
        str_free(&t->name);
        str_free(&t->value);
        free(t);
        t = tmp;
    }
}

struct page_attr * 
page_attr_lookup(struct page *e, char *s)
{
    struct page_attr *t = e->attr_top;
    while (t != NULL) {
        if (0 == strcmp(t->name.s, s))
            return t;

        t = t->next;
    }
    return NULL;
}

void 
tree_push(int tok, char *buffer)
{
    struct tree_node *t = malloc(sizeof(struct tree_node));
    t->next = NULL;
    t->token = tok;
    t->scope = 0;
    if (NULL != buffer) {
        str_init(&t->buffer);
        str_append_str(&t->buffer, buffer); 
    }

    if (NULL == tree_top) {
        tree_top = t;
    }
    else {
        struct tree_node *s = tree_top;
        while (s->next != NULL)
            s = s->next;

        s->next = t;
        t->scope = s->scope;

        if (tok == DO)
            t->scope = s->scope + 1;
        else if (tok == DONE) 
            t->scope = s->scope - 1;
    }
}

int 
next_token(char **s)
{
    while (iswhitespace(**s)) 
        (*s)++;

    str_clear(&curtok);
    while (!iswhitespace(**s) && **s != '\0') {
        str_append(&curtok, **s);
        (*s)++;
    }
    if (strcmp("{%", curtok.s) == 0)
        token = EXP_START;
    else if (strcmp("%}", curtok.s) == 0) {
        token = EXP_END;

        /* swallow whitespace to not affect output */
        while (isnewline(**s)) 
            (*s)++;
    }
    else if (strcmp("{$", curtok.s) == 0)
        token = SH_START;
    else if (strcmp("$}", curtok.s) == 0) 
        token = SH_END;
    else if (strcmp("{{", curtok.s) == 0)
        token = VAR_START;
    else if (strcmp("}}", curtok.s) == 0)
        token = VAR_END;
    else if (strcmp("for", curtok.s) == 0)
        token = FOR;
    else if (strcmp("in", curtok.s) == 0)
        token = IN;
    else if (strcmp("do", curtok.s) == 0)
        token = DO;
    else if (strcmp("done", curtok.s) == 0)
        token = DONE;
    else if (strcmp("include", curtok.s) == 0) 
        token = INCLUDE;
    else {
        token = IDENT;
    }
    return token;
}

void 
render(struct page *p)
{
    int depth;
    FILE *out;
    struct lacy_env env;
    struct page_stack p_stack;
    struct ut_str outfile;

    str_init(&curtok);

    if (NULL == p)
        return;

    str_init(&outfile);
    str_append_str(&outfile, conf.output_dir.s);
    str_append(&outfile, '/');
    str_append_str(&outfile, p->file_path);

    /* depth - 1 since we added output dir to path */
    depth = build_depth(outfile.s) - 1;
    if (NULL == (out = fopen(outfile.s, "w"))) 
        fatal("Unable to open: %: ", outfile.s);

    p_stack.size = 0;
    p_stack.pos = 0;
    /* Build Environment */
    env.depth = depth;
    env.p_stack = &p_stack;
    env.sym_tbl = NULL;

    env_build(p, &env);
    /* set stack back to top */
    p_stack.pos = 0;

    /* do it already */
    build_tree(&env);
    write_tree(out, &env);

    env_free(&env);
    fclose(out);

    str_free(&curtok);

    if (verbosity > 0) {
        printf("Rendered %s\n", outfile.s);
    }
    str_free(&outfile);
}

void 
build_tree(struct lacy_env *env)
{
    struct page *p = env_get_page(env);
    char *s = p->code;
    do_build_tree(s, env);
}

char * 
do_build_tree(char *s, struct lacy_env *env)
{
    struct page *p = env_get_page(env);
    struct ut_str buffer;
    str_init(&buffer);

    while (*s != '\0') {
        if (slook_ahead(s, "{{", 2)) {
            s += 2;
            tree_push(BLOCK, buffer.s);
            str_clear(&buffer);
            s = parse_var(s, p, env);
            continue;
        }
        else if (slook_ahead(s, "{%", 2)) {
            s += 2;
            tree_push(BLOCK, buffer.s);
            str_clear(&buffer);
            s = parse_expression(s, env);
            continue;
        }
        else if (slook_ahead(s, "{$", 2)) {
            s += 2;
            tree_push(BLOCK, buffer.s);
            str_clear(&buffer);
            s = parse_sh_exp(s, env);
            continue;
        }
        else {
            str_append(&buffer, *s);
        }
        ++s;
    }
    tree_push(BLOCK, buffer.s);
    str_free(&buffer);
}

char *
parse_var(char *s, struct page *p, struct lacy_env *env)
{
    struct ut_str var;
    str_init(&var);

    while (*s != '\0') {
        if ('.' == *s) {
            tree_push(IDENT, var.s);
            tree_push(MEMBER, NULL);
            str_clear(&var);
        }
        else if (slook_ahead(s, "}}", 2)) {
            s += 2;
            if (0 == strcmp(var.s, "content")) {
                if (env_has_next(env)) {
                    env_inc(env);
                    build_tree(env);
                    env_dec(env);
                }
            }
            else {
                tree_push(IDENT, var.s);
            }
            break;
        }
        else {
            str_append(&var, *s);
        }
        ++s;
    }
    str_free(&var);

    return s;
}

char *
parse_expression(char *s, struct lacy_env *env)
{
    int t = next_token(&s);
    switch (t) {
        case FOR:
            tree_push(FOR, NULL);
            s = parse_foreach(s, env);
            break;
        case DONE:
            tree_push(DONE, NULL);
            break;
        case INCLUDE:
            tree_push(INCLUDE, NULL);
            s = parse_include(s, env);
            break;
        default:
            fatal("excepted for\n");
    }
    t = next_token(&s);
    switch (t) {
        case EXP_END:
            break;
        default:
            fatal("excepted expression end\n");
    }
    return s;
}

char *
parse_include(char *s, struct lacy_env *env)
{
    struct page *p;
    int t = next_token(&s);
    switch (t) {
        case IDENT:
            p = page_find(curtok.s);
            do_build_tree(p->code, env);
            break;
        default:
            fatal("excepted ident");
    }
    return s;
}

char *
parse_foreach(char *s, struct lacy_env *env)
{
    int t = next_token(&s);
    switch (t) {
        case IDENT:
            tree_push(IDENT, curtok.s);
            break;
        default:
            fatal("excepted ident");
    }
    t = next_token(&s);
    switch (t) {
        case IN:
            break;
        default:
            fatal("excepted IN");
    }
    t = next_token(&s);
    switch (t) {
        case IDENT:
            tree_push(IDENT, curtok.s);
            break;
        case SH_START:
            s = parse_sh_exp(s, env);
            break;
        default:
            fatal("excepted List or Shell expression");
    }
    t = next_token(&s);
    switch (t) {
        case DO:
            tree_push(DO, NULL);
            break;
        default:
            fatal("excepted DO");
    }
    return s;
}

char *
parse_sh_exp(char *s, struct lacy_env *env)
{
    struct ut_str var;
    str_init(&var);

    while (*s != '\0') {
        if (slook_ahead(s, "$}", 2)) {
            s += 2;
            tree_push(SH_BLOCK, var.s);
            break;
        }
        else {
            str_append(&var, *s);
        }
        ++s;
    }
    str_free(&var);

    return s;
}


void 
write_tree(FILE *out, struct lacy_env *env)
{
    struct tree_node *tmp, *top;

    top = tree_top;
    do_write_tree(out, env, top);

    while (top != NULL) {
        tmp = top;
        top = top->next;
        if (NULL != tmp)
            free(tmp);  
    }
    tree_top = NULL;
}

void 
do_write_tree(FILE *out, struct lacy_env *env, struct tree_node *top)
{
    struct tree_node *t = top;
    while (t != NULL) {
        switch (t->token) {
        case BLOCK:
            fputs(t->buffer.s, out);
            break;
        case INCLUDE:
            t = write_include(out, t, env);
            break;
        case IDENT:
            t = write_var(out, t, env);
            break;
        case FOR:
            t = write_for(out, t, env);
            break;
        case DONE:
            return;
        case SH_BLOCK:
            write_sh_block(out, t, env);
            break;
        default:
            break;
        }
        t = t->next;
    }
}

struct tree_node * 
write_include(FILE *out, struct tree_node *t, struct lacy_env *env)
{
    if (t->next != NULL && IDENT == t->next->token) {
        t = t->next;
        if (NULL != t) {
            struct page *p = page_find(t->buffer.s);
            t = write_member(out, t, p, env);
        }
        t = t->next;
    }
    return t;
}

struct tree_node * 
write_var(FILE *out, struct tree_node *t, struct lacy_env *env)
{
    if (0 == strcmp(t->buffer.s, "root")) {
        write_depth(out, env);
    }
    else if (0 == strcmp(t->buffer.s, "this")) {
        struct page *p = env_get_page_top(env);
        if (NULL != p) {
            if (t->next != NULL && MEMBER == t->next->token) {
                t = t->next->next;
                t = write_member(out, t, p, env);
            }
            else {
                fprintf(out, "%s", p->file_path);
            }
        }
    }
    else {
        if (env_has_next(env)) {
            struct page_attr *a;
            a = env_attr_lookup(env, t->buffer.s);
            if (t->next != NULL && MEMBER == t->next->token) {
                t = t->next->next;
                if (NULL != a) {
                    struct page *p = page_find(a->value.s);
                    t = write_member(out, t, p, env);
                }
            }
            else 
            {
                if (NULL != a) {
                    fprintf(out, "%s", a->value.s);
                }
            }
        }
    }
    return t;
}

struct tree_node *
write_member(FILE *out, struct tree_node *t, 
             struct page *p, struct lacy_env *env) 
{
    struct page_attr *pa;
    /* the attribute does not exist in page */
    if (NULL == p) 
        return t;

    pa = page_attr_lookup(p, t->buffer.s);
    /* the page has the member */
    if (NULL != pa) 
        fprintf(out, "%s", pa->value.s);

    return t;
}

void 
write_sh_block(FILE *out, struct tree_node *t, struct lacy_env *env)
{
    char c;
    FILE *cmd;

    cmd = popen(t->buffer.s, "r");
    while ((c = fgetc(cmd)) != EOF) {
        fputc(c, out);
    }
    fclose(cmd);
}

struct tree_node *
write_for(FILE *out, struct tree_node *t, struct lacy_env *env)
{
    DIR *d;
    struct tree_node *var, *list;
    struct dirent *de; 

    /* Pop var IDENT */
    t = t->next; var = t;        
    /* Pop var list IDENT */
    t = t->next; list = t;        
    t = t->next;

    if (list->token == SH_BLOCK) {
        char c;
        struct ut_str file_path;
        str_init(&file_path);

        FILE *cmd = popen(list->buffer.s, "r");
        while ((c = fgetc(cmd)) != EOF) {
            if (!iswhitespace(c)) {
                str_append(&file_path, c);
            }
            else {
                if (!str_is_empty(&file_path)) {
                    env_set(env, var->buffer.s, file_path.s);
                    do_write_tree(out, env, t);

                    str_clear(&file_path);
                }
            }
        }
        fclose(cmd);
        str_free(&file_path);
    }
    else if (file_exists(list->buffer.s)) {
        /* Read directory */
        if (NULL != (d = opendir(list->buffer.s))) {
            while ((de = readdir(d)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 
                || strcmp(de->d_name, "..") == 0)
                    continue;

                env_set(env, var->buffer.s, de->d_name);
                do_write_tree(out, env, t);
            }
            closedir(d);
        }
    }

    while (t->scope != var->scope) 
        t = t->next;

    return t;
}

void
write_depth(FILE *out, struct lacy_env *env)
{
    int d;
    if (env->depth > 0) {
        for (d = 1; d < env->depth; ++d) {
            fprintf(out, "../");
        }
        fprintf(out, "..");
    }
    else {
        fprintf(out, ".");
    }
}

struct page_attr * 
env_attr_lookup(struct lacy_env *e, char *s)
{
    struct page_attr *t = e->sym_tbl;
    while (t != NULL) {
        if (0 == strcmp(t->name.s, s))
            return t;

        t = t->next;
    }
    return NULL;
}

void 
usage()
{
printf("Usage: " PACKAGE_NAME " [OPTION]... [FILE]... \n\
  -h, --help      Show usage information\n\
  -q, --quiet     Supress all output\n\
  -v, --verbose   Increase verbosity\n\
  -V, --version   Print version\n\
");
    exit(EXIT_SUCCESS);
}

void 
version()
{
    printf(PACKAGE_NAME " version " PACKAGE_VERSION " (" PACKAGE_URL ") \n");
    exit(EXIT_SUCCESS);
}


void 
str_resize(struct ut_str *u, long ns)
{
    while (u->len + ns >= u->size) {
        u->size += u->def_size;
        u->s = realloc(u->s, u->size);
    }
}

void
str_init(struct ut_str *u)
{
    u->len = 0;
    u->size = BUFSIZ;
    u->def_size = BUFSIZ;
    u->s = malloc(sizeof(char) * BUFSIZ);
    memset(u->s, '\0', BUFSIZ);
}

/* XXX: fix me */
void
str_append(struct ut_str *u, char c) 
{
    str_resize(u, 1);
    u->s[u->len] = c;
    u->len++;
    u->s[u->len] = '\0';
}

/* XXX: fix me */
void
str_append_str(struct ut_str *u, char *s) 
{
    long l = strlen(s);
    str_resize(u, l);
    u->len += l;
    strcat(u->s, s);
    u->s[u->len] = '\0';
}

void 
str_trim(struct ut_str *u)
{
    char *str = u->s;
    char *s = str;
    while (iswhitespace(*s) == 1 && *s != '\0') 
        s++;

    while (*s != '\0') {
        *str = *s;
        str++; s++;
    }
    *str = '\0';
    str--;
    while (iswhitespace(*str)) {
        *str = '\0';
        s--;
    }
}

int 
iswhitespace(char c)
{
    return c == ' ' || isnewline(c);
}

int
isnewline(char c)
{
    return c == '\n' || c == '\r';
}

void
str_append_long(struct ut_str *u, long l) 
{
    char buf[BUFSIZ];
    snprintf(buf, BUFSIZ, "%ld", l);
    str_append_str(u, buf);
}

int
str_is_empty(struct ut_str *u)
{
    if (u->len <= 0)
        return 1;

    return 0;
}

void
str_clear(struct ut_str *u)
{
    u->len = 0;
    u->size = u->def_size;
    u->s = realloc(u->s, u->size);
    memset(u->s, '\0', u->size);
}

void
str_free(struct ut_str *u)
{
    if (NULL != u->s) 
        free(u->s);
}

bool
file_exists(char *s)
{
    FILE *f = fopen(s, "r");
    if (NULL == f) {
        return false;
    }
    fclose(f);
    return true;

}

int
build_depth(char *file_path) 
{
    char *s;
    int depth = 0;
    struct ut_str buf;
    str_init(&buf);

    s = file_path;
    while (*s != '\0') {
        if ('/' == *s) {
            mkdir(buf.s, 0777);
            depth++;
        }
        str_append(&buf, *s);
        s++;
    }
    str_free(&buf);

    return depth;
}

int
copy_dir(char *src, char *dest)
{
    DIR *d;
    struct dirent *de; 
    if (NULL == (d = opendir(src))) {
        return 0;
    }

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 
         || strcmp(de->d_name, "..") == 0)
            continue;

        struct ut_str u_s, u_d;
        str_init(&u_d);
        str_init(&u_s);

        str_append_str(&u_d, dest);
        str_append_str(&u_s, src);

        str_append(&u_d, '/');
        str_append(&u_s, '/');

        str_append_str(&u_d, de->d_name);
        str_append_str(&u_s, de->d_name);

        if (verbosity > 1)
            printf("Copying %s\n", u_s.s);
        if (DT_DIR == de->d_type) {
            mkdir(u_d.s, 0777);
            copy_dir(u_s.s, u_d.s);
        }
        else {
            copy_file(u_s.s, u_d.s);
        }
        str_free(&u_s);
        str_free(&u_d);
    }
    closedir(d);
}

int
copy_file(char *src, char *dest)
{
    char c;
    FILE *s, *d;
    if ((s = fopen(src, "r")) == NULL) 
        return 0;

    if ((d = fopen(dest, "w")) == NULL) 
        return 0;

    while (!feof(s)) {
        c = fgetc(s);
        fputc(c, d);
    }

    fclose(s);
    fclose(d);
}

bool 
flook_ahead(FILE *f, char *s, int len)
{
    int i;
    char c;
    long offset = 0;
    for (i = 0; i < len; ++i) {
        c = fgetc(f);
        offset--;
        if (c != s[i]) {
            fseek(f, offset, SEEK_CUR);
            return false;
        }
    }
    return true;
}

bool 
slook_ahead(char *f, char *s, int len)
{
    int i;
    char c;
    for (i = 0; i < len; ++i) {
        if (f[i] != s[i]) {
            return false;
        }
    }
    return true;
}

void
warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void 
fatal(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

int
main (int argc, char **argv)
{
    int c;

    while (true)
    {
        static struct option long_options[] =
        {
            {"verbose", no_argument, NULL, (int)'v'},
            {"version", no_argument, NULL, (int)'V'},
            {"quiet",   no_argument, NULL, (int)'q'},
            {"help",    no_argument, NULL, (int)'h'},
            {0, 0, 0, 0}
        };

        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "hqvV", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 0:
            case 'v':
                verbosity += 1;
                break;
            case 'V':
                version();
                break;
            case 'q':
                quiet_flag = true;
                break;
            case 'h':
                usage();
                break;
        default:
            break;
        }
    }
    if (quiet_flag) {
        verbosity = 0;
    }

    setup();
    page_list_init();

    if (optind < argc) {
        while (optind < argc) {
            char *s = argv[optind++];
            struct page *p = page_find(s);
            render(p);
        }
    }
    page_list_free();

    return 0;
}

/* vim:set ft=c sw=4 ts=4 et: */
