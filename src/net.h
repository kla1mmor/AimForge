// Minimal asynchronous HTTP(S) client. All requests run on one background thread,
// so the game loop never waits for the network.
#pragma once
#include <stdbool.h>

typedef struct {
    int   id;
    int   status;        // HTTP status; 0 = network error (see err)
    char *body;          // NUL terminated, caller frees with free()
    int   len;
    char  err[128];
} NetResult;

void net_init(void);
void net_shutdown(void);
int  net_request(const char *method, const char *url, const char *form_body);  // id > 0, 0 if queue full
bool net_poll(NetResult *out);
int  net_pending(void);                                                         // queued + in flight
