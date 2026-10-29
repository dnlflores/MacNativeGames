/* Breath of the Wild's button prompts as PlayStation buttons (TOTK_BUTTON_ICONS=playstation, the start screen's Controls ->
   Button icons). Every button the game names in text (A, B, X, Y, L, R, ZL, ZR, +, -, the D-pad, the sticks) is a glyph
   of its icon font External_00.bffnt, the same file in each Font/Font_<region>.sbfarc (a Yaz0 SARC): characters U+E040..
   drawn from one BC4 sheet, a BNTX inside the font's TGLP block (block-linear, rows stored bottom-up, cells of the cell
   size plus one pixel). romfs.c hands the pack's bytes here when the game opens it; the glyphs are redrawn by position, as
   the controller map pairs them (runtime/controller_map.h: the right face button is A; a changed layout comes from the
   launcher, face_kind):
     A -> circle, B -> cross, X -> triangle, Y -> square, L / R -> L1 / R1, ZL / ZR -> L2 / R2, + -> Options (three
     lines), - -> Create, the D-pad -> a cross-shaped pad with the pressed arm filled.
   The sticks, the console and the arrows stay. Button outlines (the disc, the shoulder shapes) are the game's own glyphs
   with their letters filled in; the symbols are drawn here from geometry, anti-aliased by their distance to the pixel.
   Only the BC4 blocks that change are encoded again, and the pack goes back as a stored (uncompressed) Yaz0 file of the
   same unpacked size, so the resource size table still fits. Nothing from the game is kept in the repository.
   The action prompt ("A Talk" and the X / Y / B keys around it, blyt/PaDoCommand*) draws its keys from pictures of their
   own instead: 48 x 48 one-channel textures in Layout/Common.sblarc inside Pack/Bootup.pack, and the faded keys beside it
   are one-letter text panes (prompt_letters). The layout archive is unpacked, drawn on the same way, compressed again
   (yaz0_encode) and put back in the pack (sarc_replace). */
#include "botw_button_icons.h"
#include "botw_cheats.h"   /* botw_yaz0_decode, botw_sarc_get (runtime/botw_item_list.c) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

int botw_button_icons_wanted(const char *path) {
    const char *e = getenv("TOTK_BUTTON_ICONS");
    if (!e || strncmp(e, "playstation", 11) || (e[11] && e[11] != ':') || !path) return 0;
    size_t n = strlen(path);
    return (!strncmp(path, "Font/Font_", 10) && n > 17 && !strcmp(path + n - 7, ".sbfarc")) || !strcmp(path, "Pack/Bootup.pack");
}

// ---------- drawing: coverage of a shape from its signed distance (pixels, negative inside) ----------
static float cov(float d) { float a = 0.5f - d; return a < 0 ? 0 : a > 1 ? 1 : a; }
static float seg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypotf(px - ax - t * dx, py - ay - t * dy);
}
/* The arc of radius r around (cx, cy) from angle a0 to a1 degrees (y down, so 270 is the top; a1 > a0). */
static float arc(float px, float py, float cx, float cy, float r, float a0, float a1) {
    float t = atan2f(py - cy, px - cx) * 180 / (float)M_PI;
    while (t < a0) t += 360;
    if (t <= a1) return fabsf(hypotf(px - cx, py - cy) - r);
    float e0 = a0 * (float)M_PI / 180, e1 = a1 * (float)M_PI / 180;
    return fminf(hypotf(px - cx - r * cosf(e0), py - cy - r * sinf(e0)), hypotf(px - cx - r * cosf(e1), py - cy - r * sinf(e1)));
}
static float box(float px, float py, float hx, float hy, float r) {   /* centred rounded box with half sizes hx, hy */
    float qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
    return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - r;
}
static float polygon(float px, float py, const float *v, int n) {   /* convex, either winding */
    float d = 1e9f; int inside = 1, sign = 0;
    for (int i = 0; i < n; i++) {
        const float *a = v + 2 * i, *b = v + 2 * ((i + 1) % n);
        d = fminf(d, seg(px, py, a[0], a[1], b[0], b[1]));
        float c = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
        int s = c > 0 ? 1 : c < 0 ? -1 : 0;
        if (s && sign && s != sign) inside = 0;
        if (s) sign = s;
    }
    return inside ? -d : d;
}
static float triangle(float px, float py, float cx, float cy, float r, float dirx, float diry) {   /* tip towards dir */
    float v[6];
    for (int k = 0; k < 3; k++) {
        float a = atan2f(diry, dirx) + k * 2 * (float)M_PI / 3;
        v[2 * k] = cx + r * cosf(a); v[2 * k + 1] = cy + r * sinf(a);
    }
    return polygon(px, py, v, 3);
}

