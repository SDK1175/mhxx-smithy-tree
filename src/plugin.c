// MHXX Smithy Tree: a Forge plugin for MHXX Recomp (the PC port of Monster Hunter XX). While the smithy is open, a
// World-style view over the right of the screen follows the smithy's own cursor:
//   weapons - the whole tree of the weapon type (a row a weapon, a column a derivation step, the selected weapon's path
//             lit) and a card: its stats at the level under the cursor, its names by level, and the materials to make
//             it, to derive it from its parent and to take it to the next level, each with what the player has.
//   armor   - a grid of the series (a row a series, a column a piece) and a card for the piece: defense, resistances,
//             slots, skills, and the materials with what the player has.
//
// Everything comes from the game the player has: the smithy's tables (nativeNX/table, read through the game's own file
// system, so a mod's copy counts), the names the game shows (its loaded message resources - with the Korean patch, Hangul
// in the patch's codes, turned back into Hangul by the port's mhxx_hangul_decode) and the item box (GU's sItemBox, where
// the port's mhxx_gu_data says it sits in MHXX). The plugin carries no data of the game. "mhxx" in its file name tells
// the port it is made for MHXX: its addresses are MHXX's.
//
// The tables (8 bytes of header - version, count - then the entries). Weapons, NN the type:
//   weaponNNBaseData.wNNd   a weapon (stride 33; 32 the bowguns, 34 SA GL DB IG CB): +0 its number, +0xb rarity - 1,
//                           +0xc the price to make it, +0x11 element (1 fire 2 water 3 thunder 4 dragon 5 ice 6 poison
//                           7 paralysis 8 sleep 9 blast), stride - 14 the list's order, the last 10 bytes: 5 x (the
//                           level, the weapon derived from it there)
//   weaponNNLevelData.wNNd  a level (15; 100 the bowguns, 31 the bow, 16 GL HH, 17 SA DB CB): +4 the weapon, +5 the level;
//                           blades: +6 the sharpness (kireajiData), +8 attack, +10 defense, +11 affinity (s8), +12
//                           element / 10, the last byte the slots; gunners: +6 attack
//   weaponCreateWNN.wcd     38 bytes a recipe: +0 the kind (1: the usual one), +2 the weapon, +6 4 x (item u16, count,
//                           key), +26 3 x (scraps given, u16, count u16)
//   weaponProcessWNN.wpd    32 bytes a level's upgrade: +0 the weapon, +2 the level it reaches (1: deriving it), +4 4 x
//                           (item u16, count), +0x14 3 x (scraps given, u16, count u16)
//   A weapon has three names: the first, the one from the level before its first G-rank level (the first that gives
//   G-rank scraps, item rarity 7) and the last level's. (Checked against the long swords' names by level.)
// Armor: armorSeriesData.asd 127 bytes a series: +0x17 5 resistances (s8), +0x1c 5 pieces x 5 x (skill u16, points
// s8), +0x67 rarity - 1, +0x68 the top level, +0x69 its defense growth (armorBuildData: 20 bytes a growth, an increase a
// level), +0x6a the defense at level 1, +0x6c the pieces' slots. armorCreateA0P.acrd (P the piece, head .. legs): 42
// bytes: +0 the series, +10 5 x (item u16, count, key), +30 the scraps. itemData.itm: 44 bytes an item, +5 its rarity.
// The names: the resources "table\weaponNNMsgData_jpn" (6 strings a weapon: name, text, x 3), "table\itemData_jpn" (2 an
// item), "table\armorSeriesData_jpn" (10 a series: the 5 pieces' names, then their texts), "table\skillTypeData_jpn" (2
// a skill). An rGUIMessage: +0x88 the strings' count, +0x90 their bytes, +0x94 the text, +0x98 the strings' pointers.
// The smithy: its object's update (main+0x6e67d8), every frame it is open: +4 / +5 the screen, +0x5c the category (a
// weapon type + 7), +0x1f71 the cursor, +0x1f78 the rows, from +0x28d4 0x30 bytes a row: +0 flags (0x210: hidden),
// +2 the weapon, +9 its level - 1.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#define VERSION "0.7.0"

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
uint32_t mhxx_gu_data(uint32_t gu_offset);

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
static void put_hex(Buf *b, uint32_t v) {
    char r[11] = "0x";
    for (int i = 0; i < 8; i++) r[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    r[10] = 0;
    put(b, r);
}
static void log_line(const char *s) { forge_log(1, "%s", s); }

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
#define G32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#define G16(a) (*(volatile const uint16_t *)(uintptr_t)(a))
#define G8(a) (*(volatile const uint8_t *)(uintptr_t)(a))

static uint32_t g_frame;

// ------------------------------------------------------------------ files
static uint32_t read_file(const char *name, uint8_t *buf, uint32_t cap, uint32_t stride) {
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
    if (!count) {
        char l[200];
        Buf lb = {l, 0, sizeof l};
        put(&lb, "[SmithyTree] couldn't read "), put(&lb, name);
        log_line(l);
    }
    return count;
}

// ------------------------------------------------------------------ names (the game's loaded message resources)
static uint32_t find_res(uint32_t *slot, uint32_t *tried, const char *path) {
    if (*slot) return *slot;
    if (*tried && g_frame - *tried < 180) return 0;  // (a heap search: not every frame)
    *tried = g_frame | 1;
    *slot = mhxx_resource_find(path);
    char l[120];
    Buf b = {l, 0, sizeof l};
    put(&b, "[SmithyTree] "), put(&b, path), put(&b, *slot ? ": found" : ": not loaded (yet)");
    log_line(l);
    return *slot;
}

static bool msg_get(uint32_t r, uint32_t i, char *out, uint32_t cap) {
    out[0] = 0;
    if (!r) return false;
    uint32_t n = G32(r + 0x88), size = G32(r + 0x90), text = G32(r + 0x94), arr = G32(r + 0x98), s = 0;
    if (i >= n || n > 0x10000) return false;
    if (arr && G32(arr) - text < size) s = G32(arr + 4 * i);
    else {  // (the strings one after another)
        s = text;
        for (uint32_t k = 0; k < i && s < text + size; k++) s += (uint32_t)strlen((const char *)(uintptr_t)s) + 1;
    }
    if (!s) return false;
    if (!mhxx_hangul_decode((const char *)(uintptr_t)s, out, cap)) {
        Buf b = {out, 0, cap};
        put(&b, (const char *)(uintptr_t)s);
    }
    for (char *c = out; *c; c++)
        if (*c == '\n' || *c == '\r') *c = ' ';
    return out[0] != 0;
}

#define TYPES 15
static uint32_t g_res_weapon[TYPES], g_try_weapon[TYPES], g_res_item, g_try_item, g_res_armor, g_try_armor,
    g_res_skill, g_try_skill;

static void item_name(uint32_t id, char *out, uint32_t cap) {
    if (!msg_get(find_res(&g_res_item, &g_try_item, "table\\itemData_jpn"), id * 2, out, cap)) {
        Buf b = {out, 0, cap};
        put(&b, "#"), put_int(&b, (int)id);
    }
}
static void skill_name(uint32_t id, char *out, uint32_t cap) {
    if (!msg_get(find_res(&g_res_skill, &g_try_skill, "table\\skillTypeData_jpn"), id * 2, out, cap)) {
        Buf b = {out, 0, cap};
        put(&b, "스킬 #"), put_int(&b, (int)id);
    }
}
static void armor_name(uint32_t series, int piece, char *out, uint32_t cap) {
    if (!msg_get(find_res(&g_res_armor, &g_try_armor, "table\\armorSeriesData_jpn"), series * 10 + (uint32_t)piece, out,
                 cap)) {
        Buf b = {out, 0, cap};
        put(&b, "#"), put_int(&b, (int)series);
    }
}
static uint32_t weapon_res(int t) {
    char path[40];
    Buf b = {path, 0, sizeof path};
    char tt[3] = {(char)('0' + t / 10), (char)('0' + t % 10), 0};
    put(&b, "table\\weapon"), put(&b, tt), put(&b, "MsgData_jpn");
    return find_res(&g_res_weapon[t], &g_try_weapon[t], path);
}
static void weapon_name(int t, uint32_t w, int which, char *out, uint32_t cap) {
    if (!msg_get(weapon_res(t), w * 6 + (uint32_t)which * 2, out, cap)) {
        Buf b = {out, 0, cap};
        put(&b, "#"), put_int(&b, (int)w);
    }
}

// ------------------------------------------------------------------ items: rarity (for the names' rule), the box
#define ITEMS 4096
static uint8_t g_item_rare[ITEMS];
static bool g_items_ok;
static uint8_t g_big[168 * 1024];  // (a file's bytes, while it is read: itemData, then armorSeriesData keeps it)

static void load_items(void) {
    static bool tried;
    if (tried) return;
    tried = true;
    uint32_t n = read_file("itemData.itm", g_big, sizeof g_big, 44);
    for (uint32_t i = 0; i < n && i < ITEMS; i++) g_item_rare[i] = g_big[8 + 44 * i + 5];
    g_items_ok = n > 0;
}

static uint16_t g_have[ITEMS];
static bool g_have_ok;
static uint32_t g_have_frame;
static bool ok_ptr(uint32_t p) { return p >= 0x08000000u && p < 0xa7ff0000u && !(p & 3); }

static bool box_slots(uint32_t p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = p + 0x1a + i * 4, id = G16(a), q = G16(a + 2);
        if (!id) continue;
        if (id >= ITEMS || q > 99) return false;
        g_have[id] = (uint16_t)(g_have[id] + q);
    }
    return true;
}
static void read_box(void) {
    if (g_have_frame && g_frame - g_have_frame < 30) return;
    g_have_frame = g_frame | 1;
    memset(g_have, 0, sizeof g_have);
    g_have_ok = false;
    uint32_t pb = mhxx_gu_data(0x1858438), pp = mhxx_gu_data(0x18584b8), ps = mhxx_gu_data(0x1897f78);
    if (!pb || !pp || !ps) return;
    uint32_t bx = G32(pb), pouch = G32(pp), save = G32(ps);
    if (!ok_ptr(bx) || !ok_ptr(pouch) || !ok_ptr(save)) return;
    uint32_t flags = G32(save + 0x958), exp = G32(save + 0x3614), n = 1000;
    n += 200 * (((flags >> 21) & 1) + ((flags >> 22) & 1));
    for (int k = 0; k < 3; k++) n += 300 * ((exp >> k) & 1);
    if (!box_slots(bx, n) || !box_slots(pouch, 24)) {
        memset(g_have, 0, sizeof g_have);
        return;
    }
    g_have_ok = true;
}

