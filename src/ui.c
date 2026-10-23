#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "ui.h"
#include "fonts.h"

UIState ui;
bool ui_inj_press = false, ui_inj_release = false;

Color C_BG      = { 11, 13, 18, 255 };
Color C_SIDE    = { 15, 18, 25, 255 };
Color C_CARD    = { 22, 26, 35, 255 };
Color C_CARD_HI = { 29, 35, 47, 255 };
Color C_LINE    = { 40, 47, 61, 255 };
Color C_TXT     = { 232, 236, 243, 255 };
Color C_DIM     = { 140, 150, 168, 255 };
Color C_FAINT   = { 88, 97, 114, 255 };
Color C_ACC     = { 54, 220, 255, 255 };
Color C_ACC_D   = { 20, 92, 112, 255 };
Color C_GOLD    = { 255, 190, 70, 255 };
Color C_GOOD    = { 80, 230, 140, 255 };
Color C_BAD     = { 255, 90, 100, 255 };

static int g_last_cursor = -1;

float sc(float v) { return v * ui.s; }

Color fade(Color c, float a) { c.a = (unsigned char)(c.a * (a < 0 ? 0 : a > 1 ? 1 : a)); return c; }
Color mixc(Color a, Color b, float k)
{
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k),
                    (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k) };
}

void ui_begin(float scale)
{
    ui.s = scale;
    ui.m = GetMousePosition();
    ui.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    ui.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || ui_inj_press;
    ui.released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || ui_inj_release;
    if (ui_inj_press) ui.down = true;
    if (ui_inj_release && !ui_inj_press) ui.down = false;
    ui_inj_press = ui_inj_release = false;
    ui.rpressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    ui.wheel = GetMouseWheelMove();
    ui.id = 0;
    ui.want_hand = false;
    ui.clip_on = false;
    ui.edit_started = false;
    if (!ui.down && !ui.released) ui.active = 0;   // keep through the release frame so clicks register
}

void ui_end(void)
{
    // Click outside an edited field commits it on the next widget pass; if no widget
    // claimed the edit this frame (page changed), drop it.
    if (ui.edit > ui.id) ui.edit = 0;
    int cur = ui.want_hand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT;
    if (cur != g_last_cursor) { SetMouseCursor(cur); g_last_cursor = cur; }
}

void ui_clip(Rectangle r) { ui.clip = r; ui.clip_on = true; BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height); }
void ui_unclip(void) { if (ui.clip_on) EndScissorMode(); ui.clip_on = false; }

bool ui_hover(Rectangle r)
{
    if (ui.blocked) return false;
    if (!CheckCollisionPointRec(ui.m, r)) return false;
    if (ui.clip_on && !CheckCollisionPointRec(ui.m, ui.clip)) return false;
    return true;
}

bool ui_editing(void) { return ui.edit != 0; }

void ui_scroll(Scroll *s, Rectangle view)
{
    if (CheckCollisionPointRec(ui.m, view) && ui.wheel != 0 && !ui.blocked) s->target -= ui.wheel * sc(90);
    float maxs = s->content - view.height; if (maxs < 0) maxs = 0;
    if (s->target < 0) s->target = 0;
    if (s->target > maxs) s->target = maxs;
    s->y += (s->target - s->y) * 0.35f;
    if (fabsf(s->target - s->y) < 0.5f) s->y = s->target;
}

void ui_scrollbar(Scroll *s, Rectangle view)
{
    if (s->content <= view.height + 1) return;
    float h = view.height * view.height / s->content;
    float maxs = s->content - view.height;
    float y = view.y + (view.height - h) * (s->y / maxs);
    rrect((Rectangle){ view.x + view.width - sc(5), y, sc(3), h }, sc(2), fade(C_DIM, 0.5f));
}

// ------------------------------------------------------------------- text
void txt(const char *s, float x, float y, float size, Color c, int face)
{
    int px = (int)roundf(size);
    Font f = font_get(face, px);
    DrawTextEx(f, s, (Vector2){ roundf(x), roundf(y) }, (float)px, 0, c);
}