/* Shoulder-button letters as strokes on a box one unit high (y down): segments 's' x0 y0 x1 y1, arcs 'a' cx cy r a0 a1. */
typedef struct { char kind; float v[5]; } Stroke;
typedef struct { char c; float width; int n; Stroke s[5]; } Letter;
static const Letter kLetters[] = {
    {'L', 0.56f, 2, {{'s', {0, 0, 0, 1}}, {'s', {0, 1, 0.56f, 1}}}},
    {'R', 0.56f, 5, {{'s', {0, 0, 0, 1}}, {'s', {0, 0, 0.28f, 0}}, {'a', {0.28f, 0.26f, 0.26f, 270, 450}},
                     {'s', {0.28f, 0.52f, 0, 0.52f}}, {'s', {0.24f, 0.52f, 0.56f, 1}}}},
    {'1', 0.30f, 2, {{'s', {0.26f, 0, 0.26f, 1}}, {'s', {0.26f, 0, 0, 0.2f}}}},
    {'2', 0.58f, 3, {{'a', {0.28f, 0.29f, 0.28f, 190, 380}}, {'s', {0.543f, 0.386f, 0, 1}}, {'s', {0, 1, 0.58f, 1}}}},
};
static const Letter *letter(char c) {
    for (size_t i = 0; i < sizeof kLetters / sizeof kLetters[0]; i++) if (kLetters[i].c == c) return &kLetters[i];
    return NULL;
}
/* Distance to the text drawn with letters h pixels high, centred on (cx, cy). */
static float text(float px, float py, const char *s, float cx, float cy, float h) {
    float gap = 0.2f * h, w = 0;
    for (const char *c = s; *c; c++) w += letter(*c)->width * h + (c[1] ? gap : 0);
    float x = cx - w / 2, y = cy - h / 2, d = 1e9f;
    for (const char *c = s; *c; c++) {
        const Letter *l = letter(*c);
        for (int k = 0; k < l->n; k++) {
            const float *v = l->s[k].v;
            d = fminf(d, l->s[k].kind == 's' ? seg(px, py, x + v[0] * h, y + v[1] * h, x + v[2] * h, y + v[3] * h)
                                              : arc(px, py, x + v[0] * h, y + v[1] * h, v[2] * h, v[3], v[4]));
        }
        x += l->width * h + gap;
    }
    return d;
}

// ---------- the glyphs ----------
enum { FACE_CIRCLE, FACE_CROSS, FACE_TRIANGLE, FACE_SQUARE, SHOULDER, OPTIONS, CREATE, DPAD };
typedef struct { uint32_t code; int kind; const char *label; int arms; } Icon;   /* arms: D-pad bits up 1, down 2, left 4, right 8 */
static const Icon kIcons[] = {
    {0xe040, FACE_CIRCLE, NULL, 0}, {0xe041, FACE_CROSS, NULL, 0}, {0xe042, FACE_TRIANGLE, NULL, 0}, {0xe043, FACE_SQUARE, NULL, 0},
    {0xe044, SHOULDER, "L1", 0}, {0xe045, SHOULDER, "R1", 0}, {0xe046, SHOULDER, "L2", 0}, {0xe047, SHOULDER, "R2", 0},
    {0xe04b, OPTIONS, NULL, 0}, {0xe04c, CREATE, NULL, 0},
    {0xe049, DPAD, NULL, 0}, {0xe060, DPAD, NULL, 1}, {0xe061, DPAD, NULL, 2}, {0xe062, DPAD, NULL, 4}, {0xe063, DPAD, NULL, 8},
    {0xe064, DPAD, NULL, 3}, {0xe065, DPAD, NULL, 12},
};

/* The symbol A, B, X and Y get: TOTK_BUTTON_ICONS=playstation:<A><B><X><Y>, each o (circle), x (cross), t (triangle) or
   s (square), from the player's button layout (the launcher); without it the default layout's, by position. */
