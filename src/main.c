// Lamplighter of Willowmere v2 - PSP story game (PSPSDK / pspdev)
// v2 renderer: textured meshes with baked vertex lighting (no GE lighting/fog/blend in 3D).
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
#define RGBA(r, g, b, a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define CR(c) ((float)((c) & 255))
#define CG(c) ((float)(((c) >> 8) & 255))
#define CB(c) ((float)(((c) >> 16) & 255))
#define NARR(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define VFT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define VF2 (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define POOL 170000
#define MAXMESH 240
#define HALF 128.0f
#define GN 64
#define WLEVEL (-1.3f)
#define FONT_MSB_FIRST 1

static unsigned int __attribute__((aligned(16))) list[262144];
extern unsigned char msx[];

typedef struct { float u, v; unsigned int c; float x, y, z; } VT;
typedef struct { unsigned int c; short x, y, z; } V2;
typedef struct { float x, y, z, nx, ny, nz, u, v; unsigned int t; } PV;

/* ===================== exit callback ===================== */
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

/* ===================== math / noise ===================== */
static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static float dist(float ax, float az, float bx, float bz) { float dx = ax - bx, dz = az - bz; return sqrtf(dx * dx + dz * dz); }
static float angdiff(float a, float b) {
    float d = b - a;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}
static unsigned int pack(float r, float g, float b) {
    int R = (int)r, G = (int)g, B = (int)b;
    if (R < 0) R = 0; if (R > 255) R = 255;
    if (G < 0) G = 0; if (G > 255) G = 255;
    if (B < 0) B = 0; if (B > 255) B = 255;
    return 0xff000000u | ((unsigned)B << 16) | ((unsigned)G << 8) | (unsigned)R;
}
static unsigned int mul(unsigned int c, float k) { return pack(CR(c) * k, CG(c) * k, CB(c) * k); }

static float hash2(int x, int y, int s) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffff) / 65535.0f;
}
static float vn(float x, float y, int px, int py, int s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    int x0 = ((xi % px) + px) % px, x1 = (x0 + 1) % px, y0 = ((yi % py) + py) % py, y1 = (y0 + 1) % py;
    float a = hash2(x0, y0, s), b = hash2(x1, y0, s), c = hash2(x0, y1, s), d = hash2(x1, y1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float fx, float fy, int per, int oct, int s) {
    float v = 0, a = 0.5f, tot = 0;
    int p = per;
    for (int o = 0; o < oct; o++) { v += a * vn(fx * p, fy * p, p, p, s + o); tot += a; a *= 0.5f; p *= 2; }
    return v / tot;
}

/* ===================== flags / state ===================== */
enum { F_CAP_Q, F_CAPFOUND, F_CAP_DONE, F_BERRY_Q, F_BERRIES4, F_BERRY_DONE, F_FOX_Q, F_FOXFOUND, F_FOX_DONE,
       F_SHARDS3, F_PETALS5, F_PLANKS4, F_ORE3, F_BRAZ4, F_WISPS8, F_BRIDGE, F_SERA, F_KEY, F_STONES, F_GATE,
       F_LIT, F_FRAG1, F_FRAG2, F_FRAG3, F_BRAN_MET, NFLAG };
enum { G_SHARD, G_BERRY, G_PETAL, G_PLANK, G_ORE, G_BRAZ, G_WISP, G_CAP, G_FOX, NGROUP };
enum { ST_TITLE, ST_PLAY, ST_TALK, ST_END };
enum { P_STONE0 = 20, P_BRAZ0 = 23, P_ALTAR = 27, P_LANTERN = 28 };
enum { A_NONE, A_END1, A_END2, A_END3 };

static int fl[64];
static int state = ST_TITLE, stage = 0;
static int gcount[NGROUP];
static const struct { const char *name; int target, donef; } GDEF[NGROUP] = {
    {"Glowshard", 3, F_SHARDS3}, {"Sunberry", 4, F_BERRIES4}, {"Moonpetal", 5, F_PETALS5},
    {"Plank", 4, F_PLANKS4}, {"Iron ore", 3, F_ORE3}, {"Brazier", 4, F_BRAZ4},
    {"Wisp", 8, F_WISPS8}, {"Red cap", 1, F_CAPFOUND}, {"Fox cub", 1, F_FOXFOUND}};

/* ===================== terrain ===================== */
static float terrain(float x, float z) {
    float d0 = sqrtf(x * x + z * z);
    float vf = sstep(14, 34, d0);
    float lx = x + 75, lz = z + 45;
    float lm = expf(-(lx * lx + lz * lz) / (2 * 26 * 26));
    float h = (1.5f * sinf(x * 0.07f + 1.0f) * cosf(z * 0.06f) + 0.9f * sinf(z * 0.13f + x * 0.05f) +
               0.5f * sinf(x * 0.21f) * sinf(z * 0.19f)) * vf * (1 - lm);
    float mx = x - 80, mz = z + 75;
    float m = 26.0f * expf(-(mx * mx + mz * mz) / (2 * 18 * 18));
    if (m > 18) m = 18 + (m - 18) * 0.15f;
    float dl = lx * lx + lz * lz;
    h += m - 3.4f * expf(-dl / (2 * 14 * 14)) + 4.6f * expf(-dl / (2 * 5.4f * 5.4f));
    return h;
}
static void tnorm(float x, float z, float *nx, float *ny, float *nz) {
    float e = 1.0f;
    float ax = terrain(x - e, z) - terrain(x + e, z), az = terrain(x, z - e) - terrain(x, z + e), ay = 2 * e;
    float l = sqrtf(ax * ax + ay * ay + az * az);
    *nx = ax / l; *ny = ay / l; *nz = az / l;
}
static int on_bridge(float x, float z) { return fl[F_BRIDGE] && x < -51.5f && x > -71.5f && fabsf(z + 45.0f) < 1.5f; }
static float ground_y(float x, float z) {
    float h = terrain(x, z);
    if (on_bridge(x, z) && h < -0.2f) h = -0.2f;
    return h;
}
static int canwalk(float x, float z) {
    if (dist(x, z, 0, 0) > 118.0f) return 0;
    if (!fl[F_GATE] && dist(x, z, 80, -75) < 27.5f) return 0;
    if (terrain(x, z) < -0.95f && !on_bridge(x, z)) return 0;
    return 1;
}

/* ===================== procedural textures ===================== */
enum { T_GRASS, T_DIRT, T_COBBLE, T_BARK, T_LEAF, T_ROOF, T_PLASTER, T_WATER, T_CLOTH, T_SKIN, T_HAIR, T_ROCK, T_WHITE, NTEX };
static unsigned int tex[NTEX][64 * 64] __attribute__((aligned(16)));

static void gen_tex(int id) {
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float fx = x / 64.0f, fy = y / 64.0f, r = 255, g = 255, b = 255;
            float n = fbm(fx, fy, 4, 3, id * 7);
            switch (id) {
            case T_GRASS: {
                float bl = vn(fx * 40, fy * 6, 40, 6, id);
                float k = 0.66f + 0.5f * n + (bl > 0.66f ? 0.14f : 0.0f) - (vn(fx * 24, fy * 24, 24, 24, id + 3) > 0.9f ? 0.2f : 0.0f);
                r = g = b = 255 * k; break; }
            case T_DIRT: {
                float k = 0.75f + 0.5f * n;
                if (vn(fx * 20, fy * 20, 20, 20, 5) > 0.8f) k *= 0.7f;
                r = 160 * k; g = 124 * k; b = 88 * k; break; }
            case T_COBBLE: {
                int row = y / 16, xo = (x + (row & 1) * 16) & 63, lx = xo % 32, ly = y % 16;
                float st = 0.72f + 0.28f * hash2(row, xo / 32, 9);
                int d = lx; if (32 - lx < d) d = 32 - lx; if (ly < d) d = ly; if (15 - ly < d) d = 15 - ly;
                if (d < 2) { r = 66; g = 62; b = 58; }
                else { float e = d < 5 ? 0.75f + 0.05f * d : 1.0f; float k = st * e * (0.9f + 0.2f * n); r = 172 * k; g = 166 * k; b = 154 * k; }
                break; }
            case T_BARK: {
                float s = vn(fx * 10, fy * 2, 10, 2, id) * 0.6f + vn(fx * 28, fy * 4, 28, 4, id + 1) * 0.4f;
                float k = 0.45f + 0.9f * s; r = 110 * k; g = 76 * k; b = 48 * k; break; }
            case T_LEAF: {
                float s = fbm(fx, fy, 8, 3, id), sp = vn(fx * 24, fy * 24, 24, 24, id + 2);
                r = 60 + 44 * s; g = 128 + 66 * s; b = 50 + 26 * s;
                if (sp > 0.72f) { r += 20; g += 40; } else if (sp < 0.18f) { r *= 0.7f; g *= 0.75f; b *= 0.7f; }
                break; }
            case T_ROOF: {
                int row = y / 8, xo = (x + (row & 1) * 8) & 63, lx = xo % 16, ly = y % 8;
                float st = 0.78f + 0.22f * hash2(row, xo / 16, 3);
                float edge = ly > 5 ? 0.55f + 0.1f * (7 - ly) : 1.0f;
                if (lx < 1) edge *= 0.7f;
                r = g = b = 235 * st * edge * (0.9f + 0.2f * n); break; }
            case T_PLASTER: {
                float k = 0.88f + 0.18f * n + 0.05f * (vn(fx * 32, fy * 32, 32, 32, id) - 0.5f);
                r = 236 * k; g = 226 * k; b = 204 * k; break; }
            case T_WATER: {
                float w = sinf((fx * 3 + n * 0.9f) * 6.2832f) + sinf((fy * 4 + n * 0.8f) * 6.2832f);
                float c = 0.5f + 0.25f * w;
                r = 40 + 60 * c * c; g = 110 + 90 * c; b = 185 + 60 * c;
                if (w > 1.5f) { r += 60; g += 50; b += 30; }
                break; }
            case T_CLOTH: {
                int w = ((x >> 1) + (y >> 1)) & 1;
                float k = 0.86f + 0.08f * w + 0.08f * n; r = g = b = 255 * k; break; }
            case T_SKIN: { float k = 0.94f + 0.08f * n; r = g = b = 255 * k; break; }
            case T_HAIR: {
                float s = vn(fx * 32, fy * 3, 32, 3, id); float k = 0.6f + 0.5f * s; r = g = b = 255 * k; break; }
            case T_ROCK: {
                float s = fbm(fx, fy, 6, 4, id), cr = vn(fx * 10, fy * 10, 10, 10, id + 4);
                float k = 0.55f + 0.7f * s; if (fabsf(cr - 0.5f) < 0.03f) k *= 0.55f;
                r = 140 * k; g = 140 * k; b = 148 * k; break; }
            default: break;
            }
            tex[id][y * 64 + x] = pack(r, g, b);
        }
}

/* ===================== mesh pool + builders ===================== */
typedef struct { VT *v; int n; int tex; } Mesh;
static VT pool[POOL] __attribute__((aligned(16)));
static VT scratch[8];
static int pool_n = 0, nmesh = 0, cur = 0, mstart = 0, bake_lit = 1;
static Mesh meshes[MAXMESH];

static VT *alloc(int n) {
    if (pool_n + n > POOL) return scratch;
    VT *p = &pool[pool_n];
    pool_n += n;
    return p;
}
static int mbegin(int t) {
    cur = nmesh++;
    meshes[cur].v = &pool[pool_n]; meshes[cur].tex = t; meshes[cur].n = 0;
    mstart = pool_n;
    return cur;
}
static void mend(void) { meshes[cur].n = pool_n - mstart; }

