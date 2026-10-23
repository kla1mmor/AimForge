#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "game.h"

#define DEG2RADF 0.017453292519943295f
#define RAD2DEGF 57.29577951308232f

static float frand(Game *g)
{
    unsigned x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x ? x : 0x9E3779B9u;
    return (float)(g->rng >> 8) / 16777216.0f;
}
static float urange(Game *g, float a, float b) { return a + (b - a) * frand(g); }

float ang_radius_deg(float r, float d) { return atanf(r / (d > 0.01f ? d : 0.01f)) * RAD2DEGF; }

Vector3 game_dir(float yaw, float pitch)
{
    float cy = cosf(yaw * DEG2RADF), sy = sinf(yaw * DEG2RADF);
    float cp = cosf(pitch * DEG2RADF), sp = sinf(pitch * DEG2RADF);
    return (Vector3){ sy * cp, sp, -cy * cp };
}

Vector3 game_target_pos(const Target *t)
{
    Vector3 d = game_dir(t->yaw, t->pitch);
    return (Vector3){ d.x * t->dist, d.y * t->dist, d.z * t->dist };
}

float game_target_radius(const Game *g, const Target *t, float size_mult)
{
    float r = g->sc.radius * size_mult;
    if (g->sc.shrink && g->sc.lifetime > 0) {
        float k = 1.0f - (float)((g->t - t->spawn_t) / g->sc.lifetime);
        if (k < 0) k = 0;
        r *= 0.25f + 0.75f * k;
    }
    return r;
}

static float ang_between(Vector3 a, Vector3 b)
{
    float d = a.x * b.x + a.y * b.y + a.z * b.z;
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    return acosf(d) * RAD2DEGF;
}

static void pick_velocity(Game *g, Target *t)
{
    const Scenario *s = &g->sc;
    float sp = s->speed + urange(g, -s->speed_var, s->speed_var);
    if (sp < 0) sp = 0;
    switch (s->move) {
    case MV_STRAFE: {
        float sign = t->vyaw == 0 ? (frand(g) < 0.5f ? -1.f : 1.f)
                   : (frand(g) < 0.75f ? (t->vyaw > 0 ? -1.f : 1.f) : (t->vyaw > 0 ? 1.f : -1.f));
        t->vyaw = sign * sp; t->vpitch = 0;
    } break;
    case MV_BOUNCE:
    case MV_DYNAMIC: {
        float a = urange(g, 0, 6.2831853f);
        t->vyaw = cosf(a) * sp;
        t->vpitch = (s->spawn_pitch > 0.5f) ? sinf(a) * sp * 0.6f : 0;
        if (s->move == MV_DYNAMIC && s->dist_var > 0)
            t->vdist = (frand(g) < 0.5f ? -1.f : 1.f) * urange(g, 2.0f, 7.0f);
    } break;
    default: t->vyaw = t->vpitch = t->vdist = 0; break;
    }
    t->next_dir_t = g->t + urange(g, s->dir_min, s->dir_max > s->dir_min ? s->dir_max : s->dir_min);
}

static void spawn(Game *g, int idx)
{
    const Scenario *s = &g->sc;
    Target *t = &g->tg[idx];
    memset(t, 0, sizeof(*t));
    Vector3 cam = game_dir(g->yaw, g->pitch);

    if (s->flick_return && g->center_next) {
        t->yaw = 0; t->pitch = s->pitch_off; t->center = true;
    } else {
        float best_y = 0, best_p = s->pitch_off, best_sep = -1;
        for (int tries = 0; tries < 48; tries++) {
            float y;
            if (s->yaw_min > 0) {
                float hi = s->spawn_yaw > s->yaw_min ? s->spawn_yaw : s->yaw_min;
                y = urange(g, s->yaw_min, hi) * (frand(g) < 0.5f ? -1.f : 1.f);
            } else {
                y = urange(g, -s->spawn_yaw, s->spawn_yaw);
            }
            float p = s->pitch_off + urange(g, -s->spawn_pitch, s->spawn_pitch);
            Vector3 d = game_dir(y, p);
            float sep = ang_between(d, cam);
            for (int j = 0; j < s->count; j++) {
                if (j == idx || !g->tg[j].alive) continue;
                float a = ang_between(d, game_dir(g->tg[j].yaw, g->tg[j].pitch));
                if (a < sep) sep = a;
            }
            if (sep > best_sep) { best_sep = sep; best_y = y; best_p = p; }
            if (sep >= s->min_sep) break;
        }
        t->yaw = best_y; t->pitch = best_p;
    }
    if (s->flick_return) g->center_next = !g->center_next;

    t->dist = s->dist + urange(g, -s->dist_var, s->dist_var);
    if (t->dist < 3) t->dist = 3;
    t->hp = s->shoot == SM_CLICK ? (s->hp < 1 ? 1 : floorf(s->hp + 0.5f)) : s->hp;
    t->spawn_t = g->t;
    t->alive = true;
    if (s->move != MV_NONE && !t->center) pick_velocity(g, t);
}

