#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "online.h"
#include "net.h"
#include "settings.h"
#include "platform.h"

#define APP_VER "1.3"
#define MAX_SCN 32
#define PENDING_MAX 40
#define VER_EVERY_S (6 * 3600.0)   // re-check for a new version while the game stays open
#define VER_RETRY_S 300.0          // after a network error

SubmitState ONL_SUBMIT;
TestState   ONL_TEST;

static LbBoard g_boards[MAX_SCN];
static char    g_rules[MAX_SCN][16];
static struct { bool have; int rank, total; float best; } g_sum[MAX_SCN];
static int     g_sum_req = 0; static double g_sum_t = -1e9; static bool g_sum_dirty = true;
static char    g_pending_path[640];
static char   *g_pending[PENDING_MAX]; static int g_npend = 0;
static int     g_pend_req = 0; static double g_pend_t = 0;
static double  g_now = 0;
static char    g_url[220];
static int     g_ver_req = 0; static double g_ver_t = -1e9;
static char    g_new_ver[24], g_dl_url[400];

// ------------------------------------------------------------------ helpers
static void hex(const unsigned char *b, int n, char *out)
{
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[i * 2] = H[b[i] >> 4]; out[i * 2 + 1] = H[b[i] & 15]; }
    out[n * 2] = 0;
}

static void urlenc(const char *s, char *out, int cap)
{
    static const char *H = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o < cap - 4; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~')
            out[o++] = (char)*p;
        else { out[o++] = '%'; out[o++] = H[*p >> 4]; out[o++] = H[*p & 15]; }
    }
    out[o] = 0;
}

const char *online_server_url(void)
{
    char t[200]; int o = 0;
    for (const char *p = S.server; *p && o < 198; p++) if (*p != ' ' && *p != '\t') t[o++] = *p;
    t[o] = 0;
    while (o > 0 && t[o - 1] == '/') t[--o] = 0;
    if (o == 0) { g_url[0] = 0; return g_url; }
    if (strncmp(t, "http://", 7) != 0 && strncmp(t, "https://", 8) != 0) snprintf(g_url, sizeof(g_url), "https://%s", t);
    else snprintf(g_url, sizeof(g_url), "%s", t);
    return g_url;
}

bool online_configured(void) { return S.online && S.nick[0] && online_server_url()[0]; }

static const char *err_text(const char *code)
{
    if (!strcmp(code, "name_taken"))   return "этот ник уже занят — выбери другой";
    if (!strcmp(code, "bad_name"))     return "ник: 2–16 символов, буквы, цифры, пробел и _-.[]()!?#*+@~^";
    if (!strcmp(code, "auth"))         return "ID игрока не совпадает с сервером";
    if (!strcmp(code, "banned"))       return "аккаунт заблокирован администратором сервера";
    if (!strcmp(code, "rate_limited")) return "слишком часто — попробую ещё раз позже";
    if (!strcmp(code, "implausible"))  return "сервер отклонил результат как неправдоподобный";
    if (!strcmp(code, "bad_request"))  return "сервер не принял запрос (обнови игру)";
    if (!strcmp(code, "not_found"))    return "по этому адресу нет сервера AimForge";
    if (!strcmp(code, "server"))       return "ошибка на сервере";
    return code;
}

// Splits a response into lines/fields in place. Returns "ok" header fields count or -1 with err filled.
static int parse_head(char *body, char **fields, int maxf, char **rest, char *err, int errcap)
{
    if (!body) { snprintf(err, (size_t)errcap, "пустой ответ"); return -1; }
    char *nl = strchr(body, '\n');
    if (nl) { *nl = 0; *rest = nl + 1; } else *rest = body + strlen(body);
    int n = 0; char *p = body;
    while (p && n < maxf) { fields[n++] = p; char *t = strchr(p, '\t'); if (t) { *t = 0; p = t + 1; } else p = NULL; }
    if (n == 0) { snprintf(err, (size_t)errcap, "пустой ответ"); return -1; }
    if (!strcmp(fields[0], "err")) { snprintf(err, (size_t)errcap, "%s", n > 1 ? err_text(fields[1]) : "ошибка"); return -1; }
    if (strcmp(fields[0], "ok") != 0) { snprintf(err, (size_t)errcap, "это не сервер AimForge"); return -1; }
    return n;
}

