#pragma once
#include "raylib.h"
#include "game.h"

typedef struct { Color floor, wall, ceil, line, clear; } Theme;
extern const Theme THEMES[];
extern const char *const THEME_NAMES[];

void     render_init(void);
void     render_unload(void);
Camera3D render_camera(float yaw, float pitch, float hfov_deg, float aspect);
void     render_world(const Game *g, Camera3D cam);
void     render_target_preview(Rectangle r);    // small 3D preview of target style in UI
Color    render_clear_color(void);
