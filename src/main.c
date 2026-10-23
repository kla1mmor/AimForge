// AimForge — standalone aim trainer.
// raylib + OpenGL 3.3, custom frame control, Raw Input on Windows, VSync off.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "raylib.h"
#include "platform.h"
#include "settings.h"
#include "scenario.h"
#include "game.h"
#include "render.h"
#include "ui.h"
#include "fonts.h"
#include "sfx.h"
#include "display.h"
#include "crosshair.h"
#include "online.h"

#define APP_NAME "AimForge"
#define APP_VER  "1.2"

typedef enum { PAGE_SCEN = 0, PAGE_PROG, PAGE_STATS, PAGE_SETTINGS, PAGE_EDIT, PAGE_LB } Page;
enum { ST_MENU = 0, ST_GAME };
enum { GS_PLAY = 0, GS_PAUSE, GS_RESULT, GS_SETTINGS };

typedef void (*PFN_glFinish)(void);

static struct {
    int    st;
    Page   page, back_page;
    int    tab;
    int    edit_idx;
    Game   g;
    int    gs;
    int    playlist, pl_pos;
    RunResult last;
    float  best_before; bool new_pb;
    Scroll scroll[8];
    char   p_settings[600], p_scen[600], p_stats[600];
    bool   raw;
    ResMode modes[64]; int nmodes;
    int    sel_mode;          // index into modes (pending selection)
    bool   quit;
    float  fps, ft_ms; double fps_acc; int fps_n;
    bool   msaa_boot;
    float  last_scale;
    PFN_glFinish finish;
    bool   locked;
    bool   was_focused; double refocus_t;
    // demo / self-test
    bool   demo; int frame;
    bool   demo_online;
    int    lb_sel;
    char   dir[600];
    int    demo_scn; int demo_f0; bool demo_lmb; double demo_cool;
} A;

// ---------------------------------------------------------------- helpers
static const char *fmt_int(float v)
{
    static char bufs[4][32]; static int bi = 0;
    char *b = bufs[bi++ & 3];
    long long n = llroundf(v);
    bool neg = n < 0; if (neg) n = -n;
    char tmp[32]; int len = snprintf(tmp, sizeof(tmp), "%lld", n);
    int o = 0; if (neg) b[o++] = '-';
    for (int i = 0; i < len; i++) {
        b[o++] = tmp[i];
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0) b[o++] = ' ';
    }
    b[o] = 0;
    return b;
}

static const char *plural_targets(int n)
{
    int m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return "цель";
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return "цели";
    return "целей";
}

static void scen_tags(const Scenario *s, char *out, int n)
{
    const char *mv = s->move == MV_STRAFE ? " · стрейф" : s->move == MV_BOUNCE ? " · движение" :
                     s->move == MV_DYNAMIC ? " · динамика" : "";
    snprintf(out, (size_t)n, "%.0f с · %d %s · %s%s%s", s->duration, s->count, plural_targets(s->count),
             s->shoot == SM_TRACK ? "трекинг" : "клик", mv, s->lifetime > 0 ? " · на время" : "");
}

static void start_scenario(int idx)
{
    if (idx < 0) return;
    game_start(&A.g, idx, S.countdown && !A.demo ? 3.0f : 0.0f, 0);
    A.st = ST_GAME;
    A.gs = GS_PLAY;
}

static void finish_run(void)
{
    game_result(&A.g, &A.last);
    int runs = 0;
    A.best_before = stats_best(A.last.id, &runs);
    A.new_pb = runs > 0 && A.last.score > A.best_before;
    stats_add(A.p_stats, &A.last);
    online_submit(&A.g, &A.last, A.demo);
    A.gs = GS_RESULT;
}

static void pause_game(void)
{
    A.gs = GS_PAUSE;
    A.g.lmb = false;          // a held button's release may never arrive while unfocused
}

static void resume_game(void)
{
    A.gs = GS_PLAY;
    A.g.lmb = false;
    plat_flush();             // never apply motion that happened while paused
}

static void go_menu(void)
{
    A.st = ST_MENU;
    A.playlist = -1;
}

static void set_lock(bool lock)
{
    if (lock == A.locked) return;
    A.locked = lock;
    if (A.raw) plat_lock_mouse(lock);
    else if (!A.demo) { if (lock) DisableCursor(); else EnableCursor(); }
}

// ---------------------------------------------------------------- shared widgets
static void tile(Rectangle r, const char *label, const char *value, Color vc)
{
    rrect(r, sc(10), C_CARD);
    txt(label, r.x + sc(18), r.y + sc(14), sc(14), C_DIM, F_SEMI);
    txt(value, r.x + sc(18), r.y + sc(36), sc(30), vc, F_MONO);
}

static bool picker(Rectangle r, const char *label, const char *value, int *idx, int n)
{
    float lw = r.width * 0.40f;
    txt(label, r.x, r.y + (r.height - sc(17)) * 0.5f, sc(17), C_TXT, F_REG);
    Rectangle a = { r.x + lw, r.y + (r.height - sc(36)) * 0.5f, r.width - lw, sc(36) };
    bool ch = false;
    Rectangle bl = { a.x, a.y, sc(40), a.height }, br = { a.x + a.width - sc(40), a.y, sc(40), a.height };
    if (ui_button(bl, "<", BTN_SECONDARY) && *idx > 0) { (*idx)--; ch = true; }
    if (ui_button(br, ">", BTN_SECONDARY) && *idx < n - 1) { (*idx)++; ch = true; }
    Rectangle mid = { bl.x + bl.width + sc(6), a.y, a.width - sc(92), a.height };
    rrect(mid, sc(6), (Color){ 16, 19, 26, 255 });
    rrect_line(mid, sc(6), 1, C_LINE);
    txt_c(value, mid, sc(16), C_TXT, F_SEMI);
    return ch;
}

static void section(float x, float y, const char *t)
{
    // uppercase (ASCII + Cyrillic) for small caps-style section labels
    char up[160]; int o = 0;
    for (const unsigned char *p = (const unsigned char *)t; *p && o < 156; ) {
        if (*p < 0x80) { up[o++] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p); p++; continue; }
        if ((*p & 0xE0) != 0xC0) { up[o++] = (char)*p++; continue; }
        int cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
        else if (cp == 0x451) cp = 0x401;
        up[o++] = (char)(0xC0 | (cp >> 6)); up[o++] = (char)(0x80 | (cp & 0x3F));
        p += 2;
    }
    up[o] = 0;
    txt(up, x, y, sc(13), C_FAINT, F_BOLD);
}

// ---------------------------------------------------------------- sidebar
static void draw_sidebar(float W, float H)
{
    float sw = sc(268);
    DrawRectangleRec((Rectangle){ 0, 0, sw, H }, C_SIDE);
    DrawRectangleRec((Rectangle){ sw - 1, 0, 1, H }, C_LINE);

    // logo: target + crosshair mark
    float lx = sc(30), ly = sc(36);
    DrawCircle((int)(lx + sc(16)), (int)(ly + sc(16)), sc(15), C_BAD);
    DrawCircle((int)(lx + sc(16)), (int)(ly + sc(16)), sc(9), C_SIDE);
    DrawCircle((int)(lx + sc(16)), (int)(ly + sc(16)), sc(4), C_ACC);
    txt("AIM", lx + sc(44), ly + sc(2), sc(26), C_TXT, F_BOLD);
    txt("FORGE", lx + sc(44) + txt_w("AIM", sc(26), F_BOLD) + sc(4), ly + sc(2), sc(26), C_ACC, F_BOLD);

    const char *items[] = { "Сценарии", "Программы", "Рейтинг", "Статистика", "Настройки" };
    Page pages[] = { PAGE_SCEN, PAGE_PROG, PAGE_LB, PAGE_STATS, PAGE_SETTINGS };
    float y = sc(118);
    for (int i = 0; i < 5; i++) {
        bool sel = A.page == pages[i] || (pages[i] == PAGE_SCEN && A.page == PAGE_EDIT);
        if (ui_nav((Rectangle){ sc(16), y, sw - sc(32), sc(48) }, items[i], sel)) {
            if (A.page == PAGE_SETTINGS && pages[i] != PAGE_SETTINGS) settings_save(A.p_settings);
            if (A.page == PAGE_EDIT) scenarios_save(A.p_scen);
            A.page = pages[i];
        }
        y += sc(54);
    }

    // sensitivity summary
    float by = H - sc(232);
    Rectangle card = { sc(16), by, sw - sc(32), sc(150) };
    rrect(card, sc(10), C_CARD);
    txt("ЧУВСТВИТЕЛЬНОСТЬ", card.x + sc(16), card.y + sc(14), sc(12), C_FAINT, F_BOLD);
    char b[96];
    if (SENS_GAMES[S.sens_game].yaw > 0)
        snprintf(b, sizeof(b), "%s %.3g", SENS_GAMES[S.sens_game].name, S.sens);
    else snprintf(b, sizeof(b), "Напрямую");
    txt(b, card.x + sc(16), card.y + sc(36), sc(16), C_TXT, F_SEMI);
    snprintf(b, sizeof(b), "%.1f см/360", settings_cm360(&S));
    txt(b, card.x + sc(16), card.y + sc(62), sc(24), C_ACC, F_MONO);
    snprintf(b, sizeof(b), "%d DPI · FOV %.0f°", S.dpi, S.fov);
    txt(b, card.x + sc(16), card.y + sc(96), sc(14), C_DIM, F_REG);
    txt(plat_info(), card.x + sc(16), card.y + sc(118), sc(12), A.raw ? C_GOOD : C_GOLD, F_SEMI);

    if (ui_button((Rectangle){ sc(16), H - sc(70), sw - sc(32), sc(46) }, "Выход", BTN_GHOST)) A.quit = true;
    (void)W;
}

// ---------------------------------------------------------------- pages
static void page_header(float x, float y, const char *title, const char *sub)
{
    txt(title, x, y, sc(36), C_TXT, F_BOLD);
    if (sub) txt(sub, x, y + sc(50), sc(16), C_DIM, F_REG);
}