static unsigned int shade(unsigned int tint, float nx, float ny, float nz) {
    if (!bake_lit) return tint;
    float d = nx * 0.42f + ny * 0.80f + nz * 0.43f;
    if (d < 0) d = 0;
    float k = 0.42f + 0.14f * ny + 0.62f * d;
    return mul(tint, k);
}
static void put(VT *o, PV p) {
    float l = sqrtf(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz);
    float nx = p.nx, ny = p.ny, nz = p.nz;
    if (l < 1e-6f) { nx = 0; ny = 1; nz = 0; } else { nx /= l; ny /= l; nz /= l; }
    o->u = p.u; o->v = p.v; o->c = shade(p.t, nx, ny, nz); o->x = p.x; o->y = p.y; o->z = p.z;
}
static void emitq(PV a, PV b, PV c, PV d) {
    VT *o = alloc(6);
    put(o + 0, a); put(o + 1, c); put(o + 2, b); put(o + 3, b); put(o + 4, c); put(o + 5, d);
}
static PV ellpv(float cx, float cy, float cz, float rx, float ry, float rz, float t, float p, float u, float v, unsigned int tint) {
    float ux = sinf(t) * cosf(p), uy = cosf(t), uz = sinf(t) * sinf(p);
    PV q = {cx + rx * ux, cy + ry * uy, cz + rz * uz, ux / rx, uy / ry, uz / rz, u, v, tint};
    return q;
}
static void add_ell(float cx, float cy, float cz, float rx, float ry, float rz, int seg, int rings, unsigned int tint, float us, float vs) {
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < seg; j++) {
            float t0 = PI * i / rings, t1 = PI * (i + 1) / rings, p0 = 2 * PI * j / seg, p1 = 2 * PI * (j + 1) / seg;
            float u0 = us * j / seg, u1 = us * (j + 1) / seg, v0 = vs * i / rings, v1 = vs * (i + 1) / rings;
            emitq(ellpv(cx, cy, cz, rx, ry, rz, t0, p0, u0, v0, tint), ellpv(cx, cy, cz, rx, ry, rz, t0, p1, u1, v0, tint),
                  ellpv(cx, cy, cz, rx, ry, rz, t1, p0, u0, v1, tint), ellpv(cx, cy, cz, rx, ry, rz, t1, p1, u1, v1, tint));
        }
}
static PV frpv(float cx, float cy, float cz, float rB, float rT, float h, float f, float p, float u, float v, unsigned int tint) {
    float r = rB + (rT - rB) * f, c = cosf(p), s = sinf(p);
    PV q = {cx + r * c, cy + h * f, cz + r * s, c * h, rB - rT, s * h, u, v, tint};
    return q;
}
static void add_frustum(float cx, float cy, float cz, float rB, float rT, float h, int seg, int rings, unsigned int tint, float us, float vs) {
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < seg; j++) {
            float f0 = (float)i / rings, f1 = (float)(i + 1) / rings, p0 = 2 * PI * j / seg, p1 = 2 * PI * (j + 1) / seg;
            float u0 = us * j / seg, u1 = us * (j + 1) / seg;
            emitq(frpv(cx, cy, cz, rB, rT, h, f0, p0, u0, vs * f0, tint), frpv(cx, cy, cz, rB, rT, h, f0, p1, u1, vs * f0, tint),
                  frpv(cx, cy, cz, rB, rT, h, f1, p0, u0, vs * f1, tint), frpv(cx, cy, cz, rB, rT, h, f1, p1, u1, vs * f1, tint));
        }
}
static unsigned int ground_tint(float x, float z, float h) {
    float n = vn(x * 0.045f + 100, z * 0.045f + 100, 4096, 4096, 11);
    float n2 = vn(x * 0.2f + 300, z * 0.2f + 300, 4096, 4096, 12);
    float k = 0.80f + 0.35f * n + 0.10f * (n2 - 0.5f);
    float r = 108 * k, g = 176 * k, b = 66 * k;
    float wd = dist(x, z, -35, 70);
    if (wd < 36) { float t = 1 - sstep(14, 36, wd); r *= 1 - 0.28f * t; g *= 1 - 0.18f * t; b *= 1 - 0.22f * t; }
    float vd = dist(x, z, 0, 0);
    if (vd < 22) { float t = 1 - sstep(8, 22, vd); r += 14 * t; g += 6 * t; }
    float sw = sstep(0.4f, -0.5f, h);
    r += (226 * k - r) * sw; g += (206 * k - g) * sw; b += (150 * k - b) * sw;
    float rw = sstep(6, 12, h);
    r += (156 * k - r) * rw; g += (122 * k - g) * rw; b += (106 * k - b) * rw;
    float pw = sstep(15, 18.5f, h);
    r += (96 * k - r) * pw; g += (82 * k - g) * pw; b += (80 * k - b) * pw;
    return pack(r, g, b);
}
static void add_ground(void) {
    float cell = 2 * HALF / GN;
    for (int i = 0; i < GN; i++)
        for (int j = 0; j < GN; j++) {
            PV q[4];
            for (int k = 0; k < 4; k++) {
                int ii = i + (k >> 1), jj = j + (k & 1);
                float x = -HALF + jj * cell, z = -HALF + ii * cell, h = terrain(x, z), nx, ny, nz;
                tnorm(x, z, &nx, &ny, &nz);
                PV p = {x, h, z, nx, ny, nz, jj * 2.0f, ii * 2.0f, ground_tint(x, z, h)};
                q[k] = p;
            }
            emitq(q[0], q[1], q[2], q[3]);
        }
}
static void add_disc(float cx, float cz, float r, int seg, int rings, float yoff, int useterr, unsigned int tint, float us) {
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < seg; j++) {
            PV q[4];
            for (int k = 0; k < 4; k++) {
                int ii = i + (k >> 1), jj = j + (k & 1);
                float rr = r * ii / rings, a = 2 * PI * jj / seg, x = cx + rr * cosf(a), z = cz + rr * sinf(a);
                float h = useterr ? terrain(x, z) + yoff : yoff, nx = 0, ny = 1, nz = 0;
                if (useterr) tnorm(x, z, &nx, &ny, &nz);
                PV p = {x, h, z, nx, ny, nz, x * us, z * us, tint};
                q[k] = p;
            }
            emitq(q[0], q[1], q[2], q[3]);
        }
}
static void add_path(const float p[][2], int n, float w, float yoff, unsigned int tint) {
    float acc = 0;
    for (int i = 0; i < n - 1; i++) {
        float dx = p[i + 1][0] - p[i][0], dz = p[i + 1][1] - p[i][1], len = sqrtf(dx * dx + dz * dz);
        int steps = (int)(len / 2.0f) + 1;
        float px = -dz / len * w * 0.5f, pz = dx / len * w * 0.5f;
        for (int s = 0; s < steps; s++) {
            float t0 = (float)s / steps, t1 = (float)(s + 1) / steps;
            PV q[4];
            for (int k = 0; k < 4; k++) {
                float t = (k >> 1) ? t1 : t0, sd = (k & 1) ? 1.0f : -1.0f;
                float x = p[i][0] + dx * t + px * sd, z = p[i][1] + dz * t + pz * sd, nx, ny, nz;
                tnorm(x, z, &nx, &ny, &nz);
                PV v = {x, terrain(x, z) + yoff, z, nx, ny, nz, sd > 0 ? 1.0f : 0.0f, (acc + len * t) * 0.25f, tint};
                q[k] = v;
            }
            emitq(q[0], q[1], q[2], q[3]);
        }
        acc += len;
    }
}

/* ---------- paths (shared by meshes and tree placement) ---------- */
static const float PE[][2] = {{14, -4}, {30, -12}, {44, -22}, {54, -32}, {62, -42}, {68.5f, -49}, {74, -60}, {78, -68}};
static const float PN[][2] = {{-4, 14}, {-12, 30}, {-20, 46}, {-28, 57}, {-33, 63}};
static const float PW[][2] = {{-14, -6}, {-28, -18}, {-40, -30}, {-51, -43}};
static float seg_near(const float p[][2], int n, float x, float z) {
    float best = 1e9f;
    for (int i = 0; i < n - 1; i++) {
        float ax = p[i][0], az = p[i][1], bx = p[i + 1][0], bz = p[i + 1][1];
        float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
        float t = clampf(((x - ax) * dx + (z - az) * dz) / l2, 0, 1);
        float d = dist(x, z, ax + dx * t, az + dz * t);
        if (d < best) best = d;
    }
    return best;
}
static int near_path(float x, float z, float w) {
    return seg_near(PE, NARR(PE), x, z) < w || seg_near(PN, NARR(PN), x, z) < w || seg_near(PW, NARR(PW), x, z) < w ||
           dist(x, z, 0, 0) < 9.0f;
}

/* ---------- mesh handles ---------- */
static int mGround, mSq, mPath, mWater, mHill, mBlob, mBeam, mTrunk[3], mCrown[3], mPineT, mPineC, mWall[3], mRoof[3], mDoor,
    mWin, mRock[4], mFlower[4], mBush, mBerry, mShard, mPetal, mPlank, mOre, mCapM, mPost, mLampL, mGPost, mGOff, mGOn, mStone,
    mBraz, mFlame, mAltar, mAltarOrb, mPillar, mSeal, mBrPlank, mBrPost, mWell, mTent[2], mFox, mWing[6], mWisp, mFly,
    mStaff, mGloom, mTend, mOrb[8], mCamp;
enum { O_CYAN, O_RED, O_BLUE, O_GOLD, O_DIM, O_WARM, O_WHITE, O_ORANGE };

static int orb(unsigned int tint, float r) {
    int m = mbegin(T_WHITE);
    bake_lit = 0; add_ell(0, 0, 0, r, r, r, 10, 8, tint, 1, 1); bake_lit = 1;
    mend();
    return m;
}

/* ---------- characters ---------- */
typedef struct { const char *name; unsigned int skin, hair, shirt, pants; int robe, style; float sc; } CDef;
static const CDef CD[10] = {
    {"Aria", RGB(245, 205, 175), RGB(150, 55, 40), RGB(230, 195, 90), RGB(70, 60, 110), 0, 1, 1.0f},
    {"Elder Maren", RGB(240, 200, 170), RGB(235, 235, 240), RGB(110, 70, 150), RGB(80, 60, 90), 1, 1, 1.0f},
    {"Tobin", RGB(235, 190, 150), RGB(120, 70, 30), RGB(220, 140, 40), RGB(70, 90, 150), 0, 0, 0.78f},
    {"Sera", RGB(190, 140, 100), RGB(35, 30, 40), RGB(40, 150, 140), RGB(60, 50, 40), 0, 1, 1.0f},
    {"Brannoc", RGB(215, 165, 125), RGB(110, 50, 30), RGB(150, 80, 50), RGB(60, 55, 60), 0, 3, 1.15f},
    {"Hilda", RGB(240, 195, 165), RGB(190, 190, 200), RGB(200, 90, 100), RGB(90, 60, 80), 1, 0, 1.0f},
    {"Orla", RGB(200, 160, 125), RGB(200, 200, 210), RGB(70, 110, 70), RGB(60, 80, 60), 1, 1, 1.0f},
    {"Dorin", RGB(205, 150, 110), RGB(220, 220, 225), RGB(60, 90, 130), RGB(80, 70, 60), 0, 3, 1.05f},
    {"Mira", RGB(150, 105, 80), RGB(30, 25, 45), RGB(60, 50, 120), RGB(50, 40, 90), 1, 1, 1.0f},
    {"Kael", RGB(225, 180, 145), RGB(60, 50, 40), RGB(170, 175, 190), RGB(90, 95, 110), 0, 0, 1.1f}};
typedef struct { int head, hair, torso, robe, arm, leg; } CSet;
static CSet cs[10];

static void build_char(int i) {
    const CDef *d = &CD[i];
    CSet *s = &cs[i];
    unsigned int sk = d->skin, hr = d->hair, sh = d->shirt, pa = d->pants;
    s->head = mbegin(T_SKIN);
    add_ell(0, 0, 0, 0.20f, 0.23f, 0.20f, 16, 12, sk, 1, 1);
    add_ell(-0.2f, 0, 0, 0.035f, 0.055f, 0.03f, 6, 4, sk, 1, 1);
    add_ell(0.2f, 0, 0, 0.035f, 0.055f, 0.03f, 6, 4, sk, 1, 1);
    add_ell(0, -0.03f, 0.2f, 0.035f, 0.045f, 0.04f, 6, 4, mul(sk, 0.93f), 1, 1);
    for (int e = -1; e <= 1; e += 2) {
        add_ell(e * 0.075f, 0.03f, 0.172f, 0.042f, 0.048f, 0.03f, 8, 6, RGB(245, 245, 250), 1, 1);
        add_ell(e * 0.075f, 0.03f, 0.195f, 0.022f, 0.028f, 0.015f, 6, 4, RGB(30, 25, 35), 1, 1);
    }
    add_ell(0, -0.1f, 0.185f, 0.05f, 0.012f, 0.02f, 6, 4, RGB(190, 90, 90), 1, 1);
    mend();
    s->hair = mbegin(T_HAIR);
    add_ell(0, 0.09f, -0.05f, 0.215f, 0.20f, 0.22f, 14, 9, hr, 1, 1);
    if (d->style == 1) add_ell(0, -0.14f, -0.1f, 0.19f, 0.36f, 0.12f, 10, 8, hr, 1, 1);
    if (d->style == 3) add_ell(0, -0.15f, 0.1f, 0.14f, 0.18f, 0.10f, 10, 7, hr, 1, 1);
    mend();
    s->torso = mbegin(T_CLOTH);
    add_ell(0, 0, 0, 0.27f, 0.42f, 0.17f, 14, 9, sh, 1, 1);
    add_ell(0, -0.3f, 0, 0.285f, 0.05f, 0.185f, 12, 5, RGB(80, 55, 35), 1, 1);
    mend();
    s->robe = mbegin(T_CLOTH);
    add_ell(0, 0, 0, 0.36f, 0.78f, 0.30f, 16, 10, sh, 1, 1);
    add_ell(0, 0.25f, 0, 0.365f, 0.05f, 0.305f, 14, 4, mul(sh, 0.6f), 1, 1);
    mend();
    s->arm = mbegin(T_CLOTH);
    add_ell(0, -0.38f, 0, 0.095f, 0.38f, 0.095f, 10, 6, sh, 1, 1);
    add_ell(0, -0.83f, 0, 0.085f, 0.085f, 0.085f, 8, 6, sk, 1, 1);
    mend();
    s->leg = mbegin(T_CLOTH);
    add_ell(0, -0.4f, 0, 0.105f, 0.4f, 0.105f, 10, 6, pa, 1, 1);
    add_ell(0, -0.82f, 0.04f, 0.115f, 0.07f, 0.17f, 8, 5, RGB(70, 50, 40), 1, 1);
    mend();
}