static int face_kind(int k) {
    const char *e = getenv("TOTK_BUTTON_ICONS"), *m = e && strlen(e) == 16 && e[11] == ':' ? e + 12 : "oxts";
    switch (m[k]) { case 'x': return FACE_CROSS; case 't': return FACE_TRIANGLE; case 's': return FACE_SQUARE; default: return FACE_CIRCLE; }
}
/* The glyph's outline with the holes (its letter) filled: pixels the outside can't reach through clear ones become solid. */
static void fill_holes(uint8_t *a, int w, int h) {
    uint8_t *out = calloc((size_t)w * h, 1); int *queue = malloc(sizeof(int) * (size_t)w * h), head = 0, tail = 0;
    if (!out || !queue) { free(out); free(queue); return; }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && a[y * w + x] < 128) { out[y * w + x] = 1; queue[tail++] = y * w + x; }
    while (head < tail) {
        int i = queue[head++], x = i % w, y = i / w;
        const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
        for (int k = 0; k < 4; k++) {
            int nx = nb[k][0], ny = nb[k][1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || out[ny * w + nx] || a[ny * w + nx] >= 128) continue;
            out[ny * w + nx] = 1; queue[tail++] = ny * w + nx;
        }
    }
    for (int i = 0; i < w * h; i++) if (!out[i]) a[i] = 255;
    free(out); free(queue);
}
/* Redraws one cell (w x h, row 0 at the top) from the game's glyph there; 0 when the glyph is empty. */
static int draw(uint8_t *a, int w, int h, const Icon *icon) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        if (a[y * w + x] >= 128) { x0 = x < x0 ? x : x0; y0 = y < y0 ? y : y0; x1 = x > x1 ? x : x1; y1 = y > y1 ? y : y1; }
    if (x1 < 0) return 0;
    float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f, R = (x1 - x0 + 1) / 2.0f, H = (float)(y1 - y0 + 1);
    if (icon->kind != DPAD) fill_holes(a, w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float px = x + 0.5f, py = y + 0.5f, X = px - cx, Y = py - cy, t = 0.22f * R, cut = 0, value = 0;
        switch (icon->kind) {
        case FACE_CIRCLE: cut = cov(fabsf(hypotf(X, Y) - 0.42f * R) - t / 2); break;
        case FACE_CROSS: { float k = 0.37f * R; cut = cov(fminf(seg(X, Y, -k, -k, k, k), seg(X, Y, -k, k, k, -k)) - t / 2); break; }
        case FACE_TRIANGLE: cut = cov(fabsf(triangle(X, Y, 0, 0.08f * R, 0.52f * R, 0, -1)) - t / 2); break;
        case FACE_SQUARE: cut = cov(fabsf(box(X, Y, 0.37f * R, 0.37f * R, 0.04f * R)) - t / 2); break;
        case SHOULDER: { float th = 0.46f * H; cut = cov(text(px, py, icon->label, cx, cy, th) - 0.12f * th); break; }
        case OPTIONS: { float k = 0.4f * R, s = 0.28f * R, lt = 0.16f * R;
            cut = cov(fminf(fminf(seg(X, Y, -k, -s, k, -s), seg(X, Y, -k, 0, k, 0)), seg(X, Y, -k, s, k, s)) - lt / 2); break; }
        case CREATE: { float by = 0.34f * R, lt = 0.17f * R, d = 1e9f;
            for (int k = -1; k <= 1; k++) {
                float an = (-90 + 48 * k) * (float)M_PI / 180, ux = cosf(an), uy = sinf(an);
                d = fminf(d, seg(X, Y, 0.3f * R * ux, by + 0.3f * R * uy, 0.72f * R * ux, by + 0.72f * R * uy));
            }
            cut = cov(d - lt / 2); break; }
        case DPAD: { float P = fminf((float)(x1 - x0 + 1), H) / 2, lt = 0.13f * P;
            static const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            for (int k = 0; k < 4; k++) {
                float dx = dirs[k][0], dy = dirs[k][1], u = X * dx + Y * dy - 0.6f * P, v = X * dy - Y * dx;
                float arm = box(u, v, 0.4f * P, 0.33f * P, 0.1f * P), tri = triangle(X, Y, 0.58f * P * dx, 0.58f * P * dy, 0.22f * P, dx, dy);
                float c = icon->arms & 1 << k ? cov(arm)                                        /* pressed: solid */
                        : fmaxf(cov(fabsf(arm + lt / 2) - lt / 2), icon->arms ? 0 : cov(tri));  /* outline (+ arrow when none is) */
                value = fmaxf(value, c);
            }
            break; }
        }
        a[y * w + x] = (uint8_t)lrintf(icon->kind == DPAD ? 255 * value : a[y * w + x] * (1 - cut));
    }
    return 1;
}