static void identity_body(char *out, int cap)
{
    char nick[160]; urlenc(S.nick, nick, sizeof(nick));
    snprintf(out, (size_t)cap, "pid=%s&secret=%s&name=%s", S.pid, S.secret, nick);
}

// ------------------------------------------------------------------ pending queue (results made while offline)
static void pending_save(void)
{
    FILE *f = plat_fopen(g_pending_path, "w");
    if (!f) return;
    for (int i = 0; i < g_npend; i++) fprintf(f, "%s\n", g_pending[i]);
    fclose(f);
}

static void pending_push(const char *body)
{
    if (g_npend == PENDING_MAX) { free(g_pending[0]); memmove(g_pending, g_pending + 1, sizeof(char *) * (PENDING_MAX - 1)); g_npend--; }
    size_t n = strlen(body) + 1;
    g_pending[g_npend] = (char *)malloc(n); memcpy(g_pending[g_npend], body, n);
    g_npend++;
    pending_save();
}

static void pending_pop(void)
{
    if (!g_npend) return;
    free(g_pending[0]);
    memmove(g_pending, g_pending + 1, sizeof(char *) * (size_t)(g_npend - 1));
    g_npend--;
    pending_save();
}

int online_pending_count(void) { return g_npend; }

// ------------------------------------------------------------------ init
void online_init(const char *dir)
{
    net_init();
    if (strlen(S.pid) != 32 || strlen(S.secret) != 64) {
        unsigned char b[48];
        if (!plat_random(b, 48)) { for (int i = 0; i < 48; i++) b[i] = (unsigned char)(GetRandomValue(0, 255)); }
        hex(b, 16, S.pid);
        hex(b + 16, 32, S.secret);
    }
    for (int i = 0; i < SCN_COUNT && i < MAX_SCN; i++) scenario_ruleset(i, g_rules[i], sizeof(g_rules[i]));
    snprintf(g_pending_path, sizeof(g_pending_path), "%s/aimforge_pending.txt", dir);
    FILE *f = plat_fopen(g_pending_path, "r");
    if (f) {
        char line[2048];
        while (fgets(line, sizeof(line), f) && g_npend < PENDING_MAX) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (n) { g_pending[g_npend] = (char *)malloc(n + 1); memcpy(g_pending[g_npend], line, n + 1); g_npend++; }
        }
        fclose(f);
    }
    memset(&ONL_SUBMIT, 0, sizeof(ONL_SUBMIT));
    memset(&ONL_TEST, 0, sizeof(ONL_TEST));
}

void online_shutdown(void) { net_shutdown(); }

// ------------------------------------------------------------------ requests
const char *online_unranked_reason(const Game *g, bool demo)
{
    if (!S.online) return "онлайн-рейтинг выключен";
    if (!online_server_url()[0]) return "не указан адрес сервера (Настройки → Онлайн)";
    if (!S.nick[0]) return "не задан ник (Настройки → Онлайн)";
    if (demo) return NULL;
    if (!strcmp(g->sc.id, "custom")) return "«Свой сценарий» не участвует в рейтинге";
    if (!scenario_equal(&g->sc, &SCN_DEFAULT[g->sc_index])) return "параметры сценария изменены — только локальный рекорд";
    if (fabsf(S.tg_size_mult - 1.0f) > 0.001f) return "множитель размера целей не 1.0 — только локальный рекорд";
    return NULL;
}