static void draw_scenarios(Rectangle area)
{
    Scroll *s = &A.scroll[0];
    ui_scroll(s, area);
    ui_clip(area);
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36) - s->y;
    page_header(x, y, "Сценарии", "Клик по карточке — старт. «Параметры» — размер, скорость, зона и время для любого сценария.");
    y += sc(104);

    int cols = w > sc(1350) ? 3 : 2;
    float gap = sc(18);
    float cw = (w - gap * (cols - 1)) / cols, ch = sc(196);

    for (int cat = 0; cat < CAT_COUNT; cat++) {
        int cnt = 0;
        for (int i = 0; i < SCN_COUNT; i++) if (SCN[i].category == cat) cnt++;
        if (!cnt) continue;
        section(x, y, CATEGORY_NAMES[cat]);
        y += sc(26);
        int k = 0;
        for (int i = 0; i < SCN_COUNT; i++) {
            if (SCN[i].category != cat) continue;
            Scenario *sn = &SCN[i];
            Rectangle r = { x + (k % cols) * (cw + gap), y + (k / cols) * (ch + gap), cw, ch };
            bool hov = ui_hover(r);
            rrect(r, sc(12), hov ? C_CARD_HI : C_CARD);
            if (hov) rrect_line(r, sc(12), 1, C_LINE);

            txt(sn->name, r.x + sc(22), r.y + sc(20), sc(21), C_TXT, F_SEMI);
            char tags[128]; scen_tags(sn, tags, sizeof(tags));
            txt(tags, r.x + sc(22), r.y + sc(52), sc(14), C_ACC, F_SEMI);
            if (scenario_is_modified(i)) {
                float tw = txt_w("изменён", sc(12), F_BOLD);
                Rectangle bdg = { r.x + r.width - tw - sc(40), r.y + sc(22), tw + sc(18), sc(22) };
                rrect(bdg, sc(6), (Color){ 60, 46, 18, 255 });
                txt_c("изменён", bdg, sc(12), C_GOLD, F_BOLD);
            }
            txt_wrap(sn->desc, (Rectangle){ r.x + sc(22), r.y + sc(80), r.width - sc(44), sc(60) }, sc(15), C_DIM, F_REG, 2, true);

            int runs = 0; float best = stats_best(sn->id, &runs);
            char pb[64];
            if (runs > 0) snprintf(pb, sizeof(pb), "Рекорд  %s", fmt_int(best));
            else snprintf(pb, sizeof(pb), "Ещё не играли");
            txt(pb, r.x + sc(22), r.y + r.height - sc(42), sc(16), runs > 0 ? C_GOLD : C_FAINT, runs > 0 ? F_MONO : F_REG);
            int wr, wt;
            if (online_my_rank(i, &wr, &wt)) {
                char wb[48]; snprintf(wb, sizeof(wb), "Мир #%d из %d", wr, wt);
                txt(wb, r.x + sc(22), r.y + r.height - sc(64), sc(13), C_ACC, F_SEMI);
            }

            Rectangle bp = { r.x + r.width - sc(22) - sc(112), r.y + r.height - sc(54), sc(112), sc(38) };
            Rectangle be = { bp.x - sc(10) - sc(124), bp.y, sc(124), sc(38) };
            bool used = false;
            if (ui_button(be, "Параметры", BTN_SECONDARY)) { A.edit_idx = i; A.page = PAGE_EDIT; A.scroll[4] = (Scroll){ 0 }; used = true; }
            if (ui_button(bp, "Играть", BTN_PRIMARY)) { A.playlist = -1; start_scenario(i); used = true; }
            bool on_btn = CheckCollisionPointRec(ui.m, bp) || CheckCollisionPointRec(ui.m, be);
            if (hov && !on_btn) ui.want_hand = true;
            if (!used && hov && !on_btn && ui.released) { A.playlist = -1; start_scenario(i); }
            k++;
        }
        y += ((cnt + cols - 1) / cols) * (ch + gap) + sc(22);
    }
    s->content = y + s->y - area.y + sc(20);
    ui_unclip();
    ui_scrollbar(s, area);
}

static void draw_programs(Rectangle area)
{
    Scroll *s = &A.scroll[1];
    ui_scroll(s, area);
    ui_clip(area);
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36) - s->y;
    page_header(x, y, "Программы", "Сценарии подряд по целям тренировки. После каждого — результат и переход к следующему.");
    y += sc(110);
    for (int p = 0; p < PLAYLIST_COUNT; p++) {
        const Playlist *pl = &PLAYLISTS[p];
        Rectangle r = { x, y, w, sc(150) };
        bool hov = ui_hover(r);
        rrect(r, sc(12), hov ? C_CARD_HI : C_CARD);
        txt(pl->name, r.x + sc(24), r.y + sc(20), sc(22), C_TXT, F_SEMI);
        float tot = 0;
        for (int i = 0; i < pl->n; i++) { int si = scenario_find(pl->ids[i]); if (si >= 0) tot += SCN[si].duration; }
        char b[96]; snprintf(b, sizeof(b), "%d сценария · ≈ %.0f мин", pl->n, ceilf(tot / 60.0f));
        txt(b, r.x + sc(24) + txt_w(pl->name, sc(22), F_SEMI) + sc(16), r.y + sc(26), sc(14), C_ACC, F_SEMI);
        txt(pl->desc, r.x + sc(24), r.y + sc(56), sc(15), C_DIM, F_REG);
        float cx = r.x + sc(24);
        for (int i = 0; i < pl->n; i++) {
            int si = scenario_find(pl->ids[i]);
            const char *nm = si >= 0 ? SCN[si].name : pl->ids[i];
            char lab[80]; snprintf(lab, sizeof(lab), "%d. %s", i + 1, nm);
            float tw = txt_w(lab, sc(14), F_SEMI) + sc(24);
            Rectangle chip = { cx, r.y + sc(96), tw, sc(32) };
            rrect(chip, sc(8), (Color){ 16, 19, 26, 255 });
            txt_c(lab, chip, sc(14), C_TXT, F_SEMI);
            cx += tw + sc(10);
        }
        if (ui_button((Rectangle){ r.x + r.width - sc(170), r.y + sc(22), sc(146), sc(42) }, "Начать", BTN_PRIMARY)) {
            A.playlist = p; A.pl_pos = 0;
            start_scenario(scenario_find(pl->ids[0]));
        }
        y += sc(166);
    }
    s->content = y + s->y - area.y + sc(20);
    ui_unclip();
    ui_scrollbar(s, area);
}

static void sparkline(Rectangle r, const RunResult *h, int n, Color c)
{
    if (n < 2) { txt("—", r.x, r.y + (r.height - sc(15)) * 0.5f, sc(15), C_FAINT, F_REG); return; }
    float mn = h[0].score, mx = h[0].score;
    for (int i = 1; i < n; i++) { if (h[i].score < mn) mn = h[i].score; if (h[i].score > mx) mx = h[i].score; }
    if (mx - mn < 1) { mx += 1; mn -= 1; }
    Vector2 prev = { 0 };
    for (int i = 0; i < n; i++) {
        Vector2 p = { r.x + r.width * i / (n - 1), r.y + r.height - (h[i].score - mn) / (mx - mn) * r.height };
        if (i) DrawLineEx(prev, p, sc(2), c);
        prev = p;
    }
    DrawCircleV(prev, sc(3.5f), c);
}

static void draw_stats(Rectangle area)
{
    Scroll *s = &A.scroll[2];
    ui_scroll(s, area);
    ui_clip(area);
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36) - s->y;
    page_header(x, y, "Статистика", "Рекорды и динамика по каждому сценарию. История хранится в aimforge_scores.csv рядом с программой.");
    y += sc(108);

    int total = stats_total_runs(), played = 0; float minutes = 0;
    for (int i = 0; i < SCN_COUNT; i++) { int r = 0; stats_best(SCN[i].id, &r); if (r) played++; minutes += r * SCN[i].duration / 60.0f; }
    float tw = (w - sc(36)) / 3;
    char b[64];
    snprintf(b, sizeof(b), "%d", total);          tile((Rectangle){ x, y, tw, sc(88) }, "ВСЕГО ЗАХОДОВ", b, C_TXT);
    snprintf(b, sizeof(b), "%d / %d", played, SCN_COUNT); tile((Rectangle){ x + tw + sc(18), y, tw, sc(88) }, "СЦЕНАРИЕВ ОСВОЕНО", b, C_TXT);
    snprintf(b, sizeof(b), "%.0f мин", minutes);   tile((Rectangle){ x + 2 * (tw + sc(18)), y, tw, sc(88) }, "ВРЕМЯ ТРЕНИРОВОК", b, C_ACC);
    y += sc(112);

    float cols[] = { 0, 0.34f, 0.44f, 0.56f, 0.68f, 0.80f };
    const char *hd[] = { "СЦЕНАРИЙ", "ЗАХОДОВ", "РЕКОРД", "ПОСЛЕДНИЙ", "СРЕДНЕЕ (5)", "ТРЕНД" };
    for (int i = 0; i < 6; i++) txt(hd[i], x + w * cols[i] + sc(16), y, sc(12), C_FAINT, F_BOLD);
    y += sc(28);
    for (int i = 0; i < SCN_COUNT; i++) {
        Rectangle r = { x, y, w, sc(52) };
        rrect(r, sc(8), (i & 1) ? C_BG : C_CARD);
        RunResult h[20]; int n = stats_history(SCN[i].id, h, 20);
        int runs = 0; float best = stats_best(SCN[i].id, &runs);
        float ty = r.y + (r.height - sc(16)) * 0.5f;
        txt(SCN[i].name, r.x + sc(16), ty, sc(16), C_TXT, F_SEMI);
        snprintf(b, sizeof(b), "%d", runs); txt(b, x + w * cols[1] + sc(16), ty, sc(16), C_DIM, F_MONO);
        if (runs) {
            txt(fmt_int(best), x + w * cols[2] + sc(16), ty, sc(16), C_GOLD, F_MONO);
            txt(fmt_int(h[n - 1].score), x + w * cols[3] + sc(16), ty, sc(16), C_TXT, F_MONO);
            float avg = 0; int m = n < 5 ? n : 5;
            for (int k = n - m; k < n; k++) avg += h[k].score;
            txt(fmt_int(avg / m), x + w * cols[4] + sc(16), ty, sc(16), C_DIM, F_MONO);
            sparkline((Rectangle){ x + w * cols[5] + sc(16), r.y + sc(12), w * 0.18f, r.height - sc(24) }, h, n, C_ACC);
        } else {
            txt("—", x + w * cols[2] + sc(16), ty, sc(16), C_FAINT, F_REG);
        }
        y += sc(56);
    }
    s->content = y + s->y - area.y + sc(30);
    ui_unclip();
    ui_scrollbar(s, area);
}

// ---------------------------------------------------------------- settings
static const int FPS_CAPS[] = { 0, 1000, 500, 360, 300, 240, 165, 144, 120, 60 };
#define NCAPS ((int)(sizeof(FPS_CAPS) / sizeof(FPS_CAPS[0])))

static void refresh_modes(void)
{
    A.nmodes = display_modes(A.modes, 64);
    A.sel_mode = 0;
    for (int i = 0; i < A.nmodes; i++) if (A.modes[i].w == S.res_w && A.modes[i].h == S.res_h) A.sel_mode = i;
}

static void info_line(float x, float *y, float w, const char *k, const char *v, Color vc)
{
    txt(k, x, *y, sc(15), C_DIM, F_REG);
    txt_r(v, x + w, *y, sc(15), vc, F_SEMI);
    *y += sc(30);
}

