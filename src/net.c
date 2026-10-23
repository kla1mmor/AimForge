// Background HTTP worker. Windows: WinHTTP (system HTTPS stack, honours system proxy).
// Other platforms (test builds only): the curl command line tool.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"

#define QMAX 32
#define BODY_MAX (512 * 1024)

typedef struct { int id; char method[8]; char *url; char *body; } Job;

static Job       g_jobs[QMAX];   static int g_jn = 0;
static NetResult g_done[QMAX];   static int g_dn = 0;
static int       g_next_id = 1, g_inflight = 0;
static volatile int g_quit = 0, g_started = 0;

// ---------------------------------------------------------------- threading
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
static CRITICAL_SECTION   g_mx;
static CONDITION_VARIABLE g_cv;
static HANDLE             g_th;
#define LOCK()   EnterCriticalSection(&g_mx)
#define UNLOCK() LeaveCriticalSection(&g_mx)
#define WAIT()   SleepConditionVariableCS(&g_cv, &g_mx, INFINITE)
#define SIGNAL() WakeConditionVariable(&g_cv)
#else
#include <pthread.h>
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t       g_th;
#define LOCK()   pthread_mutex_lock(&g_mx)
#define UNLOCK() pthread_mutex_unlock(&g_mx)
#define WAIT()   pthread_cond_wait(&g_cv, &g_mx)
#define SIGNAL() pthread_cond_signal(&g_cv)
#endif

static char *dupstr(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *d = (char *)malloc(n); memcpy(d, s, n); return d; }

// ---------------------------------------------------------------- HTTP backends
#ifdef _WIN32
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
static HINTERNET g_session = NULL;

static void http_do(const Job *j, NetResult *r)
{
    wchar_t wurl[2048], host[256], path[2048], extra[2048], wmeth[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, j->url, -1, wurl, 2048)) { snprintf(r->err, sizeof(r->err), "некорректный адрес"); return; }
    MultiByteToWideChar(CP_UTF8, 0, j->method, -1, wmeth, 8);
    URL_COMPONENTS uc; memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { snprintf(r->err, sizeof(r->err), "некорректный адрес сервера"); return; }
    wchar_t full[4096];
    _snwprintf(full, 4096, L"%ls%ls", uc.dwUrlPathLength ? path : L"/", uc.dwExtraInfoLength ? extra : L"");
    full[4095] = 0;

    if (!g_session) {
        g_session = WinHttpOpen(L"AimForge/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session) g_session = WinHttpOpen(L"AimForge/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session) { snprintf(r->err, sizeof(r->err), "WinHTTP недоступен (%lu)", GetLastError()); return; }
        WinHttpSetTimeouts(g_session, 6000, 6000, 8000, 10000);
    }
    HINTERNET con = WinHttpConnect(g_session, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, wmeth, full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    DWORD blen = j->body ? (DWORD)strlen(j->body) : 0;
    BOOL ok = req && WinHttpSendRequest(req, L"Content-Type: application/x-www-form-urlencoded\r\n", (DWORD)-1L,
                                        blen ? (LPVOID)j->body : WINHTTP_NO_REQUEST_DATA, blen, blen, 0)
                  && WinHttpReceiveResponse(req, NULL);
    if (!ok) {
        DWORD e = GetLastError();
        const char *m = e == ERROR_WINHTTP_TIMEOUT ? "сервер не ответил (таймаут)"
                      : e == ERROR_WINHTTP_NAME_NOT_RESOLVED ? "нет интернета или неверный адрес"
                      : e == ERROR_WINHTTP_CANNOT_CONNECT ? "не удалось подключиться"
                      : e == ERROR_WINHTTP_SECURE_FAILURE ? "ошибка HTTPS-сертификата" : "ошибка сети";
        snprintf(r->err, sizeof(r->err), "%s (%lu)", m, e);
    } else {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &sz, WINHTTP_NO_HEADER_INDEX);
        int cap = 4096, len = 0;
        char *buf = (char *)malloc((size_t)cap);
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            if (len + (int)avail + 1 > BODY_MAX) break;
            while (len + (int)avail + 1 > cap) { cap *= 2; buf = (char *)realloc(buf, (size_t)cap); }
            if (!WinHttpReadData(req, buf + len, avail, &got) || got == 0) break;
            len += (int)got;
        }
        buf[len] = 0;
        r->status = (int)status; r->body = buf; r->len = len;
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
}
#else
static void http_do(const Job *j, NetResult *r)
{
    // Test-build backend. URL/body are built by the game from url-encoded parts only.
    for (const char *p = j->url; *p; p++) if (*p == '\'' || *p == ' ' || *p == '\\') { snprintf(r->err, sizeof(r->err), "некорректный адрес"); return; }
    if (j->body) for (const char *p = j->body; *p; p++) if (*p == '\'') { snprintf(r->err, sizeof(r->err), "bad body"); return; }
    size_t cl = strlen(j->url) + (j->body ? strlen(j->body) : 0) + 256;
    char *cmd = (char *)malloc(cl);
    snprintf(cmd, cl, "curl -s -m 8 -X %s -H 'Content-Type: application/x-www-form-urlencoded' %s%s%s -w '\\n%%{http_code}' '%s' 2>/dev/null",
             j->method, j->body ? "--data-binary '" : "", j->body ? j->body : "", j->body ? "'" : "", j->url);
    FILE *f = popen(cmd, "r");
    free(cmd);
    if (!f) { snprintf(r->err, sizeof(r->err), "curl недоступен"); return; }
    int cap = 4096, len = 0; char *buf = (char *)malloc((size_t)cap);
    size_t got;
    while ((got = fread(buf + len, 1, (size_t)(cap - len - 1), f)) > 0) {
        len += (int)got;
        if (len + 1 >= cap) { if (cap >= BODY_MAX) break; cap *= 2; buf = (char *)realloc(buf, (size_t)cap); }
    }
    pclose(f);
    buf[len] = 0;
    char *nl = strrchr(buf, '\n');
    int status = nl ? atoi(nl + 1) : 0;
    if (nl) { *nl = 0; len = (int)(nl - buf); }
    if (status == 0) { free(buf); snprintf(r->err, sizeof(r->err), "не удалось подключиться"); return; }
    r->status = status; r->body = buf; r->len = len;
}
#endif