static void build_meshes(void) {
    for (int i = 0; i < NTEX; i++) gen_tex(i);

    mGround = mbegin(T_GRASS); add_ground(); mend();
    mSq = mbegin(T_COBBLE); add_disc(0, 0, 8.5f, 36, 3, 0.07f, 1, RGB(215, 205, 190), 0.25f); mend();
    mPath = mbegin(T_DIRT);
    add_path(PE, NARR(PE), 3.2f, 0.09f, RGB(225, 205, 175));
    add_path(PN, NARR(PN), 3.2f, 0.09f, RGB(225, 205, 175));
    add_path(PW, NARR(PW), 3.2f, 0.09f, RGB(225, 205, 175));
    mend();
    mWater = mbegin(T_WATER); add_disc(-75, -45, 27, 32, 3, WLEVEL, 0, RGB(200, 235, 255), 0.12f); mend();
    mHill = mbegin(T_GRASS); add_ell(0, 0, 0, 34, 16, 34, 12, 7, RGB(90, 150, 115), 6, 3); mend();
    mBlob = mbegin(T_WHITE); bake_lit = 0; add_disc(0, 0, 0.55f, 14, 1, 0, 0, RGB(36, 42, 30), 1); bake_lit = 1; mend();
    mBeam = mbegin(T_WHITE); bake_lit = 0; add_frustum(0, 0, 0, 0.22f, 0.12f, 34, 8, 1, RGB(255, 240, 160), 1, 1); bake_lit = 1; mend();

    static const unsigned int leafc[3] = {RGB(70, 150, 66), RGB(100, 170, 60), RGB(58, 130, 90)};
    for (int i = 0; i < 3; i++) {
        mTrunk[i] = mbegin(T_BARK);
        add_frustum(0, 0, 0, 0.30f, 0.17f, 2.6f, 8, 3, RGB(170 - i * 14, 124 - i * 8, 92), 1, 2);
        mend();
        mCrown[i] = mbegin(T_LEAF);
        add_ell(0, 0, 0, 1.5f, 1.2f, 1.5f, 12, 8, leafc[i], 3, 3);
        add_ell(0.9f, -0.35f, 0.4f, 1.1f, 0.9f, 1.1f, 10, 7, mul(leafc[i], 0.92f), 3, 3);
        add_ell(-0.5f, 0.45f, -0.8f, 1.0f, 0.85f, 1.0f, 10, 7, mul(leafc[i], 1.08f), 3, 3);
        mend();
    }
    mPineT = mbegin(T_BARK); add_frustum(0, 0, 0, 0.28f, 0.18f, 2.2f, 8, 2, RGB(150, 108, 80), 1, 2); mend();
    mPineC = mbegin(T_LEAF);
    add_frustum(0, 1.4f, 0, 1.8f, 0.2f, 2.4f, 12, 2, RGB(48, 112, 78), 3, 2);
    add_frustum(0, 2.6f, 0, 1.4f, 0.15f, 2.2f, 12, 2, RGB(54, 124, 84), 3, 2);
    add_frustum(0, 3.8f, 0, 1.0f, 0.05f, 2.0f, 12, 2, RGB(62, 136, 92), 3, 2);
    mend();

    static const unsigned int walls[3] = {RGB(238, 228, 208), RGB(176, 130, 96), RGB(210, 205, 195)};
    static const int walltex[3] = {T_PLASTER, T_BARK, T_COBBLE};
    static const unsigned int roofs[3] = {RGB(222, 192, 112), RGB(196, 84, 62), RGB(104, 114, 138)};
    for (int i = 0; i < 3; i++) {
        mWall[i] = mbegin(walltex[i]); add_frustum(0, 0, 0, 2.2f, 2.05f, 2.4f, 16, 2, walls[i], 4, 2); mend();
        mRoof[i] = mbegin(T_ROOF); add_frustum(0, 2.3f, 0, 3.1f, 0.15f, 2.4f, 16, 2, roofs[i], 4, 2); mend();
    }
    mDoor = mbegin(T_BARK); add_ell(0, 0.95f, 2.12f, 0.6f, 0.95f, 0.18f, 10, 8, RGB(120, 80, 50), 1, 1); mend();
    mWin = mbegin(T_WHITE); bake_lit = 0;
    for (int s = -1; s <= 1; s += 2) add_ell(s * 1.55f, 1.35f, 1.45f, 0.32f, 0.36f, 0.12f, 8, 6, RGB(255, 226, 150), 1, 1);
    bake_lit = 1; mend();

    static const unsigned int rockc[4] = {RGB(150, 150, 158), RGB(170, 158, 140), RGB(124, 130, 144), RGB(168, 120, 104)};
    for (int i = 0; i < 4; i++) { mRock[i] = mbegin(T_ROCK); add_ell(0, 0.2f, 0, 0.8f, 0.55f, 0.65f, 12, 8, rockc[i], 2, 2); mend(); }
    static const unsigned int flc[4] = {RGB(250, 130, 170), RGB(255, 225, 90), RGB(250, 250, 245), RGB(170, 140, 250)};
    for (int i = 0; i < 4; i++) {
        mFlower[i] = mbegin(T_WHITE);
        add_frustum(0, 0, 0, 0.025f, 0.02f, 0.38f, 5, 1, RGB(70, 140, 60), 1, 1);
        add_ell(0, 0.42f, 0, 0.12f, 0.07f, 0.12f, 8, 5, flc[i], 1, 1);
        add_ell(0, 0.46f, 0, 0.04f, 0.03f, 0.04f, 6, 4, RGB(250, 200, 60), 1, 1);
        mend();
    }
    mBush = mbegin(T_LEAF); add_ell(0, 0.4f, 0, 0.7f, 0.45f, 0.65f, 10, 7, RGB(60, 130, 56), 2, 2); mend();
    mBerry = mbegin(T_WHITE); add_ell(0, 0, 0, 0.085f, 0.085f, 0.085f, 8, 6, RGB(220, 40, 60), 1, 1); mend();
    mShard = mbegin(T_WHITE); bake_lit = 0;
    add_ell(0, 0, 0, 0.17f, 0.42f, 0.17f, 8, 6, RGB(120, 215, 255), 1, 1); add_ell(0.12f, -0.12f, 0.05f, 0.09f, 0.26f, 0.09f, 6, 5, RGB(160, 235, 255), 1, 1);
    bake_lit = 1; mend();
    mPetal = mbegin(T_WHITE);
    add_frustum(0, 0, 0, 0.03f, 0.02f, 0.5f, 5, 1, RGB(70, 140, 80), 1, 1);
    bake_lit = 0; add_ell(0, 0.55f, 0, 0.2f, 0.1f, 0.2f, 10, 6, RGB(130, 230, 255), 1, 1); add_ell(0, 0.6f, 0, 0.07f, 0.05f, 0.07f, 6, 4, RGB(255, 255, 220), 1, 1); bake_lit = 1;
    mend();
    mPlank = mbegin(T_BARK); add_ell(0, 0, 0, 0.18f, 0.06f, 0.8f, 8, 5, RGB(190, 145, 100), 1, 2); mend();
    mOre = mbegin(T_ROCK);
    add_ell(0, 0.25f, 0, 0.4f, 0.3f, 0.35f, 10, 7, RGB(110, 105, 115), 2, 2);
    bake_lit = 0; add_ell(0.12f, 0.4f, 0.2f, 0.07f, 0.06f, 0.07f, 6, 4, RGB(255, 140, 60), 1, 1); add_ell(-0.15f, 0.3f, 0.2f, 0.06f, 0.05f, 0.06f, 6, 4, RGB(255, 170, 70), 1, 1); bake_lit = 1;
    mend();
    mCapM = mbegin(T_CLOTH);
    add_ell(0, 0.1f, 0, 0.22f, 0.14f, 0.22f, 10, 6, RGB(210, 40, 40), 1, 1); add_ell(0, 0.04f, 0.18f, 0.16f, 0.025f, 0.12f, 8, 4, RGB(160, 30, 30), 1, 1);
    mend();
    mPost = mbegin(T_BARK); add_frustum(0, 0, 0, 0.09f, 0.06f, 2.6f, 6, 1, RGB(90, 70, 60), 1, 2); mend();
    mLampL = mbegin(T_WHITE); bake_lit = 0; add_ell(0, 2.8f, 0, 0.24f, 0.3f, 0.24f, 8, 6, RGB(255, 220, 130), 1, 1); bake_lit = 1; mend();
    mGPost = mbegin(T_ROCK); add_frustum(0, 0, 0, 0.3f, 0.16f, 3.4f, 10, 2, RGB(90, 82, 78), 1, 2); add_frustum(0, 3.4f, 0, 0.7f, 0.05f, 0.9f, 10, 1, RGB(80, 74, 72), 1, 1); mend();
    mGOff = mbegin(T_ROCK); add_ell(0, 0, 0, 0.6f, 0.6f, 0.6f, 14, 10, RGB(100, 105, 125), 2, 2); mend();
    mGOn = mbegin(T_WHITE); bake_lit = 0; add_ell(0, 0, 0, 0.6f, 0.6f, 0.6f, 14, 10, RGB(255, 228, 140), 1, 1); bake_lit = 1; mend();
    mStone = mbegin(T_ROCK); add_ell(0, 1.4f, 0, 0.62f, 1.45f, 0.45f, 12, 9, RGB(160, 160, 170), 2, 3); mend();
    mBraz = mbegin(T_COBBLE); add_frustum(0, 0, 0, 0.55f, 0.38f, 1.0f, 10, 1, RGB(180, 172, 160), 2, 1); add_ell(0, 1.05f, 0, 0.5f, 0.14f, 0.5f, 10, 5, RGB(90, 82, 78), 1, 1); mend();
    mFlame = mbegin(T_WHITE); bake_lit = 0; add_ell(0, 0, 0, 0.3f, 0.55f, 0.3f, 8, 6, RGB(255, 160, 50), 1, 1); add_ell(0, -0.05f, 0, 0.18f, 0.38f, 0.18f, 8, 6, RGB(255, 230, 120), 1, 1); bake_lit = 1; mend();
    mAltar = mbegin(T_COBBLE); add_frustum(0, 0, 0, 1.7f, 1.2f, 1.1f, 12, 1, RGB(190, 184, 176), 3, 1); add_frustum(0, 1.1f, 0, 1.0f, 0.8f, 0.9f, 12, 1, RGB(176, 170, 164), 2, 1); mend();
    mAltarOrb = orb(RGB(140, 235, 255), 0.35f);
    mPillar = mbegin(T_COBBLE); add_frustum(0, 0, 0, 0.7f, 0.55f, 4.4f, 10, 3, RGB(185, 178, 168), 2, 3); add_ell(0, 4.5f, 0, 0.8f, 0.3f, 0.8f, 10, 5, RGB(170, 164, 156), 1, 1); mend();
    mSeal = mbegin(T_WHITE); bake_lit = 0; add_ell(0, 0, 0, 2.7f, 1.8f, 0.12f, 14, 8, RGB(255, 90, 60), 1, 1); bake_lit = 1; mend();
    mBrPlank = mbegin(T_BARK); add_ell(0, 0, 0, 0.58f, 0.07f, 1.6f, 8, 4, RGB(185, 140, 96), 1, 2); mend();
    mBrPost = mbegin(T_BARK); add_frustum(0, -0.4f, 0, 0.1f, 0.08f, 1.5f, 6, 1, RGB(120, 90, 66), 1, 2); mend();
    mWell = mbegin(T_COBBLE);
    add_frustum(0, 0, 0, 1.1f, 1.0f, 1.0f, 14, 2, RGB(190, 184, 172), 3, 1);
    add_frustum(-0.95f, 0, 0, 0.08f, 0.08f, 2.6f, 6, 1, RGB(120, 90, 66), 1, 1); add_frustum(0.95f, 0, 0, 0.08f, 0.08f, 2.6f, 6, 1, RGB(120, 90, 66), 1, 1);
    add_frustum(0, 2.5f, 0, 1.5f, 0.1f, 1.0f, 10, 1, RGB(196, 84, 62), 2, 1);
    mend();
    mTent[0] = mbegin(T_CLOTH); add_frustum(0, 0, 0, 1.9f, 0.05f, 2.6f, 12, 2, RGB(200, 90, 70), 3, 2); mend();
    mTent[1] = mbegin(T_CLOTH); add_frustum(0, 0, 0, 1.9f, 0.05f, 2.6f, 12, 2, RGB(90, 120, 170), 3, 2); mend();
    mCamp = mbegin(T_ROCK); add_frustum(0, 0, 0, 0.7f, 0.5f, 0.25f, 10, 1, RGB(120, 118, 124), 1, 1); mend();

    mFox = mbegin(T_HAIR);
    add_ell(0, 0.42f, 0, 0.22f, 0.2f, 0.45f, 10, 7, RGB(225, 115, 45), 1, 1);
    add_ell(0, 0.62f, 0.5f, 0.17f, 0.15f, 0.2f, 10, 7, RGB(230, 120, 50), 1, 1);
    add_ell(0, 0.57f, 0.68f, 0.06f, 0.05f, 0.1f, 6, 4, RGB(50, 40, 40), 1, 1);
    add_ell(-0.09f, 0.82f, 0.45f, 0.05f, 0.1f, 0.04f, 6, 4, RGB(215, 105, 40), 1, 1); add_ell(0.09f, 0.82f, 0.45f, 0.05f, 0.1f, 0.04f, 6, 4, RGB(215, 105, 40), 1, 1);
    add_ell(0, 0.5f, -0.55f, 0.1f, 0.1f, 0.38f, 8, 5, RGB(225, 115, 45), 1, 1); add_ell(0, 0.5f, -0.85f, 0.08f, 0.08f, 0.14f, 6, 4, RGB(250, 245, 235), 1, 1);
    for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) add_ell(a * 0.12f, 0.2f, b * 0.25f, 0.05f, 0.2f, 0.05f, 6, 4, RGB(60, 45, 40), 1, 1);
    mend();

    static const unsigned int wingc[3] = {RGB(250, 150, 60), RGB(120, 190, 255), RGB(250, 120, 200)};
    bake_lit = 0;
    for (int i = 0; i < 3; i++) {
        mWing[i * 2] = mbegin(T_WHITE); add_ell(-0.1f, 0, 0, 0.1f, 0.012f, 0.08f, 6, 4, wingc[i], 1, 1); mend();
        mWing[i * 2 + 1] = mbegin(T_WHITE); add_ell(0.1f, 0, 0, 0.1f, 0.012f, 0.08f, 6, 4, wingc[i], 1, 1); mend();
    }
    bake_lit = 1;
    mOrb[O_CYAN] = orb(RGB(140, 235, 255), 0.14f); mOrb[O_RED] = orb(RGB(255, 80, 70), 0.22f); mOrb[O_BLUE] = orb(RGB(90, 140, 255), 0.22f);
    mOrb[O_GOLD] = orb(RGB(255, 210, 70), 0.22f); mOrb[O_DIM] = orb(RGB(80, 84, 96), 0.2f); mOrb[O_WARM] = orb(RGB(255, 220, 130), 0.09f);
    mOrb[O_WHITE] = orb(RGB(255, 250, 225), 0.2f); mOrb[O_ORANGE] = orb(RGB(255, 150, 60), 0.12f);
    mWisp = mOrb[O_WHITE];
    mFly = orb(RGB(255, 240, 150), 0.045f);
    mStaff = mbegin(T_BARK); add_frustum(0, 0, 0, 0.05f, 0.04f, 2.0f, 6, 1, RGB(130, 96, 66), 1, 2);
    bake_lit = 0; add_ell(0, 2.08f, 0, 0.1f, 0.1f, 0.1f, 8, 6, RGB(160, 230, 255), 1, 1); bake_lit = 1; mend();
    mGloom = mbegin(T_WHITE); bake_lit = 0;
    add_ell(0, 0, 0, 1.7f, 1.5f, 1.7f, 16, 12, RGB(42, 12, 70), 1, 1);
    add_ell(-0.6f, 0.25f, 1.4f, 0.28f, 0.16f, 0.1f, 8, 6, RGB(255, 70, 70), 1, 1); add_ell(0.6f, 0.25f, 1.4f, 0.28f, 0.16f, 0.1f, 8, 6, RGB(255, 70, 70), 1, 1);
    bake_lit = 1; mend();
    mTend = mbegin(T_WHITE); bake_lit = 0; add_ell(0, 0, 0, 0.3f, 1.6f, 0.3f, 8, 6, RGB(60, 20, 96), 1, 1); bake_lit = 1; mend();

    for (int i = 0; i < 10; i++) build_char(i);
    pool_n = pool_n; /* keep */
}