static void tab_mouse(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    const char *games[8]; for (int i = 0; i < SENS_GAME_COUNT; i++) games[i] = SENS_GAMES[i].name;
    int gsel = S.sens_game;
    if (ui_segment((Rectangle){ x, y, w, rh }, "Профиль игры", games, SENS_GAME_COUNT, &gsel)) settings_set_game_keep_cm(&S, gsel);
    y += rh;
    ui_hint(x + w * 0.40f, y - sc(4), w * 0.60f, "При смене профиля сохраняется тот же см/360 — значение пересчитывается.");
    y += sc(26);
    bool cm = SENS_GAMES[S.sens_game].yaw <= 0;
    double yaw = SENS_GAMES[S.sens_game].yaw;
    float smax = cm ? 200.0f : (float)(10.0 * 0.022 / yaw * 1.0);
    if (!cm && yaw >= 0.07) smax = 5.0f;
    if (!cm && yaw < 0.01) smax = 100.0f;
    ui_slider_f((Rectangle){ x, y, w, rh }, cm ? "Сантиметров на 360°" : "Чувствительность", &S.sens,
                cm ? 2.0f : 0.001f, smax, cm ? 0.1f : 0.001f, cm ? "%.1f" : "%.3f");
    y += rh;
    ui_slider_i((Rectangle){ x, y, w, rh }, "DPI мыши", &S.dpi, 100, 32000, 50);
    y += rh;
    ui_slider_f((Rectangle){ x, y, w, rh }, "Множитель по вертикали", &S.vert_mult, 0.2f, 2.0f, 0.01f, "%.2f");
    y += rh;
    ui_slider_f((Rectangle){ x, y, w, rh }, "Поле зрения (FOV, по горизонтали)", &S.fov, 60, 130, 1, "%.0f°");
    y += rh;
    ui_hint(x, y, w, "Чтобы ощущения совпадали с игрой, ставь FOV как в игре: Valorant — 103°, CS2 — 106° (16:9), Overwatch 2 — 103° (вертикальный 70° по умолчанию в OW соответствует ≈103° по горизонтали).");

    // right: computed values
    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), pw = pv.width - sc(52), py = pv.y + sc(24);
    txt("ПЕРЕСЧЁТ", px, py, sc(12), C_FAINT, F_BOLD); py += sc(30);
    char b[64];
    double cm360 = settings_cm360(&S), dpc = settings_deg_per_count(&S);
    snprintf(b, sizeof(b), "%.2f", cm360);
    txt(b, px, py, sc(56), C_ACC, F_MONO);
    txt("см на 360°", px + txt_w(b, sc(56), F_MONO) + sc(12), py + sc(30), sc(16), C_DIM, F_SEMI);
    py += sc(86);
    snprintf(b, sizeof(b), "%.2f in", cm360 / 2.54);           info_line(px, &py, pw, "Дюймов на 360°", b, C_TXT);
    snprintf(b, sizeof(b), "%.0f", 360.0 / dpc);               info_line(px, &py, pw, "Отсчётов мыши на 360°", b, C_TXT);
    snprintf(b, sizeof(b), "%.5f°", dpc);                      info_line(px, &py, pw, "Градусов на отсчёт", b, C_TXT);
    if (!cm) { snprintf(b, sizeof(b), "%.0f", S.sens * S.dpi); info_line(px, &py, pw, "eDPI", b, C_TXT); }
    for (int i = 0; i < SENS_GAME_COUNT; i++) {
        if (SENS_GAMES[i].yaw <= 0 || i == S.sens_game) continue;
        double sv = 360.0 * 2.54 / (cm360 * SENS_GAMES[i].yaw * S.dpi);
        char k[64]; snprintf(k, sizeof(k), "То же в %s", SENS_GAMES[i].name);
        snprintf(b, sizeof(b), "%.3f", sv); info_line(px, &py, pw, k, b, C_DIM);
    }
    py += sc(12);
    DrawRectangleRec((Rectangle){ px, py, pw, 1 }, C_LINE); py += sc(18);
    txt("ВВОД", px, py, sc(12), C_FAINT, F_BOLD); py += sc(26);
    txt_wrap(A.raw ? "Raw Input (WM_INPUT): сырые отсчёты сенсора, без ускорения и масштабирования Windows. Каждое событие применяется в порядке поступления — выстрел учитывает поворот, сделанный в том же кадре до клика."
                   : "Тестовая сборка: мышь через raylib.",
             (Rectangle){ px, py, pw, sc(200) }, sc(15), C_DIM, F_REG, 6, true);
}

static void tab_crosshair(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    const char *st[] = { "Крест", "Крест+точка", "Точка", "Круг", "T" };
    ui_segment((Rectangle){ x, y, w, rh }, "Стиль", st, CH_COUNT, &S.ch_style); y += rh;
    ui_color((Rectangle){ x, y, w, rh * 2 }, "Цвет", &S.ch_color); y += rh * 2;
    float a = S.ch_color.a / 255.0f;
    if (ui_slider_f((Rectangle){ x, y, w, rh }, "Непрозрачность", &a, 0.1f, 1.0f, 0.01f, "%.2f")) S.ch_color.a = (unsigned char)(a * 255);
    y += rh;
    if (S.ch_style != CH_DOT && S.ch_style != CH_CIRCLE) {
        ui_slider_f((Rectangle){ x, y, w, rh }, "Длина линий, px", &S.ch_len, 0, 30, 1, "%.0f"); y += rh;
        ui_slider_f((Rectangle){ x, y, w, rh }, "Отступ от центра, px", &S.ch_gap, 0, 20, 1, "%.0f"); y += rh;
    }
    if (S.ch_style == CH_CIRCLE) { ui_slider_f((Rectangle){ x, y, w, rh }, "Радиус круга, px", &S.ch_circle, 2, 40, 1, "%.0f"); y += rh; }
    if (S.ch_style != CH_DOT) { ui_slider_f((Rectangle){ x, y, w, rh }, "Толщина, px", &S.ch_thick, 1, 10, 1, "%.0f"); y += rh; }
    if (S.ch_style == CH_DOT || S.ch_style == CH_CROSS_DOT || S.ch_style == CH_CIRCLE) {
        ui_slider_f((Rectangle){ x, y, w, rh }, "Размер точки, px", &S.ch_dot, S.ch_style == CH_CIRCLE ? 0 : 1, 10, 1, "%.0f"); y += rh;
    }
    ui_toggle((Rectangle){ x, y, w, rh }, "Обводка", &S.ch_outline); y += rh;
    if (S.ch_outline) {
        ui_slider_f((Rectangle){ x, y, w, rh }, "Толщина обводки, px", &S.ch_out_thick, 1, 4, 1, "%.0f"); y += rh;
        ui_slider_f((Rectangle){ x, y, w, rh }, "Непрозрачность обводки", &S.ch_out_alpha, 0, 1, 0.01f, "%.2f"); y += rh;
    }

    // preview: 1:1 on three backgrounds + magnified view
    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), pw = pv.width - sc(52), py = pv.y + sc(24);
    txt("ПРЕВЬЮ 1:1", px, py, sc(12), C_FAINT, F_BOLD); py += sc(28);
    Color bgs[3] = { THEMES[S.theme].wall, (Color){ 210, 205, 195, 255 }, S.tg_color };
    float bw = (pw - sc(20)) / 3, bh = sc(130);
    for (int i = 0; i < 3; i++) {
        Rectangle b = { px + i * (bw + sc(10)), py, bw, bh };
        rrect(b, sc(8), bgs[i]);
        BeginScissorMode((int)b.x, (int)b.y, (int)b.width, (int)b.height);
        crosshair_draw_at(0, 0, 1, (int)(b.x + b.width / 2), (int)(b.y + b.height / 2));
        EndScissorMode();
    }
    py += bh + sc(26);
    txt("УВЕЛИЧЕНИЕ ×8 (сетка = 1 пиксель)", px, py, sc(12), C_FAINT, F_BOLD); py += sc(28);
    Rectangle zb = { px, py, pw, pv.y + pv.height - py - sc(26) };
    rrect(zb, sc(8), THEMES[S.theme].wall);
    BeginScissorMode((int)zb.x, (int)zb.y, (int)zb.width, (int)zb.height);
    float z = 8;
    int cells_x = (int)(zb.width / z), cells_y = (int)(zb.height / z);
    float ox = zb.x + (zb.width - cells_x * z) / 2, oy = zb.y + (zb.height - cells_y * z) / 2;
    for (int i = 0; i <= cells_x; i++) DrawRectangle((int)(ox + i * z), (int)zb.y, 1, (int)zb.height, fade(BLACK, 0.12f));
    for (int j = 0; j <= cells_y; j++) DrawRectangle((int)zb.x, (int)(oy + j * z), (int)zb.width, 1, fade(BLACK, 0.12f));
    crosshair_draw_at(ox, oy, z, cells_x / 2, cells_y / 2);
    EndScissorMode();
}

static void tab_targets(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    ui_color((Rectangle){ x, y, w, rh * 2 }, "Цвет целей", &S.tg_color); y += rh * 2;
    ui_color((Rectangle){ x, y, w, rh * 2 }, "Цвет при трекинге", &S.tg_track_color); y += rh * 2;
    ui_slider_f((Rectangle){ x, y, w, rh }, "Размер (множитель)", &S.tg_size_mult, 0.5f, 2.0f, 0.05f, "%.2f×"); y += rh;
    const char *st[] = { "Объёмные", "Плоские", "С обводкой" };
    ui_segment((Rectangle){ x, y, w, rh }, "Вид шариков", st, TS_COUNT, &S.tg_style); y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Эффекты попадания", &S.tg_hit_fx); y += rh;
    y += sc(10);
    section(x, y, "ОКРУЖЕНИЕ"); y += sc(18);
    ui_segment((Rectangle){ x, y, w, rh }, "Тема комнаты", THEME_NAMES, TH_COUNT, &S.theme); y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Сетка на стенах", &S.grid); y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Показывать зону появления", &S.show_zone); y += rh;
    ui_hint(x, y, w, "Размер-множитель применяется ко всем сценариям сверху их собственных параметров. Отдельные размеры — в «Параметрах» сценария.");

    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), py = pv.y + sc(24);
    txt("ПРЕВЬЮ", px, py, sc(12), C_FAINT, F_BOLD);
    Rectangle r = { px, py + sc(28), pv.width - sc(52), pv.height - sc(80) };
    render_target_preview(r);
    txt("слева — обычная цель, справа — цель под трекингом", px, r.y + r.height + sc(10), sc(13), C_FAINT, F_REG);
}