// ------------------------------------------------------------------ weapons: one type at a time
static const uint8_t BASE_STRIDE[TYPES] = {33, 33, 33, 33, 32, 0, 32, 33, 34, 34, 33, 34, 33, 34, 34};
static const uint8_t LEVEL_STRIDE[TYPES] = {15, 15, 15, 15, 100, 0, 100, 15, 17, 16, 31, 17, 16, 15, 17};
static bool gunner(int t) { return t == 4 || t == 6 || t == 10; }

static uint8_t g_base[8 * 1024], g_level[80 * 1024], g_create[8 * 1024], g_proc[36 * 1024], g_kire[4 * 1024];
static uint32_t g_nbase, g_nlevel, g_ncreate, g_nproc, g_nkire;
static int g_type = -1;

#define WMAX 256
typedef struct {
    int16_t parent, row, root;
    uint8_t from_lv, top, rare, s1, depth;  // s1: the level the second name starts (0: none)
    uint8_t start;                          // the tree's column of its level 1 (its parent's derivation level's next)
} Weapon;
static Weapon g_w[WMAX];
static uint16_t g_rows[WMAX];  // the tree's rows (weapons), depth first
static int g_nrows;

static const uint8_t *base_of(uint32_t w) {
    return w < g_nbase ? g_base + 8 + w * BASE_STRIDE[g_type] : NULL;
}
static const uint8_t *level_of(uint32_t w, uint32_t lv) {
    uint32_t s = LEVEL_STRIDE[g_type];
    for (uint32_t i = 0; i < g_nlevel; i++) {
        const uint8_t *e = g_level + 8 + i * s;
        if (e[4] == w && e[5] == lv) return e;
    }
    return NULL;
}
static const uint8_t *proc_of(uint32_t w, uint32_t lv) {
    for (uint32_t i = 0; i < g_nproc; i++) {
        const uint8_t *e = g_proc + 8 + i * 32;
        if (rd16(e) == w && rd16(e + 2) == lv) return e;
    }
    return NULL;
}
static const uint8_t *create_of(uint32_t w) {
    for (uint32_t i = 0; i < g_ncreate; i++) {
        const uint8_t *e = g_create + 8 + i * 38;
        if (rd16(e) == 1 && rd16(e + 2) == w) return e;
    }
    return NULL;
}
static int name_tier(uint32_t w, uint32_t lv) {
    const Weapon *x = &g_w[w];
    if (lv >= x->top && x->top > 1) return 2;
    return x->s1 && lv >= x->s1 ? 1 : 0;
}

static void add_rows(uint32_t w, int depth) {
    if (g_nrows >= WMAX || g_w[w].row >= 0) return;
    g_w[w].row = (int16_t)g_nrows, g_w[w].depth = (uint8_t)depth;
    int16_t pa = g_w[w].parent;
    g_w[w].root = depth && pa >= 0 ? g_w[pa].root : (int16_t)w;
    g_w[w].start = depth && pa >= 0 ? (uint8_t)(g_w[pa].start + (g_w[w].from_lv ? g_w[w].from_lv : g_w[pa].top)) : 0;
    g_rows[g_nrows++] = (uint16_t)w;
    const uint8_t *b = base_of(w);
    uint32_t s = BASE_STRIDE[g_type];
    for (int k = 0; k < 5; k++) {
        uint32_t child = b[s - 10 + 2 * k + 1];
        if (child && child < g_nbase && g_w[child].parent == (int16_t)w) add_rows(child, depth + 1);
    }
}