// ---------- BC4 sheet ----------
/* Byte offset of byte x of row y of a Tegra block-linear image `pitch` bytes wide (block height bh GOBs). */
static size_t gob_at(size_t x, size_t y, size_t pitch, size_t bh) {
    size_t gobs_w = (pitch + 63) / 64;
    return (y / (8 * bh)) * 512 * bh * gobs_w + (x / 64) * 512 * bh + (y % (8 * bh) / 8) * 512 +
           ((x % 64) / 32) * 256 + ((y % 8) / 2) * 64 + ((x % 32) / 16) * 32 + (y % 2) * 16 + (x % 16);
}
/* The 8-byte BC4 block at column bx, row by of a sheet wb blocks wide. */
static size_t block_at(size_t bx, size_t by, size_t wb, size_t bh) { return gob_at(bx * 8, by, wb * 8, bh); }
static void bc4_palette(const uint8_t *b, int p[8]) {
    p[0] = b[0]; p[1] = b[1];
    if (b[0] > b[1]) for (int k = 2; k < 8; k++) p[k] = ((8 - k) * b[0] + (k - 1) * b[1] + 3) / 7;
    else { for (int k = 2; k < 6; k++) p[k] = ((6 - k) * b[0] + (k - 1) * b[1] + 2) / 5; p[6] = 0; p[7] = 255; }
}
static void bc4_decode(const uint8_t *b, uint8_t v[16]) {
    int p[8]; bc4_palette(b, p);
    uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) v[i] = (uint8_t)p[bits >> (3 * i) & 7];
}
static void bc4_encode(const uint8_t v[16], uint8_t *b) {
    uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) { lo = v[i] < lo ? v[i] : lo; hi = v[i] > hi ? v[i] : hi; }
    b[0] = hi; b[1] = lo;
    int p[8]; bc4_palette(b, p); uint64_t bits = 0;
    if (hi > lo) for (int i = 0; i < 16; i++) {
        int best = 0;
        for (int k = 1; k < 8; k++) if (abs(p[k] - v[i]) < abs(p[best] - v[i])) best = k;
        bits |= (uint64_t)best << (3 * i);
    }
    for (int i = 0; i < 6; i++) b[2 + i] = (uint8_t)(bits >> (8 * i));
}

