#pragma once
#include <stdbool.h>
#include <time.h>

enum { SM_CLICK = 0, SM_TRACK = 1 };
enum { MV_NONE = 0, MV_STRAFE, MV_BOUNCE, MV_DYNAMIC, MV_COUNT };
enum { CAT_FLICK = 0, CAT_REFLEX, CAT_SWITCH, CAT_TRACK, CAT_CUSTOM, CAT_COUNT };

#define MAX_TARGETS 12

typedef struct {
    char  id[24];
    char  name[48];
    char  desc[256];
    int   category;

    int   shoot;        // SM_CLICK / SM_TRACK
    int   move;         // MV_*
    float duration;     // s
    int   count;        // simultaneous targets
    float radius;       // world units (eye at origin)
    float dist;         // spawn distance
    float dist_var;     // +/- distance variation
    float spawn_yaw;    // half extent (deg)
    float spawn_pitch;  // half extent (deg)
    float pitch_off;    // vertical centre of the zone (deg)
    float yaw_min;      // minimal |yaw| (sidebox), 0 = none
    float min_sep;      // min angular separation (deg) between new spawn, crosshair and others
    float lifetime;     // s, 0 = until killed
    bool  shrink;       // shrink during lifetime
    float speed;        // deg/s
    float speed_var;    // +/- deg/s
    float dir_min;      // s between direction changes
    float dir_max;
    float hp;           // click: hits to kill; track: seconds on target to kill (0 = immortal)
    bool  flick_return; // every other target in the centre
    float delay_min;    // respawn delay (s)
    float delay_max;
    bool  one_shot;     // a miss also removes the target (first-bullet style)
    bool  show_zone;    // draw the spawn zone frame
} Scenario;

typedef enum { P_INT, P_FLOAT, P_BOOL, P_ENUM } PType;
typedef struct {
    const char *key;
    const char *label;
    const char *hint;
    PType type;
    int   offset;
    float min, max, step;
    const char *const *opts; int nopts;
} ParamDesc;

extern const ParamDesc SC_PARAMS[];
extern const int SC_PARAM_COUNT;
extern const char *const CATEGORY_NAMES[CAT_COUNT];

extern Scenario SCN[];          // current (possibly user edited) scenarios
extern int      SCN_COUNT;
extern const Scenario SCN_DEFAULT[];

void scenarios_init(void);
void scenarios_load(const char *path);
void scenarios_save(const char *path);
void scenario_reset(int idx);
bool scenario_is_modified(int idx);
bool scenario_equal(const Scenario *a, const Scenario *b);
void scenario_ruleset(int idx, char *out, int cap);   // hash of default params -> leaderboard key
int  scenario_find(const char *id);

// Programs (playlists) built from the session plans in the training guide.
typedef struct {
    const char *name;
    const char *desc;
    const char *ids[8];
    int n;
} Playlist;
extern const Playlist PLAYLISTS[];
extern const int PLAYLIST_COUNT;

// Results history
typedef struct {
    char   id[24];
    long long when;
    float  score, acc;
    int    kills, misses, expired;
    float  ttk_ms;      // avg time per kill (click) / reaction
    float  kps;
} RunResult;

void        stats_load(const char *path);
void        stats_add(const char *path, const RunResult *r);
float       stats_best(const char *id, int *runs);
int         stats_history(const char *id, RunResult *out, int max);   // newest last
int         stats_total_runs(void);
