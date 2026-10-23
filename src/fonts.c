#include <stdlib.h>
#include "raylib.h"
#include "fonts.h"

extern const unsigned char font_medium[], font_semibold[], font_bold[], font_mono[];
extern const int font_medium_len, font_semibold_len, font_bold_len, font_mono_len;

typedef struct { int face, px; Font f; } FontEntry;
#define MAX_FONTS 40
static FontEntry g_cache[MAX_FONTS];
static int g_n = 0;
static int *g_cps = NULL;
static int g_ncps = 0;

static void build_codepoints(void)
{
    if (g_cps) return;
    g_cps = (int *)malloc(sizeof(int) * 512);
    int n = 0;
    for (int c = 32; c < 127; c++) g_cps[n++] = c;
    for (int c = 0x410; c <= 0x44F; c++) g_cps[n++] = c;    // А..я
    int extra[] = { 0x401, 0x451, 0xB0, 0xD7, 0x2014, 0x2013, 0xAB, 0xBB, 0x2026, 0x2022, 0xB1, 0x2116, 0x2192, 0x2190, 0x2191, 0x2193, 0xB7, 0x2248, 0x2264, 0x2265, 0x2212, 0xA0 };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) g_cps[n++] = extra[i];
    g_ncps = n;
}

Font font_get(int face, int px)
{
    if (px < 6) px = 6;
    if (px > 200) px = 200;
    for (int i = 0; i < g_n; i++)
        if (g_cache[i].face == face && g_cache[i].px == px) return g_cache[i].f;

    build_codepoints();
    const unsigned char *data; int len;
    switch (face) {
    case F_SEMI: data = font_semibold; len = font_semibold_len; break;
    case F_BOLD: data = font_bold; len = font_bold_len; break;
    case F_MONO: data = font_mono; len = font_mono_len; break;
    default:     data = font_medium; len = font_medium_len; break;
    }
    Font f = LoadFontFromMemory(".ttf", data, len, px, g_cps, g_ncps);
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    if (g_n == MAX_FONTS) {   // evict oldest
        UnloadFont(g_cache[0].f);
        for (int i = 1; i < g_n; i++) g_cache[i - 1] = g_cache[i];
        g_n--;
    }
    g_cache[g_n++] = (FontEntry){ face, px, f };
    return f;
}

void font_clear(void)
{
    for (int i = 0; i < g_n; i++) UnloadFont(g_cache[i].f);
    g_n = 0;
}
