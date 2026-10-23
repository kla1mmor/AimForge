#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "scenario.h"
#include "kv.h"
#include "platform.h"

const char *const CATEGORY_NAMES[CAT_COUNT] = {
    "Флики", "Рефлекс и микрокоррекция", "Переводка прицела", "Трекинг и плавность", "Свои"
};

static const char *const SHOOT_OPTS[] = { "Клик", "Трекинг (держать ЛКМ)" };
static const char *const MOVE_OPTS[]  = { "Стоят", "Стрейф", "Отскоки 2D", "Динамика 3D" };

#define OFS(f) (int)offsetof(Scenario, f)
const ParamDesc SC_PARAMS[] = {
    { "shoot",       "Режим стрельбы",       "Клик — каждый выстрел отдельно; трекинг — урон пока держишь ЛКМ на цели", P_ENUM, OFS(shoot), 0, 1, 1, SHOOT_OPTS, 2 },
    { "move",        "Движение целей",       NULL, P_ENUM, OFS(move), 0, MV_COUNT - 1, 1, MOVE_OPTS, MV_COUNT },
    { "duration",    "Длительность, с",      NULL, P_FLOAT, OFS(duration), 10, 300, 5, NULL, 0 },
    { "count",       "Целей одновременно",   NULL, P_INT, OFS(count), 1, MAX_TARGETS, 1, NULL, 0 },
    { "radius",      "Радиус цели",          "В метрах на дистанции; угловой размер показан справа", P_FLOAT, OFS(radius), 0.08f, 2.0f, 0.01f, NULL, 0 },
    { "dist",        "Дистанция",            NULL, P_FLOAT, OFS(dist), 5, 50, 0.5f, NULL, 0 },
    { "dist_var",    "Разброс дистанции",    NULL, P_FLOAT, OFS(dist_var), 0, 20, 0.5f, NULL, 0 },
    { "spawn_yaw",   "Зона: ширина ±, °",    NULL, P_FLOAT, OFS(spawn_yaw), 0, 120, 0.5f, NULL, 0 },
    { "spawn_pitch", "Зона: высота ±, °",    NULL, P_FLOAT, OFS(spawn_pitch), 0, 45, 0.5f, NULL, 0 },
    { "pitch_off",   "Зона: сдвиг вверх, °", NULL, P_FLOAT, OFS(pitch_off), -20, 30, 0.5f, NULL, 0 },
    { "yaw_min",     "Мин. угол сбоку, °",   "Больше 0 — цели появляются только по бокам (сайдбокс)", P_FLOAT, OFS(yaw_min), 0, 110, 1, NULL, 0 },
    { "min_sep",     "Мин. расстояние до прицела, °", NULL, P_FLOAT, OFS(min_sep), 0, 60, 0.5f, NULL, 0 },
    { "lifetime",    "Время жизни цели, с",  "0 — пока не убьёшь", P_FLOAT, OFS(lifetime), 0, 5, 0.05f, NULL, 0 },
    { "shrink",      "Сжимается со временем", NULL, P_BOOL, OFS(shrink), 0, 1, 1, NULL, 0 },
    { "speed",       "Скорость, °/с",        NULL, P_FLOAT, OFS(speed), 0, 150, 1, NULL, 0 },
    { "speed_var",   "Разброс скорости, °/с", NULL, P_FLOAT, OFS(speed_var), 0, 80, 1, NULL, 0 },
    { "dir_min",     "Смена направления от, с", NULL, P_FLOAT, OFS(dir_min), 0.05f, 5, 0.05f, NULL, 0 },
    { "dir_max",     "Смена направления до, с", NULL, P_FLOAT, OFS(dir_max), 0.05f, 5, 0.05f, NULL, 0 },
    { "hp",          "Прочность",            "Клик: попаданий для убийства. Трекинг: секунд на цели (0 — бессмертная)", P_FLOAT, OFS(hp), 0, 10, 0.05f, NULL, 0 },
    { "flick_return","Каждая вторая — в центре", NULL, P_BOOL, OFS(flick_return), 0, 1, 1, NULL, 0 },
    { "delay_min",   "Задержка появления от, с", NULL, P_FLOAT, OFS(delay_min), 0, 4, 0.05f, NULL, 0 },
    { "delay_max",   "Задержка появления до, с", NULL, P_FLOAT, OFS(delay_max), 0, 4, 0.05f, NULL, 0 },
    { "one_shot",    "Одна попытка на цель", "Промах убирает цель", P_BOOL, OFS(one_shot), 0, 1, 1, NULL, 0 },
    { "show_zone",   "Показывать зону",      NULL, P_BOOL, OFS(show_zone), 0, 1, 1, NULL, 0 },
};
const int SC_PARAM_COUNT = sizeof(SC_PARAMS) / sizeof(SC_PARAMS[0]);