void online_submit(const Game *g, const RunResult *r, bool demo)
{
    memset(&ONL_SUBMIT, 0, sizeof(ONL_SUBMIT));
    const char *why = online_unranked_reason(g, demo);
    if (why) { ONL_SUBMIT.ranked = false; snprintf(ONL_SUBMIT.reason, sizeof(ONL_SUBMIT.reason), "%s", why); return; }
    ONL_SUBMIT.ranked = true;
    // "part" = the result itself; identity (pid/secret/current nick) is prepended at send time,
    // so a result retried later never resurrects an old nickname.
    char part[1024], id[1024], body[2200];
    snprintf(part, sizeof(part),
             "s=%s&r=%s&mode=%s&score=%.2f&acc=%.6f&kills=%d&shots=%d&hits=%d&expired=%d&ttk=%.1f&dur=%.3f&track_on=%.3f&ver=%s",
             g->sc.id, g_rules[g->sc_index], g->sc.shoot == SM_TRACK ? "track" : "click",
             r->score, r->acc, r->kills, g->shots, g->hits, r->expired, r->ttk_ms, g->sc.duration, g->track_on, APP_VER);
    identity_body(id, sizeof(id));
    snprintf(body, sizeof(body), "%s&%s", id, part);
    char url[300]; snprintf(url, sizeof(url), "%s/submit", online_server_url());
    ONL_SUBMIT.req = net_request("POST", url, body);
    ONL_SUBMIT.status = ONL_SUBMIT.req ? NS_BUSY : NS_ERROR;
    // stash the result until the server answers, so it survives a dropped connection or a restart
    if (ONL_SUBMIT.req) pending_push(part);
    g_pend_req = 0;
}

void online_fetch(int idx, bool force)
{
    if (idx < 0 || idx >= SCN_COUNT || idx >= MAX_SCN || !online_server_url()[0]) return;
    LbBoard *b = &g_boards[idx];
    if (b->status == NS_BUSY) return;
    if (!force && b->status == NS_OK && g_now - b->fetched < 30) return;
    if (!force && b->status == NS_ERROR && g_now - b->fetched < 10) return;
    char url[400];
    snprintf(url, sizeof(url), "%s/top?s=%s&r=%s&n=%d&pid=%s", online_server_url(), SCN[idx].id, g_rules[idx], LB_ROWS, S.pid);
    b->req = net_request("GET", url, NULL);
    if (b->req) b->status = NS_BUSY;
}

void online_fetch_summary(bool force)
{
    if (!online_server_url()[0] || g_sum_req) return;
    if (!force && g_now - g_sum_t < 60) return;
    char url[300]; snprintf(url, sizeof(url), "%s/summary?pid=%s", online_server_url(), S.pid);
    g_sum_req = net_request("GET", url, NULL);
    g_sum_t = g_now;
}

void online_test(void)
{
    memset(&ONL_TEST, 0, sizeof(ONL_TEST));
    if (!online_server_url()[0]) { ONL_TEST.status = NS_ERROR; snprintf(ONL_TEST.msg, sizeof(ONL_TEST.msg), "укажи адрес сервера"); return; }
    if (!S.nick[0]) { ONL_TEST.status = NS_ERROR; snprintf(ONL_TEST.msg, sizeof(ONL_TEST.msg), "введи ник"); return; }
    char body[1024]; identity_body(body, sizeof(body));
    char url[300]; snprintf(url, sizeof(url), "%s/register", online_server_url());
    ONL_TEST.req = net_request("POST", url, body);
    ONL_TEST.status = ONL_TEST.req ? NS_BUSY : NS_ERROR;
    snprintf(ONL_TEST.msg, sizeof(ONL_TEST.msg), "%s", ONL_TEST.req ? "проверяю…" : "очередь запросов заполнена");
}

void online_open_web(void)
{
    if (!online_server_url()[0]) return;
    char url[300]; snprintf(url, sizeof(url), "%s/web", online_server_url());
    OpenURL(url);
}

// "1.2.1" vs "1.3": numeric, missing parts count as 0
static int ver_cmp(const char *a, const char *b)
{
    for (int i = 0; i < 3; i++) {
        int x = atoi(a), y = atoi(b);
        if (x != y) return x < y ? -1 : 1;
        a = strchr(a, '.'); b = strchr(b, '.');
        a = a ? a + 1 : "0"; b = b ? b + 1 : "0";
    }
    return 0;
}

static void on_version(NetResult *r)
{
    g_ver_req = 0;
    char *f[4], *rest, err[128];
    if (r->status == 0 || r->status >= 500) { g_ver_t = g_now - VER_EVERY_S + VER_RETRY_S; return; }
    if (parse_head(r->body, f, 4, &rest, err, sizeof(err)) < 3) return;   // old server without /version
    bool newer = f[1][0] >= '0' && f[1][0] <= '9' && ver_cmp(f[1], APP_VER) > 0;
    bool url_ok = !strncmp(f[2], "https://", 8) || !strncmp(f[2], "http://", 7);
    if (newer && url_ok) { snprintf(g_new_ver, sizeof(g_new_ver), "%s", f[1]); snprintf(g_dl_url, sizeof(g_dl_url), "%s", f[2]); }
    else g_new_ver[0] = 0;
}