float txt_w(const char *s, float size, int face)
{
    int px = (int)roundf(size);
    return MeasureTextEx(font_get(face, px), s, (float)px, 0).x;
}

void txt_c(const char *s, Rectangle r, float size, Color c, int face)
{
    float w = txt_w(s, size, face);
    txt(s, r.x + (r.width - w) * 0.5f, r.y + (r.height - size) * 0.5f - size * 0.04f, size, c, face);
}

void txt_r(const char *s, float right, float y, float size, Color c, int face)
{
    txt(s, right - txt_w(s, size, face), y, size, c, face);
}

float txt_wrap(const char *s, Rectangle r, float size, Color c, int face, int max_lines, bool draw)
{
    char line[800] = ""; char word[256];
    float y = r.y, lh = size * 1.32f;
    int lines = 0;
    const char *p = s;
    while (*p) {
        int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < 255) word[wl++] = *p++;
        word[wl] = 0;
        if (!wl) break;
        char test[768];
        snprintf(test, sizeof(test), "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && txt_w(test, size, face) > r.width) {
            if (max_lines > 0 && lines == max_lines - 1) {
                // ellipsis on the last allowed line
                char el[820]; snprintf(el, sizeof(el), "%s…", line);
                if (draw) txt(el, r.x, y, size, c, face);
                return (lines + 1) * lh;
            }
            if (draw) txt(line, r.x, y, size, c, face);
            y += lh; lines++;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", test);
        }
    }
    if (line[0]) { if (draw) txt(line, r.x, y, size, c, face); lines++; }
    return lines * lh;
}

// ------------------------------------------------------------------- shapes
void rrect(Rectangle r, float radius, Color c)
{
    float m = r.width < r.height ? r.width : r.height;
    if (m <= 0) return;
    float rn = 2.0f * radius / m; if (rn > 1) rn = 1;
    DrawRectangleRounded(r, rn, 8, c);
}

void rrect_line(Rectangle r, float radius, float thick, Color c)
{
    float m = r.width < r.height ? r.width : r.height;
    if (m <= 0) return;
    float rn = 2.0f * radius / m; if (rn > 1) rn = 1;
    DrawRectangleRoundedLinesEx(r, rn, 8, thick, c);
}

// ------------------------------------------------------------------- widgets
static int next_id(void) { return ++ui.id; }

bool ui_button(Rectangle r, const char *label, int kind)
{
    int id = next_id();
    bool hov = ui_hover(r);
    if (hov) ui.want_hand = true;
    if (hov && ui.pressed) ui.active = id;
    bool click = hov && ui.released && ui.active == id;
    if (hov && ui.pressed) click = false;
    Color bg, fg = C_TXT, ln = BLANK;
    switch (kind) {
    case BTN_PRIMARY:   bg = hov ? (Color){ 110, 232, 255, 255 } : C_ACC; fg = (Color){ 6, 20, 28, 255 }; break;
    case BTN_DANGER:    bg = hov ? (Color){ 120, 40, 48, 255 } : (Color){ 82, 30, 36, 255 }; fg = (Color){ 255, 190, 195, 255 }; break;
    case BTN_GHOST:     bg = hov ? fade(C_CARD_HI, 1) : BLANK; fg = hov ? C_TXT : C_DIM; break;
    default:            bg = hov ? C_CARD_HI : C_CARD; ln = C_LINE; break;
    }
    if (ui.active == id && ui.down && hov) { r.y += 1; }
    rrect(r, sc(8), bg);
    if (ln.a) rrect_line(r, sc(8), 1, ln);
    txt_c(label, r, sc(17), fg, F_SEMI);
    return click;
}

