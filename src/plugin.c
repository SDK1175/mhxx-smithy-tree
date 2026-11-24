// MHXX Smithy Tree: a Forge plugin for MHXX Recomp (the PC port of Monster Hunter XX). While the smithy's weapon list is
// open, a panel over the left of the screen shows the weapon under the cursor in its tree: its names by level, the
// weapon it is made from, the weapons made from it.
//
// Everything comes from the game the player has: the smithy's tables (nativeNX/table, read through the game's own file
// system, so a mod's copy counts) and the names the game shows (its loaded message resource - with the Korean patch,
// Hangul in the patch's codes, turned back into Hangul by the port's mhxx_hangul_decode). The plugin carries no data
// of the game. "mhxx" in its file name tells the port it is made for MHXX: its addresses are MHXX's.
//
// The tables (8 bytes of header - version, count - then the entries):
//   weaponNNLevelData.wNNd  15 bytes a level: +4 the weapon, +5 the level, +8 attack (u16), +14 which of the weapon's
//                           three names it has at that level
//   weaponCreateWNN.wcd     38 bytes a recipe: +0 the kind (1: the usual one), +2 the weapon, +4 the weapon it is made
//                           from (0: from materials alone)
// The names: the resource "table\weaponNNMsgData_jpn" (an rGUIMessage): +0x88 the strings' count, +0x90 their bytes,
// +0x94 the text, +0x98 the strings' pointers. Six strings a weapon from its number x 6: name, text, name 2, ...
// The smithy: its object's update (main+0x6e67d8), every frame it is open: +4 / +5 the screen, +0x5c the category (a
// weapon type + 7), +0x1f71 the cursor, +0x1f78 the rows, from +0x28d4 0x30 bytes a row: +0 flags (0x210: hidden),
// +2 the weapon, +9 its level - 1.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#define VERSION "0.2.0"

// ------------------------------------------------------------------ the little of libc it needs (no libc here; hidden:
// not offered to other plugins)
#pragma GCC visibility push(hidden)
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else while (n--) a[n] = b[n];
    return d;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }
#pragma GCC visibility pop
void init(void) {}  // (DT_INIT / DT_FINI: link/switch32.ld)
void fini(void) {}

// ------------------------------------------------------------------ Forge's API, the port's, the game's file system
typedef struct { uint16_t major, minor, patch; } ForgeVersion;
typedef struct { uint32_t w[19]; } ForgeHook;
void forge_log(int level, const char *fmt, ...);
const char *forge_fs_getRomFsMountPoint(void);
uint32_t forge_mem_getMainTextAddr(void);
ForgeHook forge_hook_create(void *target, void *detour, void **original);
uint32_t mhxx_resource_find(const char *path);
uint32_t mhxx_hangul_decode(const char *in, char *out, uint32_t size);

typedef struct { uint32_t h; } FileHandle;
uint32_t _ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci(FileHandle *h, const char *path, int mode);
uint32_t _ZN2nn2fs11GetFileSizeEPxNS0_10FileHandleE(int64_t *size, FileHandle h);
uint32_t _ZN2nn2fs8ReadFileENS0_10FileHandleExPvj(FileHandle h, int64_t offset, void *buf, uint32_t size);
void _ZN2nn2fs9CloseFileENS0_10FileHandleE(FileHandle h);

// ------------------------------------------------------------------ text
typedef struct { char *p; uint32_t n, cap; } Buf;
static void put(Buf *b, const char *s) {
    while (*s && b->n + 1 < b->cap) b->p[b->n++] = *s++;
    b->p[b->n] = 0;
}
static void put_int(Buf *b, int v) {
    char t[12];
    int k = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    do t[k++] = (char)('0' + u % 10), u /= 10; while (u);
    if (v < 0) t[k++] = '-';
    char r[13];
    for (int i = 0; i < k; i++) r[i] = t[k - 1 - i];
    r[k] = 0;
    put(b, r);
}
static void log_line(const char *s) { forge_log(1, "%s", s); }

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
#define G32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile const uint16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile const uint8_t *)(uintptr_t)(a))

