#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "kv.h"

const SensGame SENS_GAMES[] = {
    { "Valorant",              0.07   },
    { "CS2 / Apex / Source",   0.022  },
    { "Overwatch 2",           0.0066 },
    { "cm/360",                0.0    },
};
const int SENS_GAME_COUNT = sizeof(SENS_GAMES) / sizeof(SENS_GAMES[0]);

Settings S;

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->display_mode = DM_FULLSCREEN;
    s->res_w = 1920; s->res_h = 1080; s->refresh = 0;   // 0 = highest available
    s->fps_cap = 0;
    s->low_latency = true;
    s->msaa = true;
    s->show_fps = true;

    s->sens_game = 0; s->sens = 0.35f; s->dpi = 800; s->vert_mult = 1.0f;
    s->fov = 103.0f;

    s->ch_style = CH_CROSS_DOT;
    s->ch_color = (Color){ 0, 255, 140, 255 };
    s->ch_len = 6; s->ch_thick = 2; s->ch_gap = 3;
    s->ch_outline = true; s->ch_out_thick = 1; s->ch_out_alpha = 0.8f;
    s->ch_dot = 2; s->ch_circle = 10;

    s->tg_color = (Color){ 255, 70, 90, 255 };
    s->tg_size_mult = 1.0f;
    s->tg_style = TS_LIT;
    s->tg_hit_fx = true;
    s->tg_track_color = (Color){ 80, 230, 255, 255 };

    s->theme = TH_SLATE; s->grid = true; s->show_zone = true;
    s->snd_hit = true; s->snd_miss = false; s->volume = 0.5f;
    s->countdown = true; s->show_hud = true;
    s->online = true;
#ifdef AIMFORGE_DEFAULT_SERVER
    snprintf(s->server, sizeof(s->server), "%s", AIMFORGE_DEFAULT_SERVER);
#endif
}

typedef enum { T_INT, T_FLOAT, T_BOOL, T_COLOR, T_STR } KType;
typedef struct { const char *key; KType t; void *p; int cap; } KDesc;

static int build_table(KDesc *d)
{
    int n = 0;
#define I(k, f) d[n++] = (KDesc){ k, T_INT,   &S.f }
#define F(k, f) d[n++] = (KDesc){ k, T_FLOAT, &S.f }
#define B(k, f) d[n++] = (KDesc){ k, T_BOOL,  &S.f }
#define C(k, f) d[n++] = (KDesc){ k, T_COLOR, &S.f }
#define T(k, f) d[n++] = (KDesc){ k, T_STR, S.f, (int)sizeof(S.f) }
    I("display_mode", display_mode); I("res_w", res_w); I("res_h", res_h); I("refresh", refresh);
    I("fps_cap", fps_cap); B("low_latency", low_latency); B("msaa", msaa); B("show_fps", show_fps);
    I("sens_game", sens_game); F("sens", sens); I("dpi", dpi); F("vert_mult", vert_mult); F("fov", fov);
    I("ch_style", ch_style); C("ch_color", ch_color); F("ch_len", ch_len); F("ch_thick", ch_thick);
    F("ch_gap", ch_gap); B("ch_outline", ch_outline); F("ch_out_thick", ch_out_thick);
    F("ch_out_alpha", ch_out_alpha); F("ch_dot", ch_dot); F("ch_circle", ch_circle);
    C("tg_color", tg_color); F("tg_size_mult", tg_size_mult); I("tg_style", tg_style);
    B("tg_hit_fx", tg_hit_fx); C("tg_track_color", tg_track_color);
    I("theme", theme); B("grid", grid); B("show_zone", show_zone);
    B("snd_hit", snd_hit); B("snd_miss", snd_miss); F("volume", volume);
    B("countdown", countdown); B("show_hud", show_hud);
    B("online", online); T("nick", nick); T("server", server); T("player_id", pid); T("player_secret", secret);
#undef I
#undef F
#undef B
#undef C
#undef T
    return n;
}

