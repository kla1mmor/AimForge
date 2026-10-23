// Platform layer: raw mouse input, cursor lock, high precision timing.
// Windows implementation reads WM_INPUT directly (no acceleration, no OS cursor
// processing, integer counts, sub-frame event order preserved).
#pragma once
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    int dx, dy;              // raw counts
    unsigned short btn;      // PB_* flags that happened at this point in the stream
} RawMouseEvt;

#define PB_LDOWN 0x01
#define PB_LUP   0x02
#define PB_RDOWN 0x04
#define PB_RUP   0x08

// Returns true when native raw input is active (Windows). False -> use fallback.
bool   plat_init(void *native_window);
void   plat_shutdown(void);
int    plat_drain(RawMouseEvt *out, int max);   // take all events since last call
void   plat_lock_mouse(bool lock);              // hide + confine cursor while playing
void   plat_frame(bool focused);                // keep cursor confinement valid
void   plat_flush(void);                        // drop queued mouse events (focus change / resume)
double plat_time(void);                          // seconds, monotonic, high resolution
void   plat_wait_until(double t);                // precise hybrid sleep/spin
const char *plat_info(void);                     // short description for UI
const char *plat_app_dir(void);                  // directory of the executable, UTF-8, no trailing slash
FILE  *plat_fopen(const char *utf8_path, const char *mode);   // Unicode-safe fopen
bool   plat_random(void *buf, int n);            // cryptographically secure random bytes