// Base template; presets override fields.
#define BASE .shoot = SM_CLICK, .move = MV_NONE, .duration = 60, .count = 1, .radius = 0.4f, .dist = 20, \
             .dist_var = 0, .spawn_yaw = 30, .spawn_pitch = 12, .pitch_off = 1, .yaw_min = 0, .min_sep = 10, \
             .lifetime = 0, .shrink = false, .speed = 0, .speed_var = 0, .dir_min = 0.5f, .dir_max = 1.5f, \
             .hp = 1, .flick_return = false, .delay_min = 0, .delay_max = 0, .one_shot = false, .show_zone = false

const Scenario SCN_DEFAULT[] = {
    // ---------------- Flicks
    { .id = "wide_flick", .name = "Широкие флики", .category = CAT_FLICK, BASE,
      .desc = "Одна цель в широкой зоне. Резкий флик и точный клик без лишней остановки. Базовый навык для дуэлей.",
      .radius = 0.50f, .spawn_yaw = 55, .spawn_pitch = 18, .pitch_off = 2, .min_sep = 25 },
    { .id = "flick_tech", .name = "Флик-тех: центр и обратно", .category = CAT_FLICK, BASE,
      .desc = "Цели чередуются: центр, случайная точка, снова центр. Контролируемый флик с напряжением кисти, без перелёта, плюс возврат прицела.",
      .radius = 0.45f, .spawn_yaw = 40, .spawn_pitch = 14, .min_sep = 15, .flick_return = true },
    { .id = "prime_flick", .name = "Точные флики", .category = CAT_FLICK, BASE,
      .desc = "Маленькая цель сжимается и исчезает за 1.4 с. Учит не останавливаться перед микрокоррекцией и бить сразу.",
      .radius = 0.34f, .spawn_yaw = 35, .spawn_pitch = 12, .min_sep = 15, .lifetime = 1.4f, .shrink = true },
    { .id = "headline", .name = "Хедлайн", .category = CAT_FLICK, BASE,
      .desc = "Все цели на одной высоте — уровне головы. Только горизонтальные флики: держи прицел на линии.",
      .radius = 0.33f, .spawn_yaw = 50, .spawn_pitch = 1.2f, .pitch_off = 0, .min_sep = 18 },
    { .id = "sidebox", .name = "Сайдбокс", .category = CAT_FLICK, BASE,
      .desc = "Цели ближе и только по бокам (35–85°). Те 20% ситуаций, когда враг вне центра: большие флики-развороты.",
      .radius = 0.50f, .dist = 12, .spawn_yaw = 85, .yaw_min = 35, .spawn_pitch = 15, .min_sep = 30 },

    // ---------------- Reflex
    { .id = "reflex_box", .name = "Reflex Box", .category = CAT_REFLEX, BASE,
      .desc = "Мелкие цели вспыхивают внутри центрального бокса и живут 0.65 с. Микрорефлекс: большинство дуэлей решается именно в этой зоне.",
      .radius = 0.28f, .spawn_yaw = 12, .spawn_pitch = 6, .pitch_off = 1, .min_sep = 3, .lifetime = 0.65f,
      .delay_min = 0.12f, .delay_max = 0.35f, .show_zone = true },
    { .id = "first_bullet", .name = "Первая пуля", .category = CAT_REFLEX, BASE,
      .desc = "Пауза, затем появляется цель. Один выстрел — одна попытка. Меряем реакцию и точность первой пули.",
      .radius = 0.36f, .spawn_yaw = 22, .spawn_pitch = 8, .min_sep = 6, .lifetime = 1.2f,
      .delay_min = 0.6f, .delay_max = 2.0f, .one_shot = true, .show_zone = true },
    { .id = "click_timing", .name = "Тайминг клика", .category = CAT_REFLEX, BASE,
      .desc = "Цель резко стрейфит влево-вправо. Не гонись за ней: подведи прицел на траекторию и стреляй вовремя.",
      .move = MV_STRAFE, .radius = 0.40f, .spawn_yaw = 25, .spawn_pitch = 8, .min_sep = 8,
      .speed = 38, .speed_var = 12, .dir_min = 0.3f, .dir_max = 0.9f },

    // ---------------- Switching
    { .id = "gridshot", .name = "Гридшот", .category = CAT_SWITCH, BASE,
      .desc = "Три цели рядом с центром. Короткие быстрые переводы между целями — классическая разминка.",
      .count = 3, .radius = 0.75f, .spawn_yaw = 18, .spawn_pitch = 10, .min_sep = 8 },
    { .id = "speed_switch", .name = "Быстрая переводка", .category = CAT_SWITCH, BASE,
      .desc = "Пять целей по всей ширине. Убил — сразу переводи на ближайшую. Скорость перевода и остановка на цели.",
      .count = 5, .radius = 0.50f, .spawn_yaw = 50, .spawn_pitch = 16, .min_sep = 12 },
    { .id = "switch_track", .name = "Свитч-трекинг", .category = CAT_SWITCH, BASE,
      .desc = "Три движущиеся цели. Держи ЛКМ: каждую нужно вести 0.45 с, затем переводи прицел на следующую.",
      .shoot = SM_TRACK, .move = MV_BOUNCE, .count = 3, .radius = 0.60f, .spawn_yaw = 35, .spawn_pitch = 12,
      .min_sep = 10, .speed = 22, .speed_var = 8, .dir_min = 0.5f, .dir_max = 1.4f, .hp = 0.45f },
    { .id = "dynamic", .name = "Экстрим-динамик", .category = CAT_SWITCH, BASE,
      .desc = "Три цели хаотично меняют направление, скорость и дистанцию. Флики по движущимся целям.",
      .move = MV_DYNAMIC, .count = 3, .radius = 0.55f, .dist_var = 8, .spawn_yaw = 40, .spawn_pitch = 14,
      .min_sep = 10, .speed = 45, .speed_var = 20, .dir_min = 0.25f, .dir_max = 0.8f },

    // ---------------- Tracking
    { .id = "smooth", .name = "Плавный трекинг", .category = CAT_TRACK, BASE,
      .desc = "Одна цель плавно движется. Держи ЛКМ и веди её без рывков. Считается доля времени на цели.",
      .shoot = SM_TRACK, .move = MV_BOUNCE, .radius = 0.62f, .spawn_yaw = 30, .spawn_pitch = 6, .min_sep = 0,
      .speed = 28, .speed_var = 10, .dir_min = 0.8f, .dir_max = 2.0f, .hp = 0 },
    { .id = "reactive", .name = "Реактивный трекинг", .category = CAT_TRACK, BASE,
      .desc = "Цель резко меняет направление стрейфа каждые 0.2–0.6 с. Реакция на смену движения и быстрый догон.",
      .shoot = SM_TRACK, .move = MV_STRAFE, .radius = 0.60f, .spawn_yaw = 30, .spawn_pitch = 3, .min_sep = 0,
      .speed = 55, .speed_var = 15, .dir_min = 0.2f, .dir_max = 0.6f, .hp = 0 },

    // ---------------- Custom
    { .id = "custom", .name = "Свой сценарий", .category = CAT_CUSTOM, BASE,
      .desc = "Полностью настраиваемый сценарий: количество, размер, движение, зона, время жизни и режим стрельбы.",
      .count = 2, .radius = 0.50f, .spawn_yaw = 30, .spawn_pitch = 12, .min_sep = 8 },
};

