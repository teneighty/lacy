#ifndef PAGE_H
#define PAGE_H

#include <stdio.h>
#include <string.h>

#include "util.h"

/* parser states */
enum { NORM, REF, HEADER };

struct page_attr {
    struct ut_str name;
    struct ut_str value;
    struct page_attr *next;
};

struct page {
    struct page *inherits;
    char *file_path;
    char *code;

    struct page_attr *attr_top;

    struct page *next;
    struct page *prev;
};

/* page functions */
void page_add(struct page *np);
struct page * page_find(char *file_path);
struct page * page_slurp(char *file_path);
struct page_attr * page_attr_lookup(struct page *e, char *s);


void page_list_init();
void page_list_free();
void page_free();

#endif