static bool load_type(int t) {
    if (g_type == t) return true;
    if (t < 0 || t >= TYPES || !BASE_STRIDE[t]) return false;
    static int failed = -1;
    static uint32_t failed_frame;
    if (failed == t && g_frame - failed_frame < 300) return false;
    load_items();
    if (!g_nkire) g_nkire = read_file("kireajiData.kad", g_kire, sizeof g_kire, 14);
    char n[48];
    Buf b = {n, 0, sizeof n};
    char tt[3] = {(char)('0' + t / 10), (char)('0' + t % 10), 0};
    g_type = t;
    put(&b, "weapon"), put(&b, tt), put(&b, "BaseData.w"), put(&b, tt), put(&b, "d");
    g_nbase = read_file(n, g_base, sizeof g_base, BASE_STRIDE[t]);
    b.n = 0, put(&b, "weapon"), put(&b, tt), put(&b, "LevelData.w"), put(&b, tt), put(&b, "d");
    g_nlevel = read_file(n, g_level, sizeof g_level, LEVEL_STRIDE[t]);
    b.n = 0, put(&b, "weaponCreateW"), put(&b, tt), put(&b, ".wcd");
    g_ncreate = read_file(n, g_create, sizeof g_create, 38);
    b.n = 0, put(&b, "weaponProcessW"), put(&b, tt), put(&b, ".wpd");
    g_nproc = read_file(n, g_proc, sizeof g_proc, 32);
    if (!g_nbase || !g_nlevel || g_nbase > WMAX) {
        g_type = -1, failed = t, failed_frame = g_frame;
        return false;
    }
    uint32_t s = BASE_STRIDE[t], ls = LEVEL_STRIDE[t];
    for (uint32_t w = 0; w < WMAX; w++) g_w[w] = (Weapon){-1, -1, -1, 0, 0, 0, 0, 0, 0};
    for (uint32_t w = 0; w < g_nbase; w++) {  // (who derives from whom, at which level)
        const uint8_t *e = base_of(w);
        g_w[w].rare = (uint8_t)(e[0xb] + 1);
        for (int k = 0; k < 5; k++) {
            uint32_t lv = e[s - 10 + 2 * k], child = e[s - 10 + 2 * k + 1];
            if (child && child < g_nbase && child != w && g_w[child].parent < 0)
                g_w[child].parent = (int16_t)w, g_w[child].from_lv = (uint8_t)lv;
        }
    }
    for (uint32_t i = 0; i < g_nlevel; i++) {
        const uint8_t *e = g_level + 8 + i * ls;
        if (e[4] < g_nbase && e[5] > g_w[e[4]].top) g_w[e[4]].top = e[5];
    }
    for (uint32_t w = 1; w < g_nbase; w++) {  // (the second name: from the level before the first G-rank level)
        Weapon *x = &g_w[w];
        if (x->top < 3) continue;
        int g = 0;
        for (uint32_t lv = 2; lv <= x->top && !g; lv++) {
            const uint8_t *p = proc_of(w, lv);
            if (!p) continue;
            for (int k = 0; k < 3; k++) {
                uint32_t it = rd16(p + 0x14 + 4 * k);
                if (it && it < ITEMS && g_item_rare[it] >= 7) g = (int)lv;
            }
        }
        for (uint32_t lv = 2; lv <= x->top && !g; lv++) {  // (no scraps tell: the first level asking G-rank materials)
            const uint8_t *p = proc_of(w, lv);
            if (!p) continue;
            for (int k = 0; k < 4; k++) {
                uint32_t it = rd16(p + 4 + 3 * k);
                if (it && it < ITEMS && g_item_rare[it] >= 7) g = (int)lv;
            }
        }
        int s1 = g ? g - 1 : x->top - 1;
        if (s1 < 2) s1 = 2;
        if (s1 > x->top - 1) s1 = x->top - 1;
        x->s1 = (uint8_t)s1;
    }
    // the rows: the lines' first weapons in the list's order, each followed by what derives from it
    g_nrows = 0;
    for (int order = 0; order < 256; order++)
        for (uint32_t w = 1; w < g_nbase; w++)
            if (g_w[w].parent < 0 && g_w[w].top && base_of(w)[s - 14] == order) add_rows(w, 0);
    for (uint32_t w = 1; w < g_nbase; w++)
        if (g_w[w].top && g_w[w].row < 0) add_rows(w, 0);
    char l[160];
    Buf lb = {l, 0, sizeof l};
    put(&lb, "[SmithyTree] weapon type "), put_int(&lb, t), put(&lb, ": "), put_int(&lb, (int)g_nbase),
        put(&lb, " weapons, "), put_int(&lb, g_nrows), put(&lb, " in the tree, "), put_int(&lb, (int)g_nlevel),
        put(&lb, " levels, "), put_int(&lb, (int)g_ncreate), put(&lb, " recipes, "), put_int(&lb, (int)g_nproc),
        put(&lb, " upgrades");
    log_line(l);
    return true;
}

// ------------------------------------------------------------------ armor
#define AMAX 1400
static uint8_t g_abuild[1024];
static uint8_t g_acr[5][24 * 1024];
static uint32_t g_naser, g_nabuild, g_nacr[5];
static int16_t g_acr_index[5][AMAX];  // series -> its entry in the piece's create file, -1: none
static uint16_t g_arows[AMAX];        // the series made at the smithy, in order
static int g_narows;
static bool g_armor_ok;

static bool load_armor(void) {
    static bool tried;
    if (tried) return g_armor_ok;
    tried = true;
    load_items();
    g_naser = read_file("armorSeriesData.asd", g_big, sizeof g_big, 127);
    g_nabuild = read_file("armorBuildData.abd", g_abuild, sizeof g_abuild, 20);
    if (!g_naser || g_naser > AMAX) return false;
    for (int p = 0; p < 5; p++) {
        char n[24] = "armorCreateA00.acrd";
        n[13] = (char)('0' + p);
        g_nacr[p] = read_file(n, g_acr[p], sizeof g_acr[p], 42);
        for (int i = 0; i < AMAX; i++) g_acr_index[p][i] = -1;
        for (uint32_t i = 0; i < g_nacr[p]; i++) {
            uint32_t s = rd16(g_acr[p] + 8 + i * 42);
            if (s < AMAX) g_acr_index[p][s] = (int16_t)i;
        }
    }
    g_narows = 0;
    for (uint32_t s = 1; s < g_naser; s++) {
        bool any = false;
        for (int p = 0; p < 5; p++) any |= g_acr_index[p][s] >= 0;
        if (any) g_arows[g_narows++] = (uint16_t)s;
    }
    g_armor_ok = true;
    char l[120];
    Buf lb = {l, 0, sizeof l};
    put(&lb, "[SmithyTree] armor: "), put_int(&lb, (int)g_naser), put(&lb, " series, "), put_int(&lb, g_narows),
        put(&lb, " made at the smithy");
    log_line(l);
    return true;
}
static const uint8_t *aser(uint32_t s) { return s < g_naser ? g_big + 8 + s * 127 : NULL; }
static const uint8_t *acreate(uint32_t s, int p) {
    if (s >= AMAX || p < 0 || p > 4 || g_acr_index[p][s] < 0) return NULL;
    return g_acr[p] + 8 + g_acr_index[p][s] * 42;
}

// ------------------------------------------------------------------ the smithy
#define FN_SMITHY_UPDATE 0x6e67d8u
static ForgeHook g_hook;
static void (*g_original)(uint32_t);
static uint32_t g_seen_frame;
static struct { uint8_t screen, sub; uint16_t category, id, level, rows, cursor; } g_now;

static int g_want_id = -1;  // (the game's list cursor to this weapon, if the list has it: its 3D preview follows)
static int g_want_result;   // 1 done, -1 not in the list
static void smithy_update(uint32_t obj) {
    g_original(obj);
    if (obj < 0x08000000u || (obj & 3)) return;
    if (g_want_id >= 0) {
        uint16_t n = G16(obj + 0x1f78);
        g_want_result = -1;
        for (uint32_t i = 0; i < n && n <= 64; i++) {
            uint32_t row = obj + 0x28d4 + i * 0x30u;
            if (!(G16(row) & 0x210) && G16(row + 2) == g_want_id) {
                if (G8(obj + 0x1f71) != i) *(volatile uint8_t *)(uintptr_t)(obj + 0x1f71) = (uint8_t)i;
                g_want_result = 1;
                break;
            }
        }
        static int logged = 99;
        if (g_want_result != logged) {
            logged = g_want_result;
            char l[96];
            Buf b = {l, 0, sizeof l};
            put(&b, "[SmithyTree] the game's list cursor to weapon "), put_int(&b, g_want_id),
                put(&b, g_want_result > 0 ? ": set (its 3D preview should follow)" : ": not in the list");
            log_line(l);
        }
        g_want_id = -1;
    }
    uint8_t cursor = G8(obj + 0x1f71);
    uint16_t rows = G16(obj + 0x1f78), id = 0, level = 0;
    if (cursor < rows && rows <= 64) {
        uint32_t row = obj + 0x28d4 + cursor * 0x30u;
        if (!(G16(row) & 0x210)) id = G16(row + 2), level = (uint16_t)(G8(row + 9) + 1);
    }
    uint8_t screen = G8(obj + 4), sub = G8(obj + 5);
    uint16_t cat = G16(obj + 0x5c);
    if (screen != g_now.screen || sub != g_now.sub || cat != g_now.category || rows != g_now.rows) {
        // (what the screen is: one line a change, for the log - the armor lists' categories, their rows' numbers)
        char l[200];
        Buf b = {l, 0, sizeof l};
        put(&b, "[SmithyTree] smithy screen "), put_int(&b, screen), put(&b, "/"), put_int(&b, sub),
            put(&b, " category "), put_int(&b, cat), put(&b, " rows "), put_int(&b, rows), put(&b, ":");
        for (uint32_t i = 0; i < rows && i < 6 && rows <= 64; i++) {
            uint32_t row = obj + 0x28d4 + i * 0x30u;
            put(&b, " "), put_int(&b, G16(row + 2)), put(&b, "/"), put_int(&b, G8(row + 9) + 1), put(&b, "("),
                put_hex(&b, G16(row)), put(&b, ")");
        }
        log_line(l);
    }
    g_now.screen = screen, g_now.sub = sub, g_now.category = cat;
    g_now.id = id, g_now.level = level, g_now.rows = rows, g_now.cursor = cursor;
    g_seen_frame = g_frame;
}

// ------------------------------------------------------------------ drawing
#define V2(x, y) ((ImVec2){(x), (y)})
static ImDrawList *g_dl;
static float g_fs;

