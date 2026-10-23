// Procedurally generated UI/game sounds (no audio files needed).
#include <math.h>
#include <stdlib.h>
#include "raylib.h"
#include "sfx.h"

#define SR 48000
static Sound s_hit, s_kill, s_miss, s_tick, s_go;
static Sound s_kill_alias[4];
static int   s_kill_idx = 0;
static bool  g_ok = false;

static Sound make(float dur, float f0, float f1, float noise, float decay, float gain)
{
    int n = (int)(dur * SR);
    short *d = (short *)malloc(sizeof(short) * (size_t)n);
    float ph = 0; unsigned rng = 12345;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, k = (float)i / (float)n;
        float f = f0 + (f1 - f0) * k;
        ph += 6.2831853f * f / SR;
        rng = rng * 1664525u + 1013904223u;
        float nz = ((float)(rng >> 9) / 4194304.0f - 1.0f);
        float env = expf(-t * decay) * (t < 0.002f ? t / 0.002f : 1.0f);
        float v = (sinf(ph) * 0.8f + sinf(ph * 2.01f) * 0.2f) * env + nz * noise * expf(-t * decay * 3.0f);
        v *= gain;
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        d[i] = (short)(v * 32000);
    }
    Wave w = { (unsigned)n, SR, 16, 1, d };
    Sound s = LoadSoundFromWave(w);
    free(d);
    return s;
}

void sfx_init(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    s_kill = make(0.070f, 1500, 950, 0.35f, 55, 0.75f);
    s_hit  = make(0.035f, 1100, 1000, 0.25f, 90, 0.45f);
    s_miss = make(0.050f, 220, 160, 0.15f, 70, 0.35f);
    s_tick = make(0.060f, 880, 880, 0.0f, 45, 0.45f);
    s_go   = make(0.120f, 1320, 1320, 0.0f, 25, 0.55f);
    for (int i = 0; i < 4; i++) s_kill_alias[i] = LoadSoundAlias(s_kill);
    g_ok = true;
}

void sfx_shutdown(void)
{
    if (!g_ok) return;
    for (int i = 0; i < 4; i++) UnloadSoundAlias(s_kill_alias[i]);
    UnloadSound(s_kill); UnloadSound(s_hit); UnloadSound(s_miss); UnloadSound(s_tick); UnloadSound(s_go);
    CloseAudioDevice();
    g_ok = false;
}

void sfx_volume(float v) { if (g_ok) SetMasterVolume(v); }
void sfx_kill(void) { if (g_ok) { PlaySound(s_kill_alias[s_kill_idx]); s_kill_idx = (s_kill_idx + 1) & 3; } }
void sfx_hit(void)  { if (g_ok) PlaySound(s_hit); }
void sfx_miss(void) { if (g_ok) PlaySound(s_miss); }
void sfx_tick(void) { if (g_ok) PlaySound(s_tick); }
void sfx_go(void)   { if (g_ok) PlaySound(s_go); }
