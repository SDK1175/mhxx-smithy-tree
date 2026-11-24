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

#define VERSION "0.1.0"

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

static void top_level(uint32_t weapon, int *maxlv, int *attack) {
    *maxlv = 0, *attack = 0;
    for (uint32_t i = 0; i < g_nlevel; i++) {
        const uint8_t *e = g_level + 8 + 15 * i;
        if (e[4] == weapon && e[5] > *maxlv) *maxlv = e[5], *attack = (int)rd16(e + 8);
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

// ------------------------------------------------------------------ the panel
#define CHILDREN 8
static struct {
    int type;
    uint32_t weapon;
    int ntiers, low[3], top[3], attack[3];
    char tier[3][96];
    uint32_t parent;
    char parent_name[96];
    int nchildren, child_top[CHILDREN], child_attack[CHILDREN];
    char child[CHILDREN][96];
} g_view = {.type = -1};

static bool build_view(int t, uint32_t w) {
    if (g_view.type == t && g_view.weapon == w) return true;
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
    memset(&g_view, 0, sizeof g_view);
    g_view.type = t, g_view.weapon = w;
    for (uint32_t i = 0; i < g_nlevel; i++) {  // (its levels: a tier for each of its names)
        const uint8_t *e = g_level + 8 + 15 * i;
        int k = e[14];
        if (e[4] != w || k > 2) continue;
        if (!g_view.top[k] || e[5] < g_view.low[k]) g_view.low[k] = e[5];
        if (e[5] > g_view.top[k]) g_view.top[k] = e[5], g_view.attack[k] = (int)rd16(e + 8);
        if (k + 1 > g_view.ntiers) g_view.ntiers = k + 1;
    }
    for (int k = 0; k < g_view.ntiers; k++) name_of(t, w, k, g_view.tier[k], sizeof g_view.tier[k]);
    for (uint32_t i = 0; i < g_ncreate; i++) {  // (made from; made from it)
        const uint8_t *e = g_create + 8 + 38 * i;
        uint32_t made = rd16(e + 2), from = rd16(e + 4);
        if (made == w && from && from != w && !g_view.parent) {
            g_view.parent = from;
            name_of(t, from, 0, g_view.parent_name, sizeof g_view.parent_name);
        }
        if (from == w && made != w && g_view.nchildren < CHILDREN) {
            int k = g_view.nchildren++;
            top_level(made, &g_view.child_top[k], &g_view.child_attack[k]);
            name_of(t, made, 0, g_view.child[k], sizeof g_view.child[k]);
        }
    }
    return true;
}

static const char *const TYPE_NAME[TYPES] = {"대검", "한손검", "해머", "랜스", "헤비보우건", "", "라이트보우건", "태도",
                                             "슬래시액스", "건랜스", "활", "쌍검", "수렵피리", "조충곤", "차지액스"};

static void text(const char *s, ImVec4 c) {
    igPushStyleColor_Vec4(ImGuiCol_Text, c);
    igTextUnformatted(s, NULL);
    igPopStyleColor(1);
}

static void draw(void) {
    ImGuiIO *io = igGetIO_Nil();
    float w = io->DisplaySize.x, h = io->DisplaySize.y;
    igSetNextWindowPos((ImVec2){w * 0.015f, h * 0.17f}, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){w * 0.245f, 0}, ImGuiCond_Always);
    igSetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings;
    if (igBegin("##mhxx_smithy_tree", NULL, flags)) {
        const ImVec4 gold = {1.0f, 0.82f, 0.3f, 1}, dim = {0.65f, 0.65f, 0.65f, 1}, here = {1.0f, 0.95f, 0.55f, 1},
                     white = {1, 1, 1, 1};
        char line[256];
        Buf b = {line, 0, sizeof line};
        put(&b, TYPE_NAME[g_view.type]), put(&b, " 트리");
        text(line, gold);
        igSeparator();
        if (g_view.parent) {
            text(g_view.parent_name, dim);
            text("  -> 에서 파생", dim);
        }
        for (int k = 0; k < g_view.ntiers; k++) {
            bool on = (int)g_now.level >= g_view.low[k] && (int)g_now.level <= g_view.top[k];
            if (k) text("     |", dim);
            b.n = 0;
            put(&b, on ? "> Lv" : "  Lv"), put_int(&b, g_view.low[k]);
            if (g_view.top[k] != g_view.low[k]) put(&b, "-"), put_int(&b, g_view.top[k]);
            put(&b, "  "), put(&b, g_view.tier[k]);
            text(line, on ? here : white);
            igSameLine(0, -1);
            b.n = 0, put(&b, "  공격 "), put_int(&b, g_view.attack[k]);
            text(line, dim);
        }
        if (g_view.nchildren) {
            igSpacing();
            text("파생", gold);
            for (int k = 0; k < g_view.nchildren; k++) {
                b.n = 0, put(&b, "  - "), put(&b, g_view.child[k]);
                text(line, white);
                igSameLine(0, -1);
                b.n = 0, put(&b, "  Lv"), put_int(&b, g_view.child_top[k]), put(&b, ", 공격 "), put_int(&b, g_view.child_attack[k]);
                text(line, dim);
            }
        }
    }
    igEnd();
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
    if (!build_view(cat - 7, g_now.weapon)) return;
    draw();
}
