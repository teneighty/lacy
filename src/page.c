
#include "page.h"
#include "util.h"

/* HEAD of doubly linked page list */
static struct page * page_list;
static struct page * parse_page(FILE *f, char *file_path);
static void parse_header(FILE *f, struct page *p);
static void page_attr_free(struct page *p);

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

    struct page *p = malloc(sizeof(struct page));

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
    strncpy(p->code, buffer.s, buffer.size);
    str_free(&buffer);

    int l = strlen(file_path) + 1;
    p->file_path = malloc(sizeof(char) * l);
    memset(p->file_path, '\0', l);
    strncpy(p->file_path, file_path, l - 1);

    return p;
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