static ImU32 rare_colour(int r) {  // (MH's rarity colours, as the game's icons)
    static const ImU32 c[12] = {0xffd0d0d0, 0xffe8e8e8, 0xffc8a070, 0xff40c0a0, 0xff40a8f0, 0xffd080c0,
                                0xff3050e0, 0xff50c8e8, 0xff9090f8, 0xffe0e040, 0xfff060c0, 0xff40d0ff};
    return c[r < 0 ? 0 : r > 11 ? 11 : r];
}
static void text(float x, float y, ImU32 col, const char *s) { ImDrawList_AddText_Vec2(g_dl, V2(x, y), col, s, NULL); }
static void text_s(float x, float y, float scale, ImU32 col, const char *s) {
    ImDrawList_AddText_FontPtr(g_dl, igGetFont(), g_fs * scale, V2(x, y), col, s, NULL, 0.0f, NULL);
}
static float text_w(const char *s) { return igCalcTextSize(s, NULL, false, -1.0f).x; }
static void panel(float x0, float y0, float x1, float y1) {  // (MHXX's own frames: navy, a teal edge, gold corners)
    float r = g_fs * 0.25f, c = g_fs * 0.7f;
    ImDrawList_AddRectFilledMultiColor(g_dl, V2(x0, y0), V2(x1, y1), 0xff2e2214, 0xff2e2214, 0xff1a130b, 0xff1a130b);
    ImDrawList_AddRect(g_dl, V2(x0, y0), V2(x1, y1), 0xffa89040, r, 2.0f, 0);
    ImDrawList_AddRect(g_dl, V2(x0 + 4, y0 + 4), V2(x1 - 4, y1 - 4), 0x80a89040, r, 1.0f, 0);
    ImU32 g = 0xff58b8d8;
    ImDrawList_AddLine(g_dl, V2(x0 - 1, y0 + c), V2(x0 - 1, y0 - 1), g, 3.0f), ImDrawList_AddLine(g_dl, V2(x0 - 1, y0 - 1), V2(x0 + c, y0 - 1), g, 3.0f);
    ImDrawList_AddLine(g_dl, V2(x1 + 1, y0 + c), V2(x1 + 1, y0 - 1), g, 3.0f), ImDrawList_AddLine(g_dl, V2(x1 + 1, y0 - 1), V2(x1 - c, y0 - 1), g, 3.0f);
    ImDrawList_AddLine(g_dl, V2(x0 - 1, y1 - c), V2(x0 - 1, y1 + 1), g, 3.0f), ImDrawList_AddLine(g_dl, V2(x0 - 1, y1 + 1), V2(x0 + c, y1 + 1), g, 3.0f);
    ImDrawList_AddLine(g_dl, V2(x1 + 1, y1 - c), V2(x1 + 1, y1 + 1), g, 3.0f), ImDrawList_AddLine(g_dl, V2(x1 + 1, y1 + 1), V2(x1 - c, y1 + 1), g, 3.0f);
}
static void rule(float x0, float x1, float y) { ImDrawList_AddLine(g_dl, V2(x0, y), V2(x1, y), 0x40ffffff, 1.0f); }

__attribute__((unused)) static const char *const TYPE_NAME[TYPES] = {"대검", "한손검", "해머", "랜스", "헤비보우건", "", "라이트보우건", "태도",
                                             "슬래시액스", "건랜스", "활", "쌍검", "수렵피리", "조충곤", "차지액스"};
static const char *const ELEMENT[10] = {"", "불", "물", "번개", "용", "얼음", "독", "마비", "수면", "폭파"};
static const ImU32 ELEMENT_COL[10] = {0xffffffff, 0xff3c6cff, 0xffffa040, 0xff30e0ff, 0xffb050c0,
                                      0xfff0e0a0, 0xffc050b0, 0xff20d0e0, 0xffd0a060, 0xff4090ff};
static const ImU32 SHARP_COL[7] = {0xff2828d8, 0xff2080f0, 0xff20e0f0, 0xff30d050, 0xfff08030, 0xfff8f8f8, 0xffd040b0};
static const char *const PIECE[5] = {"머리", "몸통", "팔", "허리", "다리"};

// A list of materials, each with what the player has: "name   x2   = 5". Returns the y after it.
static float materials(float x, float x1, float y, const char *title, const uint8_t *e, int n, int stride, int has_key) {
    char line[160], nm[96];
    Buf b = {line, 0, sizeof line};
    if (title) {
        ImDrawList_AddRectFilled(g_dl, V2(x - g_fs * 0.4f, y), V2(x1 + g_fs * 0.4f, y + g_fs * 1.25f), 0xff2a2620, 0, 0);
        text(x, y + g_fs * 0.12f, 0xffe0e0e0, title);
        y += g_fs * 1.5f;
    }
    int shown = 0;
    for (int k = 0; k < n; k++) {
        const uint8_t *m = e + k * stride;
        uint32_t it = rd16(m), q = m[2];
        if (!it || !q) continue;
        shown++;
        float h = g_fs * 2.15f;
        ImDrawList_AddRectFilled(g_dl, V2(x - g_fs * 0.4f, y), V2(x1 + g_fs * 0.4f, y + h - 3), 0xc0201c18, g_fs * 0.15f, 0);
        int r = it < ITEMS ? g_item_rare[it] : 1;
        ImDrawList_AddCircleFilled(g_dl, V2(x + g_fs * 0.45f, y + h * 0.5f), g_fs * 0.42f, rare_colour(r), 12);
        item_name(it, nm, sizeof nm);
        b.n = 0;
        if (has_key && m[3]) put(&b, "* ");
        put(&b, nm);
        text_s(x + g_fs * 1.3f, y + g_fs * 0.08f, 0.95f, 0xffe8e8e8, line);
        b.n = 0, put(&b, "x"), put_int(&b, (int)q);
        text_s(x + g_fs * 1.3f, y + g_fs * 1.05f, 0.9f, 0xffc8c8c8, line);
        if (g_have_ok) {
            uint32_t have = g_have[it];
            b.n = 0, put(&b, "보유 "), put_int(&b, (int)have);
            text_s(x1 - text_w(line) * 0.9f, y + g_fs * 1.05f, 0.9f, have >= q ? 0xff60e060 : 0xff6060f0, line);
        }
        y += h;
    }
    if (!shown) text_s(x, y, 0.92f, 0xff909090, "(필요 소재 없음)"), y += g_fs * 1.2f;
    return y + g_fs * 0.3f;
}

static void sharpness_bar(float x, float y, float w, float h, uint32_t id) {
    if (id >= g_nkire) return;
    const uint8_t *k = g_kire + 8 + id * 14;
    uint32_t prev = 0;
    ImDrawList_AddRectFilled(g_dl, V2(x - 1, y - 1), V2(x + w + 1, y + h + 1), 0xff000000, 0, 0);
    for (int c = 0; c < 7; c++) {
        uint32_t v = rd16(k + 2 * c);
        if (v > 450) v = 450;
        if (v > prev)
            ImDrawList_AddRectFilled(g_dl, V2(x + w * (float)prev / 450.0f, y), V2(x + w * (float)v / 450.0f, y + h),
                                     SHARP_COL[c], 0, 0);
        if (v > prev) prev = v;
    }
    float m = x + w * 400.0f / 450.0f;  // (what the Handicraft skill adds: the last 50)
    ImDrawList_AddLine(g_dl, V2(m, y - 2), V2(m, y + h + 2), 0xc0ffffff, 1.0f);
}

static void slots_text(Buf *b, int n) {
    for (int i = 0; i < 3; i++) put(b, i < n ? "O" : "-");
}

// ---- weapons: the cursor (a weapon and a level), the tree, the materials, the details
static int g_cw = -1, g_clv = 1;  // the tree's cursor
static uint16_t g_game_id, g_game_lv;
static bool g_hidden;

static int col_of(int w, int lv) { return g_w[w].start + lv - 1; }

static int g_view = -1;  // the weapon type on screen (L / R change it; the game's category sets it)
static uint16_t g_game_cat;
static void cursor_from_game(void) {
    if (g_now.category != g_game_cat || g_now.id != g_game_id || g_now.level != g_game_lv || g_view < 0) {
        g_game_cat = g_now.category, g_game_id = g_now.id, g_game_lv = g_now.level;
        g_view = g_now.category - 7, g_cw = -1;
    }
}