const char *online_new_version(void) { return g_new_ver[0] ? g_new_ver : NULL; }

void online_open_download(void) { if (g_new_ver[0]) OpenURL(g_dl_url); }

LbBoard *online_board(int idx) { return (idx >= 0 && idx < MAX_SCN) ? &g_boards[idx] : NULL; }

bool online_my_rank(int idx, int *rank, int *total)
{
    if (idx < 0 || idx >= MAX_SCN || !g_sum[idx].have) return false;
    *rank = g_sum[idx].rank; *total = g_sum[idx].total;
    return true;
}

// ------------------------------------------------------------------ responses
static void on_board(LbBoard *b, NetResult *r)
{
    b->fetched = g_now; b->req = 0;
    char *f[8], *rest;
    if (r->status == 0) { b->status = NS_ERROR; snprintf(b->err, sizeof(b->err), "%s", r->err); return; }
    int n = parse_head(r->body, f, 8, &rest, b->err, sizeof(b->err));
    if (n < 0) { b->status = NS_ERROR; return; }
    b->total = n > 1 ? atoi(f[1]) : 0;
    b->my_rank = n > 2 ? atoi(f[2]) : 0;
    b->my_best = n > 3 ? (float)atof(f[3]) : 0;
    b->n = 0;
    char *line = rest;
    while (line && *line && b->n < LB_ROWS) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *c[8]; int k = 0; char *p = line;
        while (p && k < 8) { c[k++] = p; char *t = strchr(p, '\t'); if (t) { *t = 0; p = t + 1; } else p = NULL; }
        if (k >= 7) {
            LbRow *row = &b->rows[b->n++];
            row->rank = atoi(c[0]);
            snprintf(row->name, sizeof(row->name), "%s", c[1]);
            row->score = (float)atof(c[2]); row->acc = (float)atof(c[3]);
            row->kills = atoi(c[4]); row->at = atoll(c[5]); row->me = atoi(c[6]) != 0;
        }
        line = nl ? nl + 1 : NULL;
    }
    b->status = NS_OK;
}

static int scn_by_id(const char *id) { for (int i = 0; i < SCN_COUNT && i < MAX_SCN; i++) if (!strcmp(SCN[i].id, id)) return i; return -1; }

static void on_summary(NetResult *r)
{
    g_sum_req = 0;
    if (r->status == 0) return;
    char *f[4], *rest, err[128];
    if (parse_head(r->body, f, 4, &rest, err, sizeof(err)) < 0) return;
    memset(g_sum, 0, sizeof(g_sum));
    char *line = rest;
    while (line && *line) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *c[6]; int k = 0; char *p = line;
        while (p && k < 6) { c[k++] = p; char *t = strchr(p, '\t'); if (t) { *t = 0; p = t + 1; } else p = NULL; }
        if (k >= 5) {
            int i = scn_by_id(c[0]);
            if (i >= 0 && !strcmp(c[1], g_rules[i])) { g_sum[i].have = true; g_sum[i].rank = atoi(c[2]); g_sum[i].total = atoi(c[3]); g_sum[i].best = (float)atof(c[4]); }
        }
        line = nl ? nl + 1 : NULL;
    }
}

static int scn_from_body(const char *body)
{
    const char *p = !strncmp(body, "s=", 2) ? body + 2 : strstr(body, "&s=");
    if (!p) return -1;
    if (p != body + 2) p += 3;
    char id[32]; int o = 0;
    while (*p && *p != '&' && o < 31) id[o++] = *p++;
    id[o] = 0;
    return scn_by_id(id);
}

