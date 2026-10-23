#include <math.h>
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "render.h"
#include "settings.h"

const Theme THEMES[] = {
    // floor              wall                   ceil                   line                   clear
    { {42, 47, 56, 255},  {58, 65, 77, 255},     {36, 41, 49, 255},     {98, 108, 124, 255},   {28, 32, 39, 255} },   // Slate
    { {16, 23, 38, 255},  {22, 32, 52, 255},     {14, 20, 34, 255},     {48, 70, 112, 255},    {10, 15, 26, 255} },   // Midnight
    { {170, 164, 152, 255}, {206, 200, 189, 255}, {188, 182, 171, 255}, {150, 143, 131, 255},  {196, 190, 179, 255} }, // Sand
    { {8, 8, 10, 255},    {12, 12, 15, 255},     {8, 8, 10, 255},       {34, 34, 40, 255},     {6, 6, 8, 255} },      // Void
};
const char *const THEME_NAMES[] = { "Графит", "Полночь", "Песок", "Пустота" };

static const char *ROOM_VS =
    "#version 330\n"
    "in vec3 vertexPosition; in vec3 vertexNormal;\n"
    "uniform mat4 mvp; uniform mat4 matModel;\n"
    "out vec3 wPos; out vec3 wNrm;\n"
    "void main(){ wPos=(matModel*vec4(vertexPosition,1.0)).xyz; wNrm=vertexNormal; gl_Position=mvp*vec4(vertexPosition,1.0);}\n";

static const char *ROOM_FS =
    "#version 330\n"
    "in vec3 wPos; in vec3 wNrm; out vec4 o;\n"
    "uniform vec3 cFloor; uniform vec3 cWall; uniform vec3 cCeil; uniform vec3 cLine; uniform vec3 cFog; uniform float gridOn;\n"
    "float gridf(vec2 uv,float s,float w){ vec2 c=uv/s; vec2 fw=fwidth(c)*w; vec2 g=abs(fract(c-0.5)-0.5)/max(fw,vec2(1e-5)); return 1.0-clamp(min(g.x,g.y),0.0,1.0);}\n"
    "void main(){\n"
    "  vec3 n=abs(normalize(wNrm)); vec2 uv; vec3 base;\n"
    "  if(n.y>0.5){ uv=wPos.xz; base = wPos.y<0.0 ? cFloor : cCeil; }\n"
    "  else if(n.x>0.5){ uv=wPos.zy; base=cWall; } else { uv=wPos.xy; base=cWall*0.95; }\n"
    "  float g1=gridf(uv,2.5,1.0); float g2=gridf(uv,10.0,1.6);\n"
    "  vec3 col=base;\n"
    "  col=mix(col,cLine,gridOn*clamp(g1*0.35+g2*0.75,0.0,1.0));\n"
    "  if(n.y<0.5){ float h=clamp((wPos.y+9.0)/34.0,0.0,1.0); col*=mix(0.82,1.06,h); }\n"
    "  float d=length(wPos); float f=clamp((d-25.0)/80.0,0.0,1.0);\n"
    "  o=vec4(mix(col,cFog,f*0.55),1.0);\n"
    "}\n";

static const char *TGT_VS =
    "#version 330\n"
    "in vec3 vertexPosition; in vec3 vertexNormal;\n"
    "uniform mat4 mvp; uniform mat4 matModel;\n"
    "out vec3 wPos; out vec3 wNrm;\n"
    "void main(){ wPos=(matModel*vec4(vertexPosition,1.0)).xyz; wNrm=mat3(matModel)*vertexNormal; gl_Position=mvp*vec4(vertexPosition,1.0);}\n";

static const char *TGT_FS =
    "#version 330\n"
    "in vec3 wPos; in vec3 wNrm; out vec4 o;\n"
    "uniform vec4 colDiffuse; uniform vec3 viewPos; uniform float flatK;\n"
    "void main(){\n"
    "  vec3 n=normalize(wNrm); vec3 V=normalize(viewPos-wPos); vec3 L=normalize(vec3(0.35,0.8,0.45));\n"
    "  float lam=max(dot(n,L),0.0); vec3 H=normalize(L+V); float sp=pow(max(dot(n,H),0.0),48.0);\n"
    "  float rim=pow(1.0-max(dot(n,V),0.0),2.5);\n"
    "  vec3 c=colDiffuse.rgb;\n"
    "  vec3 lit=c*(0.40+0.70*lam)+vec3(sp*0.5)+c*rim*0.35;\n"
    "  vec3 fl=c*(0.95+0.15*rim);\n"
    "  o=vec4(mix(lit,fl,flatK),colDiffuse.a);\n"
    "}\n";