void mhxx_input_capture(uint32_t ms);
bool forge_input_isDown(uint32_t button);
bool forge_input_isPressed(uint32_t button);
// a direction from the d-pad or either stick: 1 right 2 left 3 up 4 down, repeating while held
static int pad_dir(void) {
    static int held, prev;
    int dir = 0;
    if (forge_input_isDown(14) || forge_input_isDown(18) || forge_input_isDown(22)) dir = 1;
    else if (forge_input_isDown(12) || forge_input_isDown(16) || forge_input_isDown(20)) dir = 2;
    else if (forge_input_isDown(13) || forge_input_isDown(17) || forge_input_isDown(21)) dir = 3;
    else if (forge_input_isDown(15) || forge_input_isDown(19) || forge_input_isDown(23)) dir = 4;
    if (!dir || dir != prev) held = 0;
    prev = dir;
    if (!dir) return 0;
    bool go = held == 0 || (held >= 14 && ((held - 14) & 3) == 0);
    held++;
    return go ? dir : 0;
}

static void cursor_move(int dir) {
    if (!dir || g_cw < 0) return;
    const Weapon *x = &g_w[g_cw];
    if (dir == 1) {  // right: the next level, past the last a weapon derived there
        if (g_clv < x->top) g_clv++;
        else
            for (int i = x->row + 1; i < g_nrows; i++)
                if (g_w[g_rows[i]].parent == g_cw) { g_cw = g_rows[i], g_clv = 1; break; }
    } else if (dir == 2) {  // left: the level before, from level 1 the parent where it derives
        if (g_clv > 1) g_clv--;
        else if (x->parent >= 0) g_clv = x->from_lv ? x->from_lv : g_w[x->parent].top, g_cw = x->parent;
    } else {  // up / down: the row above or below, the same column
        int r = x->row + (dir == 3 ? -1 : 1);
        if (r < 0 || r >= g_nrows) return;
        int c = col_of(g_cw, g_clv), w = g_rows[r], lv = c - g_w[w].start + 1;
        g_cw = w, g_clv = lv < 1 ? 1 : lv > g_w[w].top ? g_w[w].top : lv;
    }
}

static char g_toast[160];
static uint32_t g_toast_frame;
static void toast(const char *s) {
    Buf b = {g_toast, 0, sizeof g_toast};
    put(&b, s);
    g_toast_frame = g_frame;
}

static void node(float cx, float cy, float hs, int rare, bool lit, bool sel, bool renamed, bool last) {
    ImU32 rc = rare_colour(rare);
    ImDrawList_AddRectFilled(g_dl, V2(cx - hs, cy - hs), V2(cx + hs, cy + hs), lit ? 0xff3a3020 : 0xff1e1c1a, g_fs * 0.15f, 0);
    ImDrawList_AddRect(g_dl, V2(cx - hs, cy - hs), V2(cx + hs, cy + hs), lit ? 0xffa0a0a0 : 0xff505050, g_fs * 0.15f,
                       1.2f, 0);
    float k = hs * 0.55f;  // (a blade: the rarity's colour)
    ImDrawList_AddLine(g_dl, V2(cx - k, cy + k), V2(cx + k, cy - k), rc, g_fs * 0.2f);
    ImDrawList_AddLine(g_dl, V2(cx - k, cy + k), V2(cx - k * 0.45f, cy + k * 0.45f), 0xffe0e0e0, g_fs * 0.22f);
    if (renamed) {  // (a new name from here: World's badge)
        float b = hs * 0.42f;
        ImDrawList_AddRectFilled(g_dl, V2(cx - hs - b * 0.3f, cy - hs - b * 0.3f), V2(cx - hs + b, cy - hs + b), 0xffc8b090,
                                 0, 0);
    }
    if (last) ImDrawList_AddRect(g_dl, V2(cx - hs - 2, cy - hs - 2), V2(cx + hs + 2, cy + hs + 2), rc, g_fs * 0.18f, 1.5f, 0);
    if (sel) {
        ImDrawList_AddRect(g_dl, V2(cx - hs - 4, cy - hs - 4), V2(cx + hs + 4, cy + hs + 4), 0xff40e060, g_fs * 0.2f, 3.0f, 0);
    }
}

static void draw_weapon_tree(float x0, float y0, float x1, float y1) {
    panel(x0, y0, x1, y1);
    char line[128], nm[96];
    Buf b = {line, 0, sizeof line};
    float labw = (x1 - x0) * 0.22f, gx0 = x0 + labw, top = y0 + g_fs * 0.6f, rowh = g_fs * 2.1f, colw = g_fs * 2.05f,
          hs = g_fs * 0.62f;
    int vis_rows = (int)((y1 - top - g_fs * 0.3f) / rowh), vis_cols = (int)((x1 - gx0 - g_fs) / colw);
    static float sr, sc;  // (the view, in rows and columns: eased towards the cursor)
    int crow = g_w[g_cw].row, ccol = col_of(g_cw, g_clv);
    float wr = (float)crow - (float)vis_rows * 0.4f, wc = (float)ccol - (float)vis_cols * 0.5f;
    if (wr > (float)(g_nrows - vis_rows)) wr = (float)(g_nrows - vis_rows);
    if (wr < 0) wr = 0;
    if (wc < 0) wc = 0;
    sr += (wr - sr) * 0.25f, sc += (wc - sc) * 0.25f;
    // the path to the cursor: lit up to the level where each next weapon derives
    static uint8_t lit_to[WMAX];
    memset(lit_to, 0, sizeof lit_to);
    lit_to[g_cw] = (uint8_t)g_clv;
    for (int w = g_cw; g_w[w].parent >= 0; w = g_w[w].parent)
        lit_to[g_w[w].parent] = g_w[w].from_lv ? g_w[w].from_lv : g_w[g_w[w].parent].top;
#define RY(r) (top + ((float)(r) - sr) * rowh + rowh * 0.5f)
#define CX(c) (gx0 + g_fs * 0.8f + ((float)(c) - sc) * colw + colw * 0.5f)
    // the families' bands and labels (fixed on the left)
    for (int i = 0; i < g_nrows; i++) {
        int w = g_rows[i];
        if (g_w[w].depth) continue;
        int j = i + 1;
        while (j < g_nrows && g_w[g_rows[j]].depth) j++;
        float ya = RY(i) - rowh * 0.5f, yb = RY(j - 1) + rowh * 0.5f;
        if (yb < top || ya > y1) continue;
        if (ya < top) ya = top;
        if (yb > y1 - 2) yb = y1 - 2;
        bool here = g_w[g_cw].root == w;
        ImDrawList_AddRectFilled(g_dl, V2(x0 + 2, ya + 1), V2(x1 - 2, yb - 1), here ? 0x40306878 : (i & 1) ? 0x14ffffff : 0x08ffffff,
                                 0, 0);
        ImDrawList_AddLine(g_dl, V2(x0 + 2, yb), V2(x1 - 2, yb), 0x30ffffff, 1.0f);
        weapon_name(g_type, (uint32_t)w, 0, nm, sizeof nm);
        b.n = 0, put(&b, nm), put(&b, " 파생");
        ImDrawList_PushClipRect(g_dl, V2(x0, ya), V2(gx0 - g_fs * 0.3f, yb), true);
        text_s(x0 + g_fs * 0.6f, (RY(i) > ya + rowh * 0.3f ? RY(i) : ya + rowh * 0.5f) - g_fs * 0.45f, 0.9f,
               here ? 0xff80e0ff : 0xffd8b070, line);
        ImDrawList_PopClipRect(g_dl);
    }
    ImDrawList_PushClipRect(g_dl, V2(gx0, top), V2(x1 - 2, y1 - 2), true);
    for (int i = 0; i < g_nrows; i++) {  // (lines: a weapon's levels, and from where each derives)
        int w = g_rows[i];
        const Weapon *x = &g_w[w];
        float cy = RY(i);
        for (int lv = 1; lv < x->top; lv++) {
            bool on = lit_to[w] > lv;
            ImDrawList_AddLine(g_dl, V2(CX(col_of(w, lv)) + hs, cy), V2(CX(col_of(w, lv + 1)) - hs, cy),
                               on ? 0xff40d8ff : 0xff707070, on ? 2.5f : 1.3f);
        }
        if (x->parent >= 0) {
            int p = x->parent, plv = x->from_lv ? x->from_lv : g_w[p].top;
            float px = CX(col_of(p, plv)), py = RY(g_w[p].row), mx = px + colw * 0.5f;
            bool on = lit_to[w] > 0;
            ImU32 c = on ? 0xff40d8ff : 0xff707070;
            float th = on ? 2.5f : 1.3f;
            ImDrawList_AddLine(g_dl, V2(px + hs, py), V2(mx, py), c, th);
            ImDrawList_AddLine(g_dl, V2(mx, py), V2(mx, cy), c, th);
            ImDrawList_AddLine(g_dl, V2(mx, cy), V2(CX(col_of(w, 1)) - hs, cy), c, th);
        }
    }
    for (int i = 0; i < g_nrows; i++) {  // (the nodes: a level each)
        int w = g_rows[i];
        const Weapon *x = &g_w[w];
        float cy = RY(i);
        if (cy < top - rowh || cy > y1 + rowh) continue;
        for (int lv = 1; lv <= x->top; lv++) {
            float cx = CX(col_of(w, lv));
            if (cx < gx0 - colw || cx > x1 + colw) continue;
            bool renamed = lv > 1 && name_tier((uint32_t)w, (uint32_t)lv) != name_tier((uint32_t)w, (uint32_t)lv - 1);
            node(cx, cy, hs, x->rare, lit_to[w] >= lv, w == g_cw && lv == g_clv, renamed || lv == 1, lv == x->top);
        }
    }
    ImDrawList_PopClipRect(g_dl);
#undef RY
#undef CX
}

