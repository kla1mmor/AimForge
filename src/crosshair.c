#include <math.h>
#include "raylib.h"
#include "settings.h"
#include "crosshair.h"

static float g_ox, g_oy, g_z;

static void px_rect(int x, int y, int w, int h, Color c)
{
    if (w <= 0 || h <= 0) return;
    DrawRectangle((int)(g_ox + x * g_z), (int)(g_oy + y * g_z), (int)(w * g_z), (int)(h * g_z), c);
}

typedef struct { int x, y, w, h; } IR;

// Pixel-exact crosshair. (cx,cy) = centre pixel in "crosshair pixel" space; origin/zoom
// map those pixels to the screen (zoom 1 in game, >1 for the magnified preview).
void crosshair_draw_at(float ox, float oy, float zoom, int cx, int cy)
{
    g_ox = ox; g_oy = oy; g_z = zoom;
    int t = (int)roundf(S.ch_thick); if (t < 1) t = 1;
    int L = (int)roundf(S.ch_len);
    int G = (int)roundf(S.ch_gap);
    int o = (int)roundf(S.ch_out_thick);
    int d = (int)roundf(S.ch_dot);
    Color col = S.ch_color;
    Color oc = (Color){ 0, 0, 0, (unsigned char)(255 * S.ch_out_alpha * (col.a / 255.0f)) };
    int ht = (t - 1) / 2;

    if (S.ch_style == CH_CIRCLE) {
        Vector2 c = { ox + (cx + 0.5f) * zoom, oy + (cy + 0.5f) * zoom };
        float r = S.ch_circle * zoom, hw = t * 0.5f * zoom;
        if (S.ch_outline && o > 0) DrawRing(c, r - hw - o * zoom, r + hw + o * zoom, 0, 360, 72, oc);
        DrawRing(c, r - hw, r + hw, 0, 360, 72, col);
        if (d > 0) {
            int hd = (d - 1) / 2;
            if (S.ch_outline && o > 0) px_rect(cx - hd - o, cy - hd - o, d + 2 * o, d + 2 * o, oc);
            px_rect(cx - hd, cy - hd, d, d, col);
        }
        return;
    }

    IR r[5]; int n = 0;
    if (S.ch_style != CH_DOT && L > 0) {
        r[n++] = (IR){ cx + 1 + G, cy - ht, L, t };       // right
        r[n++] = (IR){ cx - G - L, cy - ht, L, t };       // left
        r[n++] = (IR){ cx - ht, cy + 1 + G, t, L };       // bottom
        if (S.ch_style != CH_T) r[n++] = (IR){ cx - ht, cy - G - L, t, L };   // top
    }
    bool dot = S.ch_style == CH_DOT || S.ch_style == CH_CROSS_DOT;
    if (dot) {
        int dd = d < 1 ? 1 : d;
        int hd = (dd - 1) / 2;
        r[n++] = (IR){ cx - hd, cy - hd, dd, dd };
    }
    if (S.ch_outline && o > 0)
        for (int i = 0; i < n; i++) px_rect(r[i].x - o, r[i].y - o, r[i].w + 2 * o, r[i].h + 2 * o, oc);
    for (int i = 0; i < n; i++) px_rect(r[i].x, r[i].y, r[i].w, r[i].h, col);
}

void crosshair_draw(int screen_w, int screen_h)
{
    crosshair_draw_at(0, 0, 1, screen_w / 2, screen_h / 2);
}
