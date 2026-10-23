#pragma once
#include <stdbool.h>
#include "raylib.h"

enum { DM_WINDOWED = 0, DM_BORDERLESS = 1, DM_FULLSCREEN = 2 };
enum { CH_CROSS = 0, CH_CROSS_DOT, CH_DOT, CH_CIRCLE, CH_T, CH_COUNT };
enum { TS_LIT = 0, TS_FLAT, TS_OUTLINE, TS_COUNT };
enum { TH_SLATE = 0, TH_MIDNIGHT, TH_SAND, TH_VOID, TH_COUNT };

typedef struct {
    const char *name;
    double yaw;          // degrees per count at sens 1.0 (0 => cm/360 mode)
} SensGame;

extern const SensGame SENS_GAMES[];
extern const int SENS_GAME_COUNT;

typedef struct {
    // display
    int   display_mode;
    int   res_w, res_h, refresh;
    int   fps_cap;          // 0 = unlimited
    bool  low_latency;      // glFinish after swap (prevents driver frame queue)
    bool  msaa;             // needs restart
    bool  show_fps;

    // mouse
    int   sens_game;        // index into SENS_GAMES
    float sens;             // game sensitivity, or cm/360 when game is "cm/360"
    int   dpi;
    float vert_mult;        // vertical multiplier
    float fov;              // horizontal FOV (deg)

    // crosshair
    int   ch_style;
    Color ch_color;
    float ch_len, ch_thick, ch_gap;
    bool  ch_outline;
    float ch_out_thick, ch_out_alpha;
    float ch_dot;           // dot size (px)
    float ch_circle;        // circle radius (px)

    // targets
    Color tg_color;
    float tg_size_mult;
    int   tg_style;
    bool  tg_hit_fx;
    Color tg_track_color;   // colour while being tracked/damaged

    // world
    int   theme;
    bool  grid;
    bool  show_zone;

    // sound
    bool  snd_hit;
    bool  snd_miss;
    float volume;

    // gameplay
    bool  countdown;
    bool  show_hud;

    // online leaderboard
    bool  online;
    char  nick[48];          // UTF-8
    char  server[200];       // https://aimforge-leaderboard.<you>.workers.dev
    char  pid[40];           // random player id (hex), generated on first launch
    char  secret[72];        // random secret proving ownership of pid (hex)
} Settings;

extern Settings S;

void   settings_defaults(Settings *s);
bool   settings_load(const char *path);   // false if no file yet (first launch)
void   settings_save(const char *path);
double settings_deg_per_count(const Settings *s);     // yaw degrees per mouse count
double settings_cm360(const Settings *s);
void   settings_set_game_keep_cm(Settings *s, int new_game);