/* ===================== drawing helpers ===================== */
static int cur_tex = -1;
static float cex, cez, cfx, cfz;
static void bind_tex(int id) { if (id != cur_tex) { sceGuTexImage(0, 64, 64, 64, tex[id]); cur_tex = id; } }
static void draw(int m) { bind_tex(meshes[m].tex); sceGumDrawArray(GU_TRIANGLES, VFT, meshes[m].n, 0, meshes[m].v); }
static void tr(float x, float y, float z) { ScePspFVector3 v = {x, y, z}; sceGumTranslate(&v); }
static void inst(int m, float x, float y, float z, float yaw, float s) {
    sceGumMatrixMode(GU_MODEL);
    sceGumPushMatrix();
    tr(x, y, z);
    if (yaw != 0.0f) sceGumRotateY(yaw);
    if (s != 1.0f) { ScePspFVector3 v = {s, s, s}; sceGumScale(&v); }
    draw(m);
    sceGumPopMatrix();
}
static int vis(float x, float z, float r, float maxd) {
    float dx = x - cex, dz = z - cez;
    if (dx * dx + dz * dz > (maxd + r) * (maxd + r)) return 0;
    if (dx * cfx + dz * cfz < -(r + 6.0f)) return 0;
    return 1;
}

/* ===================== 2D text ===================== */
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
                    int x0 = x + (i * 8 + c) * sc, y0 = y + r * sc, x1 = x + (i * 8 + e + 1) * sc, y1 = y0 + sc;
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

/* ===================== world data ===================== */
typedef struct { float x, z, tx, tz; int wall, roof; float s; } House;
static const House HOUSES[] = {
    {8, 6, 0, 0, 0, 0, 1.0f}, {-10, 7, 0, 0, 0, 1, 1.3f}, {12, -6, 0, 0, 2, 2, 1.1f},
    {-13, -5, 0, 0, 1, 0, 0.95f}, {-35, 67, -28, 55, 1, 0, 1.0f}, {-49, -39, -30, -40, 1, 2, 0.8f}};
static float hyaw[6];
static const float LAMPS[][2] = {{8, 0}, {-1.4f, 7.9f}, {-7.5f, 2.7f}, {-6.1f, -5.1f}, {4, -6.9f}};
static const float LANTERNP[2] = {0, -7};
static const float WELLP[2] = {-3, 11};
static const float TENTS[][3] = {{3, -16, 0}, {52, -33, 1}};
static const float BRAZP[4][2] = {{88, -75}, {72, -75}, {80, -67}, {80, -83}};
static const float ALTARP[2] = {80, -75};
static const float GATEP[2] = {68.5f, -49};
static const float STONEP[3][2] = {{-77.3f, -43.7f}, {-72.7f, -43.7f}, {-75.0f, -47.6f}};
static const float WOODS[2] = {-35, 70};

typedef struct { float x, z; unsigned char type, group; short smin, smax; signed char needf; unsigned char taken; } Item;
enum { IT_SHARD, IT_BERRY, IT_PETAL, IT_PLANK, IT_ORE, IT_WISP, IT_CAP, IT_FOX };
static Item items[] = {
    {-18, -12, IT_SHARD, G_SHARD, 1, 1, -1, 0}, {22, -14, IT_SHARD, G_SHARD, 1, 1, -1, 0}, {-8, 26, IT_SHARD, G_SHARD, 1, 1, -1, 0},
    {-24, 14, IT_BERRY, G_BERRY, 0, 99, F_BERRY_Q, 0}, {20, 22, IT_BERRY, G_BERRY, 0, 99, F_BERRY_Q, 0},
    {-6, -28, IT_BERRY, G_BERRY, 0, 99, F_BERRY_Q, 0}, {27, -4, IT_BERRY, G_BERRY, 0, 99, F_BERRY_Q, 0},
    {31, 12, IT_CAP, G_CAP, 0, 99, F_CAP_Q, 0},
    {-27, 62, IT_PETAL, G_PETAL, 4, 4, -1, 0}, {-46, 57, IT_PETAL, G_PETAL, 4, 4, -1, 0}, {-38, 81, IT_PETAL, G_PETAL, 4, 4, -1, 0},
    {-20, 75, IT_PETAL, G_PETAL, 4, 4, -1, 0}, {-52, 72, IT_PETAL, G_PETAL, 4, 4, -1, 0},
    {-22, 50, IT_FOX, G_FOX, 0, 99, F_FOX_Q, 0},
    {-28, -24, IT_PLANK, G_PLANK, 6, 6, -1, 0}, {-44, -62, IT_PLANK, G_PLANK, 6, 6, -1, 0},
    {-62, -14, IT_PLANK, G_PLANK, 6, 6, -1, 0}, {-22, -50, IT_PLANK, G_PLANK, 6, 6, -1, 0},
    {46, -8, IT_ORE, G_ORE, 10, 10, -1, 0}, {60, -20, IT_ORE, G_ORE, 10, 10, -1, 0}, {36, -40, IT_ORE, G_ORE, 10, 10, -1, 0},
    {20, 45, IT_WISP, G_WISP, 0, 99, -1, 0}, {-45, 12, IT_WISP, G_WISP, 0, 99, -1, 0}, {48, 28, IT_WISP, G_WISP, 0, 99, -1, 0},
    {-18, -48, IT_WISP, G_WISP, 0, 99, -1, 0}, {75, -12, IT_WISP, G_WISP, 0, 99, -1, 0}, {-80, 8, IT_WISP, G_WISP, 0, 99, -1, 0},
    {10, 100, IT_WISP, G_WISP, 0, 99, -1, 0}, {-84, -74, IT_WISP, G_WISP, 0, 99, -1, 0}};
#define NITEM ((int)(sizeof(items) / sizeof(items[0])))
static int item_active(const Item *it) {
    return !it->taken && stage >= it->smin && stage <= it->smax && (it->needf < 0 || fl[it->needf]);
}

typedef struct { float x, z, r; } Ob;
static Ob obs[400];
static int nobs;
static void add_ob(float x, float z, float r) { if (nobs < 400) { obs[nobs].x = x; obs[nobs].z = z; obs[nobs].r = r; nobs++; } }
static void push_out(float *x, float *z, float r) {
    for (int i = 0; i < nobs; i++) {
        float dx = *x - obs[i].x, dz = *z - obs[i].z, d = sqrtf(dx * dx + dz * dz), m = r + obs[i].r;
        if (d < m && d > 0.0001f) { *x = obs[i].x + dx / d * m; *z = obs[i].z + dz / d * m; }
    }
}

typedef struct { float x, y, z, yaw, walk, move, sc; int talk, robe, lantern, staff; } Char;
static Char ch[10];
static const float NPCP[10][2] = {{0, 3}, {7, 2.2f}, {-11, -1.5f}, {3, -13}, {9, -3}, {-8, 2.5f}, {-33, 63}, {-50, -44}, {-74.5f, -44.2f}, {51, -28}};

static int near_stuff(float x, float z, float r) {
    for (int i = 0; i < 6; i++) if (dist(x, z, HOUSES[i].x, HOUSES[i].z) < r + 3.5f * HOUSES[i].s) return 1;
    for (int i = 0; i < NITEM; i++) if (dist(x, z, items[i].x, items[i].z) < r) return 1;
    for (int i = 0; i < 10; i++) if (dist(x, z, NPCP[i][0], NPCP[i][1]) < r) return 1;
    for (int i = 0; i < 2; i++) if (dist(x, z, TENTS[i][0], TENTS[i][1]) < r + 2) return 1;
    for (int i = 0; i < 4; i++) if (dist(x, z, BRAZP[i][0], BRAZP[i][1]) < r) return 1;
    if (dist(x, z, LANTERNP[0], LANTERNP[1]) < r + 2 || dist(x, z, WELLP[0], WELLP[1]) < r + 2 || dist(x, z, GATEP[0], GATEP[1]) < r + 4) return 1;
    if (dist(x, z, -75, -45) < 30) return 1;
    return 0;
}

#define MAXTREE 150
#define MAXROCK 40
#define MAXFLW 240
typedef struct { float x, y, z, yaw, s; int type; } Deco;
static Deco trees[MAXTREE], rocks[MAXROCK], flowers[MAXFLW];
static int ntree, nrock, nflw;
static unsigned int seed = 424242;
static float rnd(void) { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) & 0xffff) / 65536.0f; }

static void place_decor(void) {
    ntree = 0;
    for (int pass = 0; pass < 3; pass++) {
        int want = pass == 0 ? 62 : (pass == 1 ? 44 : 26), tries = 0;
        while (want > 0 && tries < 6000 && ntree < MAXTREE) {
            tries++;
            float x, z;
            if (pass == 0) { float a = rnd() * 6.2832f, r = sqrtf(rnd()) * 32; x = WOODS[0] + cosf(a) * r; z = WOODS[1] + sinf(a) * r; }
            else if (pass == 1) { float a = rnd() * 6.2832f, r = 24 + rnd() * 70; x = cosf(a) * r; z = sinf(a) * r; }
            else { x = 40 + rnd() * 60; z = -110 + rnd() * 70; }
            if (dist(x, z, 0, 0) > 114) continue;
            float h = terrain(x, z);
            if (h < -0.3f || h > 15.0f) continue;
            if (dist(x, z, 80, -75) < 16) continue;
            if (near_path(x, z, 5.0f) || near_stuff(x, z, 4.5f)) continue;
            Deco *d = &trees[ntree++];
            d->x = x; d->z = z; d->y = h; d->yaw = rnd() * 6.28f; d->s = 0.8f + rnd() * 0.7f;
            d->type = (pass == 2 || (pass == 0 && rnd() < 0.3f)) ? 3 : (int)(rnd() * 3);
            add_ob(x, z, 0.6f * d->s);
            want--;
        }
    }
    nrock = 0;
    for (int tries = 0; tries < 3000 && nrock < 34; tries++) {
        float a = rnd() * 6.2832f, r = 12 + rnd() * 100, x = cosf(a) * r, z = sinf(a) * r, h = terrain(x, z);
        if (h < -0.3f || near_path(x, z, 3.0f) || near_stuff(x, z, 3.0f)) continue;
        Deco *d = &rocks[nrock++];
        d->x = x; d->z = z; d->y = h; d->yaw = rnd() * 6.28f; d->s = 0.6f + rnd() * 1.2f;
        d->type = h > 6 ? 3 : (int)(rnd() * 3);
        add_ob(x, z, 0.8f * d->s);
    }
    nflw = 0;
    for (int tries = 0; tries < 6000 && nflw < MAXFLW; tries++) {
        float a = rnd() * 6.2832f, r = 6 + rnd() * 90, x = cosf(a) * r, z = sinf(a) * r, h = terrain(x, z);
        if (h < 0.15f || h > 9.0f || near_path(x, z, 1.8f) || near_stuff(x, z, 2.0f)) continue;
        Deco *d = &flowers[nflw++];
        d->x = x; d->z = z; d->y = h; d->yaw = rnd() * 6.28f; d->s = 0.8f + rnd() * 0.6f; d->type = (int)(rnd() * 4);
    }
}

static void init_chars(void) {
    for (int i = 0; i < 10; i++) {
        memset(&ch[i], 0, sizeof(Char));
        ch[i].x = NPCP[i][0]; ch[i].z = NPCP[i][1]; ch[i].y = terrain(ch[i].x, ch[i].z);
        ch[i].sc = CD[i].sc; ch[i].robe = CD[i].robe;
    }
    ch[0].lantern = 1; ch[1].staff = 1; ch[6].staff = 1; ch[8].staff = 1;
    for (int i = 1; i < 10; i++) ch[i].yaw = atan2f(NPCP[0][0] - ch[i].x, NPCP[0][1] - ch[i].z);
}

static void init_world(void) {
    init_chars();
    for (int i = 0; i < 6; i++) {
        hyaw[i] = atan2f(HOUSES[i].tx - HOUSES[i].x, HOUSES[i].tz - HOUSES[i].z);
        add_ob(HOUSES[i].x, HOUSES[i].z, 3.0f * HOUSES[i].s);
    }
    add_ob(LANTERNP[0], LANTERNP[1], 0.9f);
    add_ob(WELLP[0], WELLP[1], 1.2f);
    for (int i = 0; i < 5; i++) add_ob(LAMPS[i][0], LAMPS[i][1], 0.3f);
    for (int i = 0; i < 2; i++) add_ob(TENTS[i][0], TENTS[i][1], 1.7f);
    for (int i = 0; i < 3; i++) add_ob(STONEP[i][0], STONEP[i][1], 0.7f);
    for (int i = 0; i < 4; i++) add_ob(BRAZP[i][0], BRAZP[i][1], 0.6f);
    add_ob(ALTARP[0], ALTARP[1], 1.5f);
    add_ob(GATEP[0] - 2.8f, GATEP[1] + 1.4f, 0.7f); add_ob(GATEP[0] + 2.8f, GATEP[1] - 1.4f, 0.7f);
    place_decor();
}

/* ===================== dialogue data ===================== */
typedef struct { const char *who; const char *txt; } Line;
typedef struct { int npc, smin, smax, needf, notf, setf, setstage, act; const Line *l; int n; } Talk;
#define TK(np, a, b, nf, nt, sf, ss, ac, arr) {np, a, b, nf, nt, sf, ss, ac, arr, (int)(sizeof(arr) / sizeof((arr)[0]))}