static Shader g_room, g_tgt;
static Model  g_room_model, g_sphere;
static int    loc_floor, loc_wall, loc_ceil, loc_line, loc_fog, loc_grid, loc_view, loc_flat;
static RenderTexture2D g_prev_rt;
static bool   g_prev_ok = false;

void render_init(void)
{
    g_room = LoadShaderFromMemory(ROOM_VS, ROOM_FS);
    g_tgt  = LoadShaderFromMemory(TGT_VS, TGT_FS);
    loc_floor = GetShaderLocation(g_room, "cFloor");
    loc_wall  = GetShaderLocation(g_room, "cWall");
    loc_ceil  = GetShaderLocation(g_room, "cCeil");
    loc_line  = GetShaderLocation(g_room, "cLine");
    loc_fog   = GetShaderLocation(g_room, "cFog");
    loc_grid  = GetShaderLocation(g_room, "gridOn");
    loc_view  = GetShaderLocation(g_tgt, "viewPos");
    loc_flat  = GetShaderLocation(g_tgt, "flatK");
    g_tgt.locs[SHADER_LOC_VECTOR_VIEW] = loc_view;

    g_room_model = LoadModelFromMesh(GenMeshCube(100.0f, 34.0f, 100.0f));
    g_room_model.materials[0].shader = g_room;
    g_sphere = LoadModelFromMesh(GenMeshSphere(1.0f, 28, 36));
    g_sphere.materials[0].shader = g_tgt;
}

void render_unload(void)
{
    UnloadModel(g_room_model);
    UnloadModel(g_sphere);
    UnloadShader(g_room);
    UnloadShader(g_tgt);
    if (g_prev_ok) UnloadRenderTexture(g_prev_rt);
}

Color render_clear_color(void) { return THEMES[S.theme].clear; }

Camera3D render_camera(float yaw, float pitch, float hfov, float aspect)
{
    Camera3D c = { 0 };
    Vector3 d = game_dir(yaw, pitch);
    c.position = (Vector3){ 0, 0, 0 };
    c.target = d;
    c.up = (Vector3){ 0, 1, 0 };
    float vf = 2.0f * atanf(tanf(hfov * 0.5f * DEG2RAD) / aspect) * RAD2DEG;
    c.fovy = vf;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

static void set_vec3(Shader sh, int loc, Color c)
{
    float v[3] = { c.r / 255.f, c.g / 255.f, c.b / 255.f };
    SetShaderValue(sh, loc, v, SHADER_UNIFORM_VEC3);
}

static void draw_room(void)
{
    const Theme *t = &THEMES[S.theme];
    set_vec3(g_room, loc_floor, t->floor);
    set_vec3(g_room, loc_wall, t->wall);
    set_vec3(g_room, loc_ceil, t->ceil);
    set_vec3(g_room, loc_line, t->line);
    set_vec3(g_room, loc_fog, t->clear);
    float gon = S.grid ? 1.f : 0.f;
    SetShaderValue(g_room, loc_grid, &gon, SHADER_UNIFORM_FLOAT);
    rlDisableBackfaceCulling();
    DrawModel(g_room_model, (Vector3){ 0, 8, 0 }, 1.0f, WHITE);
    rlEnableBackfaceCulling();
}

static Color lerpc(Color a, Color b, float k)
{
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k),
                    (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k) };
}

static void draw_sphere(Vector3 p, float r, Color c, float flatk)
{
    SetShaderValue(g_tgt, loc_flat, &flatk, SHADER_UNIFORM_FLOAT);
    DrawModel(g_sphere, p, r, c);
}

static void draw_target_styled(Vector3 p, float r, Color c)
{
    if (S.tg_style == TS_OUTLINE) {
        rlSetCullFace(RL_CULL_FACE_FRONT);
        draw_sphere(p, r * 1.10f, (Color){ 10, 10, 12, 255 }, 1.0f);
        rlSetCullFace(RL_CULL_FACE_BACK);
        draw_sphere(p, r, c, 0.0f);
    } else {
        draw_sphere(p, r, c, S.tg_style == TS_FLAT ? 1.0f : 0.0f);
    }
}