static void tab_display(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    const char *dm[] = { "Окно", "Без рамки", "Полный экран" };
    ui_segment((Rectangle){ x, y, w, rh }, "Режим", dm, 3, &S.display_mode); y += rh;
    if (A.nmodes == 0) refresh_modes();
    if (S.display_mode != DM_BORDERLESS && A.nmodes > 0) {
        char rv[48]; snprintf(rv, sizeof(rv), "%d × %d", A.modes[A.sel_mode].w, A.modes[A.sel_mode].h);
        if (picker((Rectangle){ x, y, w, rh }, "Разрешение", rv, &A.sel_mode, A.nmodes)) {
            S.res_w = A.modes[A.sel_mode].w; S.res_h = A.modes[A.sel_mode].h; S.refresh = 0;
        }
        y += rh;
        if (S.display_mode == DM_FULLSCREEN) {
            ResMode *m = &A.modes[A.sel_mode];
            int ri = 0; // 0 = max
            for (int i = 0; i < m->nr; i++) if (m->rates[i] == S.refresh) ri = i + 1;
            char hv[48];
            if (ri == 0) snprintf(hv, sizeof(hv), "Максимум (%d Гц)", m->nr ? m->rates[0] : 0);
            else snprintf(hv, sizeof(hv), "%d Гц", S.refresh);
            if (picker((Rectangle){ x, y, w, rh }, "Частота обновления", hv, &ri, m->nr + 1))
                S.refresh = ri == 0 ? 0 : m->rates[ri - 1];
            y += rh;
        }
    }
    if (ui_button((Rectangle){ x + w * 0.40f, y + sc(8), sc(220), sc(42) }, "Применить режим", BTN_PRIMARY)) {
        display_apply();
        settings_save(A.p_settings);
    }
    y += rh + sc(10);
    int ci = 0; for (int i = 0; i < NCAPS; i++) if (FPS_CAPS[i] == S.fps_cap) ci = i;
    char cv[48];
    if (ci == 0) snprintf(cv, sizeof(cv), "Без ограничения"); else snprintf(cv, sizeof(cv), "%d FPS", FPS_CAPS[ci]);
    if (picker((Rectangle){ x, y, w, rh }, "Лимит FPS", cv, &ci, NCAPS)) S.fps_cap = FPS_CAPS[ci];
    y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Режим низкой задержки", &S.low_latency); y += rh;
    ui_hint(x, y - sc(6), w, "glFinish после кадра: драйвер не копит очередь кадров, картинка отстаёт от мыши минимально. Стоит немного FPS.");
    y += sc(44);
    ui_toggle((Rectangle){ x, y, w, rh }, "Сглаживание MSAA 4×", &S.msaa); y += rh;
    if (S.msaa != A.msaa_boot) { txt("Применится после перезапуска программы", x, y - sc(6), sc(14), C_GOLD, F_SEMI); y += sc(22); }
    ui_toggle((Rectangle){ x, y, w, rh }, "Показывать FPS", &S.show_fps); y += rh;

    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), pw = pv.width - sc(52), py = pv.y + sc(24);
    txt("СЕЙЧАС", px, py, sc(12), C_FAINT, F_BOLD); py += sc(30);
    char b[96];
    snprintf(b, sizeof(b), "%.0f", A.fps);
    txt(b, px, py, sc(56), C_ACC, F_MONO);
    txt("FPS", px + txt_w(b, sc(56), F_MONO) + sc(12), py + sc(30), sc(16), C_DIM, F_SEMI);
    py += sc(86);
    snprintf(b, sizeof(b), "%.2f мс", A.ft_ms);                info_line(px, &py, pw, "Время кадра", b, C_TXT);
    snprintf(b, sizeof(b), "%d × %d", GetScreenWidth(), GetScreenHeight()); info_line(px, &py, pw, "Окно рендера", b, C_TXT);
    snprintf(b, sizeof(b), "%d Гц", display_current_refresh()); info_line(px, &py, pw, "Монитор", b, C_TXT);
    info_line(px, &py, pw, "VSync", "выключен всегда", C_GOOD);
    info_line(px, &py, pw, "Ввод", A.raw ? "Raw Input" : "raylib", A.raw ? C_GOOD : C_GOLD);
    py += sc(8);
    txt_wrap(display_info(), (Rectangle){ px, py, pw, sc(60) }, sc(14), C_DIM, F_REG, 2, true);
    py += sc(50);
    DrawRectangleRec((Rectangle){ px, py, pw, 1 }, C_LINE); py += sc(18);
    txt("СОВЕТЫ ПО ЗАДЕРЖКЕ", px, py, sc(12), C_FAINT, F_BOLD); py += sc(26);
    txt_wrap("Полный экран с родной частотой монитора даёт минимальную задержку вывода. Лимит FPS ставь выше частоты (или без ограничения), если GPU не упирается в 100%. Для 1440p@240 Гц держи FPS заметно выше 240.",
             (Rectangle){ px, py, pw, sc(200) }, sc(15), C_DIM, F_REG, 7, true);
}

static void tab_misc(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    ui_toggle((Rectangle){ x, y, w, rh }, "Звук попадания", &S.snd_hit); y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Звук промаха", &S.snd_miss); y += rh;
    if (ui_slider_f((Rectangle){ x, y, w, rh }, "Громкость", &S.volume, 0, 1, 0.01f, "%.2f")) sfx_volume(S.volume);
    y += rh + sc(10);
    section(x, y, "ИГРА"); y += sc(18);
    ui_toggle((Rectangle){ x, y, w, rh }, "Обратный отсчёт 3 с перед стартом", &S.countdown); y += rh;
    ui_toggle((Rectangle){ x, y, w, rh }, "Показывать счёт и таймер", &S.show_hud); y += rh;
    y += sc(20);
    if (ui_button((Rectangle){ x, y, sc(280), sc(44) }, "Сбросить все настройки", BTN_DANGER)) {
        bool m = A.msaa_boot; settings_defaults(&S); S.msaa = m; sfx_volume(S.volume); display_apply();
    }

    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), pw = pv.width - sc(52), py = pv.y + sc(24);
    txt("УПРАВЛЕНИЕ", px, py, sc(12), C_FAINT, F_BOLD); py += sc(32);
    const char *keys[][2] = { { "ЛКМ", "выстрел / держать для трекинга" }, { "R", "перезапуск сценария" },
                              { "Esc", "пауза, назад" }, { "Пробел", "ещё раз на экране результата" } };
    for (int i = 0; i < 4; i++) {
        Rectangle k = { px, py, sc(84), sc(32) };
        rrect(k, sc(6), (Color){ 16, 19, 26, 255 }); rrect_line(k, sc(6), 1, C_LINE);
        txt_c(keys[i][0], k, sc(14), C_TXT, F_BOLD);
        txt(keys[i][1], px + sc(100), py + sc(7), sc(15), C_DIM, F_REG);
        py += sc(44);
    }
    py += sc(10);
    DrawRectangleRec((Rectangle){ px, py, pw, 1 }, C_LINE); py += sc(18);
    txt("ФАЙЛЫ", px, py, sc(12), C_FAINT, F_BOLD); py += sc(26);
    txt_wrap("Настройки, параметры сценариев и история результатов сохраняются рядом с AimForge.exe: aimforge_settings.cfg, aimforge_scenarios.cfg, aimforge_scores.csv.",
             (Rectangle){ px, py, pw, sc(200) }, sc(15), C_DIM, F_REG, 5, true);
    py += sc(110);
    txt(APP_NAME " " APP_VER, px, py, sc(14), C_FAINT, F_SEMI);
}

static Color status_color(int st) { return st == NS_OK ? C_GOOD : st == NS_ERROR ? C_BAD : C_DIM; }

static void tab_online(float x, float y, float w, Rectangle pv)
{
    float rh = sc(56);
    ui_toggle((Rectangle){ x, y, w, rh }, "Онлайн-рейтинг", &S.online); y += rh;
    if (ui_text((Rectangle){ x, y, w, rh }, "Ник", S.nick, sizeof(S.nick), 16, "как тебя показывать в таблице", false)) {
        settings_save(A.p_settings); online_test();
    }
    y += rh;
    if (ui_text((Rectangle){ x, y, w, rh }, "Адрес сервера", S.server, sizeof(S.server), 190, "https://….workers.dev", true)) {
        settings_save(A.p_settings); online_test();
    }
    y += rh;
    ui_hint(x + w * 0.40f, y - sc(4), w * 0.60f, "Кликни по полю, введи текст или вставь Ctrl+V, Enter — сохранить.");
    y += sc(30);
    if (ui_button((Rectangle){ x + w * 0.40f, y, sc(260), sc(42) }, "Проверить подключение", BTN_PRIMARY)) online_test();
    y += sc(56);
    if (ONL_TEST.status != NS_NONE)
        txt_wrap(ONL_TEST.msg, (Rectangle){ x + w * 0.40f, y, w * 0.60f, sc(60) }, sc(16), status_color(ONL_TEST.status), F_SEMI, 2, true);
    else if (!online_configured())
        txt_wrap(!S.online ? "Онлайн выключен — результаты остаются только на этом компьютере." :
                 !online_server_url()[0] ? "Нужен адрес сервера — его выдаёт Cloudflare после развёртывания (инструкция справа)." :
                 "Введи ник, чтобы попасть в таблицу.",
                 (Rectangle){ x + w * 0.40f, y, w * 0.60f, sc(60) }, sc(16), C_GOLD, F_SEMI, 2, true);
    y += sc(60);
    section(x, y, "ЧТО ПОПАДАЕТ В РЕЙТИНГ"); y += sc(26);
    txt_wrap("Только сценарии со стандартными параметрами и множителем размера целей 1.0 — так все соревнуются в равных условиях. "
             "Изменённые сценарии и «Свой сценарий» дают только локальный рекорд. Чувствительность, FOV и прицел — любые. "
             "Хранится лучший результат каждого игрока; если интернета нет, результат отправится позже.",
             (Rectangle){ x, y, w, sc(200) }, sc(15), C_DIM, F_REG, 6, true);

    ui_unclip();
    rrect(pv, sc(12), C_CARD);
    float px = pv.x + sc(26), pw = pv.width - sc(52), py = pv.y + sc(24);
    txt("СВОЙ СЕРВЕР БЕСПЛАТНО", px, py, sc(12), C_FAINT, F_BOLD); py += sc(30);
    py += txt_wrap("Рейтинг работает на Cloudflare Workers + D1: бесплатный тариф, без карты, хватает на тысячи игроков. "
                   "Разворачивается один раз за 5 минут (папка server в архиве с исходниками, файл DEPLOY.txt): "
                   "запусти deploy.bat — он сам создаст базу и выдаст адрес.",
                   (Rectangle){ px, py, pw, sc(200) }, sc(15), C_DIM, F_REG, 7, true) + sc(14);
    py += txt_wrap("Дай этот адрес друзьям — все, кто его укажет, попадут в одну таблицу.",
                   (Rectangle){ px, py, pw, sc(80) }, sc(15), C_TXT, F_SEMI, 3, true) + sc(18);
    DrawRectangleRec((Rectangle){ px, py, pw, 1 }, C_LINE); py += sc(18);
    char b[96];
    snprintf(b, sizeof(b), "%.8s…", S.pid);
    info_line(px, &py, pw, "ID игрока", b, C_TXT);
    snprintf(b, sizeof(b), "%d", online_pending_count());
    info_line(px, &py, pw, "Ждут отправки", b, online_pending_count() ? C_GOLD : C_TXT);
    py += sc(8);
    if (online_server_url()[0] && ui_button((Rectangle){ px, py, sc(300), sc(42) }, "Открыть рейтинг в браузере", BTN_SECONDARY)) online_open_web();
    py += sc(56);
    txt_wrap("ID и секретный ключ игрока лежат в aimforge_settings.cfg. Скопируй файл на новый компьютер, чтобы сохранить ник и место в таблице.",
             (Rectangle){ px, py, pw, sc(120) }, sc(13), C_FAINT, F_REG, 4, true);
}

static void fmt_ago(long long at, char *out, int cap)
{
    long long d = (long long)time(NULL) - at;
    if (d < 60) snprintf(out, (size_t)cap, "только что");
    else if (d < 3600) snprintf(out, (size_t)cap, "%lld мин назад", d / 60);
    else if (d < 86400) snprintf(out, (size_t)cap, "%lld ч назад", d / 3600);
    else if (d < 86400 * 7) snprintf(out, (size_t)cap, "%lld дн назад", d / 86400);
    else { time_t t = (time_t)at; struct tm *tm = localtime(&t); if (tm) strftime(out, (size_t)cap, "%d.%m.%Y", tm); else out[0] = 0; }
}

static bool ranked_scn(int i) { return strcmp(SCN[i].id, "custom") != 0; }

