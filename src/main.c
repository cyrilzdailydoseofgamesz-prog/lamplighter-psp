// Lamplighter of Willowmere - PSP story game starter (PSPSDK / pspdev)
// Smooth-shaded 3D characters, dialogue, quests, animation. Runs in PPSSPP.
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("Lamplighter", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272
#define PI 3.14159265f
#define DT (1.0f / 60.0f)
#define RGB(r, g, b) (0xff000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define VF3 (GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define VF2 (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define MAXV 2600
#define NARR(a) ((int)(sizeof(a) / sizeof((a)[0])))

// If dialogue text looks mirrored, set this to 0.
#define FONT_MSB_FIRST 1

static unsigned int __attribute__((aligned(16))) list[262144];
extern unsigned char msx[]; // 8x8 font from libpspdebug

typedef struct { float nx, ny, nz, x, y, z; } V3;
typedef struct { unsigned int c; short x, y, z; } V2;

/* ------------------------------------------------------------------ */
/* Exit callback                                                       */
/* ------------------------------------------------------------------ */
static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_cb(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Smooth meshes (ellipsoids with true normals => no blocky look)      */
/* ------------------------------------------------------------------ */
enum { M_HEAD, M_TORSO, M_ROBE, M_LIMB, M_HAIR, M_EYE, M_ORB, M_FOLA, M_FOLB, M_TRUNK,
       M_ROCK, M_SHARD, M_CAP, M_POST, M_BIGORB, M_GROUND, M_SQUARE, M_POND, M_FLY, M_BLOB,
       M_COUNT };
static V3 mv[M_COUNT][MAXV] __attribute__((aligned(16)));
static int mn[M_COUNT];

static void pt(V3 *o, float rx, float ry, float rz, float cy, float t, float p) {
    float ux = sinf(t) * cosf(p), uy = cosf(t), uz = sinf(t) * sinf(p);
    float nx = ux / rx, ny = uy / ry, nz = uz / rz;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    o->nx = nx / l; o->ny = ny / l; o->nz = nz / l;
    o->x = rx * ux; o->y = ry * uy + cy; o->z = rz * uz;
}
static void build(int m, float rx, float ry, float rz, float cy, int seg, int rings) {
    V3 *o = mv[m];
    int n = 0;
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < seg; j++) {
            float t0 = PI * i / rings, t1 = PI * (i + 1) / rings;
            float p0 = 2 * PI * j / seg, p1 = 2 * PI * (j + 1) / seg;
            V3 a, b, c, d;
            pt(&a, rx, ry, rz, cy, t0, p0); pt(&b, rx, ry, rz, cy, t0, p1);
            pt(&c, rx, ry, rz, cy, t1, p0); pt(&d, rx, ry, rz, cy, t1, p1);
            o[n++] = a; o[n++] = c; o[n++] = b;
            o[n++] = b; o[n++] = c; o[n++] = d;
        }
    mn[m] = n;
}
static void build_disc(int m, float r, int seg) {
    V3 *o = mv[m];
    int n = 0;
    for (int j = 0; j < seg; j++) {
        float a0 = 2 * PI * j / seg, a1 = 2 * PI * (j + 1) / seg;
        V3 c = {0, 1, 0, 0, 0, 0};
        V3 p = {0, 1, 0, r * cosf(a0), 0, r * sinf(a0)};
        V3 q = {0, 1, 0, r * cosf(a1), 0, r * sinf(a1)};
        o[n++] = c; o[n++] = q; o[n++] = p;
    }
    mn[m] = n;
}
static void build_quad(int m, float h) {
    V3 *o = mv[m];
    V3 a = {0, 1, 0, -h, 0, -h}, b = {0, 1, 0, h, 0, -h};
    V3 c = {0, 1, 0, -h, 0, h}, d = {0, 1, 0, h, 0, h};
    o[0] = a; o[1] = c; o[2] = b; o[3] = b; o[4] = c; o[5] = d;
    mn[m] = 6;
}
static void build_meshes(void) {
    build(M_HEAD, 0.20f, 0.23f, 0.20f, 0, 18, 12);
    build(M_TORSO, 0.27f, 0.42f, 0.17f, 0, 16, 10);
    build(M_ROBE, 0.36f, 0.78f, 0.30f, 0, 18, 12);
    build(M_LIMB, 0.10f, 0.40f, 0.10f, -0.40f, 12, 8);
    build(M_HAIR, 0.215f, 0.20f, 0.22f, 0, 16, 10);
    build(M_EYE, 0.03f, 0.04f, 0.03f, 0, 6, 4);
    build(M_ORB, 0.09f, 0.09f, 0.09f, 0, 10, 8);
    build(M_FOLA, 1.40f, 1.20f, 1.40f, 0, 20, 14);
    build(M_FOLB, 1.00f, 1.60f, 1.00f, 0, 20, 14);
    build(M_TRUNK, 0.18f, 1.10f, 0.18f, 0, 10, 6);
    build(M_ROCK, 0.70f, 0.45f, 0.55f, 0, 12, 8);
    build(M_SHARD, 0.16f, 0.38f, 0.16f, 0, 8, 6);
    build(M_CAP, 0.20f, 0.12f, 0.20f, 0, 12, 8);
    build(M_POST, 0.08f, 1.70f, 0.08f, 0, 8, 6);
    build(M_BIGORB, 0.60f, 0.60f, 0.60f, 0, 20, 14);
    build(M_FLY, 0.04f, 0.04f, 0.04f, 0, 6, 4);
    build_quad(M_GROUND, 60.0f);
    build_disc(M_SQUARE, 7.0f, 40);
    build_disc(M_POND, 5.0f, 40);
    build_disc(M_BLOB, 0.5f, 14);
    sceKernelDcacheWritebackAll();
}

static void M(int m, unsigned int col, unsigned int emi) {
    sceGuModelColor(emi, col, col, 0xffffffff);
    sceGumDrawArray(GU_TRIANGLES, VF3, mn[m], 0, mv[m]);
}
static void tr(float x, float y, float z) {
    ScePspFVector3 v = {x, y, z};
    sceGumTranslate(&v);
}

/* ------------------------------------------------------------------ */
/* 2D text / rects (uses the PSP's built-in 8x8 font data)             */
/* ------------------------------------------------------------------ */
static inline int fbit(unsigned char b, int c) { return FONT_MSB_FIRST ? (b >> (7 - c)) & 1 : (b >> c) & 1; }

static void draw_rect(int x, int y, int w, int h, unsigned int col) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v[0].c = col; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].c = col; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VF2, 2, 0, v);
}
static void draw_text(int x, int y, const char *s, int n, int sc, unsigned int col) {
    int cnt = 0;
    for (int i = 0; i < n && s[i]; i++) {
        const unsigned char *g = msx + ((unsigned char)s[i]) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (fbit(g[r], c) && (c == 0 || !fbit(g[r], c - 1))) cnt++;
    }
    if (!cnt) return;
    V2 *v = (V2 *)sceGuGetMemory(cnt * 2 * sizeof(V2));
    int k = 0;
    for (int i = 0; i < n && s[i]; i++) {
        const unsigned char *g = msx + ((unsigned char)s[i]) * 8;
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (fbit(g[r], c) && (c == 0 || !fbit(g[r], c - 1))) {
                    int e = c;
                    while (e + 1 < 8 && fbit(g[r], e + 1)) e++;
                    int x0 = x + (i * 8 + c) * sc, y0 = y + r * sc;
                    int x1 = x + (i * 8 + e + 1) * sc, y1 = y0 + sc;
                    v[k].c = col; v[k].x = x0; v[k].y = y0; v[k].z = 0; k++;
                    v[k].c = col; v[k].x = x1; v[k].y = y1; v[k].z = 0; k++;
                }
    }
    sceGuDrawArray(GU_SPRITES, VF2, k, 0, v);
}
static void text_sh(int x, int y, const char *s, int sc, unsigned int col) {
    int n = (int)strlen(s);
    draw_text(x + sc, y + sc, s, n, sc, 0xff000000);
    draw_text(x, y, s, n, sc, col);
}
static void text_center(int y, const char *s, int sc, unsigned int col) {
    int w = (int)strlen(s) * 8 * sc;
    text_sh((SCR_W - w) / 2, y, s, sc, col);
}