// ------------------------------------------------------------------ the tables (one weapon type at a time)
#define TYPES 15
static uint8_t g_level[80 * 1024], g_create[8 * 1024];
static uint32_t g_nlevel, g_ncreate;
static int g_loaded = -1;  // the type in the buffers
static uint32_t g_names[TYPES];  // the message resources found (0: not yet)

static uint32_t read_table(const char *name, uint8_t *buf, uint32_t cap, uint32_t stride) {
    char path[160];
    Buf b = {path, 0, sizeof path};
    put(&b, forge_fs_getRomFsMountPoint()), put(&b, ":/nativeNX/table/"), put(&b, name);
    FileHandle f;
    if (_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci(&f, path, 1)) return 0;
    int64_t size = 0;
    uint32_t count = 0;
    if (!_ZN2nn2fs11GetFileSizeEPxNS0_10FileHandleE(&size, f) && size >= 8 && size <= cap &&
        !_ZN2nn2fs8ReadFileENS0_10FileHandleExPvj(f, 0, buf, (uint32_t)size)) {
        count = rd32(buf + 4);
        if (8 + (uint64_t)count * stride > (uint64_t)size) count = 0;
    }
    _ZN2nn2fs9CloseFileENS0_10FileHandleE(f);
    return count;
}

static bool load(int t) {
    if (g_loaded == t) return true;
    char n[48];
    Buf b = {n, 0, sizeof n};
    const char d0 = (char)('0' + t / 10), d1 = (char)('0' + t % 10);
    char tt[3] = {d0, d1, 0};
    put(&b, "weapon"), put(&b, tt), put(&b, "LevelData.w"), put(&b, tt), put(&b, "d");
    g_nlevel = read_table(n, g_level, sizeof g_level, 15);
    b.n = 0, put(&b, "weaponCreateW"), put(&b, tt), put(&b, ".wcd");
    g_ncreate = read_table(n, g_create, sizeof g_create, 38);
    g_loaded = g_nlevel && g_ncreate ? t : -1;
    if (g_loaded < 0) {
        char l[96];
        Buf lb = {l, 0, sizeof l};
        put(&lb, "[SmithyTree] the tables of weapon type "), put_int(&lb, t), put(&lb, " couldn't be read");
        log_line(l);
    }
    return g_loaded == t;
}

// A weapon's name (which: 0-2, its names by level), as the game shows it: decoded from the Korean patch's codes.
static void name_of(int t, uint32_t weapon, int which, char *out, uint32_t cap) {
    out[0] = 0;
    uint32_t r = g_names[t];
    if (!r) return;
    uint32_t n = G32(r + 0x88), size = G32(r + 0x90), text = G32(r + 0x94), arr = G32(r + 0x98);
    uint32_t i = weapon * 6 + (uint32_t)which * 2, s = 0;
    if (i >= n || n > 0x4000) return;
    if (arr && G32(arr) - text < size) s = G32(arr + 4 * i);
    else {  // (the strings one after another)
        s = text;
        for (uint32_t k = 0; k < i && s < text + size; k++) s += (uint32_t)strlen((const char *)(uintptr_t)s) + 1;
    }
    if (!s) return;
    if (!mhxx_hangul_decode((const char *)(uintptr_t)s, out, cap)) {
        Buf b = {out, 0, cap};
        put(&b, (const char *)(uintptr_t)s);
    }
    for (char *c = out; *c; c++)
        if (*c == '\n') *c = ' ';
}

}

// ------------------------------------------------------------------ the smithy
#define FN_SMITHY_UPDATE 0x6e67d8u
static ForgeHook g_hook;
static void (*g_original)(uint32_t);
static uint32_t g_frame, g_seen_frame;
static struct { uint8_t screen, sub; uint16_t category, weapon, level; } g_now;