static void draw_weapon_needs(float x0, float y0, float x1, float y1, int w, int lv) {
    panel(x0, y0, x1, y1);
    const Weapon *wp = &g_w[w];
    char line[200], nm[96];
    Buf b = {line, 0, sizeof line};
    float x = x0 + g_fs * 0.8f, xr = x1 - g_fs * 0.8f, y = y0 + g_fs * 0.5f;
    weapon_name(g_type, (uint32_t)w, name_tier((uint32_t)w, (uint32_t)lv), nm, sizeof nm);
    text_s(x, y, 1.1f, 0xfff0f0f0, nm), y += g_fs * 1.45f;
    b.n = 0, put(&b, "Lv "), put_int(&b, lv), put(&b, " / "), put_int(&b, wp->top);
    text_s(x, y, 0.9f, 0xffc0c0c0, line);
    ImDrawList_AddRectFilled(g_dl, V2(x + g_fs * 4.5f, y + g_fs * 0.35f), V2(xr, y + g_fs * 0.55f), rare_colour(wp->rare), 0, 0);
    y += g_fs * 1.4f;
    ImDrawList_PushClipRect(g_dl, V2(x0, y), V2(x1, y1 - g_fs * 2.6f), true);
    const uint8_t *b0 = base_of((uint32_t)w), *mats = NULL;
    int n = 0, stride = 0, key = 0, cost = -1;
    const char *how = "";
    if (lv == 1 && wp->parent >= 0) {
        mats = proc_of((uint32_t)w, 1), n = 4, stride = 3, how = "파생 강화";
        if (mats) mats += 4;
    } else if (lv == 1) {
        mats = create_of((uint32_t)w), n = 4, stride = 4, key = 1, how = "생산";
        if (mats) mats += 6, cost = b0 ? (int)rd32(b0 + 0xc) : -1;
    } else {
        mats = proc_of((uint32_t)w, (uint32_t)lv), n = 4, stride = 3, how = "강화";
        if (mats) mats += 4;
    }
    b.n = 0, put(&b, "필요 소재  ("), put(&b, how);
    if (lv == 1 && wp->parent >= 0) {
        weapon_name(g_type, (uint32_t)wp->parent, name_tier((uint32_t)wp->parent, wp->from_lv), nm, sizeof nm);
        put(&b, ": "), put(&b, nm), put(&b, " Lv"), put_int(&b, wp->from_lv);
    } else if (lv > 1) put(&b, ": Lv"), put_int(&b, lv - 1), put(&b, " > "), put_int(&b, lv);
    put(&b, ")");
    if (mats) y = materials(x, xr, y, line, mats, n, stride, key);
    else text_s(x, y, 0.9f, 0xff909090, lv == 1 ? "생산할 수 없는 무기 (파생 / 특수 입수)" : "(정보 없음)"), y += g_fs * 1.3f;
    if (lv == 1 && wp->parent >= 0 && create_of((uint32_t)w)) {  // (made from materials alone too)
        const uint8_t *c = create_of((uint32_t)w);
        y = materials(x, xr, y, "처음부터 생산", c + 6, 4, 4, 1);
        cost = b0 ? (int)rd32(b0 + 0xc) : -1;
    }
    ImDrawList_PopClipRect(g_dl);
    float by = y1 - g_fs * 2.3f;
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 4, by), V2(x1 - 4, y1 - 4), 0xff2a2620, g_fs * 0.2f, 0);
    text_s(x, by + g_fs * 0.1f, 0.85f, 0xffc0c0c0, "필요 비용");
    b.n = 0;
    if (cost >= 0) put_int(&b, cost), put(&b, " z");
    else put(&b, "- z");
    text_s(xr - text_w(line) * 1.1f, by + g_fs * 0.8f, 1.1f, 0xfff0f0f0, line);
    if (!g_have_ok) text_s(x, y, 0.8f, 0xff808080, "(아이템 상자를 읽지 못함: 보유 수 없음)");
}

static void draw_weapon_detail(float x0, float y0, float x1, float y1, int w, int lv) {
    panel(x0, y0, x1, y1);
    const Weapon *wp = &g_w[w];
    const uint8_t *b0 = base_of((uint32_t)w), *e = level_of((uint32_t)w, (uint32_t)lv);
    char line[200], nm[96];
    Buf b = {line, 0, sizeof line};
    float x = x0 + g_fs * 0.7f, xr = x1 - g_fs * 0.7f, xv = x + (xr - x) * 0.45f, y = y0 + g_fs * 0.4f;
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, y0 + 3), V2(x1 - 3, y0 + g_fs * 1.6f), 0xff3a4a20, g_fs * 0.2f, 0);
    text_s(x, y, 0.95f, 0xfff0f0f0, "장비 세부 정보"), y += g_fs * 1.6f;
    weapon_name(g_type, (uint32_t)w, name_tier((uint32_t)w, (uint32_t)lv), nm, sizeof nm);
    text_s(x, y, 0.95f, 0xfff0f0f0, nm), y += g_fs * 1.25f;
    b.n = 0, put(&b, "RARE "), put_int(&b, wp->rare);
    text_s(xr - text_w(line), y, 1.0f, rare_colour(wp->rare), line), y += g_fs * 1.35f;
#define ROW(label, col) (text_s(x, y, 0.9f, 0xffb8b8b8, label), text_s(xv, y, 0.9f, col, line), y += g_fs * 1.2f)
    if (e) {
        bool gun = gunner(g_type);
        b.n = 0, put_int(&b, gun ? (int)rd16(e + 6) : (int)rd16(e + 8));
        ROW("공격력", 0xfff0f0f0);
        if (!gun) {
            text_s(x, y, 0.9f, 0xffb8b8b8, "예리도");
            sharpness_bar(xv, y + g_fs * 0.2f, xr - xv, g_fs * 0.55f, e[6]);
            y += g_fs * 1.2f;
            int aff = (int8_t)e[11];
            b.n = 0, put(&b, aff > 0 ? "+" : ""), put_int(&b, aff), put(&b, "%");
            ROW("회심률", aff < 0 ? 0xff7070ff : aff > 0 ? 0xff70e070 : 0xfff0f0f0);
            int el = b0 ? b0[0x11] : 0;
            b.n = 0;
            if (el > 0 && el < 10 && e[12]) put(&b, ELEMENT[el]), put(&b, " "), put_int(&b, e[12] * 10);
            else put(&b, "-");
            ROW("속성", el > 0 && el < 10 && e[12] ? ELEMENT_COL[el] : 0xfff0f0f0);
            b.n = 0;
            if (e[10]) put(&b, "+"), put_int(&b, e[10]);
            else put(&b, "-");
            ROW("방어력", 0xfff0f0f0);
            b.n = 0, slots_text(&b, e[LEVEL_STRIDE[g_type] - 1]);
            ROW("슬롯", 0xfff0f0f0);
        }
    }