/* ------------------------------------------------------------------ */
/* Game data                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, z, yaw, walk, move;
    int talk, robe, lantern;
    unsigned int shirt, pants, skin, hair;
} Char;

static Char pl, npc[3];
static const char *npcname[3] = {"Elder Maren", "Tobin", "Sera"};

static Char mk(float x, float z, float yaw, int robe, int lantern,
               unsigned int shirt, unsigned int pants, unsigned int skin, unsigned int hair) {
    Char c;
    memset(&c, 0, sizeof(c));
    c.x = x; c.z = z; c.yaw = yaw; c.robe = robe; c.lantern = lantern;
    c.shirt = shirt; c.pants = pants; c.skin = skin; c.hair = hair;
    return c;
}

typedef struct { float x, z, r; } Ob;
static Ob obs[80];
static int nobs;
static void add_ob(float x, float z, float r) { obs[nobs].x = x; obs[nobs].z = z; obs[nobs].r = r; nobs++; }
static void push_out(float *x, float *z, float r) {
    for (int i = 0; i < nobs; i++) {
        float dx = *x - obs[i].x, dz = *z - obs[i].z;
        float d = sqrtf(dx * dx + dz * dz), m = r + obs[i].r;
        if (d < m && d > 0.0001f) { *x = obs[i].x + dx / d * m; *z = obs[i].z + dz / d * m; }
    }
}

#define NTREE 28
#define NROCK 6
static float tx[NTREE], tz[NTREE], tyaw[NTREE];
static int ttype[NTREE];
static const float rockp[NROCK][2] = {{8.5f, 12.8f}, {-12, 3}, {-3, -13.5f}, {14, -3}, {-2, 12}, {18, 5}};
static const float shp[3][2] = {{-14, -6}, {15, -9}, {-4, 16}};
static const float capp[2] = {10, 14};
static const float lanternp[2] = {0, -7};

static unsigned int seed = 12345;
static float rnd(void) { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) & 0xffff) / 65536.0f; }

static void init_world(void) {
    pl = mk(0, 0, 0, 0, 1, RGB(230, 195, 90), RGB(70, 60, 110), RGB(245, 205, 175), RGB(150, 55, 40));
    npc[0] = mk(5, 3, -2.0f, 1, 0, RGB(110, 70, 150), 0, RGB(240, 200, 170), RGB(230, 230, 235));
    npc[1] = mk(-8, 7, 0, 0, 0, RGB(220, 140, 40), RGB(70, 90, 150), RGB(235, 190, 150), RGB(120, 70, 30));
    npc[2] = mk(3, -12, 0, 0, 0, RGB(40, 150, 140), RGB(60, 50, 40), RGB(190, 140, 100), RGB(35, 30, 40));
    for (int i = 0; i < NTREE; i++) {
        float a = rnd() * 2 * PI, r = 21.0f + rnd() * 12.0f;
        tx[i] = cosf(a) * r; tz[i] = sinf(a) * r; tyaw[i] = rnd() * 6.28f;
        ttype[i] = rnd() > 0.5f;
        add_ob(tx[i], tz[i], 0.7f);
    }
    for (int i = 0; i < NROCK; i++) add_ob(rockp[i][0], rockp[i][1], 0.8f);
    add_ob(lanternp[0], lanternp[1], 0.7f);
    for (int i = 0; i < 3; i++) add_ob(npc[i].x, npc[i].z, 0.5f);
}

/* ------------------------------------------------------------------ */
/* Character drawing + animation                                       */
/* ------------------------------------------------------------------ */
static void draw_char(const Char *c, float t) {
    float sw = sinf(c->walk) * 0.85f * c->move;
    float bob = fabsf(sinf(c->walk)) * 0.06f * c->move + sinf(t * 2.0f) * 0.008f;
    sceGumMatrixMode(GU_MODEL);

    sceGumPushMatrix(); tr(c->x, 0.06f, c->z); M(M_BLOB, 0x60000000, 0x60000000); sceGumPopMatrix();

    sceGumPushMatrix();
    tr(c->x, bob, c->z);
    sceGumRotateY(c->yaw);
    if (!c->robe) {
        for (int s = -1; s <= 1; s += 2) {
            sceGumPushMatrix(); tr(s * 0.13f, 0.84f, 0); sceGumRotateX(s * sw);
            M(M_LIMB, c->pants, 0); sceGumPopMatrix();
        }
        sceGumPushMatrix(); tr(0, 1.2f, 0); M(M_TORSO, c->shirt, 0); sceGumPopMatrix();
    } else {
        sceGumPushMatrix(); tr(0, 0.93f, 0); M(M_ROBE, c->shirt, 0); sceGumPopMatrix();
    }
    for (int s = -1; s <= 1; s += 2) {
        float a = -s * sw;
        if (c->talk && s == 1) a = -1.1f + sinf(t * 7.0f) * 0.35f;
        sceGumPushMatrix(); tr(s * 0.34f, 1.56f, 0);
        sceGumRotateX(a); sceGumRotateZ(s * 0.12f);
        M(M_LIMB, c->shirt, 0);
        if (c->lantern && s == -1) {
            sceGumPushMatrix(); tr(0, -0.82f, 0.08f);
            M(M_ORB, RGB(255, 220, 120), RGB(255, 200, 90));
            sceGumPopMatrix();
        }
        sceGumPopMatrix();
    }
    float nod = c->talk ? sinf(t * 5.0f) * 0.08f : 0.0f;
    sceGumPushMatrix(); tr(0, 1.86f, 0); sceGumRotateX(nod);
    M(M_HEAD, c->skin, 0);
    sceGumPushMatrix(); tr(0, 0.09f, -0.05f); M(M_HAIR, c->hair, 0); sceGumPopMatrix();
    for (int s = -1; s <= 1; s += 2) {
        sceGumPushMatrix(); tr(s * 0.075f, 0.03f, 0.17f); M(M_EYE, RGB(30, 30, 40), 0); sceGumPopMatrix();
    }
    sceGumPopMatrix();
    sceGumPopMatrix();
}