static void smithy_update(uint32_t obj) {
    g_original(obj);
    if (obj < 0x08000000u || (obj & 3)) return;
    uint8_t cursor = G8(obj + 0x1f71);
    uint16_t rows = G16(obj + 0x1f78), weapon = 0, level = 0;
    if (cursor < rows && rows <= 64) {
        uint32_t row = obj + 0x28d4 + cursor * 0x30u;
        if (!(G16(row) & 0x210)) weapon = G16(row + 2), level = (uint16_t)(G8(row + 9) + 1);
    }
    g_now.screen = G8(obj + 4), g_now.sub = G8(obj + 5), g_now.category = G16(obj + 0x5c);
    g_now.weapon = weapon, g_now.level = level;
    g_seen_frame = g_frame;
}

// ------------------------------------------------------------------ the tree (one weapon type at a time)
// A row a weapon, in the order of the smithy's recipes: a line's first weapon, then what is made from it (depth first).
// Its node sits in the column of its rarity at level 1; a bar runs to the column of its rarity at its top level (MHXX
// weapons climb rarities as they level), ticks where it is renamed; a line from a weapon's bar to what is made from it.
#define ROWS 160
typedef struct {
    uint16_t weapon, depth;
    int16_t parent_row;
    uint8_t rare_low, rare_top, top, ntiers;
    uint8_t tier_rare[4];  // the rarity where each name starts
    int attack;
    char name[64];
} Row;
static Row g_rows[ROWS];
static int g_nrows, g_rows_type = -1;

static int rarity(const uint8_t *e) { return e[7] / 2 + 1; }  // (LevelData +7: (rarity - 1) x 2)

static void add_row(int t, uint32_t w, int depth, int parent, uint8_t *seen) {
    if (w >= 256 || seen[w] || g_nrows >= ROWS) return;
    seen[w] = 1;
    Row *r = &g_rows[g_nrows];
    int me = g_nrows++;
    memset(r, 0, sizeof *r);
    r->weapon = (uint16_t)w, r->depth = (uint16_t)depth, r->parent_row = (int16_t)parent;
    for (uint32_t i = 0; i < g_nlevel; i++) {
        const uint8_t *e = g_level + 8 + 15 * i;
        if (e[4] != w) continue;
        int k = e[14] > 3 ? 3 : e[14];
        if (!r->rare_low || e[5] == 1) r->rare_low = (uint8_t)rarity(e);
        if (e[5] >= r->top) r->top = e[5], r->rare_top = (uint8_t)rarity(e), r->attack = (int)rd16(e + 8);
        if (k + 1 > r->ntiers) r->ntiers = (uint8_t)(k + 1);
        if (!r->tier_rare[k] || rarity(e) < r->tier_rare[k]) r->tier_rare[k] = (uint8_t)rarity(e);
    }
    name_of(t, w, 0, r->name, sizeof r->name);
    for (uint32_t i = 0; i < g_ncreate; i++) {
        const uint8_t *e = g_create + 8 + 38 * i;
        if (rd16(e) == 1 && rd16(e + 4) == w && rd16(e + 2) != w) add_row(t, rd16(e + 2), depth + 1, me, seen);
    }
}

static bool build_rows(int t) {
    if (g_rows_type == t) return true;
    if (!load(t)) return false;
    if (!g_names[t]) {
        static uint32_t tried[TYPES];
        if (tried[t] && g_frame - tried[t] < 120) return false;  // (a heap search: not every frame)
        tried[t] = g_frame | 1;
        char path[40];
        Buf b = {path, 0, sizeof path};
        char tt[3] = {(char)('0' + t / 10), (char)('0' + t % 10), 0};
        put(&b, "table\\weapon"), put(&b, tt), put(&b, "MsgData_jpn");
        if (!(g_names[t] = mhxx_resource_find(path))) return false;
    }
    uint8_t seen[256] = {0};
    g_nrows = 0;
    for (uint32_t i = 0; i < g_ncreate; i++) {  // (the lines' first weapons, made from materials alone)
        const uint8_t *e = g_create + 8 + 38 * i;
        uint32_t from = rd16(e + 4), w = rd16(e + 2);
        if (rd16(e) == 1 && (!from || from == w)) add_row(t, w, 0, -1, seen);
    }
    for (uint32_t i = 0; i < g_ncreate; i++) {  // (made from a weapon no recipe makes: their own lines)
        const uint8_t *e = g_create + 8 + 38 * i;
        if (rd16(e) == 1) add_row(t, rd16(e + 2), 0, -1, seen);
    }
    g_rows_type = t;
    return true;
}