static const Line LM0[] = {
    {"Elder Maren", "Aria! Thank heavens. The Great Lantern has gone dark."},
    {"Aria", "The one that guards Willowmere through the night?"},
    {"Elder Maren", "Yes. Its Glowshards scattered across the fields and hills."},
    {"Elder Maren", "Bring me three, and we can light it again."},
    {"Aria", "Leave it to me, Elder. I'll be back by dusk."}};
static const Line LM1r[] = {
    {"Elder Maren", "Three Glowshards, Aria. They shine blue in the grass."},
    {"Elder Maren", "Search the fields and hills around the village."}};
static const Line LM1d[] = {
    {"Elder Maren", "Three shards! Your lamplighter blood runs true."},
    {"Aria", "Will it be enough to wake the Great Lantern?"},
    {"Elder Maren", "Stand back, child. Let the old light remember."},
    {"Elder Maren", "...There! Look how it shines! Willowmere is safe."},
    {"Elder Maren", "But the light showed me something. A shadow in the north."},
    {"Elder Maren", "The Gloom. It slept beneath the lantern a thousand years."},
    {"Aria", "And now it's waking?"},
    {"Elder Maren", "The lantern was only a seal. We need the Moonlens."},
    {"Elder Maren", "It was broken in three. The first piece is with Orla."},
    {"Elder Maren", "She lives in the Whisper Woods, far to the north-west."},
    {"Elder Maren", "Take Sera. She waits on the south path and knows the roads."}};
static const Line LM2[] = {
    {"Elder Maren", "Sera waits by the south path, Aria. Go. Time is short."},
    {"Elder Maren", "Follow the road north-west once you find her."}};
static const Line LM3a[] = {
    {"Elder Maren", "The Great Lantern still burns. But I feel the Gloom stir."},
    {"Elder Maren", "Follow the hermits and stargazers. The Moonlens is our hope."}};
static const Line LM3b[] = {
    {"Elder Maren", "Two pieces of the lens... I can feel their warmth from here."},
    {"Elder Maren", "Bring them home, child. The Gloom grows bold."}};

static const Line LT0[] = {
    {"Tobin", "Psst! Are you the lamplighter? I lost my red cap!"},
    {"Tobin", "I was chasing a frog out by the east trees. Can you look?"},
    {"Aria", "A red cap by the east trees. I'll keep an eye out."}};
static const Line LT1[] = {{"Tobin", "East of here, past the big rocks. Please hurry!"}};
static const Line LT2[] = {
    {"Aria", "Is this your cap, Tobin?"},
    {"Tobin", "My cap! You're the best! Take my lucky charm."},
    {"Tobin", "Mum says it brings good light. Maybe it'll help you!"}};
static const Line LT3[] = {{"Tobin", "I'm never chasing frogs again. ...Maybe tomorrow."}};
static const Line LT3b[] = {
    {"Tobin", "Everyone says you're saving the world, Aria!"},
    {"Tobin", "Can I carry your lantern when you're done? Please?"}};

static const Line LS0[] = {
    {"Sera", "Shh. I'm waiting for someone. A lamplighter, Maren said."},
    {"Sera", "Don't mind me. Finish what Maren asked of you first."}};
static const Line LS2[] = {
    {"Sera", "So you're the lamplighter. Maren said to wait for you."},
    {"Aria", "You're a traveler, aren't you?"},
    {"Sera", "Eight years on the road. I know every trail to the sea."},
    {"Sera", "Whisper Woods is north-west. The trees there listen back."},
    {"Aria", "Then lead the way. I'll follow your lead."},
    {"Sera", "Ha! You carry the light, I'll carry the nerve. Let's go."}};
static const Line LSa[] = {
    {"Sera", "Stay on the trail. Orla doesn't like surprises."},
    {"Sera", "Moonpetals only open at dusk, so look for a faint blue glow."}};
static const Line LSb[] = {
    {"Sera", "This lake gives me chills. Something stirs under the water."},
    {"Sera", "Dorin's the ferryman. Gruff, but he means well."}};
static const Line LSc[] = {
    {"Sera", "Stars reflected in water... Mira's no ordinary astronomer."},
    {"Sera", "Those stones look like they want a song. Try them in order."}};
static const Line LSd[] = {
    {"Sera", "Kael's armor shines like a mirror. Let's not annoy him."},
    {"Sera", "Brannoc's forge is the only one hot enough, I hear."}};
static const Line LSe[] = {
    {"Sera", "Whatever happens at that lantern, I've got your back, Aria."},
    {"Sera", "Three pieces of moonlight. Let's finish this."}};

static const Line LB2[] = {
    {"Brannoc", "Iron ore! Fine lumps. Kael sent you, did he?"},
    {"Aria", "He says the mountain gate needs an Ember Key."},
    {"Brannoc", "Aye. Stand back, the forge is hot. This takes a minute."},
    {"Brannoc", "...Quenched in lantern oil. There. An Ember Key."},
    {"Brannoc", "Take it to Kael. And mind the mountain, lamplighter."}};
static const Line LB2r[] = {
    {"Brannoc", "Three lumps of iron ore. Foothills along the east trail."},
    {"Brannoc", "Bring them here and I'll forge the Ember Key."}};
static const Line LB0[] = {
    {"Brannoc", "Name's Brannoc. I forge whatever Willowmere needs."},
    {"Brannoc", "Need metalwork, you know where the forge is."}};
static const Line LB1[] = {{"Brannoc", "Hot work, honest work. Come back if you need a hand."}};
static const Line LB3[] = {{"Brannoc", "That key will hold against anything. Good luck up there."}};

static const Line LH0[] = {
    {"Hilda", "Aria, dear! Could you do an old woman a favor?"},
    {"Hilda", "My Sunberry pies are the pride of the inn, but the berries"},
    {"Hilda", "near the village have all gone missing. Find four, would you?"}};
static const Line LH1[] = {{"Hilda", "Four Sunberries, dear. Red bushes around the village edge."}};
static const Line LH2[] = {
    {"Hilda", "Oh, bless you! Sit, sit. The oven's still warm."},
    {"Hilda", "Here, a fresh Sunberry pie. Eat it on the road."},
    {"Aria", "Mmm! I feel like I could run all day!"}};
static const Line LH3[] = {{"Hilda", "Eat up! A hero runs on pie."}};

static const Line LO3[] = {
    {"Orla", "Who tramples my ferns? ...Ah. The lamplighter's apprentice."},
    {"Aria", "Elder Maren sent me. The Gloom is waking."},
    {"Orla", "Yes, yes, I felt it. The sky has been wrong for days."},
    {"Orla", "I keep a piece of the Moonlens. It sleeps, and needs petals."},
    {"Orla", "Five Moonpetals. They bloom blue, deep in the woods."},
    {"Sera", "I'll keep watch. Shout if the trees start whispering."},
    {"Orla", "Oh, and my fox cub Ember wandered off again. Find him?"}};
static const Line LO4r[] = {{"Orla", "Five Moonpetals, child. Blue as a promise, deep in the woods."}};
static const Line LO4d[] = {
    {"Orla", "Oh, lovely. They still hum with moonlight."},
    {"Orla", "There. The first piece of the Moonlens wakes. Take it."},
    {"Aria", "It's warm... and it shows a lake beneath the stars."},
    {"Orla", "Moonlake. The second piece lies with Mira the astronomer."},
    {"Orla", "Dorin the ferryman keeps the dock. Tell him I sent you."}};
static const Line LO5[] = {
    {"Orla", "Mind the shadows near the water, child."},
    {"Orla", "They lean the wrong way now."}};
static const Line LOf[] = {
    {"Orla", "Ember! You naughty little thing! Oh, thank you, Aria."},
    {"Orla", "Take this moon charm. It glows when the dark is near."},
    {"Sera", "Look at him purr. Foxes don't purr. Do they?"}};

static const Line LD0[] = {
    {"Dorin", "Moonlake's been strange lately. Best keep your distance."},
    {"Dorin", "The dock's closed to strangers."}};
static const Line LD5[] = {
    {"Dorin", "Easy there, lass. Dock's closed."},
    {"Aria", "Orla sent me. I need to reach the island."},
    {"Dorin", "Orla, eh? Then listen: the bridge rotted in the last storm."},
    {"Dorin", "Bring me four planks. Driftwood piles up around the lake."},
    {"Dorin", "Four good boards and I'll have the bridge built by sundown."}};
static const Line LD6r[] = {
    {"Dorin", "Four planks. Dry ones, mind."},
    {"Dorin", "They wash up all around the shore and the fields."}};
static const Line LD6d[] = {
    {"Dorin", "Fine boards! Stand clear, lass."},
    {"Dorin", "...There. Sturdy enough for a lamplighter."},
    {"Dorin", "Mira hasn't left that island in a month. Tell her I said hello."}};
static const Line LD7[] = {
    {"Dorin", "Mind the planks, they're slick."},
    {"Dorin", "The island's quiet. Too quiet."}};

static const Line LMi7[] = {
    {"Mira", "A visitor! Careful, don't step on my star charts."},
    {"Aria", "Orla sent me. You have the second piece of the Moonlens?"},
    {"Mira", "I do. But it's bound to those three stones. They sing in order."},
    {"Mira", "The stars told me: Red, then Blue, then Gold."},
    {"Aria", "Red, Blue, Gold. Got it."}};
static const Line LMi8r[] = {{"Mira", "Red, then Blue, then Gold. A misstep and they fall silent."}};
static const Line LMi8d[] = {
    {"Mira", "Oh! They sang! I haven't heard that chord since I was a girl."},
    {"Mira", "Here, the second piece of the Moonlens. It's yours."},
    {"Sera", "Two down, one to go."},
    {"Mira", "The last piece sleeps in the ruins atop Emberpeak."},
    {"Mira", "Beware the knight Kael. He trusts no one."}};
static const Line LMi9[] = {
    {"Mira", "The stars are brighter since you came."},
    {"Mira", "Go well, lamplighter."}};

static const Line LK0[] = {
    {"Kael", "Turn back, traveler. This mountain is closed."},
    {"Kael", "Orders of the old guard. I make no exceptions."}};
static const Line LK9[] = {
    {"Kael", "Halt. The mountain is closed."},
    {"Aria", "I carry two pieces of the Moonlens. The Gloom is waking."},
    {"Kael", "...I felt it too. But the gate is sealed with an Ember Key."},
    {"Kael", "It was lost long ago. Only Brannoc's forge in Willowmere"},
    {"Kael", "is hot enough to make another. Bring iron ore from these"},
    {"Kael", "foothills to him. Three lumps. Then return to me."},
    {"Aria", "You could have said that before I walked all this way."}};
static const Line LK10[] = {{"Kael", "Iron ore, three lumps. East trail. Then Brannoc, then me."}};
static const Line LK11[] = {
    {"Kael", "The Ember Key... so he did it. Stand back."},
    {"Kael", "The seal on the gate flares and goes dark."},
    {"Kael", "The path is open. Light the four braziers by the altar."},
    {"Kael", "That is the old trial of light. Then the altar will answer."}};
static const Line LK12[] = {{"Kael", "Four braziers around the altar. Light them, then speak to it."}};
static const Line LK13[] = {{"Kael", "The mountain is quiet now. Go. The lantern needs you."}};

static const Line LAld[] = {
    {"Voice", "The braziers blaze. The ancient stone hums."},
    {"Aria", "The last piece of the Moonlens... it's shining!"},
    {"Voice", "Lamplighter. The Gloom is not a monster. It is grief."},
    {"Voice", "Carry the light home. Show it what it forgot."}};
static const Line LAlr[] = {{"Voice", "Four braziers must burn before the altar will answer."}};
static const Line LAlo[] = {{"Voice", "The old stone is quiet now."}};
static const Line LF1[] = {
    {"Aria", "All three pieces of the Moonlens. Together..."},
    {"Elder Maren", "Aria! The Gloom is rising from the lantern!"},
    {"Sera", "Everyone back! Aria, whatever you're doing, do it now!"},
    {"Aria", "I'm not afraid of the dark. I grew up lighting it."}};
static const Line LL0[] = {{"Aria", "The Great Lantern. Warm gold light, and a faint hum."}};

static const Line LF2[] = {
    {"Gloom", "...so cold... so alone..."},
    {"Aria", "You're not a monster. You were left in the dark."},
    {"Gloom", "They sealed me. They forgot. They WENT AWAY."},
    {"Aria", "I'm not going away. None of us are."},
    {"Sera", "Aria... the lens!"},
    {"Aria", "Light isn't a wall. It's a hand held out."}};
static const Line LF3[] = {
    {"Elder Maren", "The Gloom... it's gone. And the lantern..."},
    {"Elder Maren", "It burns with every color of dawn."},
    {"Sera", "No more darkness between the stars. Not tonight."},
    {"Aria", "It wasn't gone. It just needed a light to walk home by."},
    {"Tobin", "Does this mean the frogs are safe too?"},
    {"Elder Maren", "Willowmere learned the oldest lesson: light is meant to be shared."}};

