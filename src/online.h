// Online leaderboard client (talks to server/worker.js).
#pragma once
#include <stdbool.h>
#include "game.h"
#include "scenario.h"

#define LB_ROWS 50

typedef struct {
    int   rank;
    char  name[48];
    float score, acc;
    int   kills;
    long long at;
    bool  me;
} LbRow;

enum { NS_NONE = 0, NS_BUSY, NS_OK, NS_ERROR };

typedef struct {
    int    status;
    char   err[128];
    LbRow  rows[LB_ROWS];
    int    n, total, my_rank;
    float  my_best;
    double fetched;
    int    req;
} LbBoard;

typedef struct {
    int   status;          // NS_*
    bool  ranked;          // false -> reason explains why it wasn't sent
    char  reason[200];
    int   rank, total;
    float best;
    bool  improved;
    bool  queued;          // network down: stored, will be retried
    int   req;
} SubmitState;

typedef struct {
    int  status;
    char msg[160];
    int  req;
} TestState;

void        online_init(const char *dir);
void        online_shutdown(void);
void        online_update(double now);
bool        online_configured(void);                 // enabled + nick + server
const char *online_unranked_reason(const Game *g, bool demo);
void        online_submit(const Game *g, const RunResult *r, bool demo);
void        online_fetch(int scn_idx, bool force);
void        online_fetch_summary(bool force);
void        online_test(void);                         // register nick / check connection
void        online_open_web(void);
bool        online_my_rank(int scn_idx, int *rank, int *total);
LbBoard    *online_board(int scn_idx);
int         online_pending_count(void);
const char *online_server_url(void);                   // normalized
const char *online_new_version(void);                  // newer version on the server, or NULL
void        online_open_download(void);
extern SubmitState ONL_SUBMIT;
extern TestState   ONL_TEST;
