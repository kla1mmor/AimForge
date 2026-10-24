#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "playtime.h"
#include "kv.h"

#define SAVE_EVERY_S 60.0
#define MAX_STEP_S   1.0     // a stalled frame (debugger, sleep) doesn't count as play

static char   g_path[640];
static double g_total = 0, g_session = 0, g_last = -1, g_saved_t = 0;
static int    g_sessions = 0;

static void on_kv(const char *k, const char *v, void *user)
{
    (void)user;
    if (!strcmp(k, "total_s")) { double t = atof(v); g_total = t > 0 ? t : 0; }
    else if (!strcmp(k, "sessions")) { int n = atoi(v); g_sessions = n > 0 ? n : 0; }
}

void playtime_init(const char *dir)
{
    snprintf(g_path, sizeof(g_path), "%s/aimforge_playtime.cfg", dir);
    kv_parse_file(g_path, on_kv, NULL);
    g_sessions++;
    g_session = 0; g_last = -1;
    playtime_save();
}

void playtime_tick(double now, bool active)
{
    if (g_last >= 0 && active) {
        double d = now - g_last;
        if (d > 0 && d < MAX_STEP_S) g_session += d;
    }
    g_last = now;
    if (now - g_saved_t > SAVE_EVERY_S) { g_saved_t = now; playtime_save(); }
}

void playtime_save(void)
{
    if (!g_path[0]) return;
    FILE *f = plat_fopen(g_path, "w");
    if (!f) return;
    fprintf(f, "# AimForge: time in game (seconds)\ntotal_s=%.0f\nsessions=%d\n", g_total + g_session, g_sessions);
    fclose(f);
}

double playtime_total(void) { return g_total + g_session; }
double playtime_session(void) { return g_session; }
int    playtime_sessions(void) { return g_sessions; }

void playtime_fmt(double sec, bool with_sec, char *out, int cap)
{
    long s = (long)sec, h = s / 3600, m = (s / 60) % 60;
    if (h > 0) snprintf(out, (size_t)cap, "%ld ч %02ld мин", h, m);
    else if (with_sec) snprintf(out, (size_t)cap, "%ld мин %02ld с", m, s % 60);
    else snprintf(out, (size_t)cap, "%ld мин", m);
}