static const Talk TALKS[] = {
    TK(1, 0, 0, -1, -1, -1, 1, 0, LM0), TK(1, 1, 1, F_SHARDS3, -1, F_LIT, 2, 0, LM1d), TK(1, 1, 1, -1, -1, -1, -1, 0, LM1r),
    TK(1, 2, 2, -1, -1, -1, -1, 0, LM2), TK(1, 3, 8, -1, -1, -1, -1, 0, LM3a), TK(1, 9, 99, -1, -1, -1, -1, 0, LM3b),
    TK(2, 0, 99, F_CAPFOUND, F_CAP_DONE, F_CAP_DONE, -1, 0, LT2), TK(2, 0, 99, F_CAP_Q, F_CAPFOUND, -1, -1, 0, LT1),
    TK(2, 0, 99, -1, F_CAP_Q, F_CAP_Q, -1, 0, LT0), TK(2, 9, 99, F_CAP_DONE, -1, -1, -1, 0, LT3b), TK(2, 0, 99, F_CAP_DONE, -1, -1, -1, 0, LT3),
    TK(3, 2, 2, -1, -1, F_SERA, 3, 0, LS2), TK(3, 0, 1, -1, -1, -1, -1, 0, LS0), TK(3, 3, 4, -1, -1, -1, -1, 0, LSa),
    TK(3, 5, 6, -1, -1, -1, -1, 0, LSb), TK(3, 7, 8, -1, -1, -1, -1, 0, LSc), TK(3, 9, 12, -1, -1, -1, -1, 0, LSd), TK(3, 13, 99, -1, -1, -1, -1, 0, LSe),
    TK(4, 10, 10, F_ORE3, -1, F_KEY, 11, 0, LB2), TK(4, 10, 10, -1, -1, -1, -1, 0, LB2r), TK(4, 0, 99, -1, F_BRAN_MET, F_BRAN_MET, -1, 0, LB0),
    TK(4, 0, 9, F_BRAN_MET, -1, -1, -1, 0, LB1), TK(4, 11, 99, -1, -1, -1, -1, 0, LB3),
    TK(5, 0, 99, F_BERRIES4, F_BERRY_DONE, F_BERRY_DONE, -1, 0, LH2), TK(5, 0, 99, F_BERRY_Q, F_BERRIES4, -1, -1, 0, LH1),
    TK(5, 0, 99, -1, F_BERRY_Q, F_BERRY_Q, -1, 0, LH0), TK(5, 0, 99, F_BERRY_DONE, -1, -1, -1, 0, LH3),
    TK(6, 0, 99, F_FOXFOUND, F_FOX_DONE, F_FOX_DONE, -1, 0, LOf), TK(6, 3, 3, -1, -1, F_FOX_Q, 4, 0, LO3),
    TK(6, 4, 4, F_PETALS5, -1, F_FRAG1, 5, 0, LO4d), TK(6, 4, 4, -1, -1, -1, -1, 0, LO4r), TK(6, 5, 99, -1, -1, -1, -1, 0, LO5),
    TK(7, 5, 5, -1, -1, -1, 6, 0, LD5), TK(7, 6, 6, F_PLANKS4, -1, F_BRIDGE, 7, 0, LD6d), TK(7, 6, 6, -1, -1, -1, -1, 0, LD6r),
    TK(7, 7, 99, -1, -1, -1, -1, 0, LD7), TK(7, 0, 4, -1, -1, -1, -1, 0, LD0),
    TK(8, 7, 7, -1, -1, -1, 8, 0, LMi7), TK(8, 8, 8, F_STONES, -1, F_FRAG2, 9, 0, LMi8d), TK(8, 8, 8, -1, -1, -1, -1, 0, LMi8r),
    TK(8, 9, 99, -1, -1, -1, -1, 0, LMi9),
    TK(9, 9, 9, -1, -1, -1, 10, 0, LK9), TK(9, 10, 10, -1, -1, -1, -1, 0, LK10), TK(9, 11, 11, -1, -1, F_GATE, 12, 0, LK11),
    TK(9, 12, 12, -1, -1, -1, -1, 0, LK12), TK(9, 13, 99, -1, -1, -1, -1, 0, LK13), TK(9, 0, 8, -1, -1, -1, -1, 0, LK0),
    TK(P_ALTAR, 12, 12, F_BRAZ4, -1, F_FRAG3, 13, 0, LAld), TK(P_ALTAR, 12, 12, -1, -1, -1, -1, 0, LAlr), TK(P_ALTAR, 0, 99, -1, -1, -1, -1, 0, LAlo),
    TK(P_LANTERN, 13, 13, -1, -1, -1, 14, A_END1, LF1), TK(P_LANTERN, 0, 99, -1, -1, -1, -1, 0, LL0)};
static const Talk T_F2 = TK(0, 0, 0, -1, -1, -1, -1, A_END2, LF2);
static const Talk T_F3 = TK(0, 0, 0, -1, -1, -1, -1, A_END3, LF3);

/* ===================== HUD / objectives ===================== */
static const struct { const char *t; int g, donef; const char *ret; } OBJ[15] = {
    {"Talk to Elder Maren", -1, -1, 0}, {"Glowshards: %d/%d", G_SHARD, F_SHARDS3, "Return to Elder Maren"},
    {"Find Sera (south path)", -1, -1, 0}, {"Find Orla in the woods", -1, -1, 0},
    {"Moonpetals: %d/%d", G_PETAL, F_PETALS5, "Return to Orla"}, {"Visit Dorin at Moonlake", -1, -1, 0},
    {"Planks: %d/%d", G_PLANK, F_PLANKS4, "Return to Dorin"}, {"Cross to Mira's island", -1, -1, 0},
    {"Touch stones R,B,G", -1, F_STONES, "Speak to Mira"}, {"Find Kael at Emberpeak", -1, -1, 0},
    {"Iron ore: %d/%d", G_ORE, F_ORE3, "Take ore to Brannoc"}, {"Take the key to Kael", -1, -1, 0},
    {"Light braziers: %d/%d", G_BRAZ, F_BRAZ4, "Speak to the altar"}, {"Return to the Lantern", -1, -1, 0},
    {"", -1, -1, 0}};
static const struct { int q, done, found, g; const char *t; const char *ret; } SIDE[3] = {
    {F_CAP_Q, F_CAP_DONE, F_CAPFOUND, G_CAP, "Find Tobin's red cap", "Return cap to Tobin"},
    {F_BERRY_Q, F_BERRY_DONE, F_BERRIES4, G_BERRY, "Sunberries: %d/%d", "Bring berries to Hilda"},
    {F_FOX_Q, F_FOX_DONE, F_FOXFOUND, G_FOX, "Find Orla's fox cub", "Bring fox to Orla"}};

/* ===================== game state ===================== */
static float gt = 0, cam_yaw = 0, cam_d = 6.5f, bant = 0, play_t = 0;
static char bantxt[40];
static const Talk *curtalk;
static int di, typing, nearact = -1, endp = 0, stone_next = 0;
static float dtime, tkx, tkz, endt = 0;
static char wrapped[256];
static int stone_lit[3], braz_lit[4];

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
static void say(const Talk *t, float fx, float fz) {
    curtalk = t; di = 0; dtime = 0; tkx = fx; tkz = fz;
    wrap(t->l[0].txt);
    state = ST_TALK;
}
static void set_stage(int s) {
    stage = s;
    const char *c = 0;
    if (s == 3) c = "Chapter 2: Whisper Woods";
    else if (s == 5) c = "Chapter 3: Moonlake";
    else if (s == 9) c = "Chapter 4: Emberpeak";
    else if (s == 13) c = "Chapter 5: The Lantern";
    if (c) banner(c);
}
static void finish_talk(void) {
    const Talk *t = curtalk;
    if (t->setf >= 0) {
        fl[t->setf] = 1;
        switch (t->setf) {
        case F_CAP_DONE: banner("Got: Lucky Charm"); break;
        case F_BERRY_DONE: banner("Hilda's pie: run faster!"); break;
        case F_FOX_DONE: banner("Got: Moon Charm"); break;
        case F_FRAG1: banner("Moonlens piece 1/3"); break;
        case F_FRAG2: banner("Moonlens piece 2/3"); break;
        case F_FRAG3: banner("Moonlens piece 3/3"); break;
        case F_KEY: banner("Got: Ember Key"); break;
        case F_BRIDGE: banner("The bridge is built!"); break;
        case F_SERA: banner("Sera joins you!"); break;
        case F_GATE: banner("The gate is open!"); break;
        case F_LIT: banner("The Great Lantern glows!"); break;
        default: break;
        }
    }
    if (t->setstage >= 0) set_stage(t->setstage);
    switch (t->act) {
    case A_END1: endp = 1; endt = 0; state = ST_END; return;
    case A_END2: endp = 2; endt = 0; state = ST_END; return;
    case A_END3: endp = 3; endt = 0; state = ST_END; return;
    default: break;
    }
    state = ST_PLAY;
}

static void talk_lookup(int id, float fx, float fz) {
    for (int i = 0; i < NARR(TALKS); i++) {
        const Talk *t = &TALKS[i];
        if (t->npc != id || stage < t->smin || stage > t->smax) continue;
        if (t->needf >= 0 && !fl[t->needf]) continue;
        if (t->notf >= 0 && fl[t->notf]) continue;
        say(t, fx, fz);
        return;
    }
}
static void pickup(Item *it) {
    it->taken = 1;
    int g = it->group;
    gcount[g]++;
    char b[40];
    if (GDEF[g].target == 1) snprintf(b, sizeof(b), "Found: %s", GDEF[g].name);
    else if (gcount[g] >= GDEF[g].target) snprintf(b, sizeof(b), "%s: all found!", GDEF[g].name);
    else snprintf(b, sizeof(b), "%s %d/%d", GDEF[g].name, gcount[g], GDEF[g].target);
    if (gcount[g] >= GDEF[g].target) fl[GDEF[g].donef] = 1;
    banner(b);
}

#define pl ch[0]
typedef struct { int id; float x, z, r; const char *prompt; } Prop;
static Prop props[9];
static int nprops;
static void init_props(void) {
    nprops = 0;
    for (int i = 0; i < 3; i++) { Prop p = {P_STONE0 + i, STONEP[i][0], STONEP[i][1], 2.0f, "X: Touch the stone"}; props[nprops++] = p; }
    for (int i = 0; i < 4; i++) { Prop p = {P_BRAZ0 + i, BRAZP[i][0], BRAZP[i][1], 2.2f, "X: Light the brazier"}; props[nprops++] = p; }
    { Prop p = {P_ALTAR, ALTARP[0], ALTARP[1], 2.8f, "X: Examine the altar"}; props[nprops++] = p; }
    { Prop p = {P_LANTERN, LANTERNP[0], LANTERNP[1], 2.8f, "X: The Great Lantern"}; props[nprops++] = p; }
}
static int act_ok(int id) {
    if (id >= P_STONE0 && id < P_STONE0 + 3) return stage == 8 && !fl[F_STONES];
    if (id >= P_BRAZ0 && id < P_BRAZ0 + 4) return stage == 12 && !braz_lit[id - P_BRAZ0];
    return 1;
}
static void act_pos(int id, float *x, float *z) {
    if (id >= 0 && id < 10) { *x = ch[id].x; *z = ch[id].z; return; }
    for (int i = 0; i < nprops; i++) if (props[i].id == id) { *x = props[i].x; *z = props[i].z; return; }
    *x = 0; *z = 0;
}
static void interact(int id) {
    float ax, az;
    act_pos(id, &ax, &az);
    if (id >= P_STONE0 && id < P_STONE0 + 3) {
        int k = id - P_STONE0;
        if (k == stone_next) {
            stone_lit[k] = 1; stone_next++;
            if (stone_next >= 3) { fl[F_STONES] = 1; banner("The stones sing!"); } else banner("The stone hums...");
        } else { stone_next = 0; memset(stone_lit, 0, sizeof(stone_lit)); banner("The stones fall silent."); }
        return;
    }
    if (id >= P_BRAZ0 && id < P_BRAZ0 + 4) {
        braz_lit[id - P_BRAZ0] = 1; gcount[G_BRAZ]++;
        char b[40];
        if (gcount[G_BRAZ] >= 4) { fl[F_BRAZ4] = 1; snprintf(b, sizeof(b), "All braziers lit!"); }
        else snprintf(b, sizeof(b), "Brazier %d/4", gcount[G_BRAZ]);
        banner(b);
        return;
    }
    talk_lookup(id, ax, az);
}

static int nearest_item(int g, float *x, float *z) {
    float best = 1e9f; int f = 0;
    for (int i = 0; i < NITEM; i++) {
        Item *it = &items[i];
        if (it->group != g || !item_active(it)) continue;
        float d = dist(pl.x, pl.z, it->x, it->z);
        if (d < best) { best = d; *x = it->x; *z = it->z; f = 1; }
    }
    return f;
}
static int objective_target(float *x, float *z) {
    int n = -1;
    switch (stage) {
    case 0: n = 1; break;
    case 1: if (fl[F_SHARDS3]) n = 1; else return nearest_item(G_SHARD, x, z); break;
    case 2: n = 3; break;
    case 3: n = 6; break;
    case 4: if (fl[F_PETALS5]) n = 6; else return nearest_item(G_PETAL, x, z); break;
    case 5: n = 7; break;
    case 6: if (fl[F_PLANKS4]) n = 7; else return nearest_item(G_PLANK, x, z); break;
    case 7: case 8: n = 8; break;
    case 9: case 11: n = 9; break;
    case 10: if (fl[F_ORE3]) n = 4; else return nearest_item(G_ORE, x, z); break;
    case 12:
        if (fl[F_BRAZ4]) { *x = ALTARP[0]; *z = ALTARP[1]; return 1; }
        { float best = 1e9f; int f = 0;
          for (int i = 0; i < 4; i++) if (!braz_lit[i]) { float d = dist(pl.x, pl.z, BRAZP[i][0], BRAZP[i][1]); if (d < best) { best = d; *x = BRAZP[i][0]; *z = BRAZP[i][1]; f = 1; } }
          return f; }
    case 13: *x = LANTERNP[0]; *z = LANTERNP[1]; return 1;
    default: return 0;
    }
    if (n < 0) return 0;
    *x = ch[n].x; *z = ch[n].z;
    return 1;
}

static void reset_game(void) {
    stage = 0; memset(fl, 0, sizeof(fl)); memset(gcount, 0, sizeof(gcount));
    for (int i = 0; i < NITEM; i++) items[i].taken = 0;
    memset(braz_lit, 0, sizeof(braz_lit)); memset(stone_lit, 0, sizeof(stone_lit)); stone_next = 0;
    init_chars();
    cam_yaw = 0; cam_d = 6.5f; endp = 0; endt = 0; bant = 0; nearact = -1;
    state = ST_TITLE;
}

