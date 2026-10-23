#pragma once
#include <stdbool.h>
#include "raylib.h"

typedef struct {
    float   s;                       // UI scale (screen height / 1080)
    Vector2 m;
    bool    down, pressed, released, rpressed;
    float   wheel;
    int     id;                      // per-frame widget counter
    int     active;                  // widget being dragged
    int     edit;                    // widget in text edit mode
    char    ebuf[32]; int elen;
    char    tbuf[256];
    bool    edit_started;
    Rectangle clip; bool clip_on;
    bool    want_hand;
    bool    blocked;                 // input consumed by a modal
} UIState;
extern UIState ui;
extern bool ui_inj_press, ui_inj_release;   // set before ui_begin from raw mouse events

typedef struct { float y, target, content; } Scroll;

extern Color C_BG, C_SIDE, C_CARD, C_CARD_HI, C_LINE, C_TXT, C_DIM, C_FAINT, C_ACC, C_ACC_D, C_GOLD, C_GOOD, C_BAD;

void  ui_begin(float scale);
void  ui_end(void);
float sc(float v);
void  ui_clip(Rectangle r);
void  ui_unclip(void);
bool  ui_hover(Rectangle r);
bool  ui_editing(void);
void  ui_scroll(Scroll *s, Rectangle view);
void  ui_scrollbar(Scroll *s, Rectangle view);

void  txt(const char *s, float x, float y, float size, Color c, int face);
void  txt_c(const char *s, Rectangle r, float size, Color c, int face);
void  txt_r(const char *s, float right, float y, float size, Color c, int face);
float txt_w(const char *s, float size, int face);
float txt_wrap(const char *s, Rectangle r, float size, Color c, int face, int max_lines, bool draw);

void  rrect(Rectangle r, float radius, Color c);
void  rrect_line(Rectangle r, float radius, float thick, Color c);
Color fade(Color c, float a);
Color mixc(Color a, Color b, float k);

enum { BTN_PRIMARY = 0, BTN_SECONDARY, BTN_GHOST, BTN_DANGER };
bool  ui_button(Rectangle r, const char *label, int kind);
bool  ui_nav(Rectangle r, const char *label, bool selected);
bool  ui_tab(Rectangle r, const char *label, bool selected);
bool  ui_slider_f(Rectangle r, const char *label, float *v, float mn, float mx, float step, const char *fmt);
bool  ui_slider_i(Rectangle r, const char *label, int *v, int mn, int mx, int step);
bool  ui_toggle(Rectangle r, const char *label, bool *v);
bool  ui_segment(Rectangle r, const char *label, const char *const *opts, int n, int *v);
bool  ui_color(Rectangle r, const char *label, Color *c);   // needs 2 rows height
bool  ui_number(Rectangle r, float *v, float mn, float mx, const char *fmt);
// Single-line UTF-8 text field. ascii_only: URLs. Returns true when the text was committed and changed.
bool  ui_text(Rectangle r, const char *label, char *buf, int cap, int max_chars, const char *placeholder, bool ascii_only);
void  ui_hint(float x, float y, float w, const char *s);