// ---------------------------------------------------------------- worker
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID arg)
#else
static void *worker(void *arg)
#endif
{
    (void)arg;
    for (;;) {
        LOCK();
        while (g_jn == 0 && !g_quit) WAIT();
        if (g_quit) { UNLOCK(); break; }
        Job j = g_jobs[0];
        memmove(g_jobs, g_jobs + 1, sizeof(Job) * (size_t)(--g_jn));
        g_inflight = 1;
        UNLOCK();

        NetResult r; memset(&r, 0, sizeof(r));
        r.id = j.id;
        http_do(&j, &r);
        free(j.url); free(j.body);

        LOCK();
        if (g_dn < QMAX) g_done[g_dn++] = r; else free(r.body);
        g_inflight = 0;
        UNLOCK();
    }
    return 0;
}

void net_init(void)
{
    if (g_started) return;
#ifdef _WIN32
    InitializeCriticalSection(&g_mx);
    InitializeConditionVariable(&g_cv);
    g_th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    g_started = g_th != NULL;
#else
    g_started = pthread_create(&g_th, NULL, worker, NULL) == 0;
#endif
}

void net_shutdown(void)
{
    if (!g_started) return;
    LOCK(); g_quit = 1; SIGNAL(); UNLOCK();
#ifdef _WIN32
    // Don't hang on exit if a request is stuck: give it a moment, then let the process end.
    WaitForSingleObject(g_th, 300);
#else
    if (!g_inflight) pthread_join(g_th, NULL);
#endif
}

int net_request(const char *method, const char *url, const char *body)
{
    if (!g_started) return 0;
    LOCK();
    if (g_jn >= QMAX) { UNLOCK(); return 0; }
    Job *j = &g_jobs[g_jn++];
    j->id = g_next_id++;
    snprintf(j->method, sizeof(j->method), "%s", method);
    j->url = dupstr(url);
    j->body = dupstr(body);
    int id = j->id;
    SIGNAL();
    UNLOCK();
    return id;
}

bool net_poll(NetResult *out)
{
    if (!g_started) return false;
    LOCK();
    if (g_dn == 0) { UNLOCK(); return false; }
    *out = g_done[0];
    memmove(g_done, g_done + 1, sizeof(NetResult) * (size_t)(--g_dn));
    UNLOCK();
    return true;
}

int net_pending(void)
{
    if (!g_started) return 0;
    LOCK(); int n = g_jn + g_inflight; UNLOCK();
    return n;
}