/* ===================== update ===================== */
static void move_char(Char *c, float nx, float nz) {
    if (canwalk(nx, nz)) { c->x = nx; c->z = nz; }
    else if (canwalk(nx, c->z)) c->x = nx;
    else if (canwalk(c->x, nz)) c->z = nz;
}
static void update(const SceCtrlData *pd, unsigned int pr) {
    gt += DT;
    if (bant > 0) bant -= DT;
    if (endp) endt += DT;

    for (int i = 1; i < 10; i++) {
        Char *c = &ch[i];
        c->talk = (state == ST_TALK && curtalk->npc == i && typing);
        if (!(i == 3 && fl[F_SERA])) c->move += (0 - c->move) * fminf(1.0f, 10.0f * DT);
        float d = dist(pl.x, pl.z, c->x, c->z);
        if (d < 7.0f || (state == ST_TALK && curtalk->npc == i))
            c->yaw += angdiff(c->yaw, atan2f(pl.x - c->x, pl.z - c->z)) * fminf(1.0f, 5.0f * DT);
        c->y = ground_y(c->x, c->z);
    }

    /* companion */
    if (fl[F_SERA] && (state == ST_PLAY || state == ST_TALK)) {
        Char *s = &ch[3];
        float d = dist(s->x, s->z, pl.x, pl.z);
        if (d > 30.0f) { s->x = pl.x - 1.5f; s->z = pl.z - 1.5f; }
        else if (d > 3.2f && state == ST_PLAY) {
            float sp = d > 7.0f ? 6.5f : 3.6f, dx = (pl.x - s->x) / d, dz = (pl.z - s->z) / d;
            move_char(s, s->x + dx * sp * DT, s->z + dz * sp * DT);
            s->walk += sp * DT * 2.4f;
            s->yaw += angdiff(s->yaw, atan2f(dx, dz)) * fminf(1.0f, 10.0f * DT);
            s->move += (1 - s->move) * fminf(1.0f, 10.0f * DT);
        } else s->move += (0 - s->move) * fminf(1.0f, 10.0f * DT);
        s->y = ground_y(s->x, s->z);
    }
    pl.y = ground_y(pl.x, pl.z);
    float ctd = (state == ST_TALK) ? 4.4f : 6.5f;
    cam_d += (ctd - cam_d) * fminf(1.0f, 3.0f * DT);

    if (state == ST_TITLE) {
        if (pr & PSP_CTRL_CROSS) { state = ST_PLAY; play_t = 0; banner("Chapter 1: The Dark Lantern"); }
        return;
    }

    if (state == ST_END) {
        float lyaw = atan2f(LANTERNP[0] - pl.x, LANTERNP[1] - pl.z);
        cam_yaw += angdiff(cam_yaw, lyaw) * fminf(1.0f, 2.0f * DT);
        pl.yaw += angdiff(pl.yaw, lyaw) * fminf(1.0f, 4.0f * DT);
        pl.move += (0 - pl.move) * fminf(1.0f, 10.0f * DT);
        if (endp == 1 && endt > 6.0f) say(&T_F2, LANTERNP[0], LANTERNP[1]);
        else if (endp == 2 && endt > 6.0f) say(&T_F3, LANTERNP[0], LANTERNP[1]);
        else if (endp == 3 && endt > 3.0f && (pr & PSP_CTRL_CROSS)) reset_game();
        return;
    }

    if (state == ST_TALK) {
        pl.move += (0 - pl.move) * fminf(1.0f, 10.0f * DT);
        float tyaw = atan2f(tkx - pl.x, tkz - pl.z);
        pl.yaw += angdiff(pl.yaw, tyaw) * fminf(1.0f, 6.0f * DT);
        cam_yaw += angdiff(cam_yaw, tyaw) * fminf(1.0f, 3.0f * DT);
        dtime += DT;
        int len = (int)strlen(wrapped), vs = (int)(dtime * 45.0f);
        typing = vs < len;
        if (pr & PSP_CTRL_CROSS) {
            if (typing) dtime = 999.0f;
            else {
                di++;
                if (di >= curtalk->n) { typing = 0; finish_talk(); }
                else { wrap(curtalk->l[di].txt); dtime = 0; }
            }
        }
        return;
    }

    /* ST_PLAY */
    play_t += DT;
    float sx = (pd->Lx - 128) / 128.0f, sy = (pd->Ly - 128) / 128.0f;
    if (fabsf(sx) < 0.22f) sx = 0;
    if (fabsf(sy) < 0.22f) sy = 0;
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag > 1.0f) mag = 1.0f;
    if (pd->Buttons & PSP_CTRL_LTRIGGER) cam_yaw -= 1.8f * DT;
    if (pd->Buttons & PSP_CTRL_RTRIGGER) cam_yaw += 1.8f * DT;
    float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
    float mx = fx * (-sy) + (-fz) * sx, mz = fz * (-sy) + fx * sx, target = 0;
    if (mag > 0) {
        float l = sqrtf(mx * mx + mz * mz);
        mx /= l; mz /= l;
        float sp = ((pd->Buttons & PSP_CTRL_CIRCLE) ? 5.6f : 3.4f) * mag * (fl[F_BERRY_DONE] ? 1.25f : 1.0f);
        move_char(&pl, pl.x + mx * sp * DT, pl.z + mz * sp * DT);
        pl.walk += sp * DT * 2.4f;
        pl.yaw += angdiff(pl.yaw, atan2f(mx, mz)) * fminf(1.0f, 12.0f * DT);
        target = 1;
    }
    pl.move += (target - pl.move) * fminf(1.0f, 10.0f * DT);
    push_out(&pl.x, &pl.z, 0.45f);
    for (int i = 1; i < 10; i++) {
        float dx = pl.x - ch[i].x, dz = pl.z - ch[i].z, d = sqrtf(dx * dx + dz * dz);
        if (d < 0.9f && d > 0.0001f) { pl.x = ch[i].x + dx / d * 0.9f; pl.z = ch[i].z + dz / d * 0.9f; }
    }
    if (!canwalk(pl.x, pl.z)) { /* pushed into water/gate: nudge back toward village */
        float d = dist(pl.x, pl.z, 0, 0);
        if (d > 0.1f) { pl.x -= pl.x / d * 0.3f; pl.z -= pl.z / d * 0.3f; }
    }

    for (int i = 0; i < NITEM; i++) {
        Item *it = &items[i];
        if (!item_active(it)) continue;
        if (dist(pl.x, pl.z, it->x, it->z) < (it->type == IT_FOX ? 1.9f : 1.5f)) pickup(it);
    }

    nearact = -1;
    float best = 99.0f;
    for (int i = 1; i < 10; i++) {
        float d = dist(pl.x, pl.z, ch[i].x, ch[i].z);
        if (d < 2.8f && d < best) { best = d; nearact = i; }
    }
    for (int i = 0; i < nprops; i++) {
        float d = dist(pl.x, pl.z, props[i].x, props[i].z);
        if (d < props[i].r && d < best && act_ok(props[i].id)) { best = d; nearact = props[i].id; }
    }
    if (nearact >= 0 && (pr & PSP_CTRL_CROSS)) interact(nearact);
}

/* ===================== render ===================== */
static void setup_cam(float t) {
    ScePspFVector3 eye, ctr, up = {0, 1, 0};
    if (state == ST_TITLE) {
        float a = t * 0.12f;
        eye.x = sinf(a) * 24.0f; eye.y = 10.0f; eye.z = -4.0f + cosf(a) * 24.0f;
        ctr.x = 0; ctr.y = 2.0f; ctr.z = -4.0f;
    } else {
        float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
        eye.x = pl.x - fx * cam_d; eye.z = pl.z - fz * cam_d; eye.y = pl.y + 2.6f + cam_d * 0.18f;
        float tg = terrain(eye.x, eye.z) + 1.5f;
        if (eye.y < tg) eye.y = tg;
        ctr.x = pl.x; ctr.y = pl.y + 1.4f; ctr.z = pl.z;
    }
    cex = eye.x; cez = eye.z;
    float dx = ctr.x - eye.x, dz = ctr.z - eye.z, l = sqrtf(dx * dx + dz * dz);
    if (l < 0.001f) l = 1;
    cfx = dx / l; cfz = dz / l;
    sceGumMatrixMode(GU_PROJECTION); sceGumLoadIdentity();
    sceGumPerspective(55.0f, 16.0f / 9.0f, 2.0f, 220.0f);
    sceGumMatrixMode(GU_VIEW); sceGumLoadIdentity();
    sceGumLookAt(&eye, &ctr, &up);
    sceGumMatrixMode(GU_MODEL); sceGumLoadIdentity();
}

static void draw_char(int id, const Char *c, float t) {
    const CSet *s = &cs[id];
    float sw = sinf(c->walk) * 0.85f * c->move;
    float bob = fabsf(sinf(c->walk)) * 0.06f * c->move * c->sc + sinf(t * 2.0f + id) * 0.008f;
    sceGumMatrixMode(GU_MODEL);
    inst(mBlob, c->x, c->y + 0.12f, c->z, 0, c->sc);
    sceGumPushMatrix();
    tr(c->x, c->y + bob, c->z);
    sceGumRotateY(c->yaw);
    { ScePspFVector3 v = {c->sc, c->sc, c->sc}; sceGumScale(&v); }
    if (!c->robe) {
        for (int sd = -1; sd <= 1; sd += 2) {
            sceGumPushMatrix(); tr(sd * 0.13f, 0.88f, 0); sceGumRotateX(sd * sw); draw(s->leg); sceGumPopMatrix();
        }
        sceGumPushMatrix(); tr(0, 1.2f, 0); draw(s->torso); sceGumPopMatrix();
    } else { sceGumPushMatrix(); tr(0, 0.93f, 0); draw(s->robe); sceGumPopMatrix(); }
    for (int sd = -1; sd <= 1; sd += 2) {
        float a = -sd * sw + sinf(t * 1.5f + id) * 0.03f;
        if (c->talk && sd == 1) a = -1.1f + sinf(t * 7.0f) * 0.35f;
        sceGumPushMatrix(); tr(sd * 0.34f, 1.56f, 0); sceGumRotateX(a); sceGumRotateZ(sd * 0.12f);
        draw(s->arm);
        if (c->lantern && sd == -1) { sceGumPushMatrix(); tr(0, -0.9f, 0.1f); draw(mOrb[O_WARM]); sceGumPopMatrix(); }
        sceGumPopMatrix();
    }
    if (c->staff) { sceGumPushMatrix(); tr(-0.5f, 0, 0.15f); draw(mStaff); sceGumPopMatrix(); }
    sceGumPushMatrix(); tr(0, 1.86f, 0); sceGumRotateX(c->talk ? sinf(t * 5.0f) * 0.08f : 0.0f);
    draw(s->head); draw(s->hair);
    sceGumPopMatrix();
    sceGumPopMatrix();
}

