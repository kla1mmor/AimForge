// Display modes via GLFW directly: windowed / borderless / exclusive fullscreen
// with explicit resolution + refresh rate. VSync is never enabled.
#include <stdio.h>
#include <string.h>
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
#include "raylib.h"
#include "display.h"
#include "settings.h"

static char g_info[160] = "";

static GLFWmonitor *cur_monitor(void)
{
    int n = 0;
    GLFWmonitor **ms = glfwGetMonitors(&n);
    GLFWwindow *w = glfwGetCurrentContext();
    if (w) {
        GLFWmonitor *fm = glfwGetWindowMonitor(w);
        if (fm) return fm;
    }
    int idx = GetCurrentMonitor();
    if (ms && idx >= 0 && idx < n) return ms[idx];
    return glfwGetPrimaryMonitor();
}

int display_modes(ResMode *out, int max)
{
    GLFWmonitor *m = cur_monitor();
    if (!m) return 0;
    int cnt = 0;
    const GLFWvidmode *vm = glfwGetVideoModes(m, &cnt);
    int n = 0;
    for (int i = 0; i < cnt; i++) {
        if (vm[i].width < 1024 || vm[i].height < 600) continue;
        int k = -1;
        for (int j = 0; j < n; j++) if (out[j].w == vm[i].width && out[j].h == vm[i].height) { k = j; break; }
        if (k < 0) {
            if (n >= max) continue;
            k = n++;
            out[k].w = vm[i].width; out[k].h = vm[i].height; out[k].nr = 0;
        }
        bool dup = false;
        for (int r = 0; r < out[k].nr; r++) if (out[k].rates[r] == vm[i].refreshRate) dup = true;
        if (!dup && out[k].nr < 24) out[k].rates[out[k].nr++] = vm[i].refreshRate;
    }
    // sort resolutions by pixel count desc, rates desc
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (out[b].w * out[b].h > out[a].w * out[a].h) { ResMode t = out[a]; out[a] = out[b]; out[b] = t; }
    for (int a = 0; a < n; a++)
        for (int x = 0; x < out[a].nr; x++)
            for (int y = x + 1; y < out[a].nr; y++)
                if (out[a].rates[y] > out[a].rates[x]) { int t = out[a].rates[x]; out[a].rates[x] = out[a].rates[y]; out[a].rates[y] = t; }
    return n;
}

void display_desktop(int *w, int *h, int *hz)
{
    GLFWwindow *win = glfwGetCurrentContext();
    GLFWmonitor *m = cur_monitor();
    if (win && glfwGetWindowMonitor(win)) {
        // In exclusive mode the "desktop" is reported as the current mode; good enough.
    }
    const GLFWvidmode *dm = m ? glfwGetVideoMode(m) : NULL;
    if (dm) { *w = dm->width; *h = dm->height; *hz = dm->refreshRate; }
    else { *w = 1920; *h = 1080; *hz = 60; }
}

int display_current_refresh(void)
{
    GLFWmonitor *m = cur_monitor();
    const GLFWvidmode *dm = m ? glfwGetVideoMode(m) : NULL;
    return dm ? dm->refreshRate : 60;
}

void display_apply(void)
{
    GLFWwindow *w = glfwGetCurrentContext();
    if (!w) return;
    GLFWmonitor *m = cur_monitor();

    // Leave exclusive fullscreen first so the desktop mode is restored.
    if (glfwGetWindowMonitor(w)) {
        glfwSetWindowMonitor(w, NULL, 100, 100, 1280, 720, GLFW_DONT_CARE);
        m = cur_monitor();
    }
    const GLFWvidmode *dm = glfwGetVideoMode(m);
    int mx = 0, my = 0;
    glfwGetMonitorPos(m, &mx, &my);
    int dw = dm ? dm->width : 1920, dh = dm ? dm->height : 1080, dhz = dm ? dm->refreshRate : 60;

    switch (S.display_mode) {
    case DM_WINDOWED: {
        int ww = S.res_w, hh = S.res_h;
        if (ww > dw) ww = dw;
        if (hh > dh - 40) hh = dh - 40;
        glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(w, NULL, mx + (dw - ww) / 2, my + (dh - hh) / 2, ww, hh, GLFW_DONT_CARE);
        snprintf(g_info, sizeof(g_info), "Окно %d×%d · монитор %d Гц", ww, hh, dhz);
    } break;
    case DM_BORDERLESS:
        glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(w, NULL, mx, my, dw, dh, GLFW_DONT_CARE);
        snprintf(g_info, sizeof(g_info), "Без рамки %d×%d · %d Гц (разрешение рабочего стола)", dw, dh, dhz);
        break;
    default: {
        int rate = S.refresh > 0 ? S.refresh : GLFW_DONT_CARE;   // DONT_CARE -> highest
        glfwSetWindowMonitor(w, m, 0, 0, S.res_w, S.res_h, rate);
        const GLFWvidmode *nm = glfwGetVideoMode(m);
        snprintf(g_info, sizeof(g_info), "Полный экран %d×%d · %d Гц", nm ? nm->width : S.res_w,
                 nm ? nm->height : S.res_h, nm ? nm->refreshRate : 0);
    } break;
    }
    glfwSwapInterval(0);   // VSync off, always
    glfwFocusWindow(w);
}

const char *display_info(void) { return g_info; }

void *display_glfw_proc(const char *name) { return (void *)glfwGetProcAddress(name); }