/* ------------------------------------------------------------------ */
/* Dialogue + quests                                                   */
/* ------------------------------------------------------------------ */
typedef struct { const char *who; const char *txt; } Line;

static const Line L_M1[] = {
    {"Elder Maren", "Aria! Thank heavens. The Great Lantern has gone dark."},
    {"Aria", "The one that guards Willowmere through the night?"},
    {"Elder Maren", "Yes. Its Glowshards scattered into the wilds."},
    {"Elder Maren", "Bring me three, and we can light it again."},
    {"Aria", "Leave it to me, Elder. I'll be back by dusk."}};
static const Line L_M2[] = {{"Elder Maren", "Three Glowshards, Aria. They shine blue in the grass."}};
static const Line L_M3[] = {
    {"Elder Maren", "Three shards! Your lamplighter blood runs true."},
    {"Aria", "Will it be enough to wake the Great Lantern?"},
    {"Elder Maren", "Stand back, child. Let the old light remember."},
    {"Elder Maren", "...There! Look how it shines! Willowmere is safe."},
    {"Elder Maren", "But the light showed me something in the north..."}};
static const Line L_M4[] = {{"Elder Maren", "The Great Lantern burns bright. Rest, hero of Willowmere."}};

static const Line L_T1[] = {
    {"Tobin", "Psst! Are you the lamplighter? I lost my red cap!"},
    {"Tobin", "I was chasing a frog by the east trees. Can you look?"},
    {"Aria", "A red cap by the east trees. I'll keep an eye out."}};
