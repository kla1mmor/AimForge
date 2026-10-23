#pragma once
#include "raylib.h"

enum { F_REG = 0, F_SEMI, F_BOLD, F_MONO };

Font font_get(int face, int px);   // cached; loads on first use
void font_clear(void);