#define NSCN ((int)(sizeof(SCN_DEFAULT) / sizeof(SCN_DEFAULT[0])))
Scenario SCN[NSCN];
int SCN_COUNT = NSCN;

const Playlist PLAYLISTS[] = {
    { "Разминка перед матчем", "Рефлекс, статика и первая пуля — разогреть кисть перед игрой.",
      { "reflex_box", "gridshot", "headline", "first_bullet" }, 4 },
    { "Флики: скорость и контроль", "Если флики неточные или медленные: широкие, флик-тех, точные и сайдбокс.",
      { "wide_flick", "flick_tech", "prime_flick", "sidebox" }, 4 },
    { "Микрокоррекция и подтверждение", "Первая пуля, микрорефлекс и тайминг клика.",
      { "first_bullet", "reflex_box", "click_timing" }, 3 },
    { "Переводка прицела", "Быстрые переводы: от статики к движущимся целям.",
      { "gridshot", "speed_switch", "switch_track", "dynamic" }, 4 },
    { "Плавность (flow)", "Плавный и реактивный трекинг, затем свитч.",
      { "smooth", "reactive", "switch_track" }, 3 },
};
const int PLAYLIST_COUNT = sizeof(PLAYLISTS) / sizeof(PLAYLISTS[0]);

void scenarios_init(void)
{
    memcpy(SCN, SCN_DEFAULT, sizeof(SCN));
}