static const Line L_T2[] = {{"Tobin", "East trees, past the big rock. Please hurry!"}};
static const Line L_T3[] = {
    {"Aria", "Is this your cap, Tobin?"},
    {"Tobin", "My cap! You're the best! Take my lucky charm."},
    {"Tobin", "Mum says it brings good light. Maybe it'll help you!"}};
static const Line L_T4[] = {{"Tobin", "I'm never chasing frogs again. ...Maybe tomorrow."}};

static const Line L_S1[] = {
    {"Sera", "Travelers say Willowmere's lantern never sleeps. Odd."},
    {"Sera", "Whatever took its light, the wilds hum strangely tonight."}};
static const Line L_S2[] = {
    {"Sera", "Ha! I saw that glow from the road. Impressive, lamplighter."},
    {"Sera", "The north trail is calling me. Maybe we'll meet out there."}};

enum { A_NONE, A_START_MAIN, A_FINISH_MAIN, A_START_CAP, A_FINISH_CAP };
enum { ST_TITLE, ST_PLAY, ST_TALK };

static float gt = 0, cam_yaw = 0, cam_d = 6.5f, bant = 0;
static int state = ST_TITLE;
static int q_main = 0, q_cap = 0, shards = 0, charm = 0, lantern_lit = 0;
static int got[3];
static char bantxt[40];