static const char *const TYPE_NAME[TYPES] = {"대검", "한손검", "해머", "랜스", "헤비보우건", "", "라이트보우건", "태도",
                                             "슬래시액스", "건랜스", "활", "쌍검", "수렵피리", "조충곤", "차지액스"};

// MH's rarity colours (as the game's icons: 1 white .. 10 purple)
static ImU32 rare_colour(int r) {
    static const ImU32 c[11] = {0xffd0d0d0, 0xffe8e8e8, 0xffc8a070, 0xff40c0a0, 0xff40a8f0, 0xffd080c0,
                                0xff3050e0, 0xff50c8e8, 0xff9090f8, 0xffe0e040, 0xfff060c0};
    return c[r < 0 ? 0 : r > 10 ? 10 : r];
}
#define V2(x, y) ((ImVec2){(x), (y)})

static void draw_tree(int sel_row) {
    ImGuiIO *io = igGetIO_Nil();
    float W = io->DisplaySize.x, H = io->DisplaySize.y, fs = igGetFontSize();
    ImDrawList *dl = igGetForegroundDrawList_Nil();
    float x0 = W * 0.265f, y0 = H * 0.145f, x1 = W * 0.985f, y1 = H * 0.805f;
    ImDrawList_AddRectFilled(dl, V2(x0, y0), V2(x1, y1), 0xe8181410, fs * 0.4f, 0);
    ImDrawList_AddRect(dl, V2(x0, y0), V2(x1, y1), 0xff4080c0, fs * 0.4f, 2.0f, 0);
    char line[128];
    Buf b = {line, 0, sizeof line};
    put(&b, TYPE_NAME[g_rows_type]), put(&b, " 트리");
    ImDrawList_AddText_Vec2(dl, V2(x0 + fs, y0 + fs * 0.5f), 0xff50d0ff, line, NULL);
    float names_w = (x1 - x0) * 0.30f, gx0 = x0 + names_w, gx1 = x1 - fs, top = y0 + fs * 2.2f;
    int cols = 10;
    float colw = (gx1 - gx0) / (float)cols, rowh = fs * 1.9f;
    for (int c = 0; c < cols; c++) {  // (the rarity columns)
        float cx = gx0 + colw * (float)c;
        ImDrawList_AddLine(dl, V2(cx, top), V2(cx, y1 - fs * 0.5f), 0x30ffffff, 1.0f);
        b.n = 0, put(&b, "R"), put_int(&b, c + 1);
        ImDrawList_AddText_Vec2(dl, V2(cx + colw * 0.35f, y0 + fs * 0.5f), 0xff808080, line, NULL);
    }
    int visible = (int)((y1 - top - fs * 0.5f) / rowh);
    static float scroll;  // (in rows: eased towards the selection)
    float want = (float)sel_row - (float)visible / 2.0f;
    if (want > (float)(g_nrows - visible)) want = (float)(g_nrows - visible);
    if (want < 0) want = 0;
    scroll += (want - scroll) * 0.25f;
    ImDrawList_PushClipRect(dl, V2(x0, top), V2(x1, y1 - fs * 0.3f), true);
    for (int i = 0; i < g_nrows; i++) {
        const Row *r = &g_rows[i];
        float y = top + ((float)i - scroll) * rowh, cy = y + rowh * 0.5f;
        if (y + rowh < top || y > y1) continue;
        bool sel = i == sel_row;
        if (sel) ImDrawList_AddRectFilled(dl, V2(x0 + 2, y), V2(x1 - 2, y + rowh), 0x40ffd060, 0, 0);
        ImDrawList_AddText_Vec2(dl, V2(x0 + fs * (0.8f + 0.9f * (float)r->depth), cy - fs * 0.5f),
                                sel ? 0xff70e8ff : 0xffe0e0e0, r->name, NULL);
        float nx = gx0 + colw * ((float)r->rare_low - 0.5f), ex = gx0 + colw * ((float)r->rare_top - 0.5f);
        ImDrawList_AddLine(dl, V2(nx, cy), V2(ex, cy), rare_colour(r->rare_top), fs * 0.25f);  // (its levels)
        for (int k = 1; k < r->ntiers; k++) {  // (renamed here)
            float tx = gx0 + colw * ((float)r->tier_rare[k] - 0.5f);
            ImDrawList_AddCircleFilled(dl, V2(tx, cy), fs * 0.22f, rare_colour(r->tier_rare[k]), 12);
        }
        float hs = fs * 0.55f;
        ImDrawList_AddRectFilled(dl, V2(nx - hs, cy - hs), V2(nx + hs, cy + hs), rare_colour(r->rare_low), fs * 0.15f, 0);
        if (sel) ImDrawList_AddRect(dl, V2(nx - hs - 3, cy - hs - 3), V2(nx + hs + 3, cy + hs + 3), 0xff40e0ff, fs * 0.2f, 2.5f, 0);
        b.n = 0, put(&b, "Lv"), put_int(&b, r->top), put(&b, "  "), put_int(&b, r->attack);
        ImDrawList_AddText_Vec2(dl, V2(ex + fs * 0.4f, cy - fs * 0.5f), 0xff909090, line, NULL);
        if (r->parent_row >= 0) {  // (made from the row above: an elbow from its bar's end)
            const Row *p = &g_rows[r->parent_row];
            float px = gx0 + colw * ((float)p->rare_top - 0.5f), py = top + ((float)r->parent_row - scroll) * rowh + rowh * 0.5f;
            ImDrawList_AddLine(dl, V2(px, py), V2(px, cy), 0xffb0b0b0, 1.5f);
            ImDrawList_AddLine(dl, V2(px, cy), V2(nx - hs, cy), 0xffb0b0b0, 1.5f);
        }
    }
    ImDrawList_PopClipRect(dl);
}