static void draw_leaderboard(Rectangle area)
{
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36);
    page_header(x, y, "Рейтинг", "Лучший результат каждого игрока на сервере. В зачёт идут только стандартные параметры сценариев.");
    y += sc(104);

    if (!online_server_url()[0] || !S.online) {
        Rectangle c = { x, y, w, sc(220) };
        rrect(c, sc(12), C_CARD);
        txt(!S.online ? "Онлайн-рейтинг выключен" : "Сервер рейтинга не настроен", c.x + sc(32), c.y + sc(32), sc(24), C_TXT, F_SEMI);
        txt_wrap("Рейтинг бесплатно работает на Cloudflare. Разверни свой сервер за 5 минут (DEPLOY.txt в папке server) или впиши адрес, который дал друг, "
                 "в Настройки → Онлайн и выбери ник.", (Rectangle){ c.x + sc(32), c.y + sc(76), c.width - sc(64), sc(80) }, sc(16), C_DIM, F_REG, 3, true);
        if (ui_button((Rectangle){ c.x + sc(32), c.y + c.height - sc(70), sc(240), sc(44) }, "Открыть настройки", BTN_PRIMARY)) {
            A.page = PAGE_SETTINGS; A.tab = 5;
        }
        return;
    }
    if (A.lb_sel < 0 || A.lb_sel >= SCN_COUNT || !ranked_scn(A.lb_sel)) A.lb_sel = 0;

    // scenario list
    float lw = sc(320);
    Rectangle list = { x, y, lw, area.y + area.height - y - sc(36) };
    rrect(list, sc(12), C_CARD);
    Scroll *ls = &A.scroll[5];
    ui_scroll(ls, list);
    ui_clip(list);
    float ly = list.y + sc(10) - ls->y;
    for (int i = 0; i < SCN_COUNT; i++) {
        if (!ranked_scn(i)) continue;
        Rectangle r = { list.x + sc(8), ly, list.width - sc(16), sc(46) };
        bool sel = A.lb_sel == i, hov = ui_hover(r);
        if (hov) ui.want_hand = true;
        if (sel) rrect(r, sc(8), C_CARD_HI);
        else if (hov) rrect(r, sc(8), fade(C_CARD_HI, 0.6f));
        if (sel) DrawRectangleRec((Rectangle){ r.x, r.y + sc(10), sc(3), r.height - sc(20) }, C_ACC);
        txt(SCN[i].name, r.x + sc(16), r.y + (r.height - sc(16)) * 0.5f, sc(16), sel || hov ? C_TXT : C_DIM, F_SEMI);
        int wr, wt;
        if (online_my_rank(i, &wr, &wt)) {
            char b[24]; snprintf(b, sizeof(b), "#%d", wr);
            txt_r(b, r.x + r.width - sc(12), r.y + (r.height - sc(15)) * 0.5f, sc(15), wr <= 3 ? C_GOLD : C_ACC, F_MONO);
        }
        if (hov && ui.released) { A.lb_sel = i; A.scroll[6] = (Scroll){ 0 }; }
        ly += sc(50);
    }
    ls->content = ly + ls->y - list.y + sc(6);
    ui_unclip();
    ui_scrollbar(ls, list);

    // board
    int si = A.lb_sel;
    online_fetch(si, false);
    online_fetch_summary(false);      // throttled to once a minute
    LbBoard *b = online_board(si);
    float bx = x + lw + sc(24), bw = w - lw - sc(24);
    Rectangle card = { bx, y, bw, list.height };
    rrect(card, sc(12), C_CARD);
    float cx = card.x + sc(28), cw = card.width - sc(56), cy = card.y + sc(24);
    txt(SCN[si].name, cx, cy, sc(26), C_TXT, F_BOLD);
    char sub[160];
    if (b->status == NS_OK || b->total) {
        if (b->my_rank > 0) snprintf(sub, sizeof(sub), "Игроков: %d   ·   Твоё место: #%d  (%s)", b->total, b->my_rank, fmt_int(b->my_best));
        else snprintf(sub, sizeof(sub), "Игроков: %d   ·   Тебя пока нет в таблице", b->total);
    } else snprintf(sub, sizeof(sub), " ");
    txt(sub, cx, cy + sc(40), sc(15), C_DIM, F_SEMI);
    float bxr = cx + cw;
    bxr -= sc(130); if (ui_button((Rectangle){ bxr, cy, sc(130), sc(42) }, "Играть", BTN_PRIMARY)) { A.playlist = -1; start_scenario(si); return; }
    bxr -= sc(142); if (ui_button((Rectangle){ bxr, cy, sc(130), sc(42) }, "Обновить", BTN_SECONDARY)) { online_fetch(si, true); online_fetch_summary(true); }
    bxr -= sc(142); if (ui_button((Rectangle){ bxr, cy, sc(130), sc(42) }, "В браузере", BTN_GHOST)) online_open_web();
    cy += sc(82);

    float col[] = { 0, 0.10f, 0.52f, 0.68f, 0.82f };
    const char *hd[] = { "#", "ИГРОК", "СЧЁТ", "ТОЧНОСТЬ", "КОГДА" };
    for (int i = 0; i < 5; i++) txt(hd[i], cx + cw * col[i] + sc(14), cy, sc(12), C_FAINT, F_BOLD);
    cy += sc(26);
    Rectangle view = { card.x, cy, card.width, card.y + card.height - cy - sc(12) };

    if (b->status == NS_BUSY && b->n == 0) { txt("Загрузка…", cx + sc(14), cy + sc(16), sc(17), C_DIM, F_SEMI); return; }
    if (b->status == NS_ERROR && b->n == 0) {
        char e[200]; snprintf(e, sizeof(e), "Не удалось загрузить: %s", b->err);
        txt_wrap(e, (Rectangle){ cx + sc(14), cy + sc(16), cw - sc(28), sc(60) }, sc(17), C_BAD, F_SEMI, 2, true);
        if (ui_button((Rectangle){ cx + sc(14), cy + sc(80), sc(160), sc(42) }, "Повторить", BTN_SECONDARY)) online_fetch(si, true);
        return;
    }
    if (b->status == NS_OK && b->n == 0) {
        txt("Пока никто не играл этот сценарий — стань первым.", cx + sc(14), cy + sc(16), sc(17), C_DIM, F_SEMI);
        return;
    }
    Scroll *ts = &A.scroll[6];
    ui_scroll(ts, view);
    ui_clip(view);
    float ry = view.y - ts->y;
    bool me_shown = false;
    for (int i = 0; i < b->n; i++) {
        LbRow *row = &b->rows[i];
        Rectangle r = { cx, ry, cw, sc(44) };
        if (row->me) { rrect(r, sc(8), C_ACC_D); me_shown = true; }
        else if (i & 1) rrect(r, sc(8), fade(C_BG, 0.5f));
        float ty = r.y + (r.height - sc(16)) * 0.5f;
        char t[64];
        snprintf(t, sizeof(t), "%d", row->rank);
        Color dim = row->me ? C_TXT : C_DIM, faint = row->me ? (Color){ 190, 225, 235, 255 } : C_FAINT;
        txt(t, cx + cw * col[0] + sc(14), ty, sc(16), row->rank == 1 ? C_GOLD : row->rank <= 3 ? (Color){ 230, 200, 140, 255 } : dim, F_MONO);
        txt(row->name, cx + cw * col[1] + sc(14), ty, sc(16), C_TXT, row->me ? F_BOLD : F_SEMI);
        txt(fmt_int(row->score), cx + cw * col[2] + sc(14), ty, sc(16), row->rank <= 3 ? C_GOLD : C_TXT, F_MONO);
        snprintf(t, sizeof(t), "%.1f%%", row->acc * 100); txt(t, cx + cw * col[3] + sc(14), ty, sc(16), dim, F_MONO);
        fmt_ago(row->at, t, sizeof(t)); txt(t, cx + cw * col[4] + sc(14), ty, sc(15), faint, F_REG);
        ry += sc(48);
    }
    if (!me_shown && b->my_rank > 0) {
        txt("…", cx + sc(14), ry + sc(4), sc(16), C_FAINT, F_BOLD);
        ry += sc(30);
        Rectangle r = { cx, ry, cw, sc(44) };
        rrect(r, sc(8), C_ACC_D);
        float ty = r.y + (r.height - sc(16)) * 0.5f;
        char t[32]; snprintf(t, sizeof(t), "%d", b->my_rank);
        txt(t, cx + cw * col[0] + sc(14), ty, sc(16), C_TXT, F_MONO);
        txt(S.nick, cx + cw * col[1] + sc(14), ty, sc(16), C_TXT, F_BOLD);
        txt(fmt_int(b->my_best), cx + cw * col[2] + sc(14), ty, sc(16), C_TXT, F_MONO);
        ry += sc(48);
    }
    ts->content = ry + ts->y - view.y + sc(8);
    ui_unclip();
    ui_scrollbar(ts, view);
}

static void draw_settings(Rectangle area, bool in_game)
{
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36);
    page_header(x, y, "Настройки", NULL);
    if (in_game) {
        if (ui_button((Rectangle){ x + w - sc(160), y, sc(160), sc(44) }, "Назад", BTN_SECONDARY)) {
            settings_save(A.p_settings); A.gs = GS_PAUSE;
        }
    }
    y += sc(64);
    const char *tabs[] = { "Мышь", "Прицел", "Цели и мир", "Экран", "Звук и игра", "Онлайн" };
    float tx = x;
    for (int i = 0; i < 6; i++) {
        float tw = txt_w(tabs[i], sc(17), F_SEMI) + sc(36);
        if (ui_tab((Rectangle){ tx, y, tw, sc(46) }, tabs[i], A.tab == i)) { A.tab = i; A.scroll[3] = (Scroll){ 0 }; }
        tx += tw + sc(4);
    }
    y += sc(46);
    DrawRectangleRec((Rectangle){ x, y, w, 1 }, C_LINE);
    y += sc(26);

    float lw = w * 0.56f;
    Rectangle pv = { x + lw + sc(40), y, w - lw - sc(40), area.y + area.height - y - sc(36) };
    Rectangle view = { area.x, y - sc(6), x + lw + sc(20) - area.x, area.y + area.height - y };
    Scroll *s = &A.scroll[3];
    ui_scroll(s, view);
    ui_clip(view);
    float fy = y - s->y;
    float content = 0;
    switch (A.tab) {
    case 0: tab_mouse(x, fy, lw, pv); content = sc(560); break;
    case 1: tab_crosshair(x, fy, lw, pv); content = sc(900); break;
    case 2: tab_targets(x, fy, lw, pv); content = sc(820); break;
    case 3: tab_display(x, fy, lw, pv); content = sc(720); break;
    case 4: tab_misc(x, fy, lw, pv); content = sc(520); break;
    default: tab_online(x, fy, lw, pv); content = sc(640); break;
    }
    s->content = content;
    ui_unclip();
    ui_scrollbar(s, view);
}