static const Line *dl;
static int dn, di, dact, dnpc, typing;
static float dtime;
static char wrapped[256];

static void banner(const char *s) { snprintf(bantxt, sizeof(bantxt), "%s", s); bant = 3.5f; }

static void wrap(const char *s) {
    int w = 26, col = 0, o = 0;
    const char *p = s;
    while (*p && o < 240) {
        const char *q = p;
        int len = 0;
        while (*q && *q != ' ') { q++; len++; }
        if (col > 0 && col + 1 + len > w) { wrapped[o++] = '\n'; col = 0; }
        else if (col > 0) { wrapped[o++] = ' '; col++; }
        for (int k = 0; k < len; k++) { wrapped[o++] = p[k]; col++; }
        p = q;
        while (*p == ' ') p++;
    }
    wrapped[o] = 0;
}
static void say(const Line *l, int n, int act, int who) {
    dl = l; dn = n; di = 0; dact = act; dnpc = who; dtime = 0;
    wrap(dl[0].txt);
    state = ST_TALK;
}
static void run_action(int a) {
    switch (a) {
    case A_START_MAIN: q_main = 1; banner("New quest: Glowshards"); break;
    case A_FINISH_MAIN: q_main = 3; lantern_lit = 1; banner("The Great Lantern glows!"); break;
    case A_START_CAP: q_cap = 1; banner("New quest: Red Cap"); break;
    case A_FINISH_CAP: q_cap = 3; charm = 1; banner("Got: Lucky Charm"); break;
    default: break;
    }
}
static void interact(int i) {
    if (i == 0) {
        if (q_main == 0) say(L_M1, NARR(L_M1), A_START_MAIN, i);
        else if (q_main == 1 && shards < 3) say(L_M2, NARR(L_M2), A_NONE, i);
        else if (q_main == 1) say(L_M3, NARR(L_M3), A_FINISH_MAIN, i);
        else say(L_M4, NARR(L_M4), A_NONE, i);
    } else if (i == 1) {
        if (q_cap == 0) say(L_T1, NARR(L_T1), A_START_CAP, i);
        else if (q_cap == 1) say(L_T2, NARR(L_T2), A_NONE, i);
        else if (q_cap == 2) say(L_T3, NARR(L_T3), A_FINISH_CAP, i);
        else say(L_T4, NARR(L_T4), A_NONE, i);
    } else {
        if (q_main < 3) say(L_S1, NARR(L_S1), A_NONE, i);
        else say(L_S2, NARR(L_S2), A_NONE, i);
    }
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */
static int nearnpc = -1;
static float nearshard = 999;

static float angdiff(float a, float b) {
    float d = b - a;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}
static float dist(float ax, float az, float bx, float bz) {
    float dx = ax - bx, dz = az - bz;
    return sqrtf(dx * dx + dz * dz);
}

static void update(const SceCtrlData *pd, unsigned int pr) {
    gt += DT;
    if (bant > 0) bant -= DT;

    for (int i = 0; i < 3; i++) {
        Char *c = &npc[i];
        float d = dist(pl.x, pl.z, c->x, c->z);
        if (d < 7.0f || (state == ST_TALK && dnpc == i))
            c->yaw += angdiff(c->yaw, atan2f(pl.x - c->x, pl.z - c->z)) * fminf(1.0f, 5.0f * DT);
        c->talk = (state == ST_TALK && dnpc == i && typing);
    }

    if (state == ST_TITLE) {
        if (pr & PSP_CTRL_CROSS) { state = ST_PLAY; banner("Find Elder Maren"); }
        return;
    }

    if (state == ST_TALK) {
        const Char *n = &npc[dnpc];
        pl.move += (0 - pl.move) * fminf(1.0f, 10.0f * DT);
        pl.yaw += angdiff(pl.yaw, atan2f(n->x - pl.x, n->z - pl.z)) * fminf(1.0f, 6.0f * DT);
        cam_yaw += angdiff(cam_yaw, atan2f(n->x - pl.x, n->z - pl.z)) * fminf(1.0f, 3.0f * DT);
        dtime += DT;
        int len = (int)strlen(wrapped), vis = (int)(dtime * 45.0f);
        typing = vis < len;
        if (pr & PSP_CTRL_CROSS) {
            if (typing) dtime = 999.0f;
            else {
                di++;
                if (di >= dn) { run_action(dact); state = ST_PLAY; typing = 0; }
                else { wrap(dl[di].txt); dtime = 0; }
            }
        }
        return;
    }

    /* ST_PLAY */
    float sx = (pd->Lx - 128) / 128.0f, sy = (pd->Ly - 128) / 128.0f;
    if (fabsf(sx) < 0.22f) sx = 0;
    if (fabsf(sy) < 0.22f) sy = 0;
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag > 1.0f) mag = 1.0f;
    if (pd->Buttons & PSP_CTRL_LTRIGGER) cam_yaw -= 1.8f * DT;
    if (pd->Buttons & PSP_CTRL_RTRIGGER) cam_yaw += 1.8f * DT;
    float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
    float mx = fx * (-sy) + (-fz) * sx, mz = fz * (-sy) + fx * sx;
    float target = 0;
    if (mag > 0) {
        float l = sqrtf(mx * mx + mz * mz);
        mx /= l; mz /= l;
        float sp = ((pd->Buttons & PSP_CTRL_CIRCLE) ? 6.5f : 3.8f) * mag;
        pl.x += mx * sp * DT; pl.z += mz * sp * DT;
        pl.walk += sp * DT * 2.4f;
        pl.yaw += angdiff(pl.yaw, atan2f(mx, mz)) * fminf(1.0f, 12.0f * DT);
        target = 1;
    }
    pl.move += (target - pl.move) * fminf(1.0f, 10.0f * DT);
    push_out(&pl.x, &pl.z, 0.45f);
    float rr = dist(pl.x, pl.z, 0, 0);
    if (rr > 34.0f) { pl.x *= 34.0f / rr; pl.z *= 34.0f / rr; }
    cam_d += (6.5f - cam_d) * fminf(1.0f, 3.0f * DT);

    /* pickups */
    nearshard = 999;
    if (q_main == 1)
        for (int i = 0; i < 3; i++) {
            if (got[i]) continue;
            float d = dist(pl.x, pl.z, shp[i][0], shp[i][1]);
            if (d < nearshard) nearshard = d;
            if (d < 1.4f) {
                got[i] = 1; shards++;
                char b[40];
                if (shards >= 3) snprintf(b, sizeof(b), "All Glowshards found!");
                else snprintf(b, sizeof(b), "Glowshard %d/3", shards);
                banner(b);
            }
        }
    if (q_cap == 1 && dist(pl.x, pl.z, capp[0], capp[1]) < 1.4f) { q_cap = 2; banner("Found Tobin's red cap!"); }

    /* talking */
    nearnpc = -1;
    float best = 2.8f;
    for (int i = 0; i < 3; i++) {
        float d = dist(pl.x, pl.z, npc[i].x, npc[i].z);
        if (d < best) { best = d; nearnpc = i; }
    }
    if (nearnpc >= 0 && (pr & PSP_CTRL_CROSS)) { cam_d = 6.5f; interact(nearnpc); }
}