int botw_button_icons_font(uint8_t *f, size_t n, char *error, size_t error_size) {
#define FAIL(...) do { snprintf(error, error_size, __VA_ARGS__); return -1; } while (0)
    if (n < 0x14 || memcmp(f, "FFNT", 4) || rd16(f + 4) != 0xfeff) FAIL("not a little-endian BFFNT");
    size_t at = rd16(f + 6), tglp = 0, cmaps[16]; int ncmap = 0;
    for (unsigned k = 0; k < rd16(f + 16) && at + 8 <= n; k++) {
        size_t size = rd32(f + at + 4);
        if (!memcmp(f + at, "TGLP", 4)) tglp = at;
        if (!memcmp(f + at, "CMAP", 4) && ncmap < 16) cmaps[ncmap++] = at;
        if (size < 8) break;
        at += size;
    }
    if (!tglp || tglp + 0x20 > n) FAIL("no TGLP block");
    int cw = f[tglp + 8], ch = f[tglp + 9], cols = (int)rd16(f + tglp + 20);
    unsigned w = rd16(f + tglp + 24), h = rd16(f + tglp + 26); size_t bntx = rd32(f + tglp + 28);
    if (f[tglp + 10] != 1 || !cw || !ch || !cols) FAIL("glyph sheets: %u (one expected)", f[tglp + 10]);
    if (bntx + 0x30 > n || memcmp(f + bntx, "BNTX", 4) || rd32(f + bntx + 0x24) != 1) FAIL("the sheet is not a single-texture BNTX");
    size_t brti = bntx + rd64(f + bntx + rd64(f + bntx + 0x28));
    if (brti + 0x78 > n || memcmp(f + brti, "BRTI", 4)) FAIL("no BRTI");
    if (rd32(f + brti + 0x1c) != 0x1d01 || rd16(f + brti + 0x16) != 1) FAIL("sheet format %#x (BC4, one level expected)", rd32(f + brti + 0x1c));
    if (rd32(f + brti + 0x24) != w || rd32(f + brti + 0x28) != h || w % 4 || h % 4) FAIL("sheet size");
    size_t bh = (size_t)1 << rd32(f + brti + 0x34), size = rd32(f + brti + 0x50), data = bntx + rd64(f + bntx + rd64(f + brti + 0x70));
    size_t wb = w / 4, hb = h / 4;
    if (data + size > n || block_at(wb - 1, hb - 1, wb, bh) + 8 > size) FAIL("sheet data out of bounds");
    uint8_t *sheet = f + data, *img = malloc((size_t)w * h), *cell = malloc((size_t)cw * ch);
    if (!img || !cell) { free(img); free(cell); FAIL("out of memory"); }
    for (size_t by = 0; by < hb; by++) for (size_t bx = 0; bx < wb; bx++) {   /* upright: stored row s is image row h - 1 - s */
        uint8_t v[16]; bc4_decode(sheet + block_at(bx, by, wb, bh), v);
        for (int i = 0; i < 16; i++) img[(h - 1 - (by * 4 + i / 4)) * w + bx * 4 + i % 4] = v[i];
    }
    uint8_t *before = malloc((size_t)w * h); int drawn = 0;
    if (!before) { free(img); free(cell); FAIL("out of memory"); }
    memcpy(before, img, (size_t)w * h);
    for (size_t i = 0; i < sizeof kIcons / sizeof kIcons[0]; i++) {
        long g = -1;
        for (int k = 0; k < ncmap && g < 0; k++) {
            const uint8_t *c = f + cmaps[k]; uint32_t lo = rd32(c + 8), hi = rd32(c + 12), code = kIcons[i].code;
            if (code < lo || code > hi) continue;
            if (rd16(c + 16) == 0) g = (long)rd16(c + 24) + code - lo;
            else if (rd16(c + 16) == 1 && cmaps[k] + 26 + 2 * (code - lo) <= n) { uint32_t v = rd16(c + 24 + 2 * (code - lo)); g = v == 0xffff ? -1 : (long)v; }
        }
        if (g < 0) continue;
        int cx0 = 1 + (int)(g % cols) * (cw + 1), cy0 = 1 + (int)(g / cols) * (ch + 1);
        if (cx0 + cw > (int)w || cy0 + ch > (int)h) continue;
        for (int y = 0; y < ch; y++) memcpy(cell + y * cw, img + (cy0 + y) * w + cx0, (size_t)cw);
        Icon icon = kIcons[i];
        if (icon.code <= 0xe043) icon.kind = face_kind((int)(icon.code - 0xe040));
        if (!draw(cell, cw, ch, &icon)) continue;
        for (int y = 0; y < ch; y++) memcpy(img + (cy0 + y) * w + cx0, cell + y * cw, (size_t)cw);
        drawn++;
    }
    for (size_t by = 0; by < hb; by++) for (size_t bx = 0; bx < wb; bx++) {
        uint8_t v[16]; int changed = 0;
        for (int i = 0; i < 16; i++) { size_t p = (h - 1 - (by * 4 + i / 4)) * w + bx * 4 + i % 4; v[i] = img[p]; changed |= img[p] != before[p]; }
        if (changed) bc4_encode(v, sheet + block_at(bx, by, wb, bh));
    }
    free(img); free(cell); free(before);
    if (!drawn) FAIL("none of the button glyphs found");
    return drawn;
#undef FAIL
}

// ---------- the action prompt's key pictures ----------
typedef struct { const char *name; int face; const char *label; } KeyPicture;   /* face: A B X Y = 0..3, -1 a shoulder */
static const KeyPicture kKeyPictures[] = {
    {"Nt_KeyTexA_00^d", 0, NULL}, {"Nt_KeyTexB_00^d", 1, NULL}, {"Nt_KeyTexX_00^d", 2, NULL}, {"Nt_KeyTexY_00^d", 3, NULL},
    {"Nt_KeyTexL_00^d", -1, "L1"}, {"Nt_KeyTexZL_00^d", -1, "L2"},
};
/* Redraws the key pictures among a BNTX's textures (one-channel R8, one level, block-linear, upright) in place: the number
   redrawn, or -1 with a reason. */