static void schedule_respawn(Game *g, int idx)
{
    const Scenario *s = &g->sc;
    float d = s->delay_max > s->delay_min ? urange(g, s->delay_min, s->delay_max) : s->delay_min;
    g->tg[idx].alive = false;
    g->tg[idx].respawn_at = g->t + d;
}

static void add_fx(Game *g, Vector3 p, float r)
{
    for (int i = 0; i < 48; i++) if (!g->fx[i].on) { g->fx[i] = (Fx){ true, p, r, 0 }; return; }
}

void game_start(Game *g, int sc_index, float countdown, unsigned seed)
{
    memset(g, 0, sizeof(*g));
    g->sc = SCN[sc_index];
    if (g->sc.count < 1) g->sc.count = 1;
    if (g->sc.count > MAX_TARGETS) g->sc.count = MAX_TARGETS;
    g->sc_index = sc_index;
    g->rng = seed ? seed : (unsigned)time(NULL) | 1u;
    g->countdown = countdown;
    g->phase = countdown > 0 ? GP_COUNTDOWN : GP_RUNNING;
    g->yaw = 0; g->pitch = 0;
    g->last_kill_t = 0;
    for (int i = 0; i < g->sc.count; i++) {
        g->tg[i].alive = false;
        g->tg[i].respawn_at = (g->sc.delay_max > 0) ? urange(g, g->sc.delay_min, g->sc.delay_max) : 0;
    }
}

static void kill_target(Game *g, int i)
{
    Target *t = &g->tg[i];
    double from = t->spawn_t > g->last_kill_t ? t->spawn_t : g->last_kill_t;
    double ttk = g->t - from;
    if (ttk < 0) ttk = 0;
    g->kills++;
    g->ttk_sum += ttk; g->ttk_n++;
    if (g->sc.shoot == SM_CLICK) {
        double bonus = 1.0 - ttk / 1.0; if (bonus < 0) bonus = 0;
        g->points += 100.0 * (1.0 + bonus);
    }
    g->last_kill_t = g->t;
    g->ev_kill++;
    add_fx(g, game_target_pos(t), g->sc.radius);
    schedule_respawn(g, i);
}

// Nearest target hit by the view ray, or -1.
static int raycast(const Game *g, float size_mult)
{
    Vector3 d = game_dir(g->yaw, g->pitch);
    int best = -1; float best_t = 1e9f;
    for (int i = 0; i < g->sc.count; i++) {
        const Target *t = &g->tg[i];
        if (!t->alive) continue;
        Vector3 c = game_target_pos(t);
        float r = game_target_radius(g, t, size_mult);
        float tca = c.x * d.x + c.y * d.y + c.z * d.z;
        if (tca < 0) continue;
        float d2 = (c.x * c.x + c.y * c.y + c.z * c.z) - tca * tca;
        if (d2 > r * r) continue;
        float th = tca - sqrtf(r * r - d2);
        if (th < best_t) { best_t = th; best = i; }
    }
    return best;
}

static void update_targets(Game *g, double dt)
{
    const Scenario *s = &g->sc;
    float ymin = -s->spawn_yaw, ymax = s->spawn_yaw;
    float pmin = s->pitch_off - s->spawn_pitch, pmax = s->pitch_off + s->spawn_pitch;
    float dmin = s->dist - s->dist_var, dmax = s->dist + s->dist_var;
    if (dmin < 3) dmin = 3;

    for (int i = 0; i < s->count; i++) {
        Target *t = &g->tg[i];
        if (t->flash > 0) t->flash -= (float)dt;
        if (t->tracked > 0) t->tracked -= (float)dt;
        if (!t->alive) {
            if (g->t >= t->respawn_at) spawn(g, i);
            continue;
        }
        if (s->lifetime > 0 && g->t - t->spawn_t >= s->lifetime) {
            g->expired++;
            schedule_respawn(g, i);
            continue;
        }
        if (s->move != MV_NONE && !t->center) {
            if (g->t >= t->next_dir_t) pick_velocity(g, t);
            t->yaw   += t->vyaw * (float)dt;
            t->pitch += t->vpitch * (float)dt;
            t->dist  += t->vdist * (float)dt;
            if (s->yaw_min > 0) {
                // sidebox movers: keep on their side
                float lo = s->yaw_min, hi = s->spawn_yaw > s->yaw_min ? s->spawn_yaw : s->yaw_min;
                if (t->yaw >= 0) { ymin = lo; ymax = hi; } else { ymin = -hi; ymax = -lo; }
            }
            if (t->yaw < ymin) { t->yaw = ymin; t->vyaw = fabsf(t->vyaw); }
            if (t->yaw > ymax) { t->yaw = ymax; t->vyaw = -fabsf(t->vyaw); }
            if (t->pitch < pmin) { t->pitch = pmin; t->vpitch = fabsf(t->vpitch); }
            if (t->pitch > pmax) { t->pitch = pmax; t->vpitch = -fabsf(t->vpitch); }
            if (t->dist < dmin) { t->dist = dmin; t->vdist = fabsf(t->vdist); }
            if (t->dist > dmax) { t->dist = dmax; t->vdist = -fabsf(t->vdist); }
        }
    }
    for (int i = 0; i < 48; i++) if (g->fx[i].on) {
        g->fx[i].t += (float)dt;
        if (g->fx[i].t > 0.16f) g->fx[i].on = false;
    }
}