int scenario_find(const char *id)
{
    for (int i = 0; i < SCN_COUNT; i++) if (strcmp(SCN[i].id, id) == 0) return i;
    return -1;
}

void scenario_reset(int idx)
{
    if (idx >= 0 && idx < SCN_COUNT) SCN[idx] = SCN_DEFAULT[idx];
}

static bool param_equal(const Scenario *a, const Scenario *b, const ParamDesc *p)
{
    const char *pa = (const char *)a + p->offset, *pb = (const char *)b + p->offset;
    switch (p->type) {
    case P_INT: case P_ENUM: return *(const int *)pa == *(const int *)pb;
    case P_FLOAT: { float d = *(const float *)pa - *(const float *)pb; return d < 1e-5f && d > -1e-5f; }
    case P_BOOL: return *(const bool *)pa == *(const bool *)pb;
    }
    return true;
}

bool scenario_equal(const Scenario *a, const Scenario *b)
{
    for (int i = 0; i < SC_PARAM_COUNT; i++)
        if (!param_equal(a, b, &SC_PARAMS[i])) return false;
    return true;
}

bool scenario_is_modified(int idx) { return !scenario_equal(&SCN[idx], &SCN_DEFAULT[idx]); }

// Leaderboard key: FNV-1a over a text dump of the default parameters + scoring version.
// Changing a scenario's defaults in a future version automatically starts a fresh board for it.
#define SCORING_VERSION "1"
void scenario_ruleset(int idx, char *out, int cap)
{
    char buf[2048]; int o = snprintf(buf, sizeof(buf), "v" SCORING_VERSION ";%s;", SCN_DEFAULT[idx].id);
    for (int i = 0; i < SC_PARAM_COUNT && o < (int)sizeof(buf) - 64; i++) {
        const ParamDesc *p = &SC_PARAMS[i];
        const char *fp = (const char *)&SCN_DEFAULT[idx] + p->offset;
        switch (p->type) {
        case P_INT: case P_ENUM: o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s=%d;", p->key, *(const int *)fp); break;
        case P_FLOAT: o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s=%.4g;", p->key, *(const float *)fp); break;
        case P_BOOL: o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s=%d;", p->key, *(const bool *)fp ? 1 : 0); break;
        }
    }
    unsigned h = 2166136261u;
    for (int i = 0; i < o; i++) { h ^= (unsigned char)buf[i]; h *= 16777619u; }
    snprintf(out, (size_t)cap, "%08x", h);
}