int botw_button_icons_layout_textures(uint8_t *b, size_t n, char *error, size_t error_size) {
#define FAIL(...) do { snprintf(error, error_size, __VA_ARGS__); return -1; } while (0)
    if (n < 0x30 || memcmp(b, "BNTX", 4) || memcmp(b + 0x20, "NX  ", 4)) FAIL("not a BNTX");
    size_t count = rd32(b + 0x24), info = rd64(b + 0x28); int drawn = 0;
    if (info + 8 * count > n) FAIL("BNTX texture table out of bounds");
    for (size_t i = 0; i < count; i++) {
        size_t t = rd64(b + info + 8 * i);
        if (t + 0x78 > n || memcmp(b + t, "BRTI", 4)) FAIL("no BRTI");
        size_t name = rd64(b + t + 0x60);
        if (name + 2 > n || name + 2 + rd16(b + name) > n) continue;
        const KeyPicture *k = NULL;
        for (size_t j = 0; j < sizeof kKeyPictures / sizeof kKeyPictures[0]; j++)
            if (rd16(b + name) == strlen(kKeyPictures[j].name) && !memcmp(b + name + 2, kKeyPictures[j].name, rd16(b + name))) k = &kKeyPictures[j];
        if (!k) continue;
        size_t w = rd32(b + t + 0x24), h = rd32(b + t + 0x28), bh = (size_t)1 << (rd32(b + t + 0x34) & 7), size = rd32(b + t + 0x50);
        if (rd32(b + t + 0x1c) != 0x0201 || rd16(b + t + 0x16) != 1 || rd16(b + t + 0x12) != 0 || !w || !h || w > 64 || h > 64) continue;
        while (bh > 1 && h <= bh / 2 * 8) bh /= 2;   /* the block height a level this short uses */
        size_t ptrs = rd64(b + t + 0x70), data = ptrs + 8 <= n ? rd64(b + ptrs) : n;
        if (data + size > n || gob_at(w - 1, h - 1, w, bh) >= size) continue;
        uint8_t img[64 * 64];
        for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) img[y * w + x] = b[data + gob_at(x, y, w, bh)];
        Icon icon = {0, k->face >= 0 ? face_kind(k->face) : SHOULDER, k->label, 0};
        if (!draw(img, (int)w, (int)h, &icon)) continue;
        for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) b[data + gob_at(x, y, w, bh)] = img[y * w + x];
        drawn++;
    }
    if (!drawn) FAIL("none of the key pictures found");
    return drawn;
#undef FAIL
}

// ---------- Yaz0 encoder and SARC member swap (Pack/Bootup.pack) ----------
static uint32_t hash3(const uint8_t *p) { return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u >> 16; }
/* The longest earlier match for position i (3..273 bytes, up to 4 KiB back) along the hash chain of the positions before i. */
static size_t yaz0_find(const uint8_t *s, size_t n, size_t i, const int32_t *head, const int32_t *prev, size_t *dist) {
    if (i + 3 > n) return 0;
    size_t best = 0, max = n - i < 0x111 ? n - i : 0x111;
    int32_t c = head[hash3(s + i)];
    for (int k = 0; k < 48 && c >= 0 && i - (size_t)c <= 0x1000; k++, c = prev[c & 0xfff]) {
        const uint8_t *a = s + c, *p = s + i;
        if (a[best] != p[best]) continue;
        size_t l = 0;
        while (l < max && a[l] == p[l]) l++;
        if (l > best) { best = l; *dist = i - (size_t)c; if (l == max) break; }
    }
    return best >= 3 ? best : 0;
}
/* Yaz0 of s with the 16-byte header of the file it replaces (its alignment word kept): greedy matching that waits one byte
   when a longer match starts there. */
