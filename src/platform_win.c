// Windows platform layer. Compiled only on _WIN32. Does NOT include raylib.h
// (name clashes with windows.h), communicates through platform.h only.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <bcrypt.h>
#include "platform.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Ask hybrid-GPU laptops to use the discrete GPU.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#define RING_SIZE 32768
static RawMouseEvt g_ring[RING_SIZE];
static unsigned g_head = 0, g_tail = 0;   // single-threaded (window proc runs inside poll)

static HWND    g_hwnd = NULL;
static WNDPROC g_prev = NULL;
static bool    g_lock = false;
static bool    g_raw_ok = false;
static LARGE_INTEGER g_freq, g_start;
static HANDLE  g_timer = NULL;
static HANDLE  g_mmcss = NULL;
static LONG    g_abs_x = -1, g_abs_y = -1;
static char    g_info[128] = "";
static bool    g_clipped = false;

static RECT g_clip_rect;
static void release_clip(void)
{
    // Also release a clip that still matches ours even if our flag says otherwise
    // (focus can be lost between messages, e.g. Win key / Alt+Tab / Ctrl+Alt+Del).
    RECT cur;
    if (g_clipped || (GetClipCursor(&cur) && cur.left == g_clip_rect.left && cur.top == g_clip_rect.top &&
                      cur.right == g_clip_rect.right && cur.bottom == g_clip_rect.bottom &&
                      cur.right - cur.left <= 2)) ClipCursor(NULL);
    g_clipped = false;
}

static void push_evt(int dx, int dy, unsigned short btn)
{
    unsigned next = (g_head + 1) % RING_SIZE;
    if (next == g_tail) {
        // Ring full: merge into the newest event so no motion is ever lost.
        unsigned last = (g_head + RING_SIZE - 1) % RING_SIZE;
        g_ring[last].dx += dx; g_ring[last].dy += dy; g_ring[last].btn |= btn;
        return;
    }
    g_ring[g_head].dx = dx; g_ring[g_head].dy = dy; g_ring[g_head].btn = btn;
    g_head = next;
}

static bool is_active(void)
{
    return g_hwnd && GetForegroundWindow() == g_hwnd && !IsIconic(g_hwnd);
}

static void apply_clip(void)
{
    // Never grab/move the cursor unless we are the active, visible window
    // (a minimized window reports a client rect at -32000,-32000).
    if (!is_active()) { release_clip(); return; }
    RECT r; GetClientRect(g_hwnd, &r);
    if (r.right - r.left < 2 || r.bottom - r.top < 2) return;
    POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
    ClientToScreen(g_hwnd, &c);
    RECT cr = { c.x, c.y, c.x + 1, c.y + 1 };
    SetCursorPos(c.x, c.y);
    ClipCursor(&cr);
    g_clip_rect = cr;
    g_clipped = true;
}

static LRESULT CALLBACK aim_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_INPUT: {
        RAWINPUT ri; UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE) {
            RAWMOUSE *mo = &ri.data.mouse;
            int dx = 0, dy = 0;
            if (mo->usFlags & MOUSE_MOVE_ABSOLUTE) {
                // Remote desktop / tablets: convert absolute to relative.
                int vw = GetSystemMetrics((mo->usFlags & MOUSE_VIRTUAL_DESKTOP) ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
                int vh = GetSystemMetrics((mo->usFlags & MOUSE_VIRTUAL_DESKTOP) ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                LONG ax = (LONG)((double)mo->lLastX / 65535.0 * vw);
                LONG ay = (LONG)((double)mo->lLastY / 65535.0 * vh);
                if (g_abs_x >= 0) { dx = ax - g_abs_x; dy = ay - g_abs_y; }
                g_abs_x = ax; g_abs_y = ay;
            } else {
                dx = mo->lLastX; dy = mo->lLastY;
            }
            USHORT f = mo->usButtonFlags;
            unsigned short b = 0;
            if (f & RI_MOUSE_LEFT_BUTTON_DOWN)  b |= PB_LDOWN;
            if (f & RI_MOUSE_LEFT_BUTTON_UP)    b |= PB_LUP;
            if (f & RI_MOUSE_RIGHT_BUTTON_DOWN) b |= PB_RDOWN;
            if (f & RI_MOUSE_RIGHT_BUTTON_UP)   b |= PB_RUP;
            if (dx || dy || b) push_evt(dx, dy, b);
        }
        break;
    }
    case WM_SETCURSOR:
        if (g_lock && LOWORD(l) == HTCLIENT && is_active()) { SetCursor(NULL); return TRUE; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) release_clip();
        else if (g_lock && !HIWORD(w)) apply_clip();      // HIWORD = minimized
        break;
    case WM_ACTIVATEAPP:
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
        if (m != WM_ACTIVATEAPP || !w) { release_clip(); if (m != WM_CAPTURECHANGED) g_lock = false; }
        break;
    case WM_SIZE:
    case WM_MOVE:
        if (g_lock && GetForegroundWindow() == g_hwnd) apply_clip();
        break;
    }
    return CallWindowProcW(g_prev, h, m, w, l);
}