bool ui_nav(Rectangle r, const char *label, bool selected)
{
    int id = next_id();
    bool hov = ui_hover(r);
    if (hov) ui.want_hand = true;
    if (hov && ui.pressed) ui.active = id;
    bool click = hov && ui.released && ui.active == id;
    if (selected) {
        rrect(r, sc(8), C_CARD_HI);
        DrawRectangleRec((Rectangle){ r.x, r.y + sc(10), sc(3), r.height - sc(20) }, C_ACC);
    } else if (hov) rrect(r, sc(8), C_CARD);
    txt(label, r.x + sc(18), r.y + (r.height - sc(18)) * 0.5f, sc(18), selected ? C_TXT : (hov ? C_TXT : C_DIM), F_SEMI);
    return click;
}

bool ui_tab(Rectangle r, const char *label, bool selected)
{
    int id = next_id();
    bool hov = ui_hover(r);
    if (hov) ui.want_hand = true;
    if (hov && ui.pressed) ui.active = id;
    bool click = hov && ui.released && ui.active == id;
    txt_c(label, r, sc(17), selected ? C_TXT : (hov ? C_TXT : C_DIM), F_SEMI);
    if (selected) DrawRectangleRec((Rectangle){ r.x + sc(10), r.y + r.height - sc(3), r.width - sc(20), sc(3) }, C_ACC);
    return click;
}

static float snapf(float v, float mn, float mx, float step)
{
    if (step > 0) v = mn + roundf((v - mn) / step) * step;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    return v;
}

// Editable numeric box. Returns true when value changed.
bool ui_number(Rectangle r, float *v, float mn, float mx, const char *fmt)
{
    int id = next_id();
    bool hov = ui_hover(r);
    bool changed = false;
    if (hov) ui.want_hand = true;

    if (ui.edit == id) {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (ui.elen < 30 && ((ch >= '0' && ch <= '9') || ch == '.' || ch == ',' || ch == '-')) {
                ui.ebuf[ui.elen++] = (char)(ch == ',' ? '.' : ch); ui.ebuf[ui.elen] = 0;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) { if (ui.elen) ui.ebuf[--ui.elen] = 0; }
        bool commit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB) ||
                      (ui.pressed && !hov);
        if (IsKeyPressed(KEY_ESCAPE)) { ui.edit = 0; }
        else if (commit) {
            if (ui.elen) {
                float nv = (float)atof(ui.ebuf);
                if (nv < mn) nv = mn;
                if (nv > mx) nv = mx;
                if (nv != *v) { *v = nv; changed = true; }
            }
            ui.edit = 0;
        }
    } else if (hov && ui.pressed) {
        ui.edit = id; ui.elen = 0; ui.ebuf[0] = 0; ui.edit_started = true;
        while (GetCharPressed()) {}
    }

    bool editing = ui.edit == id;
    rrect(r, sc(6), editing ? (Color){ 14, 30, 38, 255 } : (hov ? C_CARD_HI : (Color){ 16, 19, 26, 255 }));
    rrect_line(r, sc(6), 1, editing ? C_ACC : C_LINE);
    char buf[48];
    if (editing) {
        bool blink = fmodf((float)GetTime(), 1.0f) < 0.55f;
        snprintf(buf, sizeof(buf), "%s%s", ui.ebuf, blink ? "|" : " ");
    } else snprintf(buf, sizeof(buf), fmt, *v);
    txt_c(buf, r, sc(16), editing ? C_TXT : C_TXT, F_MONO);
    return changed;
}

static Rectangle row_label(Rectangle r, const char *label, float frac)
{
    if (!label) return r;
    float lw = r.width * frac;
    txt(label, r.x, r.y + (r.height - sc(17)) * 0.5f, sc(17), C_TXT, F_REG);
    return (Rectangle){ r.x + lw, r.y, r.width - lw, r.height };
}

