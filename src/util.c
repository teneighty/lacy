
#include <string.h>

#include "util.h"

static void str_resize(struct ut_str *u, long ns);

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

