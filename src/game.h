#pragma once
#include <stdbool.h>
#include "raylib.h"
#include "scenario.h"
#include "platform.h"

enum { GP_COUNTDOWN = 0, GP_RUNNING, GP_DONE };

typedef struct {
    bool   alive, center;
    float  yaw, pitch, dist;      // angular position (deg) and range
    float  vyaw, vpitch, vdist;   // deg/s, units/s
    double spawn_t, next_dir_t, respawn_at;
    float  hp;
    float  flash;                 // hit flash timer
    float  tracked;               // >0 while being damaged (tracking)
} Target;

typedef struct { bool on; Vector3 p; float r; float t; } Fx;

typedef struct {
    Scenario sc;
    int      sc_index;
    int      phase;
    double   phase_t;      // time spent in current phase
    double   t;            // running time
    float    countdown;    // seconds of countdown
    float    yaw, pitch;   // camera, degrees

    Target   tg[MAX_TARGETS];
    int      shots, hits, kills, expired;
    double   last_kill_t, ttk_sum; int ttk_n;
    double   points;
    double   track_fire, track_on;
    bool     lmb;
    bool     center_next;
    Fx       fx[48];
    unsigned rng;

    // per-frame events (for sounds / UI); reset by caller
    int ev_hit, ev_kill, ev_miss;
} Game;

void    game_start(Game *g, int sc_index, float countdown, unsigned seed);
// One frame: advance time, then apply mouse events in order (rotation + shots), then tracking damage.
void    game_frame(Game *g, double dt, const RawMouseEvt *ev, int n, double deg_per_count, float vmult, float size_mult);
float   game_score(const Game *g);
float   game_accuracy(const Game *g);   // 0..1
float   game_time_left(const Game *g);
Vector3 game_dir(float yaw, float pitch);
Vector3 game_target_pos(const Target *t);
float   game_target_radius(const Game *g, const Target *t, float size_mult);
void    game_result(const Game *g, RunResult *r);
float   ang_radius_deg(float r, float d);