bool ui_slider_f(Rectangle r, const char *label, float *v, float mn, float mx, float step, const char *fmt)
{
    Rectangle a = row_label(r, label, 0.40f);
    float bw = sc(104);
    Rectangle track = { a.x, a.y + a.height * 0.5f - sc(3), a.width - bw - sc(14), sc(6) };
    Rectangle hit = { track.x - sc(6), a.y, track.width + sc(12), a.height };
    Rectangle box = { a.x + a.width - bw, a.y + (a.height - sc(34)) * 0.5f, bw, sc(34) };
    int id = next_id();
    bool hov = ui_hover(hit);
    bool changed = false;
    if (hov) ui.want_hand = true;
    if (hov && ui.pressed) ui.active = id;
    if (ui.active == id && ui.down) {
        float k = (ui.m.x - track.x) / track.width;
        if (k < 0) k = 0;
        if (k > 1) k = 1;
        float nv = snapf(mn + (mx - mn) * k, mn, mx, step);
        if (nv != *v) { *v = nv; changed = true; }
    }
    float k = (mx > mn) ? (*v - mn) / (mx - mn) : 0;
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    rrect(track, sc(3), (Color){ 34, 40, 53, 255 });
    rrect((Rectangle){ track.x, track.y, track.width * k, track.height }, sc(3), C_ACC);
    float kx = track.x + track.width * k;
    DrawCircle((int)kx, (int)(track.y + track.height * 0.5f), sc(ui.active == id || hov ? 9 : 8), C_TXT);
    DrawCircle((int)kx, (int)(track.y + track.height * 0.5f), sc(4), C_ACC);
    if (ui_number(box, v, mn, mx, fmt)) changed = true;
    return changed;
}

bool ui_slider_i(Rectangle r, const char *label, int *v, int mn, int mx, int step)
{
    float f = (float)*v;
    bool ch = ui_slider_f(r, label, &f, (float)mn, (float)mx, (float)step, "%.0f");
    if (ch) *v = (int)roundf(f);
    return ch;
}

bool ui_toggle(Rectangle r, const char *label, bool *v)
{
    int id = next_id();
    bool hov = ui_hover(r);
    if (hov) ui.want_hand = true;
    if (hov && ui.pressed) ui.active = id;
    bool click = hov && ui.released && ui.active == id;
    if (click) *v = !*v;
    if (label) txt(label, r.x, r.y + (r.height - sc(17)) * 0.5f, sc(17), C_TXT, F_REG);
    Rectangle sw = { r.x + r.width - sc(52), r.y + (r.height - sc(28)) * 0.5f, sc(52), sc(28) };
    rrect(sw, sc(14), *v ? C_ACC : (Color){ 44, 51, 66, 255 });
    float kx = *v ? sw.x + sw.width - sc(14) : sw.x + sc(14);
    DrawCircle((int)kx, (int)(sw.y + sw.height * 0.5f), sc(10), *v ? (Color){ 6, 20, 28, 255 } : C_DIM);
    return click;
}

bool ui_segment(Rectangle r, const char *label, const char *const *opts, int n, int *v)
{
    Rectangle a = row_label(r, label, 0.40f);
    float h = sc(36);
    Rectangle box = { a.x, a.y + (a.height - h) * 0.5f, a.width, h };
    rrect(box, sc(8), (Color){ 16, 19, 26, 255 });
    rrect_line(box, sc(8), 1, C_LINE);
    float w = box.width / n;
    bool changed = false;
    for (int i = 0; i < n; i++) {
        Rectangle c = { box.x + w * i + sc(3), box.y + sc(3), w - sc(6), box.height - sc(6) };
        int id = next_id();
        bool hov = ui_hover(c);
        if (hov) ui.want_hand = true;
        if (hov && ui.pressed) ui.active = id;
        if (hov && ui.released && ui.active == id && *v != i) { *v = i; changed = true; }
        if (*v == i) rrect(c, sc(6), C_ACC_D);
        else if (hov) rrect(c, sc(6), C_CARD_HI);
        float fs = sc(15);
        while (txt_w(opts[i], fs, F_SEMI) > c.width - sc(6) && fs > sc(11)) fs -= 1;
        txt_c(opts[i], c, fs, *v == i ? C_TXT : C_DIM, F_SEMI);
    }
    return changed;
}