/* ------------------------------------------------------------------ */
/* Render                                                              */
/* ------------------------------------------------------------------ */
static void render_scene(float t) {
    ScePspFVector3 eye, ctr, up = {0, 1, 0};
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumPerspective(55.0f, 16.0f / 9.0f, 1.0f, 100.0f);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    if (state == ST_TITLE) {
        float a = t * 0.15f;
        eye.x = sinf(a) * 16.0f; eye.y = 5.5f; eye.z = -3.0f + cosf(a) * 16.0f;
        ctr.x = 0; ctr.y = 2.0f; ctr.z = -3.0f;
    } else {
        float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
        eye.x = pl.x - fx * cam_d; eye.y = 2.4f + cam_d * 0.15f; eye.z = pl.z - fz * cam_d;
        ctr.x = pl.x; ctr.y = 1.4f; ctr.z = pl.z;
    }
    sceGumLookAt(&eye, &ctr, &up);
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();

    ScePspFVector3 ld = {0.5f, 1.0f, 0.4f};
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &ld);

    /* ground */
    M(M_GROUND, RGB(86, 150, 70), 0);
    sceGumPushMatrix(); tr(0, 0.05f, 0); M(M_SQUARE, RGB(176, 146, 104), 0); sceGumPopMatrix();
    sceGumPushMatrix(); tr(-10, 0.08f, -12); M(M_POND, RGB(60, 120, 190), RGB(18, 36, 64)); sceGumPopMatrix();

    /* rocks */
    for (int i = 0; i < NROCK; i++) {
        sceGumPushMatrix(); tr(rockp[i][0], 0.3f, rockp[i][1]); sceGumRotateY(i * 1.7f);
        M(M_ROCK, RGB(130, 130, 140), 0); sceGumPopMatrix();
    }

    /* trees with gentle sway */
    static const unsigned int greens[3] = {RGB(48, 120, 56), RGB(70, 140, 50), RGB(40, 100, 70)};
    for (int i = 0; i < NTREE; i++) {
        sceGumPushMatrix(); tr(tx[i], 0, tz[i]); sceGumRotateY(tyaw[i]);
        sceGumPushMatrix(); tr(0, 1.1f, 0); M(M_TRUNK, RGB(100, 70, 45), 0); sceGumPopMatrix();
        sceGumPushMatrix(); tr(0, ttype[i] ? 3.4f : 3.0f, 0); sceGumRotateZ(sinf(t * 1.2f + i) * 0.03f);
        M(ttype[i] ? M_FOLB : M_FOLA, greens[i % 3], 0); sceGumPopMatrix();
        sceGumPopMatrix();
    }

    /* the Great Lantern */
    sceGumPushMatrix(); tr(lanternp[0], 0, lanternp[1]);
    sceGumPushMatrix(); tr(0, 1.7f, 0); M(M_POST, RGB(70, 60, 55), 0); sceGumPopMatrix();
    sceGumPushMatrix(); tr(0, 3.9f, 0);
    if (lantern_lit) {
        int v = (int)(200 + 55 * sinf(t * 3.0f));
        M(M_BIGORB, RGB(255, 220, 130), RGB(v, v * 8 / 10, v * 35 / 100));
    } else {
        M(M_BIGORB, RGB(70, 70, 80), 0);
    }
    sceGumPopMatrix();
    sceGumPopMatrix();

    if (lantern_lit)
        for (int i = 0; i < 14; i++) {
            float a = t * (0.4f + 0.05f * (i % 5)) + i * 1.7f, r = 2.0f + (i % 4) * 1.2f;
            sceGumPushMatrix();
            tr(lanternp[0] + sinf(a) * r, 1.4f + sinf(t * 1.3f + i) * 0.8f + (i % 3) * 0.4f, lanternp[1] + cosf(a) * r);
            M(M_FLY, RGB(255, 240, 150), RGB(255, 240, 150));
            sceGumPopMatrix();
        }

    /* quest items */
    if (q_main == 1)
        for (int i = 0; i < 3; i++)
            if (!got[i]) {
                sceGumPushMatrix(); tr(shp[i][0], 1.0f + sinf(t * 2.0f + i) * 0.2f, shp[i][1]);
                sceGumRotateY(t * 1.5f); M(M_SHARD, RGB(120, 220, 255), RGB(60, 160, 220)); sceGumPopMatrix();
            }
    if (q_cap == 1) {
        sceGumPushMatrix(); tr(capp[0], 0.15f, capp[1]); M(M_CAP, RGB(210, 40, 40), 0); sceGumPopMatrix();
    }

    for (int i = 0; i < 3; i++) draw_char(&npc[i], t);
    draw_char(&pl, t);
}