static void on_kv(const char *k, const char *v, void *user)
{
    (void)user;
    const char *dot = strchr(k, '.');
    if (!dot) return;
    char id[24]; size_t n = (size_t)(dot - k); if (n >= sizeof(id)) return;
    memcpy(id, k, n); id[n] = 0;
    int si = scenario_find(id);
    if (si < 0) return;
    for (int i = 0; i < SC_PARAM_COUNT; i++) {
        const ParamDesc *p = &SC_PARAMS[i];
        if (strcmp(p->key, dot + 1) != 0) continue;
        char *f = (char *)&SCN[si] + p->offset;
        float x = (float)atof(v);
        if (x < p->min) x = p->min;
        if (x > p->max) x = p->max;
        switch (p->type) {
        case P_INT: case P_ENUM: *(int *)f = (int)x; break;
        case P_FLOAT: *(float *)f = x; break;
        case P_BOOL: *(bool *)f = x != 0; break;
        }
    }
}

void scenarios_load(const char *path)
{
    scenarios_init();
    kv_parse_file(path, on_kv, NULL);
}

void scenarios_save(const char *path)
{
    FILE *f = plat_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# AimForge: изменённые параметры сценариев\n");
    for (int s = 0; s < SCN_COUNT; s++) {
        if (!scenario_is_modified(s)) continue;
        for (int i = 0; i < SC_PARAM_COUNT; i++) {
            const ParamDesc *p = &SC_PARAMS[i];
            const char *fp = (const char *)&SCN[s] + p->offset;
            switch (p->type) {
            case P_INT: case P_ENUM: fprintf(f, "%s.%s=%d\n", SCN[s].id, p->key, *(const int *)fp); break;
            case P_FLOAT: fprintf(f, "%s.%s=%.4g\n", SCN[s].id, p->key, *(const float *)fp); break;
            case P_BOOL: fprintf(f, "%s.%s=%d\n", SCN[s].id, p->key, *(const bool *)fp ? 1 : 0); break;
            }
        }
    }
    fclose(f);
}

// ------------------------------------------------------------------ stats
static RunResult *g_runs = NULL;
static int g_nruns = 0, g_cap = 0;

static void runs_push(const RunResult *r)
{
    if (g_nruns == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 256;
        g_runs = (RunResult *)realloc(g_runs, sizeof(RunResult) * (size_t)g_cap);
    }
    g_runs[g_nruns++] = *r;
}

void stats_load(const char *path)
{
    g_nruns = 0;
    FILE *f = plat_fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        RunResult r; memset(&r, 0, sizeof(r));
        if (sscanf(line, "%23[^;];%lld;%f;%f;%d;%d;%d;%f;%f", r.id, &r.when, &r.score, &r.acc,
                   &r.kills, &r.misses, &r.expired, &r.ttk_ms, &r.kps) >= 4)
            runs_push(&r);
    }
    fclose(f);
}

void stats_add(const char *path, const RunResult *r)
{
    runs_push(r);
    FILE *f = plat_fopen(path, "a");
    if (!f) return;
    fprintf(f, "%s;%lld;%.1f;%.4f;%d;%d;%d;%.1f;%.3f\n", r->id, r->when, r->score, r->acc,
            r->kills, r->misses, r->expired, r->ttk_ms, r->kps);
    fclose(f);
}

float stats_best(const char *id, int *runs)
{
    float best = -1; int n = 0;
    for (int i = 0; i < g_nruns; i++) if (strcmp(g_runs[i].id, id) == 0) {
        n++; if (g_runs[i].score > best) best = g_runs[i].score;
    }
    if (runs) *runs = n;
    return best;
}

int stats_history(const char *id, RunResult *out, int max)
{
    int n = 0;
    for (int i = g_nruns - 1; i >= 0 && n < max; i--)
        if (strcmp(g_runs[i].id, id) == 0) out[n++] = g_runs[i];
    // reverse -> oldest first
    for (int i = 0; i < n / 2; i++) { RunResult t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
    return n;
}

int stats_total_runs(void) { return g_nruns; }