static const Color SWATCHES[] = {
    { 0, 255, 140, 255 }, { 0, 230, 255, 255 }, { 255, 240, 0, 255 }, { 255, 70, 90, 255 }, { 255, 80, 200, 255 },
    { 255, 255, 255, 255 }, { 170, 100, 255, 255 }, { 255, 150, 30, 255 }, { 180, 255, 0, 255 }, { 50, 130, 255, 255 },
};

bool ui_color(Rectangle r, const char *label, Color *c)
{
    float rh = r.height * 0.5f;
    Rectangle top = { r.x, r.y, r.width, rh };
    Rectangle a = row_label(top, label, 0.40f);
    bool changed = false;
    int ns = sizeof(SWATCHES) / sizeof(SWATCHES[0]);
    float sz = sc(26), gap = (a.width - sz * ns) / (ns - 1);
    if (gap > sc(10)) gap = sc(10);
    for (int i = 0; i < ns; i++) {
        Rectangle s = { a.x + i * (sz + gap), a.y + (rh - sz) * 0.5f, sz, sz };
        int id = next_id();
        bool hov = ui_hover(s);
        if (hov) ui.want_hand = true;
        if (hov && ui.pressed) ui.active = id;
        if (hov && ui.released && ui.active == id) { Color k = SWATCHES[i]; k.a = c->a; *c = k; changed = true; }
        bool sel = SWATCHES[i].r == c->r && SWATCHES[i].g == c->g && SWATCHES[i].b == c->b;
        rrect(s, sc(6), SWATCHES[i]);
        if (sel) rrect_line((Rectangle){ s.x - sc(3), s.y - sc(3), s.width + sc(6), s.height + sc(6) }, sc(8), sc(2), C_TXT);
        else if (hov) rrect_line(s, sc(6), 1, C_TXT);
    }
    // RGB row
    Rectangle bot = { r.x + r.width * 0.40f, r.y + rh, r.width * 0.60f, rh };
    float cw = (bot.width - sc(46)) / 3.0f;
    const char *nm[3] = { "R", "G", "B" };
    unsigned char *ch[3] = { &c->r, &c->g, &c->b };
    for (int i = 0; i < 3; i++) {
        Rectangle cell = { bot.x + i * cw, bot.y, cw - sc(8), rh };
        txt(nm[i], cell.x, cell.y + (rh - sc(15)) * 0.5f, sc(15), C_DIM, F_SEMI);
        float f = *ch[i];
        Rectangle nb = { cell.x + sc(18), cell.y + (rh - sc(32)) * 0.5f, cell.width - sc(18), sc(32) };
        if (ui_number(nb, &f, 0, 255, "%.0f")) { *ch[i] = (unsigned char)f; changed = true; }
    }
    Rectangle chip = { bot.x + bot.width - sc(38), bot.y + (rh - sc(32)) * 0.5f, sc(38), sc(32) };
    rrect(chip, sc(6), (Color){ c->r, c->g, c->b, 255 });
    rrect_line(chip, sc(6), 1, C_LINE);
    return changed;
}

static int utf8_count(const char *s)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) if ((*p & 0xC0) != 0x80) n++;
    return n;
}

static bool text_char_ok(int cp, bool ascii_only)
{
    if (cp >= 32 && cp < 127) return true;
    if (ascii_only) return false;
    return (cp >= 0x410 && cp <= 0x44F) || cp == 0x401 || cp == 0x451;   // glyphs our font has
}

static void text_append(int cp, int cap, int max_chars, bool ascii_only)
{
    if (!text_char_ok(cp, ascii_only) || utf8_count(ui.tbuf) >= max_chars) return;
    int sz = 0;
    const char *u = CodepointToUTF8(cp, &sz);
    int len = (int)strlen(ui.tbuf);
    if (len + sz >= cap || len + sz >= (int)sizeof(ui.tbuf)) return;
    memcpy(ui.tbuf + len, u, (size_t)sz);
    ui.tbuf[len + sz] = 0;
}