void game_frame(Game *g, double dt, const RawMouseEvt *ev, int n, double dpc, float vmult, float size_mult)
{
    g->phase_t += dt;
    if (g->phase == GP_COUNTDOWN && g->phase_t >= g->countdown) {
        g->phase = GP_RUNNING; g->phase_t = 0;
    }
    if (g->phase == GP_RUNNING) {
        g->t += dt;
        if (g->t >= g->sc.duration) {
            g->t = g->sc.duration;
            g->phase = GP_DONE; g->phase_t = 0;
            g->lmb = false;
        } else {
            update_targets(g, dt);
        }
    }

    // Mouse events in arrival order: rotation, then button at that exact orientation.
    for (int i = 0; i < n; i++) {
        g->yaw   += (float)(ev[i].dx * dpc);
        g->pitch -= (float)(ev[i].dy * dpc * vmult);
        if (g->pitch > 89.f) g->pitch = 89.f;
        if (g->pitch < -89.f) g->pitch = -89.f;
        if (g->yaw > 180.f) g->yaw -= 360.f;
        if (g->yaw < -180.f) g->yaw += 360.f;

        if (ev[i].btn & PB_LDOWN) {
            g->lmb = true;
            if (g->phase == GP_RUNNING && g->sc.shoot == SM_CLICK) {
                g->shots++;
                int h = raycast(g, size_mult);
                if (h >= 0) {
                    Target *t = &g->tg[h];
                    g->hits++;
                    t->hp -= 1; t->flash = 0.08f;
                    if (t->hp <= 0.001f) kill_target(g, h); else g->ev_hit++;
                } else {
                    g->ev_miss++;
                    if (g->sc.one_shot) {
                        for (int k = 0; k < g->sc.count; k++)
                            if (g->tg[k].alive) { g->expired++; schedule_respawn(g, k); }
                    }
                }
            }
        }
        if (ev[i].btn & PB_LUP) g->lmb = false;
    }

    // Tracking damage with the final orientation of this frame.
    if (g->phase == GP_RUNNING && g->sc.shoot == SM_TRACK && g->lmb) {
        g->track_fire += dt;
        int h = raycast(g, size_mult);
        if (h >= 0) {
            Target *t = &g->tg[h];
            g->track_on += dt;
            g->points += 100.0 * dt;
            t->tracked = 0.06f;
            if (g->sc.hp > 0) {
                t->hp -= (float)dt;
                if (t->hp <= 0) kill_target(g, h);
            }
        }
    }
}

float game_accuracy(const Game *g)
{
    if (g->sc.shoot == SM_TRACK) return g->track_fire > 0 ? (float)(g->track_on / g->track_fire) : 0.f;
    return g->shots > 0 ? (float)g->hits / (float)g->shots : 0.f;
}

float game_score(const Game *g)
{
    if (g->sc.shoot == SM_TRACK) return (float)g->points;
    // Click: kills weighted by speed, multiplied by accuracy.
    // Kills already include the speed bonus; points from kill_target in tracking mode are added too.
    return (float)(g->points * (g->shots > 0 ? game_accuracy(g) : 1.0));
}

float game_time_left(const Game *g)
{
    float l = g->sc.duration - (float)g->t;
    return l < 0 ? 0 : l;
}

void game_result(const Game *g, RunResult *r)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", g->sc.id);
    r->when = (long long)time(NULL);
    r->score = game_score(g);
    r->acc = game_accuracy(g);
    r->kills = g->kills;
    r->misses = g->shots - g->hits;
    r->expired = g->expired;
    r->ttk_ms = g->ttk_n ? (float)(g->ttk_sum / g->ttk_n * 1000.0) : 0;
    r->kps = g->sc.duration > 0 ? (float)g->kills / g->sc.duration : 0;
}