// ---------------------------------------------------------------- scenario editor
static void draw_editor(Rectangle area)
{
    int idx = A.edit_idx;
    Scenario *sn = &SCN[idx];
    float x = area.x + sc(48), w = area.width - sc(96);
    float y = area.y + sc(36);
    char title[96]; snprintf(title, sizeof(title), "Параметры · %s", sn->name);
    page_header(x, y, title, NULL);
    float bx = x + w;
    bx -= sc(150); if (ui_button((Rectangle){ bx, y, sc(150), sc(44) }, "Играть", BTN_PRIMARY)) { scenarios_save(A.p_scen); A.playlist = -1; start_scenario(idx); }
    bx -= sc(160); if (ui_button((Rectangle){ bx, y, sc(150), sc(44) }, "Сбросить", BTN_SECONDARY)) scenario_reset(idx);
    bx -= sc(160); if (ui_button((Rectangle){ bx, y, sc(150), sc(44) }, "Назад", BTN_GHOST)) { scenarios_save(A.p_scen); A.page = PAGE_SCEN; }
    y += sc(56);
    txt_wrap(sn->desc, (Rectangle){ x, y, w * 0.7f, sc(60) }, sc(16), C_DIM, F_REG, 2, true);
    y += sc(52);
    char info[200];
    float ar = ang_radius_deg(sn->radius * S.tg_size_mult, sn->dist);
    snprintf(info, sizeof(info), "Угловой размер цели: Ø %.2f°   ·   Зона: ±%.1f° × ±%.1f°   ·   Ширина зоны на экране: %.0f%% FOV",
             ar * 2, sn->spawn_yaw, sn->spawn_pitch, fminf(100.f, sn->spawn_yaw * 2 / S.fov * 100));
    Rectangle ib = { x, y, w, sc(44) };
    rrect(ib, sc(8), C_CARD);
    txt(info, ib.x + sc(18), ib.y + (ib.height - sc(15)) * 0.5f, sc(15), C_ACC, F_SEMI);
    y += sc(64);

    Rectangle view = { area.x, y, area.width, area.y + area.height - y };
    Scroll *s = &A.scroll[4];
    ui_scroll(s, view);
    ui_clip(view);
    float rh = sc(54), colw = (w - sc(60)) / 2;
    int per_col = (SC_PARAM_COUNT + 1) / 2;
    for (int i = 0; i < SC_PARAM_COUNT; i++) {
        const ParamDesc *p = &SC_PARAMS[i];
        int col = i / per_col, row = i % per_col;
        Rectangle r = { x + col * (colw + sc(60)), y + row * rh - s->y, colw, rh };
        char *fp = (char *)sn + p->offset;
        switch (p->type) {
        case P_FLOAT: {
            const char *fmt = p->step >= 1 ? "%.0f" : (p->step >= 0.1f ? "%.1f" : "%.2f");
            ui_slider_f(r, p->label, (float *)fp, p->min, p->max, p->step, fmt);
        } break;
        case P_INT: ui_slider_i(r, p->label, (int *)fp, (int)p->min, (int)p->max, (int)p->step); break;
        case P_BOOL: ui_toggle(r, p->label, (bool *)fp); break;
        case P_ENUM: ui_segment(r, p->label, p->opts, p->nopts, (int *)fp); break;
        }
    }
    s->content = per_col * rh + sc(20);
    ui_unclip();
    ui_scrollbar(s, view);
    if (sn->dir_max < sn->dir_min) sn->dir_max = sn->dir_min;
    if (sn->delay_max < sn->delay_min) sn->delay_max = sn->delay_min;
}

// ---------------------------------------------------------------- menu frame
static void menu_frame(void)
{
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    set_lock(false);
    bool esc = IsKeyPressed(KEY_ESCAPE) && !ui_editing();
    ClearBackground(C_BG);
    draw_sidebar(W, H);
    Rectangle area = { sc(268), 0, W - sc(268), H };
    switch (A.page) {
    case PAGE_SCEN: draw_scenarios(area); break;
    case PAGE_PROG: draw_programs(area); break;
    case PAGE_STATS: draw_stats(area); break;
    case PAGE_SETTINGS: draw_settings(area, false); break;
    case PAGE_EDIT: draw_editor(area); break;
    case PAGE_LB: draw_leaderboard(area); break;
    }
    if (esc && A.page != PAGE_SCEN) {
        if (A.page == PAGE_SETTINGS) settings_save(A.p_settings);
        if (A.page == PAGE_EDIT) scenarios_save(A.p_scen);
        A.page = PAGE_SCEN;
    }
}

// ---------------------------------------------------------------- in-game
static void hud(float W, float H)
{
    Game *g = &A.g;
    if (S.show_hud) {
        char b[64];
        float tl = game_time_left(g);
        snprintf(b, sizeof(b), "%d:%02d", (int)tl / 60, (int)tl % 60);
        float bw = sc(150), bh = sc(64);
        Rectangle mid = { W / 2 - bw / 2, sc(18), bw, bh };
        rrect(mid, sc(10), fade(BLACK, 0.45f));
        txt_c(b, (Rectangle){ mid.x, mid.y + sc(4), mid.width, sc(42) }, sc(34), tl <= 5 && g->phase == GP_RUNNING ? C_GOLD : C_TXT, F_MONO);
        DrawRectangleRec((Rectangle){ mid.x + sc(14), mid.y + bh - sc(10), (mid.width - sc(28)) * (tl / g->sc.duration), sc(3) }, C_ACC);

        Rectangle lft = { mid.x - sc(10) - sc(190), sc(18), sc(190), bh };
        rrect(lft, sc(10), fade(BLACK, 0.45f));
        txt("СЧЁТ", lft.x + sc(16), lft.y + sc(10), sc(12), C_DIM, F_BOLD);
        txt(fmt_int(game_score(g)), lft.x + sc(16), lft.y + sc(26), sc(26), C_TXT, F_MONO);

        Rectangle rgt = { mid.x + bw + sc(10), sc(18), sc(190), bh };
        rrect(rgt, sc(10), fade(BLACK, 0.45f));
        txt(g->sc.shoot == SM_TRACK ? "НА ЦЕЛИ" : "ТОЧНОСТЬ", rgt.x + sc(16), rgt.y + sc(10), sc(12), C_DIM, F_BOLD);
        snprintf(b, sizeof(b), "%.1f%%", game_accuracy(g) * 100);
        txt(b, rgt.x + sc(16), rgt.y + sc(26), sc(26), C_TXT, F_MONO);
        if (g->sc.shoot == SM_CLICK || g->sc.hp > 0) {
            snprintf(b, sizeof(b), "%d", g->kills);
            txt_r(b, rgt.x + rgt.width - sc(16), rgt.y + sc(30), sc(20), C_ACC, F_MONO);
        }
    }
    if (S.show_fps) {
        char b[48]; snprintf(b, sizeof(b), "%.0f FPS  %.2f мс", A.fps, A.ft_ms);
        txt_r(b, W - sc(18), sc(14), sc(14), fade(C_TXT, 0.75f), F_MONO);
    }
    if (g->phase == GP_COUNTDOWN) {
        int n = (int)ceilf(g->countdown - (float)g->phase_t);
        char b[8]; snprintf(b, sizeof(b), "%d", n);
        float k = (float)fmod(g->phase_t, 1.0);
        txt_c(b, (Rectangle){ 0, H * 0.30f - sc(60), W, sc(120) }, sc(110) * (1.15f - 0.15f * k), fade(C_TXT, 1.0f - k * 0.6f), F_BOLD);
        txt_c(g->sc.name, (Rectangle){ 0, H * 0.30f + sc(70), W, sc(30) }, sc(22), C_DIM, F_SEMI);
        txt_c("Esc — пауза   ·   R — заново", (Rectangle){ 0, H - sc(60), W, sc(30) }, sc(15), fade(C_TXT, 0.5f), F_SEMI);
    }
}

static Rectangle center_card(float W, float H, float w, float h)
{
    return (Rectangle){ (W - w) / 2, (H - h) / 2, w, h };
}

static void pause_overlay(float W, float H)
{
    DrawRectangle(0, 0, (int)W, (int)H, fade(BLACK, 0.6f));
    Rectangle c = center_card(W, H, sc(440), sc(430));
    rrect(c, sc(16), C_SIDE);
    rrect_line(c, sc(16), 1, C_LINE);
    txt_c("Пауза", (Rectangle){ c.x, c.y + sc(28), c.width, sc(40) }, sc(32), C_TXT, F_BOLD);
    txt_c(A.g.sc.name, (Rectangle){ c.x, c.y + sc(72), c.width, sc(24) }, sc(16), C_DIM, F_SEMI);
    float bx = c.x + sc(40), bw = c.width - sc(80), by = c.y + sc(122);
    if (ui_button((Rectangle){ bx, by, bw, sc(50) }, "Продолжить", BTN_PRIMARY)) resume_game();
    by += sc(62);
    if (ui_button((Rectangle){ bx, by, bw, sc(50) }, "Заново  (R)", BTN_SECONDARY)) start_scenario(A.g.sc_index);
    by += sc(62);
    if (ui_button((Rectangle){ bx, by, bw, sc(50) }, "Настройки", BTN_SECONDARY)) { A.gs = GS_SETTINGS; A.nmodes = 0; }
    by += sc(62);
    if (ui_button((Rectangle){ bx, by, bw, sc(50) }, "Выйти в меню", BTN_GHOST)) go_menu();
}