static void draw_zone_rect(float y0, float y1, float p0, float p1, float d, Color c)
{
    const int N = 24;
    for (int i = 0; i < N; i++) {
        float a = y0 + (y1 - y0) * i / N, b = y0 + (y1 - y0) * (i + 1) / N;
        Vector3 u0 = game_dir(a, p1), u1 = game_dir(b, p1), l0 = game_dir(a, p0), l1 = game_dir(b, p0);
        DrawLine3D(Vector3Scale(u0, d), Vector3Scale(u1, d), c);
        DrawLine3D(Vector3Scale(l0, d), Vector3Scale(l1, d), c);
    }
    for (int i = 0; i < 8; i++) {
        float a = p0 + (p1 - p0) * i / 8, b = p0 + (p1 - p0) * (i + 1) / 8;
        DrawLine3D(Vector3Scale(game_dir(y0, a), d), Vector3Scale(game_dir(y0, b), d), c);
        DrawLine3D(Vector3Scale(game_dir(y1, a), d), Vector3Scale(game_dir(y1, b), d), c);
    }
}

void render_world(const Game *g, Camera3D cam)
{
    BeginMode3D(cam);
    draw_room();

    if (g) {
        SetShaderValue(g_tgt, loc_view, &cam.position, SHADER_UNIFORM_VEC3);
        const Scenario *s = &g->sc;

        if (s->show_zone && S.show_zone) {
            Color zc = THEMES[S.theme].line; zc.a = 150;
            float d = s->dist * 0.999f;
            float p0 = s->pitch_off - s->spawn_pitch - 0.6f, p1 = s->pitch_off + s->spawn_pitch + 0.6f;
            if (s->yaw_min > 0) {
                draw_zone_rect(s->yaw_min, s->spawn_yaw, p0, p1, d, zc);
                draw_zone_rect(-s->spawn_yaw, -s->yaw_min, p0, p1, d, zc);
            } else {
                draw_zone_rect(-s->spawn_yaw - 0.6f, s->spawn_yaw + 0.6f, p0, p1, d, zc);
            }
        }

        if (g->phase != GP_COUNTDOWN) {
            for (int i = 0; i < s->count; i++) {
                const Target *t = &g->tg[i];
                if (!t->alive) continue;
                float r = game_target_radius(g, t, S.tg_size_mult);
                Color c = S.tg_color;
                if (t->tracked > 0) c = S.tg_track_color;
                if (s->shoot == SM_TRACK && s->hp > 0) {
                    // darken as hp drops
                    float k = 1.0f - t->hp / s->hp;
                    c = lerpc(c, (Color){ 255, 255, 255, 255 }, k * 0.45f);
                }
                if (t->flash > 0 && S.tg_hit_fx) c = lerpc(c, WHITE, t->flash / 0.08f * 0.7f);
                draw_target_styled(game_target_pos(t), r, c);
            }
            if (S.tg_hit_fx) {
                rlDisableDepthMask();
                for (int i = 0; i < 48; i++) {
                    const Fx *f = &g->fx[i];
                    if (!f->on) continue;
                    float k = f->t / 0.16f;
                    Color c = lerpc(S.tg_color, WHITE, 0.5f);
                    c.a = (unsigned char)(110 * (1.0f - k) * (1.0f - k));
                    draw_sphere(f->p, f->r * S.tg_size_mult * (1.0f + k * 0.55f), c, 1.0f);
                }
                rlEnableDepthMask();
            }
        }
    }
    EndMode3D();
}

void render_target_preview(Rectangle r)
{
    int w = (int)r.width, h = (int)r.height;
    if (!g_prev_ok || g_prev_rt.texture.width != w || g_prev_rt.texture.height != h) {
        if (g_prev_ok) UnloadRenderTexture(g_prev_rt);
        g_prev_rt = LoadRenderTexture(w, h);
        g_prev_ok = true;
    }
    BeginTextureMode(g_prev_rt);
    ClearBackground(THEMES[S.theme].clear);
    Camera3D cam = render_camera(0, 0, 40.0f, (float)w / (float)h);
    BeginMode3D(cam);
    draw_room();
    SetShaderValue(g_tgt, loc_view, &cam.position, SHADER_UNIFORM_VEC3);
    float t = (float)GetTime();
    draw_target_styled((Vector3){ -1.6f, 0.0f, -14 }, 1.05f * S.tg_size_mult, S.tg_color);
    draw_target_styled((Vector3){ 1.8f + sinf(t * 1.5f) * 0.3f, 0.2f, -14 }, 0.85f * S.tg_size_mult, S.tg_track_color);
    EndMode3D();
    EndTextureMode();
    DrawTexturePro(g_prev_rt.texture, (Rectangle){ 0, 0, (float)w, -(float)h }, r, (Vector2){ 0, 0 }, 0, WHITE);
}
