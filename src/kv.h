#pragma once
#include <stdio.h>
#include <string.h>
#include "platform.h"

// Tiny "key=value" line parser shared by settings and scenario overrides.
typedef void (*KvFn)(const char *key, const char *val, void *user);

static inline void kv_trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static inline int kv_parse_file(const char *path, KvFn fn, void *user)
{
    FILE *f = plat_fopen(path, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        kv_trim(line);
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        fn(line, eq + 1, user);
    }
    fclose(f);
    return 1;
}