bool plat_init(void *native_window)
{
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);

    g_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!g_timer) g_timer = CreateWaitableTimerW(NULL, TRUE, NULL);

    // Multimedia class scheduling ("Games") — lower scheduling jitter.
    HMODULE avrt = LoadLibraryA("avrt.dll");
    if (avrt) {
        typedef HANDLE (WINAPI *PFN_AvSet)(LPCWSTR, LPDWORD);
        PFN_AvSet fn = (PFN_AvSet)(void *)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW");
        DWORD idx = 0;
        if (fn) g_mmcss = fn(L"Games", &idx);
    }
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

    g_hwnd = (HWND)native_window;
    if (g_hwnd) {
        g_prev = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)aim_wndproc);
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;   // generic desktop
        rid.usUsage     = 0x02;   // mouse
        rid.dwFlags     = 0;      // foreground only, legacy messages kept for menus
        rid.hwndTarget  = g_hwnd;
        g_raw_ok = RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
    }
    snprintf(g_info, sizeof(g_info), "%s%s",
             g_raw_ok ? "Raw Input (WM_INPUT)" : "Raw Input недоступен",
             g_mmcss ? " + MMCSS" : "");
    return g_raw_ok;
}

void plat_shutdown(void)
{
    release_clip();
    if (g_hwnd && g_prev) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_prev);
    if (g_timer) CloseHandle(g_timer);
}

int plat_drain(RawMouseEvt *out, int max)
{
    int n = 0;
    while (g_tail != g_head && n < max) {
        out[n++] = g_ring[g_tail];
        g_tail = (g_tail + 1) % RING_SIZE;
    }
    // If caller buffer was too small, fold the remainder into the last event.
    while (g_tail != g_head && n > 0) {
        out[n - 1].dx += g_ring[g_tail].dx; out[n - 1].dy += g_ring[g_tail].dy; out[n - 1].btn |= g_ring[g_tail].btn;
        g_tail = (g_tail + 1) % RING_SIZE;
    }
    return n;
}

void plat_lock_mouse(bool lock)
{
    if (lock == g_lock) return;
    g_lock = lock;
    if (lock) {
        apply_clip();
        SetCursor(NULL);
    } else {
        release_clip();
        if (is_active()) {     // only recenter the cursor if the user is actually in our window
            RECT r; GetClientRect(g_hwnd, &r);
            POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
            ClientToScreen(g_hwnd, &c);
            SetCursorPos(c.x, c.y);
        }
        if (is_active()) SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
}

void plat_flush(void) { g_tail = g_head; g_abs_x = g_abs_y = -1; }

void plat_frame(bool focused)
{
    static bool was_active = true;
    bool act = focused && is_active();
    if (g_lock && act && !was_active) apply_clip();
    if (!act) {
        if (was_active) release_clip();
        else if (g_clipped) release_clip();
        // never keep the cursor hidden for other apps
        if (g_lock) { g_lock = false; SetCursor(LoadCursor(NULL, IDC_ARROW)); }
    }
    was_active = act;
}

double plat_time(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_start.QuadPart) / (double)g_freq.QuadPart;
}

void plat_wait_until(double t)
{
    for (;;) {
        double rem = t - plat_time();
        if (rem <= 0.0) return;
        if (rem > 0.0016 && g_timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((rem - 0.0011) * 1e7);   // wake ~1.1ms early, then spin
            if (SetWaitableTimer(g_timer, &due, 0, NULL, NULL, FALSE))
                WaitForSingleObject(g_timer, INFINITE);
            else Sleep(0);
        } else {
            YieldProcessor();
        }
    }
}

const char *plat_info(void) { return g_info; }

const char *plat_app_dir(void)
{
    static char dir[MAX_PATH * 4] = "";
    wchar_t w[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return ".";
    while (n > 0 && w[n - 1] != L'\\' && w[n - 1] != L'/') n--;
    if (n > 0) n--;                       // drop the separator
    w[n] = 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, dir, sizeof(dir), NULL, NULL)) return ".";
    return dir;
}

bool plat_random(void *buf, int n)
{
    return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

FILE *plat_fopen(const char *utf8_path, const char *mode)
{
    wchar_t wp[MAX_PATH * 2], wm[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wp, MAX_PATH * 2)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 16)) return NULL;
    return _wfopen(wp, wm);
}
#endif
