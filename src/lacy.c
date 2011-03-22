#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "page.h"
#include "util.h"

#define MAX_INHERIT 50

#define env_get_page_top(env)  env->p_stack->stack[env->p_stack->size - 1]
#define env_get_page(env)      env->p_stack->stack[env->p_stack->pos]
#define env_get_next_page(env) env->p_stack->stack[env->p_stack->pos + 1]
#define env_has_next(env)      (env->p_stack->pos + 1 < env->p_stack->size)
#define env_inc(env)           env->p_stack->pos++
#define env_dec(env)           env->p_stack->pos--

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

/* config */
struct appconf conf;

/* curr token */
int token;
struct ut_str curtok;

/* top of stack */
struct tree_node *tree_top;

bool quiet_flag = 0;
int verbosity = 1;

static void setup();
static void breakdown();
static void render(struct page *p);

static void env_build(struct page *p, struct lacy_env *env);
static void env_free(struct lacy_env *env);
static void env_set(struct lacy_env *env, char *ident, char *value);

static void tree_push(int tok, char *buffer);

static void build_tree(struct lacy_env *env);
static char *do_build_tree(char *s, struct lacy_env *env);
static char *parse_var(char *s, struct page *p, struct lacy_env *env);
static char *parse_expression(char *s, struct lacy_env *env);
static char *parse_include(char *s, struct lacy_env *env);
static char *parse_foreach(char *s, struct lacy_env *env);
static char *parse_sh_exp(char *s, struct lacy_env *env);


static void write_tree(FILE *out, struct lacy_env *env);
static void do_write_tree(FILE *out, struct lacy_env *env, struct tree_node *top);

static struct tree_node * write_include(FILE *out, struct tree_node *t, struct lacy_env *env);
static struct tree_node * write_var(FILE *out, struct tree_node *t, struct lacy_env *env);
static void write_sh_block(FILE *out, struct tree_node *t, struct lacy_env *env);
static struct tree_node * write_for(FILE *out, struct tree_node *t, struct lacy_env *env);

static struct tree_node * write_member(FILE *out, struct tree_node *t, 
                                       struct page *p, struct lacy_env *env) ;
static void write_depth(FILE *out, struct lacy_env *env);

static int next_token(char **s);

struct page_attr * env_attr_lookup(struct lacy_env *e, char *s);

static void usage();
static void version();

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