static void render_ui(void) {
    char b1[40], b2[40], b3[40];
    b1[0] = b2[0] = b3[0] = 0;

    if (state == ST_TITLE) {
        draw_rect(0, 0, SCR_W, SCR_H, 0x40000000);
        text_center(50, "LAMPLIGHTER", 4, RGB(255, 220, 120));
        text_center(100, "of Willowmere", 2, RGB(255, 255, 255));
        if (((int)(gt * 2)) % 2 == 0) text_center(190, "Press X to begin", 2, RGB(255, 255, 255));
        text_center(240, "Stick:move O:run L/R:camera X:talk", 1, RGB(220, 220, 220));
        return;
    }

    /* objectives */
    if (q_main == 0) snprintf(b1, sizeof(b1), "Talk to Elder Maren");
    else if (q_main == 1 && shards < 3) snprintf(b1, sizeof(b1), "Glowshards: %d/3", shards);
    else if (q_main == 1) snprintf(b1, sizeof(b1), "Return to Elder Maren");
    else snprintf(b1, sizeof(b1), "Chapter 1 complete!");
    if (q_cap == 1) snprintf(b2, sizeof(b2), "Find Tobin's red cap");
    else if (q_cap == 2) snprintf(b2, sizeof(b2), "Return cap to Tobin");
    if (q_main == 1 && shards < 3 && nearshard < 900) snprintf(b3, sizeof(b3), "Nearest: %dm", (int)nearshard);
    int lines = 1 + (b2[0] ? 1 : 0) + (b3[0] ? 1 : 0);
    int wmax = (int)strlen(b1);
    if ((int)strlen(b2) > wmax) wmax = (int)strlen(b2);
    if ((int)strlen(b3) > wmax) wmax = (int)strlen(b3);
    draw_rect(6, 6, wmax * 16 + 14, lines * 18 + 8, 0x90000000);
    int y = 10;
    text_sh(12, y, b1, 2, RGB(255, 230, 120)); y += 18;
    if (b2[0]) { text_sh(12, y, b2, 2, RGB(255, 255, 255)); y += 18; }
    if (b3[0]) { text_sh(12, y, b3, 2, RGB(140, 220, 255)); }
    if (charm) text_sh(SCR_W - 150, 10, "Lucky Charm", 2, RGB(255, 180, 220));

    if (bant > 0) {
        int w = (int)strlen(bantxt) * 16;
        draw_rect((SCR_W - w) / 2 - 8, 40, w + 16, 26, 0xA0000000);
        text_sh((SCR_W - w) / 2, 45, bantxt, 2, RGB(255, 255, 255));
    }

    if (state == ST_PLAY && nearnpc >= 0) {
        char p[40];
        snprintf(p, sizeof(p), "X: Talk to %s", npcname[nearnpc]);
        draw_rect(80, 226, 320, 26, 0x90000000);
        text_center(231, p, 2, RGB(255, 255, 255));
    }

    if (state == ST_TALK) {
        draw_rect(10, 182, 460, 86, 0xC0000000);
        draw_text(20, 188, dl[di].who, (int)strlen(dl[di].who), 2, RGB(255, 220, 90));
        int len = (int)strlen(wrapped), vis = (int)(dtime * 45.0f);
        if (vis > len) vis = len;
        int shown = 0, yy = 207;
        const char *p = wrapped;
        while (*p && shown < vis) {
            int ln = 0;
            while (p[ln] && p[ln] != '\n') ln++;
            int take = ln;
            if (shown + take > vis) take = vis - shown;
            draw_text(20, yy, p, take, 2, RGB(255, 255, 255));
            shown += ln; p += ln;
            if (*p == '\n') p++;
            yy += 18;
        }
        if (vis >= len && ((int)(gt * 3)) % 2 == 0) draw_text(440, 246, ">", 1, 2, RGB(255, 255, 255));
    }
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xffe0b070);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_LIGHTING);
    sceGuEnable(GU_LIGHT0);
    sceGuEnable(GU_FOG);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(22.0f, 62.0f, 0xffe0b070);
    sceGuAmbient(0xff606060);
    sceGuLightColor(0, GU_DIFFUSE, 0xffd8d8d8);
    sceGuShadeModel(GU_SMOOTH);
    render_scene(gt);

    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_DEPTH_TEST);
    render_ui();

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void gu_setup(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_cb();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    build_meshes();
    init_world();
    gu_setup();

    SceCtrlData pad;
    unsigned int prev = 0;
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pr = pad.Buttons & ~prev;
        prev = pad.Buttons;
        update(&pad, pr);
        render();
    }
    return 0;
}