#undef ROW
    y += g_fs * 0.3f;
    rule(x0 + 4, x1 - 4, y), y += g_fs * 0.4f;
    text_s(x, y, 0.85f, 0xff50d0ff, "이름 (레벨별)"), y += g_fs * 1.15f;
    int s1 = wp->s1, top = wp->top;
    for (int k = 0; k < 3; k++) {
        int a = k == 0 ? 1 : k == 1 ? s1 : top, z = k == 0 ? (s1 ? s1 - 1 : top - 1) : k == 1 ? top - 1 : top;
        if (k == 1 && (!s1 || s1 > top - 1)) continue;
        if (k == 2 && top < 2) continue;
        if (k == 0 && z < 1) z = 1;
        weapon_name(g_type, (uint32_t)w, k, nm, sizeof nm);
        bool here = name_tier((uint32_t)w, (uint32_t)lv) == k;
        b.n = 0, put(&b, "Lv"), put_int(&b, a);
        if (z > a) put(&b, "-"), put_int(&b, z);
        text_s(x, y, 0.8f, here ? 0xff70e8ff : 0xff909090, line), y += g_fs * 0.9f;
        text_s(x + g_fs * 0.5f, y, 0.85f, here ? 0xff70e8ff : 0xffc8c8c8, nm), y += g_fs * 1.1f;
    }
    if (y < y1 - g_fs * 2.4f) {
        text_s(x, y1 - g_fs * 2.3f, 0.75f, 0xff808080, "R스틱: 트리 이동");
        text_s(x, y1 - g_fs * 1.3f, 0.75f, 0xff808080, "R3: 트리 숨기기/보이기");
    }
}

// ---- armor
static void draw_armor(float tx0, float tx1, float cx1, float y0, float y1, uint32_t sel, int piece) {
    char line[200], nm[96];
    Buf b = {line, 0, sizeof line};
    // the grid
    panel(tx0, y0, tx1, y1);
    text(tx0 + g_fs, y0 + g_fs * 0.5f, 0xff50d0ff, "방어구 시리즈");
    float top = y0 + g_fs * 2.0f, rowh = g_fs * 1.5f, namew = (tx1 - tx0) * 0.48f, gx = tx0 + namew,
          cw = (tx1 - g_fs * 0.6f - gx) / 5.0f;
    for (int p = 0; p < 5; p++)
        text_s(gx + cw * (float)p + cw * 0.2f, y0 + g_fs * 0.6f, 0.85f, p == piece ? 0xff70e8ff : 0xff909090, PIECE[p]);
    int selrow = 0;
    for (int i = 0; i < g_narows; i++)
        if (g_arows[i] == sel) selrow = i;
    int visible = (int)((y1 - top - g_fs * 0.4f) / rowh);
    static float scroll;
    float want = (float)selrow - (float)visible * 0.45f;
    if (want > (float)(g_narows - visible)) want = (float)(g_narows - visible);
    if (want < 0) want = 0;
    scroll += (want - scroll) * 0.25f;
    ImDrawList_PushClipRect(g_dl, V2(tx0, top - 2), V2(tx1, y1 - g_fs * 0.3f), true);
    for (int i = 0; i < g_narows; i++) {
        float y = top + ((float)i - scroll) * rowh, cy = y + rowh * 0.5f;
        if (y + rowh < top || y > y1) continue;
        uint32_t s = g_arows[i];
        const uint8_t *a = aser(s);
        bool is = s == sel;
        if (is) ImDrawList_AddRectFilled(g_dl, V2(tx0 + 3, y + 1), V2(tx1 - 3, y + rowh - 1), 0x38ffd060, g_fs * 0.2f, 0);
        int first = 0;
        while (first < 4 && !acreate(s, first)) first++;
        armor_name(s, first, nm, sizeof nm);
        int rare = a ? a[0x67] + 1 : 1;
        text_s(tx0 + g_fs * 0.7f, cy - g_fs * 0.45f, 0.9f, is ? 0xff70e8ff : rare_colour(rare), nm);
        for (int p = 0; p < 5; p++) {
            float px = gx + cw * (float)p + cw * 0.5f, h = g_fs * 0.4f;
            if (!acreate(s, p)) {
                ImDrawList_AddLine(g_dl, V2(px - h, cy), V2(px + h, cy), 0x50ffffff, 1.0f);
                continue;
            }
            ImDrawList_AddRectFilled(g_dl, V2(px - h, cy - h), V2(px + h, cy + h), rare_colour(rare), g_fs * 0.1f, 0);
            if (is && p == piece)
                ImDrawList_AddRect(g_dl, V2(px - h - 3, cy - h - 3), V2(px + h + 3, cy + h + 3), 0xff40e0ff, g_fs * 0.2f,
                                   2.5f, 0);
        }
    }
    ImDrawList_PopClipRect(g_dl);
    // the card
    float x0 = tx1 + g_fs * 0.4f, x = x0 + g_fs * 0.8f, xr = cx1 - g_fs * 0.8f, y = y0 + g_fs * 0.5f;
    panel(x0, y0, cx1, y1);
    const uint8_t *a = aser(sel);
    if (!a) return;
    int p = piece >= 0 && piece < 5 ? piece : 0;
    armor_name(sel, p, nm, sizeof nm);
    text_s(x, y, 1.2f, 0xfff0f0f0, nm), y += g_fs * 1.5f;
    const uint8_t *t = a + 0x67;
    int rare = t[0] + 1, def = t[3], alv = t[1], max = def;
    if (t[2] < g_nabuild)
        for (int k = 0; k + 1 < alv && k < 20; k++) max += g_abuild[8 + t[2] * 20 + k];
    b.n = 0, put(&b, "레어 "), put_int(&b, rare);
    text(x, y, rare_colour(rare), line);
    b.n = 0, put(&b, "슬롯 "), slots_text(&b, t[5 + p]);
    text(x + (xr - x) * 0.5f, y, 0xffe8e8e8, line), y += g_fs * 1.25f;
    b.n = 0, put(&b, "방어력 "), put_int(&b, def), put(&b, " ~ "), put_int(&b, max), put(&b, "  (Lv"), put_int(&b, alv),
        put(&b, ")");
    text(x, y, 0xffe8e8e8, line), y += g_fs * 1.25f;
    static const char *const RES[5] = {"불", "물", "번개", "얼음", "용"};
    static const ImU32 RES_COL[5] = {0xff3c6cff, 0xffffa040, 0xff30e0ff, 0xfff0e0a0, 0xffb050c0};
    for (int r = 0; r < 5; r++) {
        int v = (int8_t)a[0x17 + r];
        b.n = 0, put(&b, RES[r]), put(&b, " "), put(&b, v > 0 ? "+" : ""), put_int(&b, v);
        text_s(x + (xr - x) * 0.2f * (float)r, y, 0.9f, v ? RES_COL[r] : 0xff909090, line);
    }
    y += g_fs * 1.3f;
    rule(x0 + 4, cx1 - 4, y), y += g_fs * 0.35f;
    text(x, y, 0xff50d0ff, "스킬"), y += g_fs * 1.2f;
    int shown = 0;
    for (int k = 0; k < 5; k++) {
        const uint8_t *s = a + 0x1c + 15 * p + 3 * k;
        uint32_t id = rd16(s);
        int pts = (int8_t)s[2];
        if (!id || !pts) continue;
        skill_name(id, nm, sizeof nm);
        b.n = 0, put(&b, nm), put(&b, "  "), put(&b, pts > 0 ? "+" : ""), put_int(&b, pts);
        text_s(x + g_fs * 0.6f, y, 0.92f, pts > 0 ? 0xffe8e8e8 : 0xff7070ff, line), y += g_fs * 1.1f;
        shown++;
    }
    if (!shown) text_s(x + g_fs * 0.6f, y, 0.92f, 0xff909090, "(없음)"), y += g_fs * 1.1f;
    y += g_fs * 0.3f;
    rule(x0 + 4, cx1 - 4, y), y += g_fs * 0.35f;
    const uint8_t *c = acreate(sel, p);
    if (c) y = materials(x, xr, y, "생산", c + 10, 5, 4, 1);
    if (!g_have_ok) text_s(x, y, 0.85f, 0xff808080, "(아이템 상자를 읽지 못했어요: 보유 수 없음)");
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
    log_line("[SmithyTree] " VERSION " ready: the weapon tree and the armor grid show over the smithy");
}