static void render_scene(float t) {
    setup_cam(t);
    for (int i = 0; i < 12; i++) { float a = i * (2 * PI / 12); inst(mHill, cosf(a) * 150, -4, sinf(a) * 150, a, 1); }
    draw(mGround);
    draw(mPath);
    draw(mSq);
    sceGuTexOffset(t * 0.02f, t * 0.015f);
    draw(mWater);
    sceGuTexOffset(0, 0);

    for (int i = 0; i < 6; i++) {
        const House *h = &HOUSES[i];
        if (!vis(h->x, h->z, 5, 110)) continue;
        sceGumPushMatrix(); tr(h->x, terrain(h->x, h->z), h->z); sceGumRotateY(hyaw[i]);
        { ScePspFVector3 v = {h->s, h->s, h->s}; sceGumScale(&v); }
        draw(mWall[h->wall]); draw(mRoof[h->roof]); draw(mDoor); draw(mWin);
        sceGumPopMatrix();
    }
    for (int i = 0; i < 5; i++) { float y = terrain(LAMPS[i][0], LAMPS[i][1]); inst(mPost, LAMPS[i][0], y, LAMPS[i][1], 0, 1); inst(mLampL, LAMPS[i][0], y, LAMPS[i][1], 0, 1); }
    { float ly = terrain(LANTERNP[0], LANTERNP[1]);
      inst(mGPost, LANTERNP[0], ly, LANTERNP[1], 0, 1);
      if (fl[F_LIT]) inst(mGOn, LANTERNP[0], ly + 4.9f, LANTERNP[1], 0, 1.0f + 0.06f * sinf(t * 3.0f));
      else inst(mGOff, LANTERNP[0], ly + 4.9f, LANTERNP[1], 0, 1);
      if (fl[F_LIT])
          for (int i = 0; i < 14; i++) {
              float a = t * (0.4f + 0.05f * (i % 5)) + i * 1.7f, r = 2.0f + (i % 4) * 1.2f;
              inst(mFly, LANTERNP[0] + sinf(a) * r, ly + 1.4f + sinf(t * 1.3f + i) * 0.8f + (i % 3) * 0.4f, LANTERNP[1] + cosf(a) * r, 0, 1);
          } }
    inst(mWell, WELLP[0], terrain(WELLP[0], WELLP[1]), WELLP[1], 0, 1);
    for (int i = 0; i < 2; i++) if (vis(TENTS[i][0], TENTS[i][1], 3, 110)) inst(mTent[(int)TENTS[i][2]], TENTS[i][0], terrain(TENTS[i][0], TENTS[i][1]), TENTS[i][1], 0, 1);
    { float cy = terrain(50, -31); inst(mCamp, 50, cy, -31, 0, 1); inst(mFlame, 50, cy + 0.6f, -31, 0, 0.8f + 0.2f * sinf(t * 9.0f)); }
    if (!fl[F_GATE]) {
        float gy = terrain(GATEP[0], GATEP[1]);
        inst(mPillar, GATEP[0] + 2.7f, terrain(GATEP[0] + 2.7f, GATEP[1] + 1.35f), GATEP[1] + 1.35f, 0, 1);
        inst(mPillar, GATEP[0] - 2.7f, terrain(GATEP[0] - 2.7f, GATEP[1] - 1.35f), GATEP[1] - 1.35f, 0, 1);
        inst(mSeal, GATEP[0], gy + 2.0f, GATEP[1], -0.47f, 1.0f + 0.04f * sinf(t * 4.0f));
    }
    static const int stoneOrb[3] = {O_RED, O_BLUE, O_GOLD};
    for (int i = 0; i < 3; i++) {
        float sy = terrain(STONEP[i][0], STONEP[i][1]);
        inst(mStone, STONEP[i][0], sy, STONEP[i][1], i * 1.3f, 1);
        inst(mOrb[(stone_lit[i] || fl[F_STONES]) ? stoneOrb[i] : O_DIM], STONEP[i][0], sy + 3.2f + sinf(t * 2 + i) * 0.1f, STONEP[i][1], 0, 1);
    }
    inst(mAltar, ALTARP[0], terrain(ALTARP[0], ALTARP[1]), ALTARP[1], 0, 1);
    inst(mOrb[fl[F_BRAZ4] ? O_CYAN : O_DIM], ALTARP[0], terrain(ALTARP[0], ALTARP[1]) + 2.4f + sinf(t * 2) * 0.12f, ALTARP[1], 0, fl[F_BRAZ4] ? 2.4f : 1.4f);
    for (int i = 0; i < 4; i++) {
        float by = terrain(BRAZP[i][0], BRAZP[i][1]);
        inst(mBraz, BRAZP[i][0], by, BRAZP[i][1], 0, 1);
        if (braz_lit[i]) inst(mFlame, BRAZP[i][0], by + 1.5f, BRAZP[i][1], t, 0.8f + 0.25f * sinf(t * 8.0f + i));
        else inst(mOrb[O_DIM], BRAZP[i][0], by + 1.2f, BRAZP[i][1], 0, 0.6f);
    }
    if (fl[F_BRIDGE]) {
        for (int i = 0; i < 17; i++) inst(mBrPlank, -52.5f - i * 1.15f, -0.2f, -45, 1.5708f, 1);
        for (int i = 0; i < 6; i++) { inst(mBrPost, -53.0f - i * 3.6f, -0.2f, -43.5f, 0, 1); inst(mBrPost, -53.0f - i * 3.6f, -0.2f, -46.5f, 0, 1); }
    }

    for (int i = 0; i < ntree; i++) {
        const Deco *d = &trees[i];
        if (!vis(d->x, d->z, 3, 85)) continue;
        sceGumPushMatrix(); tr(d->x, d->y, d->z); sceGumRotateY(d->yaw);
        { ScePspFVector3 v = {d->s, d->s, d->s}; sceGumScale(&v); }
        if (d->type < 3) {
            draw(mTrunk[d->type]);
            sceGumPushMatrix(); tr(0, 3.0f, 0); sceGumRotateZ(sinf(t * 1.2f + i) * 0.03f); draw(mCrown[d->type]); sceGumPopMatrix();
        } else { draw(mPineT); sceGumPushMatrix(); sceGumRotateZ(sinf(t * 1.0f + i) * 0.02f); draw(mPineC); sceGumPopMatrix(); }
        sceGumPopMatrix();
    }
    for (int i = 0; i < nrock; i++) { const Deco *d = &rocks[i]; if (vis(d->x, d->z, 2, 80)) inst(mRock[d->type], d->x, d->y, d->z, d->yaw, d->s); }
    for (int i = 0; i < nflw; i++) { const Deco *d = &flowers[i]; if (vis(d->x, d->z, 1, 45)) inst(mFlower[d->type], d->x, d->y, d->z, d->yaw, d->s); }

    for (int i = 0; i < 12; i++) {
        float bx = cosf(i * 0.52f) * (12 + i * 3.0f), bz = sinf(i * 0.52f) * (12 + i * 3.0f);
        float x = bx + sinf(t * 0.5f + i) * 3, z = bz + cosf(t * 0.4f + i * 1.3f) * 3;
        if (!vis(x, z, 1, 40)) continue;
        float y = terrain(x, z) + 1.1f + sinf(t * 1.3f + i) * 0.3f, fl_ = sinf(t * 18.0f + i) * 0.8f;
        sceGumPushMatrix(); tr(x, y, z); sceGumRotateY(t * 0.5f + i);
        sceGumPushMatrix(); sceGumRotateZ(-fl_); draw(mWing[(i % 3) * 2]); sceGumPopMatrix();
        sceGumPushMatrix(); sceGumRotateZ(fl_); draw(mWing[(i % 3) * 2 + 1]); sceGumPopMatrix();
        sceGumPopMatrix();
    }

    for (int i = 0; i < NITEM; i++) {
        const Item *it = &items[i];
        if (!item_active(it) || !vis(it->x, it->z, 2, 70)) continue;
        float y = terrain(it->x, it->z);
        switch (it->type) {
        case IT_SHARD: inst(mShard, it->x, y + 1.0f + sinf(t * 2 + i) * 0.2f, it->z, t * 1.5f, 1); break;
        case IT_BERRY:
            inst(mBush, it->x, y, it->z, i, 1);
            for (int k = 0; k < 3; k++) inst(mBerry, it->x + cosf(k * 2.1f) * 0.4f, y + 0.6f + 0.1f * k, it->z + sinf(k * 2.1f) * 0.4f, 0, 1);
            break;
        case IT_PETAL: inst(mPetal, it->x, y + sinf(t * 2 + i) * 0.05f, it->z, t, 1); break;
        case IT_PLANK: inst(mPlank, it->x, y + 0.12f, it->z, i * 1.1f, 1); break;
        case IT_ORE: inst(mOre, it->x, y, it->z, i, 1); break;
        case IT_WISP: inst(mWisp, it->x, y + 1.5f + sinf(t * 2 + i) * 0.3f, it->z, 0, 1.0f + 0.15f * sinf(t * 4 + i)); break;
        case IT_CAP: inst(mCapM, it->x, y + 0.1f, it->z, 0.7f, 1); break;
        case IT_FOX: inst(mFox, it->x, y + fabsf(sinf(t * 3)) * 0.15f, it->z, atan2f(pl.x - it->x, pl.z - it->z), 1); break;
        }
    }

    for (int i = 1; i < 10; i++) if (vis(ch[i].x, ch[i].z, 2, 90)) draw_char(i, &ch[i], t);
    if (state != ST_TITLE) draw_char(0, &pl, t);

    if (state == ST_PLAY || state == ST_TALK) {
        float ox, oz;
        if (stage <= 13 && objective_target(&ox, &oz)) inst(mBeam, ox, terrain(ox, oz), oz, 0, 1);
    }

    if (endp == 1 || endp == 2) {
        float s = endp == 1 ? fminf(1.0f, endt / 5.0f) : fmaxf(0.0f, 1.0f - endt / 3.0f);
        if (s > 0.01f) {
            float gy = terrain(LANTERNP[0], LANTERNP[1]) + 7.5f + sinf(t * 1.5f) * 0.4f;
            sceGumPushMatrix(); tr(LANTERNP[0], gy, LANTERNP[1]); sceGumRotateY(t * 0.6f);
            { ScePspFVector3 v = {s, s, s}; sceGumScale(&v); }
            draw(mGloom);
            for (int i = 0; i < 7; i++) {
                sceGumPushMatrix(); sceGumRotateY(i * 0.9f + t * 0.8f); sceGumRotateZ(0.9f + 0.3f * sinf(t * 2 + i)); tr(0, 2.2f, 0); draw(mTend); sceGumPopMatrix();
            }
            sceGumPopMatrix();
        }
    }
}

static void draw_sky(void) {
    unsigned int top = RGB(70, 130, 222), bot = RGB(205, 228, 245);
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v[0].c = top; v[0].x = 0; v[0].y = 0; v[0].z = 0;
    v[1].c = top; v[1].x = SCR_W; v[1].y = 0; v[1].z = 0;
    v[2].c = bot; v[2].x = 0; v[2].y = 150; v[2].z = 0;
    v[3].c = bot; v[3].x = SCR_W; v[3].y = 150; v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}

static void render_ui(void) {
    if (endp == 1 || endp == 2) {
        float s = endp == 1 ? fminf(1.0f, endt / 5.0f) : fmaxf(0.0f, 1.0f - endt / 3.0f);
        draw_rect(0, 0, SCR_W, SCR_H, RGBA(10, 0, 30, (int)(150 * s)));
    }
    if (endp == 2) {
        float a = endt < 3.0f ? endt / 3.0f : fmaxf(0.0f, 1.0f - (endt - 3.0f) / 3.0f);
        draw_rect(0, 0, SCR_W, SCR_H, RGBA(255, 255, 240, (int)(255 * a)));
    }
    if (state == ST_TITLE) {
        draw_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 70));
        text_center(50, "LAMPLIGHTER", 4, RGB(255, 220, 120));
        text_center(100, "of Willowmere", 2, RGB(255, 255, 255));
        if (((int)(gt * 2)) % 2 == 0) text_center(190, "Press X to begin", 2, RGB(255, 255, 255));
        text_center(240, "Stick:move O:run L/R:camera X:talk", 1, RGB(230, 230, 230));
        return;
    }
    if (endp == 3) {
        static const char *CRED[] = {"THE END", "", "Lamplighter of Willowmere", "", "Moonlens pieces: 3/3", "WISPS", "", "Thank you for playing!", "", "Press X"};
        draw_rect(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 215));
        float off = fminf(endt * 30.0f, (float)(SCR_H + 10 * 30 - 150));
        for (int i = 0; i < 10; i++) {
            int y = (int)(SCR_H - off + i * 30);
            if (y < -20 || y > SCR_H) continue;
            char b[40];
            const char *s = CRED[i];
            if (i == 5) { snprintf(b, sizeof(b), "Wisps found: %d/8", gcount[G_WISP]); s = b; }
            text_center(y, s, 2, i == 0 ? RGB(255, 220, 120) : RGB(255, 255, 255));
        }
        return;
    }

    char buf[5][40];
    unsigned int col[5];
    int n = 0;
    if (stage <= 13) {
        int g = OBJ[stage].g;
        if (g >= 0 && !fl[OBJ[stage].donef]) snprintf(buf[n], 40, OBJ[stage].t, gcount[g], GDEF[g].target);
        else if (OBJ[stage].ret && OBJ[stage].donef >= 0 && fl[OBJ[stage].donef]) snprintf(buf[n], 40, "%s", OBJ[stage].ret);
        else snprintf(buf[n], 40, "%s", OBJ[stage].t);
        col[n++] = RGB(255, 230, 120);
        float ox, oz;
        if (objective_target(&ox, &oz)) {
            float rel = angdiff(cam_yaw, atan2f(ox - pl.x, oz - pl.z));
            const char *w = fabsf(rel) < 0.6f ? "ahead" : (fabsf(rel) > 2.4f ? "behind" : (rel < 0 ? "right" : "left"));
            snprintf(buf[n], 40, "%dm %s", (int)dist(pl.x, pl.z, ox, oz), w);
            col[n++] = RGB(140, 220, 255);
        }
    }
    for (int k = 0; k < 3 && n < 5; k++) {
        if (!fl[SIDE[k].q] || fl[SIDE[k].done]) continue;
        if (fl[SIDE[k].found]) snprintf(buf[n], 40, "%s", SIDE[k].ret);
        else snprintf(buf[n], 40, SIDE[k].t, gcount[SIDE[k].g], GDEF[SIDE[k].g].target);
        col[n++] = RGB(255, 255, 255);
    }
    if (n > 0 && state != ST_TALK) {
        int wmax = 0;
        for (int i = 0; i < n; i++) if ((int)strlen(buf[i]) > wmax) wmax = (int)strlen(buf[i]);
        draw_rect(6, 6, wmax * 16 + 14, n * 18 + 8, RGBA(0, 0, 0, 140));
        for (int i = 0; i < n; i++) text_sh(12, 10 + i * 18, buf[i], 2, col[i]);
    }
    if (state == ST_PLAY) {
        char b[40];
        int fr = fl[F_FRAG1] + fl[F_FRAG2] + fl[F_FRAG3];
        if (gcount[G_WISP] > 0) { snprintf(b, sizeof(b), "Wisps %d/8", gcount[G_WISP]); text_sh(SCR_W - 8 * 16 - 20, 232, b, 2, RGB(255, 250, 200)); }
        if (fr > 0) { snprintf(b, sizeof(b), "Moonlens %d/3", fr); text_sh(SCR_W - 12 * 16 - 12, 250, b, 2, RGB(150, 230, 255)); }
        if (play_t < 10.0f) text_center(262, "Stick:move O:run L/R:camera X:talk", 1, RGB(255, 255, 255));
    }
    if (bant > 0) {
        int w = (int)strlen(bantxt) * 16;
        draw_rect((SCR_W - w) / 2 - 8, 40, w + 16, 26, RGBA(0, 0, 0, 160));
        text_sh((SCR_W - w) / 2, 45, bantxt, 2, RGB(255, 255, 255));
    }
    if (state == ST_PLAY && nearact >= 0) {
        const char *p = 0;
        char pb[40];
        if (nearact < 10) { snprintf(pb, sizeof(pb), "X: Talk to %s", CD[nearact].name); p = pb; }
        else for (int i = 0; i < nprops; i++) if (props[i].id == nearact) p = props[i].prompt;
        if (p) {
            int w = (int)strlen(p) * 16;
            draw_rect((SCR_W - w) / 2 - 8, 196, w + 16, 26, RGBA(0, 0, 0, 160));
            text_sh((SCR_W - w) / 2, 201, p, 2, RGB(255, 255, 255));
        }
    }
    if (state == ST_TALK) {
        draw_rect(10, 182, 460, 86, RGBA(0, 0, 0, 200));
        const char *who = curtalk->l[di].who;
        draw_text(20, 188, who, (int)strlen(who), 2, RGB(255, 220, 90));
        int len = (int)strlen(wrapped), vs = (int)(dtime * 45.0f);
        if (vs > len) vs = len;
        int shown = 0, yy = 207;
        const char *p = wrapped;
        while (*p && shown < vs) {
            int ln = 0;
            while (p[ln] && p[ln] != '\n') ln++;
            int take = ln;
            if (shown + take > vs) take = vs - shown;
            draw_text(20, yy, p, take, 2, RGB(255, 255, 255));
            shown += ln; p += ln;
            if (*p == '\n') p++;
            yy += 18;
        }
        if (vs >= len && ((int)(gt * 3)) % 2 == 0) draw_text(440, 246, ">", 1, 2, RGB(255, 255, 255));
    }
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xfff5e4cd);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    draw_sky();

    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    cur_tex = -1;
    render_scene(gt);

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    render_ui();
    sceGuDisable(GU_BLEND);

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
    sceGuEnable(GU_CLIP_PLANES);
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
    sceKernelDcacheWritebackAll();
    init_world();
    init_props();
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
