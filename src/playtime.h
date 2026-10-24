// Time spent in the game: all-time total and the current session.
// Only active time counts: window focused, and a scenario running or recent input.
#pragma once
#include <stdbool.h>

void   playtime_init(const char *dir);           // loads aimforge_playtime.cfg, starts a new session
void   playtime_tick(double now, bool active);   // call every loop iteration
void   playtime_save(void);
double playtime_total(void);                     // seconds, including this session
double playtime_session(void);
int    playtime_sessions(void);                  // including this one
void   playtime_fmt(double sec, bool with_sec, char *out, int cap);   // "12 ч 05 мин", "4 мин 09 с"
