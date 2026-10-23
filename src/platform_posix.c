// Fallback platform layer (Linux test builds). Mouse comes from raylib in main.c.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "platform.h"

static struct timespec g_t0;

bool plat_init(void *native_window) { (void)native_window; clock_gettime(CLOCK_MONOTONIC, &g_t0); return false; }
void plat_shutdown(void) {}
int  plat_drain(RawMouseEvt *out, int max) { (void)out; (void)max; return 0; }
void plat_lock_mouse(bool lock) { (void)lock; }
void plat_frame(bool focused) { (void)focused; }
void plat_flush(void) {}

double plat_time(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - g_t0.tv_sec) + (double)(t.tv_nsec - g_t0.tv_nsec) * 1e-9;
}

void plat_wait_until(double t)
{
    for (;;) {
        double rem = t - plat_time();
        if (rem <= 0) return;
        if (rem > 0.002) {
            struct timespec s = { 0, (long)((rem - 0.001) * 1e9) };
            nanosleep(&s, NULL);
        }
    }
}

const char *plat_info(void) { return "raylib mouse (тестовая сборка)"; }
const char *plat_app_dir(void) { return "."; }
FILE *plat_fopen(const char *p, const char *m) { return fopen(p, m); }
bool plat_random(void *buf, int n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    bool ok = fread(buf, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}
#endif