static uint8_t *yaz0_encode(const uint8_t *s, size_t n, const uint8_t *header, size_t *out_size) {
    uint8_t *out = n <= INT32_MAX ? malloc(16 + n + n / 8 + 9) : NULL;
    int32_t *head = malloc(sizeof(int32_t) << 16), *prev = malloc(sizeof(int32_t) * 0x1000);
    if (!out || !head || !prev) { free(out); free(head); free(prev); return NULL; }
    memset(head, 0xff, sizeof(int32_t) << 16);
    memcpy(out, header, 16);
    out[4] = (uint8_t)(n >> 24); out[5] = (uint8_t)(n >> 16); out[6] = (uint8_t)(n >> 8); out[7] = (uint8_t)n;
    size_t i = 0, chained = 0, o = 16, flags = 0, next_at = SIZE_MAX, next_len = 0, next_dist = 0; int bits = 0;
    while (i < n) {
        for (; chained < i; chained++) if (chained + 3 <= n) { uint32_t h = hash3(s + chained); prev[chained & 0xfff] = head[h]; head[h] = (int32_t)chained; }
        size_t dist = 0, len = i == next_at ? (dist = next_dist, next_len) : yaz0_find(s, n, i, head, prev, &dist);
        if (len && len < 0x111 && i + 1 < n) {
            if (i + 3 <= n) { uint32_t h = hash3(s + i); prev[i & 0xfff] = head[h]; head[h] = (int32_t)i; }
            chained = i + 1; next_at = i + 1; next_len = yaz0_find(s, n, i + 1, head, prev, &next_dist);
            if (next_len > len) len = 0;
        }
        if (!bits) { flags = o++; out[flags] = 0; bits = 8; }
        bits--;
        if (!len) { out[flags] |= (uint8_t)(1 << bits); out[o++] = s[i++]; continue; }
        size_t back = dist - 1;
        if (len < 0x12) { out[o++] = (uint8_t)((len - 2) << 4 | back >> 8); out[o++] = (uint8_t)back; }
        else { out[o++] = (uint8_t)(back >> 8); out[o++] = (uint8_t)back; out[o++] = (uint8_t)(len - 0x12); }
        i += len;
    }
    free(head); free(prev);
    *out_size = o;
    return out;
}
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* The SARC with the member at member (old bytes) replaced by data. It stays where it is; when it outgrows the gap before the
   next member, everything after moves by whole 8 KiB, so each member keeps its alignment. */
static uint8_t *sarc_replace(const uint8_t *d, size_t n, size_t member, size_t old, const uint8_t *data, size_t dn, size_t *out_size) {
    size_t base = rd32(d + 0xc), sfat = rd16(d + 4), count = rd16(d + sfat + 6), nodes = sfat + rd16(d + sfat + 4), next = n, self = count;
    for (size_t k = 0; k < count; k++) {
        size_t start = base + rd32(d + nodes + 16 * k + 8), end = base + rd32(d + nodes + 16 * k + 12);
        if (start == member && end == member + old) self = k;
        else if (start >= member + old && start < next) next = start;
    }
    if (self == count || dn > UINT32_MAX - n) return NULL;
    size_t shift = member + dn > next ? (member + dn - next + 0x1fff) & ~(size_t)0x1fff : 0;
    uint8_t *out = calloc(n + shift, 1);
    if (!out) return NULL;
    memcpy(out, d, member); memcpy(out + member, data, dn); memcpy(out + next + shift, d + next, n - next);
    for (size_t k = 0; k < count; k++) {
        uint8_t *e = out + nodes + 16 * k;
        if (k == self) wr32(e + 12, (uint32_t)(member - base + dn));
        else if (base + rd32(e + 8) >= next) { wr32(e + 8, rd32(e + 8) + (uint32_t)shift); wr32(e + 12, rd32(e + 12) + (uint32_t)shift); }
    }
    wr32(out + 8, (uint32_t)(n + shift));
    *out_size = n + shift;
    return out;
}
/* The faded keys around the action prompt are text, not pictures: one-letter text panes (T_A_00, T_B_00, T_X_00, T_Y_00,
   T_A_01) of the blyt/PaDoCommand* layouts in the small text font, which also has ○ × △ □. The letter becomes the symbol of
   its button (same length, so in place), and the pane (centred on its position, not animated) is scaled so the symbols look
   alike: that font's × is 14 pixels to the △'s 20 x 18, and its □ looks smaller than the △ beside it (the owner,
   2026-10-28). The number changed. */