static void results_overlay(float W, float H)
{
    DrawRectangle(0, 0, (int)W, (int)H, fade(BLACK, 0.62f));
    Rectangle c = center_card(W, H, sc(980), sc(660));
    rrect(c, sc(18), C_SIDE);
    rrect_line(c, sc(18), 1, C_LINE);
    float x = c.x + sc(44), w = c.width - sc(88), y = c.y + sc(36);
    const RunResult *r = &A.last;
    const Scenario *sn = &A.g.sc;

    char head[128];
    if (A.playlist >= 0) snprintf(head, sizeof(head), "%s  ·  %d / %d", PLAYLISTS[A.playlist].name, A.pl_pos + 1, PLAYLISTS[A.playlist].n);
    else snprintf(head, sizeof(head), "%s", CATEGORY_NAMES[sn->category]);
    txt(head, x, y, sc(14), C_FAINT, F_BOLD);
    txt(sn->name, x, y + sc(22), sc(30), C_TXT, F_BOLD);
    y += sc(80);

    txt("СЧЁТ", x, y, sc(13), C_DIM, F_BOLD);
    txt(fmt_int(r->score), x, y + sc(18), sc(76), C_TXT, F_MONO);
    float sx = x + txt_w(fmt_int(r->score), sc(76), F_MONO) + sc(24);
    if (A.new_pb) {
        Rectangle bdg = { sx, y + sc(42), sc(190), sc(40) };
        rrect(bdg, sc(8), (Color){ 64, 48, 16, 255 });
        txt_c("Новый рекорд!", bdg, sc(18), C_GOLD, F_BOLD);
        char d[64]; snprintf(d, sizeof(d), "прошлый: %s", fmt_int(A.best_before));
        txt(d, sx + sc(206), y + sc(52), sc(16), C_DIM, F_SEMI);
    } else if (A.best_before >= 0) {
        char d[96];
        float pct = A.best_before > 0 ? (r->score / A.best_before - 1) * 100 : 0;
        snprintf(d, sizeof(d), "Рекорд %s   (%+.1f%%)", fmt_int(A.best_before), pct);
        txt(d, sx, y + sc(52), sc(18), C_GOLD, F_SEMI);
    } else {
        txt("Первый результат — от него и растём", sx, y + sc(52), sc(18), C_DIM, F_SEMI);
    }
    // online rank block (right side of the score row)
    {
        const SubmitState *os = &ONL_SUBMIT;
        float rx = x + w;
        if (os->ranked && os->status == NS_OK) {
            txt_r("МИРОВОЙ РЕЙТИНГ", rx, y, sc(13), C_DIM, F_BOLD);
            char v[48], t[48]; snprintf(v, sizeof(v), "#%d", os->rank); snprintf(t, sizeof(t), " из %d", os->total);
            float tw2 = txt_w(t, sc(22), F_SEMI);
            Color vc = os->rank <= 3 ? C_GOLD : C_ACC;
            txt_r(t, rx, y + sc(48), sc(22), C_DIM, F_SEMI);
            txt_r(v, rx - tw2, y + sc(22), sc(54), vc, F_MONO);
            if (os->improved) txt_r("новый онлайн-рекорд", rx, y + sc(86), sc(15), C_GOLD, F_SEMI);
            else { char bb[64]; snprintf(bb, sizeof(bb), "лучший в таблице: %s", fmt_int(os->best)); txt_r(bb, rx, y + sc(86), sc(15), C_DIM, F_SEMI); }
        } else {
            char ol[260]; Color oc = C_FAINT;
            if (!os->ranked) snprintf(ol, sizeof(ol), "%s", os->reason);
            else if (os->status == NS_BUSY) { snprintf(ol, sizeof(ol), "отправка…"); oc = C_DIM; }
            else { snprintf(ol, sizeof(ol), "%s", os->reason); oc = os->queued ? C_GOLD : C_BAD; }
            float bw = sc(330);
            txt_r("МИРОВОЙ РЕЙТИНГ", rx, y, sc(13), C_DIM, F_BOLD);
            txt_wrap(ol, (Rectangle){ rx - bw, y + sc(24), bw, sc(70) }, sc(15), oc, F_SEMI, 3, true);
        }
    }
    y += sc(118);

    float tw = (w - sc(36)) / 4, th = sc(88);
    char b[4][48];
    if (sn->shoot == SM_TRACK) {
        snprintf(b[0], 48, "%.1f%%", r->acc * 100);
        snprintf(b[1], 48, "%.1f с", A.g.track_on);
        snprintf(b[2], 48, "%d", r->kills);
        snprintf(b[3], 48, "%.1f с", A.g.track_fire);
        tile((Rectangle){ x, y, tw, th }, "НА ЦЕЛИ", b[0], C_ACC);
        tile((Rectangle){ x + (tw + sc(12)), y, tw, th }, "ВРЕМЯ НА ЦЕЛИ", b[1], C_TXT);
        tile((Rectangle){ x + 2 * (tw + sc(12)), y, tw, th }, "УБИЙСТВ", b[2], C_TXT);
        tile((Rectangle){ x + 3 * (tw + sc(12)), y, tw, th }, "ВРЕМЯ СТРЕЛЬБЫ", b[3], C_TXT);
    } else {
        snprintf(b[0], 48, "%.1f%%", r->acc * 100);
        snprintf(b[1], 48, "%d", r->kills);
        snprintf(b[2], 48, "%.0f мс", r->ttk_ms);
        snprintf(b[3], 48, "%.2f", r->kps);
        tile((Rectangle){ x, y, tw, th }, "ТОЧНОСТЬ", b[0], C_ACC);
        char kl[64]; snprintf(kl, sizeof(kl), r->expired ? "УБИЙСТВ (упущено %d)" : "УБИЙСТВ", r->expired);
        tile((Rectangle){ x + (tw + sc(12)), y, tw, th }, kl, b[1], C_TXT);
        tile((Rectangle){ x + 2 * (tw + sc(12)), y, tw, th }, sn->delay_max > 0 ? "РЕАКЦИЯ" : "ВРЕМЯ НА УБИЙСТВО", b[2], C_TXT);
        tile((Rectangle){ x + 3 * (tw + sc(12)), y, tw, th }, "УБИЙСТВ В СЕКУНДУ", b[3], C_TXT);
    }
    y += th + sc(26);

    // history chart
    RunResult h[24]; int n = stats_history(sn->id, h, 24);
    Rectangle ch = { x, y, w, sc(150) };
    rrect(ch, sc(10), C_CARD);
    txt("ПОСЛЕДНИЕ ЗАХОДЫ", ch.x + sc(18), ch.y + sc(14), sc(12), C_DIM, F_BOLD);
    if (n > 0) {
        float mx = 1; for (int i = 0; i < n; i++) if (h[i].score > mx) mx = h[i].score;
        float bx0 = ch.x + sc(18), bw = (ch.width - sc(36)) / 24, base = ch.y + ch.height - sc(16), hh = ch.height - sc(56);
        for (int i = 0; i < n; i++) {
            float bh = hh * (h[i].score / mx); if (bh < sc(2)) bh = sc(2);
            Color col = i == n - 1 ? C_ACC : (h[i].score >= mx ? C_GOLD : (Color){ 58, 68, 88, 255 });
            rrect((Rectangle){ bx0 + i * bw + sc(3), base - bh, bw - sc(6), bh }, sc(3), col);
        }
    }
    y += ch.height + sc(28);

    float by = c.y + c.height - sc(36) - sc(52);
    bool has_next = A.playlist >= 0 && A.pl_pos + 1 < PLAYLISTS[A.playlist].n;
    float bxr = x + w;
    if (has_next) {
        int ni = scenario_find(PLAYLISTS[A.playlist].ids[A.pl_pos + 1]);
        char nb[96]; snprintf(nb, sizeof(nb), "Далее: %s", ni >= 0 ? SCN[ni].name : "?");
        float bw2 = txt_w(nb, sc(17), F_SEMI) + sc(48);
        bxr -= bw2;
        if (ui_button((Rectangle){ bxr, by, bw2, sc(52) }, nb, BTN_PRIMARY) || IsKeyPressed(KEY_ENTER)) {
            A.pl_pos++; start_scenario(ni); return;
        }
        bxr -= sc(12);
    }
    bxr -= sc(200);
    if (ui_button((Rectangle){ bxr, by, sc(200), sc(52) }, "Ещё раз (R)", has_next ? BTN_SECONDARY : BTN_PRIMARY) ||
        IsKeyPressed(KEY_SPACE)) { start_scenario(A.g.sc_index); return; }
    if (ui_button((Rectangle){ x, by, sc(180), sc(52) }, "В меню (Esc)", BTN_GHOST)) { go_menu(); return; }
    if (A.playlist >= 0 && !has_next)
        txt("Программа завершена", x + sc(200), by + sc(16), sc(17), C_GOOD, F_SEMI);
}

static void game_frame_app(double dt, RawMouseEvt *ev, int n)
{
    Game *g = &A.g;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    bool focused = IsWindowFocused() || A.demo;
    if (A.gs == GS_PLAY && !focused) pause_game();

    bool esc = IsKeyPressed(KEY_ESCAPE) && !ui_editing();
    if (A.gs == GS_PLAY) {
        set_lock(true);
        int prev_phase = g->phase; int prev_cd = (int)ceilf(g->countdown - (float)g->phase_t);
        g->ev_hit = g->ev_kill = g->ev_miss = 0;
        game_frame(g, dt, ev, n, settings_deg_per_count(&S), S.vert_mult, S.tg_size_mult);
        if (S.snd_hit) { if (g->ev_kill) sfx_kill(); else if (g->ev_hit) sfx_hit(); }
        if (S.snd_miss && g->ev_miss) sfx_miss();
        if (g->phase == GP_COUNTDOWN) {
            int cd = (int)ceilf(g->countdown - (float)g->phase_t);
            if (cd != prev_cd && S.snd_hit) sfx_tick();
        } else if (prev_phase == GP_COUNTDOWN && S.snd_hit) sfx_go();
        if (g->phase == GP_DONE) { set_lock(false); finish_run(); }
        else if (esc) pause_game();
        else if (IsKeyPressed(KEY_R)) start_scenario(g->sc_index);
    } else {
        set_lock(false);
        if (A.gs == GS_PAUSE) {
            if (esc) resume_game();
            else if (IsKeyPressed(KEY_R)) start_scenario(g->sc_index);
        } else if (A.gs == GS_RESULT) {
            if (esc) { go_menu(); }
            else if (IsKeyPressed(KEY_R)) start_scenario(g->sc_index);
        } else if (A.gs == GS_SETTINGS) {
            if (esc) { settings_save(A.p_settings); A.gs = GS_PAUSE; }
        }
    }
    if (A.st != ST_GAME) { ClearBackground(C_BG); return; }

    ClearBackground(render_clear_color());
    Camera3D cam = render_camera(g->yaw, g->pitch, S.fov, W / H);
    render_world(g, cam);

    if (A.gs == GS_PLAY) {
        hud(W, H);
        crosshair_draw((int)W, (int)H);
    } else if (A.gs == GS_PAUSE) {
        crosshair_draw((int)W, (int)H);
        pause_overlay(W, H);
    } else if (A.gs == GS_RESULT) {
        results_overlay(W, H);
    } else if (A.gs == GS_SETTINGS) {
        DrawRectangle(0, 0, (int)W, (int)H, fade(C_BG, 0.97f));
        draw_settings((Rectangle){ 0, 0, W, H }, true);
    }
}

// ---------------------------------------------------------------- demo / self-test (used for automated checks)
static int demo_bot(RawMouseEvt *ev)
{
    Game *g = &A.g;
    if (g->phase != GP_RUNNING) return 0;
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < g->sc.count; i++) {
        if (!g->tg[i].alive) continue;
        float dy = g->tg[i].yaw - g->yaw, dp = g->tg[i].pitch - g->pitch;
        float d = dy * dy + dp * dp;
        if (d < bd) { bd = d; best = i; }
    }
    int n = 0;
    double dpc = settings_deg_per_count(&S);
    bool track = g->sc.shoot == SM_TRACK;
    if (track && !A.demo_lmb) { ev[n++] = (RawMouseEvt){ 0, 0, PB_LDOWN }; A.demo_lmb = true; }
    if (best < 0) return n;
    Target *t = &g->tg[best];
    float lead = track ? 0.03f : 0.0f;
    float ty = t->yaw + t->vyaw * lead, tp = t->pitch + t->vpitch * lead;
    float k = track ? 0.55f : 0.30f;
    float dyaw = (ty - g->yaw) * k, dpit = (tp - g->pitch) * k;
    int cx = (int)lround(dyaw / dpc), cy = (int)lround(-dpit / (dpc * S.vert_mult));
    float err = sqrtf((ty - g->yaw) * (ty - g->yaw) + (tp - g->pitch) * (tp - g->pitch));
    float ar = ang_radius_deg(game_target_radius(g, t, S.tg_size_mult), t->dist);
    unsigned short btn = 0;
    A.demo_cool -= 1.0 / 144.0;
    if (!track && err < ar * 0.8f && A.demo_cool <= 0) { btn = PB_LDOWN | PB_LUP; A.demo_cool = A.demo_online ? 0.16 : 0.06; }
    ev[n++] = (RawMouseEvt){ cx, cy, 0 };
    if (btn) ev[n++] = (RawMouseEvt){ 0, 0, btn };
    return n;
}

// Online self-test: settings tab, three ranked runs submitted to DEMO_SERVER, then the leaderboard.
static const char *demo_control_online(void)
{
    static char name[64];
    static const char *ids[] = { "gridshot", "smooth", "wide_flick" };
    static int step = 0, t0 = 0, cur = 0;
    int f = A.frame;
    if (f == 2) { A.page = PAGE_SETTINGS; A.tab = 5; online_test(); }
    if (f == 100) return "o1_settings_online.png";
    if (f == 110) { step = 1; cur = 0; }
    switch (step) {
    case 1:
        A.demo_lmb = false; A.demo_cool = 0;
        start_scenario(scenario_find(ids[cur]));
        A.g.sc.duration = 12; A.g.rng = 777u + (unsigned)cur;
        step = 2; break;
    case 2: if (A.gs == GS_RESULT) { step = 3; t0 = f; } break;
    case 3:
        if (f - t0 == 150) {
            printf("[online] %-11s score=%7.0f submit status=%d rank=%d/%d reason=%s\n", ids[cur], A.last.score,
                   ONL_SUBMIT.status, ONL_SUBMIT.rank, ONL_SUBMIT.total, ONL_SUBMIT.reason);
            fflush(stdout);
            snprintf(name, sizeof(name), "o2_result_%s.png", ids[cur]); step = 4; return name;
        }
        break;
    case 4:
        if (++cur < 3) step = 1;
        else { go_menu(); A.page = PAGE_LB; A.lb_sel = scenario_find("gridshot"); step = 5; t0 = f; }
        break;
    case 5:
        if (f - t0 == 150) { step = 6; t0 = f; return "o3_leaderboard_gridshot.png"; }
        break;
    case 6:
        if (f - t0 == 5) { A.lb_sel = scenario_find("smooth"); }
        if (f - t0 == 150) { step = 7; t0 = f; return "o4_leaderboard_smooth.png"; }
        break;
    case 7:
        if (f - t0 == 5) A.page = PAGE_SCEN;
        if (f - t0 == 60) { step = 8; return "o5_cards_world_rank.png"; }
        break;
    default: if (step == 8) A.quit = true; break;
    }
    return NULL;
}

