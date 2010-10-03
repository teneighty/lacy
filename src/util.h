#ifndef UTIL_H
#define UTIL_H

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdarg.h>

struct ut_str {
    char *s;
    long def_size;
    long size;
    long len;
};

extern int verbosity;
extern bool quiet_flag;

void str_init(struct ut_str *u);
void str_append(struct ut_str *u, char c);
void str_append_str(struct ut_str *u, char *s);
void str_append_long(struct ut_str *u, long l);
void str_trim(struct ut_str *u);
int str_is_empty(struct ut_str *u);
void str_clear(struct ut_str *u);
void str_free(struct ut_str *u);

int build_depth(char *file_path);
bool file_exists(char *s);
int copy_dir(char *src, char *dest);
int copy_file(char *src, char *dest);

bool flook_ahead(FILE *f, char *s, int n);
bool slook_ahead(char *f, char *s, int n);
int iswhitespace(char c);
int isnewline(char c);

void warn(const char *s, ...);
void fatal(const char *s, ...);

#endif