static int prompt_letters(uint8_t *d, size_t n) {
    if (n < 0x20 || memcmp(d, "SARC", 4)) return 0;
    size_t data = rd32(d + 0xc), sfat = rd16(d + 4), count = rd16(d + sfat + 6), nodes = sfat + rd16(d + sfat + 4);
    size_t sfnt = nodes + 16 * count, names = sfnt + 8; int changed = 0;
    for (size_t k = 0; k < count && names < n; k++) {
        const uint8_t *e = d + nodes + 16 * k;
        size_t at = names + (size_t)(rd32(e + 4) & 0xffffff) * 4, start = data + rd32(e + 8), end = data + rd32(e + 12);
        if (!(rd32(e + 4) >> 24) || at + 17 > n || memcmp(d + at, "blyt/PaDoCommand", 16) || start > end || end > n) continue;
        uint8_t *f = d + start; size_t fn = end - start;
        if (fn < 0x14 || memcmp(f, "FLYT", 4)) continue;
        size_t s = rd16(f + 6);
        for (unsigned i = 0; i < rd16(f + 0x10) && s + 0x84 <= fn; i++) {
            size_t size = rd32(f + s + 4);
            const char *pane = (const char *)f + s + 0x0c;
            if (!memcmp(f + s, "txt1", 4) && pane[0] == 'T' && pane[1] == '_' && pane[3] == '_' && pane[4] == '0' && pane[6] == 0) {
                const char *letters = "ABXY", *which = strchr(letters, pane[2]);
                size_t text = s + rd32(f + s + 0x64);
                if (which && *which && rd16(f + s + 0x56) >= 4 && text + 4 <= fn && rd16(f + text) == (uint32_t)pane[2] && !rd16(f + text + 2)) {
                    static const uint16_t symbol[] = {[FACE_CIRCLE] = 0x25cb, [FACE_CROSS] = 0x00d7, [FACE_TRIANGLE] = 0x25b3, [FACE_SQUARE] = 0x25a1};
                    static const float grow[] = {[FACE_CIRCLE] = 1, [FACE_CROSS] = 1.6f, [FACE_TRIANGLE] = 1, [FACE_SQUARE] = 1.05f};
                    int kind = face_kind((int)(which - letters));
                    f[text] = (uint8_t)symbol[kind]; f[text + 1] = (uint8_t)(symbol[kind] >> 8); changed++;
                    for (int axis = 0; axis < 2; axis++) {   /* the pane's scale, x and y */
                        float v; memcpy(&v, f + s + 0x44 + 4 * axis, 4); v *= grow[kind]; memcpy(f + s + 0x44 + 4 * axis, &v, 4);
                    }
                }
            }
            if (size < 8) break;
            s += size;
        }
    }
    return changed;
}
/* Pack/Bootup.pack (a SARC, not compressed) with the key pictures and letters of Layout/Common.sblarc redrawn. */
static uint8_t *bootup_pack(const uint8_t *data, size_t size, size_t *out_size, char *error, size_t error_size) {
    size_t zn = 0, n = 0, bn = 0, yn = 0; const uint8_t *z = botw_sarc_get(data, size, "Layout/Common.sblarc", &zn);
    if (!z) { snprintf(error, error_size, "no Layout/Common.sblarc"); return NULL; }
    uint8_t *layout = botw_yaz0_decode(z, zn, &n), *packed = NULL, *out = NULL;
    const uint8_t *bntx = layout ? botw_sarc_get(layout, n, "timg/__Combined.bntx", &bn) : NULL;
    if (!bntx) snprintf(error, error_size, "no timg/__Combined.bntx in Layout/Common.sblarc");
    else if (botw_button_icons_layout_textures(layout + (bntx - layout), bn, error, error_size) >= 0) {
        prompt_letters(layout, n);
        packed = yaz0_encode(layout, n, z, &yn);
        out = packed ? sarc_replace(data, size, (size_t)(z - data), zn, packed, yn, out_size) : NULL;
        if (!out) snprintf(error, error_size, "out of memory");
    }
    free(layout); free(packed);
    return out;
}

uint8_t *botw_button_icons_pack(const uint8_t *data, size_t size, size_t *out_size, char *error, size_t error_size) {
    if (size >= 4 && !memcmp(data, "SARC", 4)) return bootup_pack(data, size, out_size, error, error_size);
    size_t n = 0; uint8_t *sarc = botw_yaz0_decode(data, size, &n);
    if (!sarc) { snprintf(error, error_size, "not a Yaz0 file"); return NULL; }
    size_t fn = 0; const uint8_t *font = botw_sarc_get(sarc, n, "External_00.bffnt", &fn);
    if (!font) { snprintf(error, error_size, "no External_00.bffnt"); free(sarc); return NULL; }
    if (botw_button_icons_font(sarc + (font - sarc), fn, error, error_size) < 0) { free(sarc); return NULL; }
    /* Yaz0 with every byte a literal: a flag byte of 0xff before each 8 */
    uint8_t *out = malloc(16 + n + (n + 7) / 8);
    if (!out) { snprintf(error, error_size, "out of memory"); free(sarc); return NULL; }
    memcpy(out, data, 16);
    out[4] = (uint8_t)(n >> 24); out[5] = (uint8_t)(n >> 16); out[6] = (uint8_t)(n >> 8); out[7] = (uint8_t)n;
    size_t o = 16;
    for (size_t i = 0; i < n; i += 8) {
        size_t k = n - i < 8 ? n - i : 8;
        out[o++] = 0xff; memcpy(out + o, sarc + i, k); o += k;
    }
    free(sarc); *out_size = o;
    return out;
}