// Result of a submit (live one or a retried pending one). Returns true if the pending entry is done.
static bool on_submit(NetResult *r, bool live)
{
    if (r->status == 0 || r->status >= 500) {
        if (live) { ONL_SUBMIT.status = NS_ERROR; ONL_SUBMIT.queued = true; snprintf(ONL_SUBMIT.reason, sizeof(ONL_SUBMIT.reason), "нет связи с сервером: %s — отправлю позже", r->status ? "ошибка сервера" : r->err); }
        return false;
    }
    char *f[8], *rest, err[128];
    int n = parse_head(r->body, f, 8, &rest, err, sizeof(err));
    bool retry_later = r->body && strstr(r->body, "rate_limited");
    if (live) {
        if (n < 0) {
            ONL_SUBMIT.status = NS_ERROR; ONL_SUBMIT.queued = retry_later;
            snprintf(ONL_SUBMIT.reason, sizeof(ONL_SUBMIT.reason), "%s", err);
        } else {
            ONL_SUBMIT.status = NS_OK;
            ONL_SUBMIT.rank = n > 1 ? atoi(f[1]) : 0;
            ONL_SUBMIT.total = n > 2 ? atoi(f[2]) : 0;
            ONL_SUBMIT.best = n > 3 ? (float)atof(f[3]) : 0;
            ONL_SUBMIT.improved = n > 4 && atoi(f[4]) != 0;
        }
    }
    return !retry_later;
}

void online_update(double now)
{
    g_now = now;
    NetResult r;
    while (net_poll(&r)) {
        if (r.id == ONL_SUBMIT.req && r.id) {
            ONL_SUBMIT.req = 0;
            int si = g_npend ? scn_from_body(g_pending[g_npend - 1]) : -1;
            if (on_submit(&r, true)) {
                // the live submit is always the newest pending entry
                if (g_npend) { free(g_pending[g_npend - 1]); g_npend--; pending_save(); }
                if (si >= 0) { g_boards[si].fetched = -1e9; if (g_boards[si].status == NS_OK) g_boards[si].status = NS_NONE; }
                g_sum_dirty = true;
            }
            g_pend_t = now;
        } else if (r.id == g_pend_req && r.id) {
            g_pend_req = 0; g_pend_t = now;
            if (on_submit(&r, false)) { pending_pop(); g_sum_dirty = true; }
        } else if (r.id == ONL_TEST.req && r.id) {
            ONL_TEST.req = 0;
            char *f[4], *rest;
            if (r.status == 0) { ONL_TEST.status = NS_ERROR; snprintf(ONL_TEST.msg, sizeof(ONL_TEST.msg), "%s", r.err); }
            else if (parse_head(r.body, f, 4, &rest, ONL_TEST.msg, sizeof(ONL_TEST.msg)) < 0) ONL_TEST.status = NS_ERROR;
            else { ONL_TEST.status = NS_OK; snprintf(ONL_TEST.msg, sizeof(ONL_TEST.msg), "подключено · ник «%s» закреплён за тобой", S.nick); g_sum_dirty = true; }
        } else if (r.id == g_sum_req && r.id) {
            on_summary(&r);
        } else if (r.id == g_ver_req && r.id) {
            on_version(&r);
        } else {
            for (int i = 0; i < MAX_SCN; i++) if (g_boards[i].req == r.id && r.id) { on_board(&g_boards[i], &r); break; }
        }
        free(r.body);
    }
    // retry results that were made offline: oldest first, one at a time, not while a live submit is in flight
    if (online_configured() && g_npend > 0 && !g_pend_req && !ONL_SUBMIT.req && now - g_pend_t > 15) {
        char url[300], id[1024], body[2200];
        snprintf(url, sizeof(url), "%s/submit", online_server_url());
        identity_body(id, sizeof(id));
        snprintf(body, sizeof(body), "%s&%s", id, g_pending[0]);
        g_pend_req = net_request("POST", url, body);
        g_pend_t = now;
    }
    // "my ranks" summary only when something changed (start, submit, nickname) — saves D1 reads
    if (online_configured() && g_sum_dirty && !g_sum_req) { g_sum_dirty = false; online_fetch_summary(true); }
    // new version check: at start and every few hours; needs online on, not a nick
    if (S.online && online_server_url()[0] && !g_ver_req && now - g_ver_t > VER_EVERY_S) {
        char url[300]; snprintf(url, sizeof(url), "%s/version?v=%s", online_server_url(), APP_VER);
        g_ver_req = net_request("GET", url, NULL);
        g_ver_t = now;
    }
}