// ------------------------------------------------------------------ Forge's calls
void forge_onLoad(ForgeVersion *required) { required->major = 1, required->minor = 0, required->patch = 0; }

void forge_onInit(void) {
    void *target = (void *)(uintptr_t)(forge_mem_getMainTextAddr() + FN_SMITHY_UPDATE);
    if (G32((uintptr_t)target) != 0xe92d41f0u) {  // (not MHXX 1.5.1's smithy: leave the game alone)
        log_line("[SmithyTree] " VERSION ": the smithy's function is not the one this plugin knows (not MHXX 1.5.1?): off");
        return;
    }
    g_hook = forge_hook_create(target, (void *)smithy_update, (void **)&g_original);
    log_line("[SmithyTree] " VERSION " ready: the weapon tree shows over the smithy's weapon lists");
}

void forge_onUpdate(float dt) {
    (void)dt;
    g_frame++;
}

void forge_onImGuiFreeRender(void) {
    if (!g_original || g_frame - g_seen_frame > 2) return;  // (the smithy not open)
    int cat = g_now.category;
    if (cat < 7 || cat - 7 >= TYPES || cat - 7 == 5 || !g_now.weapon) return;
    if (!build_rows(cat - 7)) return;
    int sel = -1;
    for (int i = 0; i < g_nrows; i++)
        if (g_rows[i].weapon == g_now.weapon) sel = i;
    draw_tree(sel);
}