bool ui_text(Rectangle r, const char *label, char *buf, int cap, int max_chars, const char *placeholder, bool ascii_only)
{
    Rectangle a = r;
    if (label) {
        txt(label, r.x, r.y + (r.height - sc(17)) * 0.5f, sc(17), C_TXT, F_REG);
        a = (Rectangle){ r.x + r.width * 0.40f, r.y, r.width * 0.60f, r.height };
    }
    Rectangle box = { a.x, a.y + (a.height - sc(38)) * 0.5f, a.width, sc(38) };
    int id = next_id();
    bool hov = ui_hover(box);
    bool changed = false;
    if (hov) ui.want_hand = true;

    if (ui.edit == id) {
        int ch;
        while ((ch = GetCharPressed()) != 0) text_append(ch, cap, max_chars, ascii_only);
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (ctrl && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) {
                int off = 0, cp, sz;
                while (clip[off] && (cp = GetCodepointNext(clip + off, &sz)) != 0) {
                    if (cp != '\n' && cp != '\r' && cp != '\t') text_append(cp, cap, max_chars, ascii_only);
                    off += sz;
                }
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            int len = (int)strlen(ui.tbuf);
            if (ctrl) len = 0;
            else { while (len > 0 && (((unsigned char)ui.tbuf[len - 1]) & 0xC0) == 0x80) len--; if (len > 0) len--; }
            ui.tbuf[len] = 0;
        }
        bool commit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB) || (ui.pressed && !hov);
        if (IsKeyPressed(KEY_ESCAPE)) ui.edit = 0;
        else if (commit) {
            // trim
            char *s = ui.tbuf; while (*s == ' ') s++;
            int len = (int)strlen(s); while (len > 0 && s[len - 1] == ' ') s[--len] = 0;
            if (strcmp(s, buf) != 0) { snprintf(buf, (size_t)cap, "%s", s); changed = true; }
            ui.edit = 0;
        }
    } else if (hov && ui.pressed) {
        ui.edit = id; ui.edit_started = true;
        snprintf(ui.tbuf, sizeof(ui.tbuf), "%s", buf);
        while (GetCharPressed()) {}
    }

    bool editing = ui.edit == id;
    rrect(box, sc(6), editing ? (Color){ 14, 30, 38, 255 } : (hov ? C_CARD_HI : (Color){ 16, 19, 26, 255 }));
    rrect_line(box, sc(6), 1, editing ? C_ACC : C_LINE);
    const char *shown = editing ? ui.tbuf : buf;
    BeginScissorMode((int)box.x + 2, (int)box.y, (int)box.width - 4, (int)box.height);
    float tx = box.x + sc(12), ty = box.y + (box.height - sc(16)) * 0.5f;
    float tw = txt_w(shown, sc(16), F_SEMI);
    if (tw > box.width - sc(28)) tx -= tw - (box.width - sc(28));      // keep the caret visible
    if (shown[0]) txt(shown, tx, ty, sc(16), C_TXT, F_SEMI);
    else if (!editing && placeholder) txt(placeholder, tx, ty, sc(16), C_FAINT, F_REG);
    if (editing && fmodf((float)GetTime(), 1.0f) < 0.55f)
        DrawRectangle((int)(tx + tw + sc(2)), (int)(ty - sc(1)), (int)fmaxf(1, sc(2)), (int)sc(19), C_ACC);
    EndScissorMode();
    if (ui.clip_on) BeginScissorMode((int)ui.clip.x, (int)ui.clip.y, (int)ui.clip.width, (int)ui.clip.height);
    return changed;
}

void ui_hint(float x, float y, float w, const char *s)
{
    txt_wrap(s, (Rectangle){ x, y, w, 1000 }, sc(14), C_FAINT, F_REG, 3, true);
}