static const char *demo_control(void)
{
    static char name[64];
    if (A.demo_online) return demo_control_online();
    int f = A.frame;
    struct { int f; Page p; int tab; const char *shot; } menu[] = {
        { 10, PAGE_SCEN, 0, "01_scenarios.png" }, { 30, PAGE_PROG, 0, "02_programs.png" },
        { 50, PAGE_STATS, 0, "03_stats.png" }, { 70, PAGE_SETTINGS, 0, "04_set_mouse.png" },
        { 90, PAGE_SETTINGS, 1, "05_set_crosshair.png" }, { 110, PAGE_SETTINGS, 2, "06_set_targets.png" },
        { 130, PAGE_SETTINGS, 3, "07_set_display.png" }, { 150, PAGE_SETTINGS, 4, "08_set_misc.png" },
        { 170, PAGE_EDIT, 0, "09_editor.png" },
    };
    for (unsigned i = 0; i < sizeof(menu) / sizeof(menu[0]); i++) {
        if (f == menu[i].f - 8) { A.page = menu[i].p; A.tab = menu[i].tab; A.edit_idx = scenario_find("dynamic"); }
        if (f == menu[i].f) return menu[i].shot;
    }
    const int PER = 520;
    int base = 200;
    if (f < base) return NULL;
    int si = (f - base) / PER, lf = (f - base) % PER;
    if (si >= SCN_COUNT) { A.quit = true; return NULL; }
    if (lf == 0) { A.demo_lmb = false; A.demo_cool = 0; start_scenario(si); A.g.rng = 1234567u + (unsigned)si; }
    if (lf == 300) { snprintf(name, sizeof(name), "g%02d_%s.png", si, SCN[si].id); return name; }
    if (si == 0 && lf == 320) A.gs = GS_PAUSE;
    if (si == 0 && lf == 322) return "10_pause.png";
    if (si == 0 && lf == 326) A.gs = GS_PLAY;
    if (lf == 480 && A.gs == GS_PLAY) { A.g.sc.duration = (float)A.g.t + 0.001f; }
    if (lf == 500) {
        printf("[demo] %-14s score=%8.0f acc=%5.1f%% kills=%3d shots=%3d expired=%2d ttk=%.0fms track_on=%.2fs\n",
               SCN[si].id, A.last.score, A.last.acc * 100, A.last.kills, A.g.shots, A.last.expired, A.last.ttk_ms, A.g.track_on);
        fflush(stdout);
        if (si == 2 || si == 8 || si == 12) { snprintf(name, sizeof(name), "r%02d_results_%s.png", si, SCN[si].id); return name; }
    }
    return NULL;
}

static Image make_icon(void)
{
    Image im = GenImageColor(64, 64, BLANK);
    ImageDrawCircle(&im, 32, 32, 30, (Color){ 15, 18, 25, 255 });
    ImageDrawCircle(&im, 32, 32, 22, (Color){ 255, 90, 100, 255 });
    ImageDrawCircle(&im, 32, 32, 15, (Color){ 15, 18, 25, 255 });
    ImageDrawCircle(&im, 32, 32, 8, (Color){ 255, 90, 100, 255 });
    ImageDrawRectangle(&im, 30, 4, 4, 16, (Color){ 54, 220, 255, 255 });
    ImageDrawRectangle(&im, 30, 44, 4, 16, (Color){ 54, 220, 255, 255 });
    ImageDrawRectangle(&im, 4, 30, 16, 4, (Color){ 54, 220, 255, 255 });
    ImageDrawRectangle(&im, 44, 30, 16, 4, (Color){ 54, 220, 255, 255 });
    return im;
}

// ---------------------------------------------------------------- main
int main(int argc, char **argv)
{
    const char *demo_dir = NULL;
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "--demo") == 0 && i + 1 < argc) demo_dir = argv[++i];
    A.demo = demo_dir != NULL;
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "--print-rulesets") == 0) {
        // for server admins: leaderboard keys of this build
        for (int k = 0; k < SCN_COUNT; k++) { char r[16]; scenario_ruleset(k, r, sizeof(r)); printf("%s\t%s\n", SCN_DEFAULT[k].id, r); }
        return 0;
    }
    A.playlist = -1;

#ifdef _WIN32
    const char *dir = plat_app_dir();
#else
    char exedir[600]; snprintf(exedir, sizeof(exedir), "%s", GetApplicationDirectory());
    size_t el = strlen(exedir); if (el > 1 && (exedir[el - 1] == '/' || exedir[el - 1] == '\\')) exedir[el - 1] = 0;
    const char *dir = exedir;
#endif
    if (A.demo) { ChangeDirectory(demo_dir); dir = demo_dir; }
    snprintf(A.dir, sizeof(A.dir), "%s", dir);
    snprintf(A.p_settings, sizeof(A.p_settings), "%s/aimforge_settings.cfg", dir);
    snprintf(A.p_scen, sizeof(A.p_scen), "%s/aimforge_scenarios.cfg", dir);
    snprintf(A.p_stats, sizeof(A.p_stats), "%s/aimforge_scores.csv", dir);
    bool first_launch = !settings_load(A.p_settings);
    scenarios_load(A.p_scen);
    stats_load(A.p_stats);
    if (A.demo) {
        S.display_mode = DM_WINDOWED; S.res_w = 2560; S.res_h = 1440; S.msaa = false;
        if (getenv("DEMO_W")) S.res_w = atoi(getenv("DEMO_W"));
        if (getenv("DEMO_H")) S.res_h = atoi(getenv("DEMO_H"));
    }
    A.msaa_boot = S.msaa;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(S.msaa ? FLAG_MSAA_4X_HINT : 0);   // no FLAG_VSYNC_HINT on purpose
    InitWindow(1280, 720, APP_NAME);
    SetExitKey(KEY_NULL);
    Image icon = make_icon(); SetWindowIcon(icon); UnloadImage(icon);

    A.raw = plat_init(GetWindowHandle());
    if (first_launch && !A.demo) {
        // First launch: native desktop resolution, highest refresh rate.
        int dw, dh, dhz; display_desktop(&dw, &dh, &dhz);
        S.res_w = dw; S.res_h = dh; S.refresh = 0;
    }
    if (A.demo) { S.display_mode = DM_WINDOWED; }
    display_apply();
    A.finish = (PFN_glFinish)display_glfw_proc("glFinish");
    render_init();
    if (A.demo && getenv("DEMO_SERVER")) {
        A.demo_online = true; S.online = true;
        snprintf(S.server, sizeof(S.server), "%s", getenv("DEMO_SERVER"));
        snprintf(S.nick, sizeof(S.nick), "%s", getenv("DEMO_NICK") ? getenv("DEMO_NICK") : "DemoBot");
    }
    online_init(A.dir);
    settings_save(A.p_settings);      // persist the freshly generated player id right away
    sfx_init();
    sfx_volume(S.volume);

    double prev = plat_time(), next = prev, fps_t = prev;
    A.was_focused = true; A.refocus_t = -1;
    static RawMouseEvt ev[8192];
    while (!A.quit) {
        // 1) frame limiter BEFORE polling input, so input is as fresh as possible when rendering
        if (S.fps_cap > 0 && !A.demo) {
            next += 1.0 / S.fps_cap;
            double now = plat_time();
            if (now - next > 0.05 || next - now > 1.0) next = now;
            plat_wait_until(next);
        }
        // 2) input
        PollInputEvents();
        if (WindowShouldClose()) A.quit = true;

        online_update(plat_time());

        // Alt+Tab / minimize handling
        bool focused = IsWindowFocused() || A.demo;
        bool hidden = !A.demo && (IsWindowMinimized() || GetScreenWidth() < 64 || GetScreenHeight() < 64);
        if (!focused && A.st == ST_GAME && A.gs == GS_PLAY) pause_game();
        if (!focused && A.locked) set_lock(false);
        if (focused && !A.was_focused) { plat_flush(); A.refocus_t = plat_time(); }
        A.was_focused = focused;
        if (hidden) {
            // Minimized (exclusive fullscreen loses its window on Alt+Tab): don't render at 0x0,
            // don't touch fonts/viewport, just idle until restored.
            plat_drain(ev, 8192);
            plat_wait_until(plat_time() + 0.05);
            prev = plat_time(); next = prev;
            continue;
        }
        double now = plat_time();
        double dt = now - prev; prev = now;
        if (dt > 0.1) dt = 0.1;
        if (A.demo) dt = 1.0 / 144.0;

        int n = plat_drain(ev, 8192);
        if (!A.raw && !A.demo && A.locked) {
            Vector2 d = GetMouseDelta();
            ev[n++] = (RawMouseEvt){ (int)d.x, (int)d.y, 0 };
            unsigned short b = 0;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) b |= PB_LDOWN;
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) b |= PB_LUP;
            if (b) ev[n++] = (RawMouseEvt){ 0, 0, b };
        }
        if (A.demo && A.st == ST_GAME && A.gs == GS_PLAY) n = demo_bot(ev);
        plat_frame(IsWindowFocused());

        A.fps_acc += now - fps_t; fps_t = now; A.fps_n++;
        if (A.fps_acc >= 0.5) { A.fps = (float)(A.fps_n / A.fps_acc); A.ft_ms = (float)(A.fps_acc / A.fps_n * 1000.0); A.fps_acc = 0; A.fps_n = 0; }

        float scale = fminf(GetScreenHeight() / 1080.0f, GetScreenWidth() / 1600.0f);
        if (scale < 0.55f) scale = 0.55f;
        if (fabsf(scale - A.last_scale) > 0.001f) { font_clear(); A.last_scale = scale; }

        // 3) update + render
        const char *shot = A.demo ? demo_control() : NULL;
        BeginDrawing();
        ui_begin(scale);
        // swallow the click that brings the window back into focus
        ui.blocked = (now - A.refocus_t) < 0.25;
        if (A.st == ST_GAME) game_frame_app(dt, ev, n);
        else menu_frame();
        ui_end();
        EndDrawing();
        if (shot) TakeScreenshot(shot);
        SwapScreenBuffer();
        if (S.low_latency && A.finish && focused) A.finish();
        A.frame++;
        // In the background: ~30 FPS so the game doesn't eat the GPU of whatever you alt-tabbed to.
        if (!focused) plat_wait_until(plat_time() + 1.0 / 30.0);
    }

    settings_save(A.p_settings);
    scenarios_save(A.p_scen);
    set_lock(false);
    online_shutdown();
    plat_shutdown();
    sfx_shutdown();
    render_unload();
    font_clear();
    CloseWindow();
    return 0;
}
