#pragma once
#include <stdbool.h>

typedef struct { int w, h; int rates[24]; int nr; } ResMode;

int  display_modes(ResMode *out, int max);    // modes of current monitor, biggest first
void display_apply(void);                      // apply S.display_mode/res/refresh
void display_focus(bool focused);              // every frame: Alt+Tab handling for exclusive fullscreen
bool display_suspended(void);
void display_desktop(int *w, int *h, int *hz);
int  display_current_refresh(void);
const char *display_info(void);
void *display_glfw_proc(const char *name);