void forge_onUpdate(float dt) {
    (void)dt;
    g_frame++;
}

static void log_box_once(void) {
    static bool done;
    if (done || !g_have_frame) return;
    done = true;
    char l[300], nm[64];
    Buf b = {l, 0, sizeof l};
    put(&b, "[SmithyTree] item box: ");
    if (!g_have_ok) {
        put(&b, "not read (GU's addresses: ");
        put_hex(&b, mhxx_gu_data(0x1858438)), put(&b, " "), put_hex(&b, mhxx_gu_data(0x18584b8)), put(&b, " "),
            put_hex(&b, mhxx_gu_data(0x1897f78)), put(&b, ")");
    } else {
        int kinds = 0, shown = 0;
        for (int i = 1; i < ITEMS; i++)
            if (g_have[i]) {
                kinds++;
                if (shown < 4) {
                    item_name((uint32_t)i, nm, sizeof nm);
                    put(&b, nm), put(&b, " "), put_int(&b, g_have[i]), put(&b, ", "), shown++;
                }
            }
        put_int(&b, kinds), put(&b, " kinds of item");
    }
    log_line(l);
}

static void help_line(float W, float y, const char *s) {
    ImDrawList_AddRectFilled(g_dl, V2(W * 0.015f, y), V2(W * 0.985f, y + g_fs * 1.5f), 0xff1a130b, g_fs * 0.2f, 0);
    text_s(W * 0.025f, y + g_fs * 0.2f, 0.9f, 0xffd8d8d8, s);
}

void forge_onImGuiFreeRender(void) {
    ImGuiIO *io = igGetIO_Nil();
    float W = io->DisplaySize.x, H = io->DisplaySize.y;
    g_dl = igGetForegroundDrawList_Nil(), g_fs = igGetFontSize();
    if (g_toast[0] && g_frame - g_toast_frame < 150) {  // (a word for the player, over the game too)
        float w = text_w(g_toast) + g_fs * 2;
        ImDrawList_AddRectFilled(g_dl, V2((W - w) * 0.5f, H * 0.06f), V2((W + w) * 0.5f, H * 0.06f + g_fs * 1.8f), 0xf01a130b,
                                 g_fs * 0.3f, 0);
        text((W - w) * 0.5f + g_fs, H * 0.06f + g_fs * 0.4f, 0xff70e8ff, g_toast);
    }
    static uint32_t last;
    bool fresh = last != g_frame;  // (input: once a game frame)
    last = g_frame;
    if (!g_original || g_frame - g_seen_frame > 2 || !g_now.id) {  // (the smithy not open: the screen opens with it)
        if (g_frame - g_seen_frame > 2) g_hidden = false, g_view = -1;
        return;
    }
    int cat = g_now.category;
    bool weapons = cat >= 7 && cat - 7 < TYPES && cat - 7 != 5, armor = cat < 7;
    if (!weapons && !armor) return;
    if (fresh && forge_input_isPressed(5)) g_hidden = !g_hidden;  // (R3: the screen, or the game's own)
    if (g_hidden) return;
    mhxx_input_capture(150);  // (the controller is the screen's while it is up)
    float y0 = H * 0.11f, y1 = H * 0.86f;
    // the game's own 3D preview of the weapon stays seen (its frame on the left): the backdrop round it
    float px0 = W * 0.012f, py0 = H * 0.174f, px1 = W * 0.258f, py1 = H * 0.670f, by0 = y0 - H * 0.015f, by1 = H * 0.93f;
    bool hole = weapons;
    if (hole) {
        ImDrawList_AddRectFilled(g_dl, V2(0, by0), V2(W, py0), 0xff0c0805, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(0, py1), V2(W, by1), 0xff0c0805, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(0, py0), V2(px0, py1), 0xff0c0805, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(px1, py0), V2(W, py1), 0xff0c0805, 0, 0);
    } else ImDrawList_AddRectFilled(g_dl, V2(0, by0), V2(W, by1), 0xff0c0805, 0, 0);
    read_box();
    log_box_once();
    int dir = fresh ? pad_dir() : 0;
    if (fresh && forge_input_isPressed(1)) {  // (B: back to the game's own screen)
        g_hidden = true;
        return;
    }
    if (weapons) {
        cursor_from_game();
        if (fresh && (forge_input_isPressed(6) || forge_input_isPressed(7))) {  // (L / R: the weapon type)
            int step = forge_input_isPressed(6) ? -1 : 1, v = g_view;
            do v = (v + step + TYPES) % TYPES; while (v == 5);
            g_view = v, g_cw = -1;
        }
        if (!load_type(g_view)) return;
        if (g_cw < 0 || g_cw >= (int)g_nbase || !g_w[g_cw].top) {
            bool game = g_view == cat - 7 && g_now.id < g_nbase && g_w[g_now.id].top;
            g_cw = game ? g_now.id : g_rows[0];
            g_clv = game && g_now.level ? g_now.level : 1;
            if (g_clv > g_w[g_cw].top) g_clv = g_w[g_cw].top;
        }
        cursor_move(dir);
        g_want_id = g_view == cat - 7 ? g_cw : -1;  // (the game's list follows: its 3D preview shows the weapon)
        if (fresh && forge_input_isPressed(0)) {  // (A: back to the game's list, on this weapon, to make or upgrade it)
            if (g_view != cat - 7) toast("게임 목록의 무기 종류가 달라요: 게임 화면에서 종류를 바꾼 뒤 다시 열어 주세요 (R3)");
            else if (g_want_result < 0) toast("이 무기는 지금 게임 목록에 없어요 (해금 전이거나 다른 페이지)");
            else g_hidden = true;
        }
        draw_weapon_tree(W * 0.265f, y0, W * 0.70f, y1);
        draw_weapon_detail(W * 0.707f, y0, W * 0.985f, H * 0.47f, g_cw, g_clv);
        draw_weapon_needs(W * 0.707f, H * 0.48f, W * 0.985f, y1, g_cw, g_clv);
        {  // under the preview: whether it shows this weapon
            float ly = py1 + H * 0.01f;
            panel(px0, ly, px1, y1);
            char nm[96];
            weapon_name(g_type, (uint32_t)g_cw, name_tier((uint32_t)g_cw, (uint32_t)g_clv), nm, sizeof nm);
            text_s(px0 + g_fs * 0.7f, ly + g_fs * 0.5f, 1.05f, rare_colour(g_w[g_cw].rare), nm);
            const char *st = g_view != cat - 7 ? "미리보기: 게임 목록과 무기 종류가 달라요"
                             : g_want_result < 0 ? "미리보기: 게임 목록에 없는 무기"
                                                 : "미리보기: 이 무기  (A: 게임 목록으로)";
            text_s(px0 + g_fs * 0.7f, ly + g_fs * 1.9f, 0.82f, 0xffa0a0a0, st);
        }
        help_line(W, y1 + H * 0.012f,
                  "방향키/스틱: 이동    L / R: 무기 종류    A: 이 무기로 게임 화면에 돌아가기 (생산 / 강화)    B: 닫기    R3: 열기 / 닫기");
    } else {
        if (!load_armor() || g_now.id >= g_naser) return;
        static int sel_row = -1, piece = 0;
        static uint16_t from_id, from_cat;
        if (sel_row < 0 || g_now.id != from_id || cat != from_cat) {
            from_id = g_now.id, from_cat = (uint16_t)cat, piece = cat < 5 ? cat : 0, sel_row = 0;
            for (int i = 0; i < g_narows; i++)
                if (g_arows[i] == g_now.id) sel_row = i;
        }
        if (dir == 1 && piece < 4) piece++;
        if (dir == 2 && piece > 0) piece--;
        if (dir == 3 && sel_row > 0) sel_row--;
        if (dir == 4 && sel_row + 1 < g_narows) sel_row++;
        draw_armor(W * 0.015f, W * 0.6f, W * 0.985f, y0, y1, g_arows[sel_row], piece);
        help_line(W, y1 + H * 0.012f, "방향키/스틱: 이동 (위아래 시리즈, 좌우 부위)    B: 닫기    R3: 열기 / 닫기");
    }
}