static void on_kv(const char *k, const char *v, void *user)
{
    (void)user;
    KDesc d[64]; int n = build_table(d);
    for (int i = 0; i < n; i++) {
        if (strcmp(d[i].key, k) != 0) continue;
        switch (d[i].t) {
        case T_INT:   *(int *)d[i].p = atoi(v); break;
        case T_FLOAT: *(float *)d[i].p = (float)atof(v); break;
        case T_BOOL:  *(bool *)d[i].p = atoi(v) != 0; break;
        case T_STR: snprintf((char *)d[i].p, (size_t)d[i].cap, "%s", v); break;
        case T_COLOR: {
            unsigned r = 255, g = 255, b = 255, a = 255;
            sscanf(v, "%u,%u,%u,%u", &r, &g, &b, &a);
            *(Color *)d[i].p = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
        } break;
        }
        return;
    }
}

bool settings_load(const char *path)
{
    settings_defaults(&S);
    bool existed = kv_parse_file(path, on_kv, NULL) != 0;
    if (S.sens_game < 0 || S.sens_game >= SENS_GAME_COUNT) S.sens_game = 0;
    if (S.sens <= 0) S.sens = 0.35f;
    if (S.dpi <= 0) S.dpi = 800;
    if (S.fov < 60 || S.fov > 150) S.fov = 103;
    if (S.ch_style < 0 || S.ch_style >= CH_COUNT) S.ch_style = CH_CROSS_DOT;
    if (S.tg_style < 0 || S.tg_style >= TS_COUNT) S.tg_style = TS_LIT;
    if (S.theme < 0 || S.theme >= TH_COUNT) S.theme = 0;
    if (S.tg_size_mult < 0.3f || S.tg_size_mult > 3.0f) S.tg_size_mult = 1.0f;
#ifdef AIMFORGE_DEFAULT_SERVER
    // official build: the shared leaderboard server is filled in unless the player set their own
    if (!S.server[0]) snprintf(S.server, sizeof(S.server), "%s", AIMFORGE_DEFAULT_SERVER);
#endif
    return existed;
}

void settings_save(const char *path)
{
    FILE *f = plat_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# AimForge settings\n");
    KDesc d[64]; int n = build_table(d);
    for (int i = 0; i < n; i++) {
        switch (d[i].t) {
        case T_INT:   fprintf(f, "%s=%d\n", d[i].key, *(int *)d[i].p); break;
        case T_FLOAT: fprintf(f, "%s=%.6g\n", d[i].key, *(float *)d[i].p); break;
        case T_BOOL:  fprintf(f, "%s=%d\n", d[i].key, *(bool *)d[i].p ? 1 : 0); break;
        case T_STR:   fprintf(f, "%s=%s\n", d[i].key, (const char *)d[i].p); break;
        case T_COLOR: { Color c = *(Color *)d[i].p; fprintf(f, "%s=%u,%u,%u,%u\n", d[i].key, c.r, c.g, c.b, c.a); } break;
        }
    }
    fclose(f);
}

double settings_deg_per_count(const Settings *s)
{
    double yaw = SENS_GAMES[s->sens_game].yaw;
    if (yaw > 0) return (double)s->sens * yaw;
    // cm/360 mode: counts per 360 = cm/2.54*dpi
    double counts = (double)s->sens / 2.54 * (double)s->dpi;
    return counts > 0 ? 360.0 / counts : 0.01;
}

double settings_cm360(const Settings *s)
{
    double dpc = settings_deg_per_count(s);
    if (dpc <= 0) return 0;
    return 360.0 / dpc / (double)s->dpi * 2.54;
}

void settings_set_game_keep_cm(Settings *s, int new_game)
{
    if (new_game < 0 || new_game >= SENS_GAME_COUNT) return;
    double cm = settings_cm360(s);
    s->sens_game = new_game;
    double yaw = SENS_GAMES[new_game].yaw;
    if (yaw > 0) {
        // cm = 360 / (sens*yaw) / dpi * 2.54  ->  sens = 360*2.54 / (cm*yaw*dpi)
        s->sens = (float)(360.0 * 2.54 / (cm * yaw * (double)s->dpi));
    } else {
        s->sens = (float)cm;
    }
}
