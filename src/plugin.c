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

#define VERSION "0.12.2"

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
    char l[200];
    Buf b = {l, 0, sizeof l};
    put(&b, "[SmithyTree] "), put(&b, path), put(&b, *slot ? ": found" : ": not loaded (yet)");
    if (*slot) {
        uint32_t r = *slot;
        put(&b, ", "), put_int(&b, (int)G32(r + 0x88)), put(&b, " strings, "), put_int(&b, (int)G32(r + 0x90)),
            put(&b, " bytes, text "), put_hex(&b, G32(r + 0x94)), put(&b, " pointers "), put_hex(&b, G32(r + 0x98));
        for (int o = 0x80; o < 0xa0; o += 4) put(&b, " "), put_hex(&b, G32(r + (uint32_t)o));
    }
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
    uint32_t r = find_res(&g_res_armor, &g_try_armor, "table\\armorSeriesData_jpn"), i = series * 10 + (uint32_t)piece;
    static bool told;
    if (r && !told) {  // (once: what the string looks like where a piece's name should be)
        told = true;
        char l[300];
        Buf b = {l, 0, sizeof l};
        uint32_t text = G32(r + 0x94), arr = G32(r + 0x98), sp = arr ? G32(arr + 4 * i) : 0;
        put(&b, "[SmithyTree] armor name "), put_int(&b, (int)i), put(&b, ": pointer "), put_hex(&b, sp), put(&b, " (text "),
            put_hex(&b, text), put(&b, "), bytes");
        for (int k = 0; k < 16 && sp; k++) put(&b, " "), put_int(&b, G8(sp + (uint32_t)k));
        log_line(l);
    }
    if (!msg_get(r, i, out, cap)) {
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

// ------------------------------------------------------------------ the equipment box (what the player owns) and the
// items the player has ever had (what unlocks recipes): GU's sEquipBox (+0x1c 44 bytes an entry: +4 its kind - a weapon
// type + 7, a piece 1-5 - +6 its number) and the item-acquired flags (+0x9c a bit an item), where mhxx_gu_data says
#define TAGS 22
static uint8_t g_owned[TAGS][2048 / 8];
static bool g_owned_ok;
static uint32_t g_acq;  // (the flags' address, 0: unknown)
static uint32_t g_equip_at;
static void read_equip(void) {
    uint32_t at = g_equip_at;
    if (at && g_frame - at < 30) return;
    g_equip_at = g_frame | 1;
    memset(g_owned, 0, sizeof g_owned);
    g_owned_ok = false;
    uint32_t pb = mhxx_gu_data(0x1856fe8), ps = mhxx_gu_data(0x1897f78), pa = mhxx_gu_data(0x18583a8);
    g_acq = pa && ok_ptr(G32(pa)) ? G32(pa) + 0x9c : 0;
    if (!pb || !ps) return;
    uint32_t bx = G32(pb), save = G32(ps);
    if (!ok_ptr(bx) || !ok_ptr(save) || G32(bx) != mhxx_gu_data(0x173e3d0)) return;
    uint32_t flags = G32(save + 0x958), exp = G32(save + 0x3614), n = 1000;
    n += 200 * (((flags >> 21) & 1) + ((flags >> 22) & 1));
    for (int k = 0; k < 3; k++) n += 200 * ((exp >> k) & 1);
    if (n > 2000) return;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = bx + 0x1c + i * 44, tag = G16(e + 4) & 31, id = G16(e + 6);
        if (tag && tag < TAGS && id < 2048) g_owned[tag][id >> 3] |= (uint8_t)(1u << (id & 7));
    }
    g_owned_ok = true;
}
static bool owned(int tag, uint32_t id) { return tag > 0 && tag < TAGS && id < 2048 && (g_owned[tag][id >> 3] >> (id & 7)) & 1; }
static bool acquired(uint32_t item) { return !g_acq || item >= ITEMS || (G8(g_acq + (item >> 3)) >> (item & 7)) & 1; }

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

// ------------------------------------------------------------------ drawing: MHXX's own look (as the reference mod's
// pictures: the game's teal frames with gold corner hooks, red title bars, tan cards; the game's 3D preview, message box,
// money and buttons stay the game's)
#define V2(x, y) ((ImVec2){(x), (y)})
static ImDrawList *g_dl;
static float g_fs;

static ImU32 rare_colour(int r) {  // (MH's rarity colours, as the game's icons)
    static const ImU32 c[12] = {0xffd0d0d0, 0xffe8e8e8, 0xffc8a070, 0xff40c0a0, 0xff40a8f0, 0xffd080c0,
                                0xff3050e0, 0xff50c8e8, 0xff9090f8, 0xffe0e040, 0xfff060c0, 0xff40d0ff};
    return c[r < 0 ? 0 : r > 11 ? 11 : r];
}
static void text_s(float x, float y, float scale, ImU32 col, const char *s) {
    ImDrawList_AddText_FontPtr(g_dl, igGetFont(), g_fs * scale, V2(x, y), col, s, NULL, 0.0f, NULL);
}
static void text_o(float x, float y, float scale, ImU32 col, const char *s) {  // (the game's: a dark edge round it)
    ImDrawList_AddText_FontPtr(g_dl, igGetFont(), g_fs * scale, V2(x + 1.5f, y + 1.5f), 0xc0000000, s, NULL, 0.0f, NULL);
    ImDrawList_AddText_FontPtr(g_dl, igGetFont(), g_fs * scale, V2(x, y), col, s, NULL, 0.0f, NULL);
}
static float text_w(const char *s, float scale) {  // (measured at the font's size, drawn at g_fs x scale)
    return igCalcTextSize(s, NULL, false, -1.0f).x * scale * g_fs / igGetFontSize();
}
static void text_r(float xr, float y, float scale, ImU32 col, const char *s) { text_o(xr - text_w(s, scale), y, scale, col, s); }

// MHXX's own look (its smithy list and info panels): maroon lists with a gold bar on the chosen row, navy info panels
// with azure labels, black title boxes with blue ends, teal edges with small gold corner hooks
#define COL_TEAL 0xff9a8a3a
#define COL_GOLD 0xff30c0f0
#define COL_RED 0xf0080808       // (title boxes: black)
#define COL_TAN 0xe628190a       // (info panels: navy)
#define COL_TAN_DARK 0xe6201408
#define COL_HEAD 0xffff9a30      // (labels and headings: the game's azure)
#define COL_HAVE 0xff40e060
#define COL_LACK 0xff4848ff
#define COL_LIST 0xe816163c      // (lists: maroon)
#define COL_LIST_HEAD 0xff1e1e64
#define COL_SEL 0xe028a0c8       // (the chosen row: gold)

static void hooks(float x0, float y0, float x1, float y1) {  // (the game's small gold corner hooks)
    float k = g_fs * 0.32f;
    float c[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    for (int i = 0; i < 4; i++) {
        float x = c[i][0], y = c[i][1];
        ImDrawList_AddRectFilled(g_dl, V2(x - k, y - k), V2(x + k, y + k), 0xff101010, 0, 0);
        ImDrawList_AddRect(g_dl, V2(x - k, y - k), V2(x + k, y + k), COL_GOLD, 0, 1.5f, 0);
        ImDrawList_AddLine(g_dl, V2(x - k * 0.45f, y - k * 0.1f), V2(x + k * 0.45f, y - k * 0.1f), COL_GOLD, 1.5f);
        ImDrawList_AddLine(g_dl, V2(x + k * 0.45f, y - k * 0.1f), V2(x + k * 0.45f, y + k * 0.45f), COL_GOLD, 1.5f);
    }
}
static void frame(float x0, float y0, float x1, float y1, ImU32 fill) {  // (the game's window: a teal double edge)
    if (fill) ImDrawList_AddRectFilled(g_dl, V2(x0, y0), V2(x1, y1), fill, 0, 0);
    ImDrawList_AddRect(g_dl, V2(x0, y0), V2(x1, y1), COL_TEAL, 0, 2.0f, 0);
    ImDrawList_AddRect(g_dl, V2(x0 + 4, y0 + 4), V2(x1 - 4, y1 - 4), (COL_TEAL & 0x00ffffff) | 0x70000000, 0, 1.0f, 0);
    hooks(x0, y0, x1, y1);
}
static void red_bar(float x0, float y0, float x1, float y1) {  // (the game's title box: black, blue bars at its ends)
    float e = g_fs * 0.22f;
    ImDrawList_AddRectFilled(g_dl, V2(x0, y0), V2(x1, y1), COL_RED, 0, 0);
    for (int side = 0; side < 2; side++) {
        float xa = side ? x1 - e * 3 : x0 + e;
        ImDrawList_AddRectFilled(g_dl, V2(xa, y0 + 3), V2(xa + e, y1 - 3), 0xffe8641e, 0, 0);
        for (float yy = y0 + 4; yy < y1 - 4; yy += g_fs * 0.3f)  // (its dotted line beside)
            ImDrawList_AddRectFilled(g_dl, V2(xa + (side ? -e : e * 1.6f), yy), V2(xa + (side ? -e * 0.5f : e * 2.1f), yy + g_fs * 0.12f),
                                     0xffe8641e, 0, 0);
    }
    ImDrawList_AddLine(g_dl, V2(x0, y1), V2(x1, y1), COL_TEAL, 1.5f);
}
static void tan_box(float x0, float y0, float x1, float y1) {
    ImDrawList_AddRectFilled(g_dl, V2(x0, y0), V2(x1, y1), COL_TAN, 0, 0);
}
static void tan_rule(float x0, float x1, float y) { ImDrawList_AddLine(g_dl, V2(x0, y), V2(x1, y), 0x40ffffff, 1.0f); }

// a weapon type's glyph (a blade), an armor piece's: shapes, in a colour
static void shape_weapon(float cx, float cy, float h, ImU32 c) {
    ImDrawList_AddLine(g_dl, V2(cx - h * 0.55f, cy + h * 0.55f), V2(cx + h * 0.6f, cy - h * 0.6f), c, g_fs * 0.28f);
    ImDrawList_AddTriangleFilled(g_dl, V2(cx + h * 0.6f, cy - h * 0.6f), V2(cx + h * 0.15f, cy - h * 0.45f),
                                 V2(cx + h * 0.45f, cy - h * 0.15f), c);
    ImDrawList_AddLine(g_dl, V2(cx - h * 0.62f, cy + h * 0.2f), V2(cx - h * 0.2f, cy + h * 0.62f), 0xffe0e0e0, g_fs * 0.16f);
}
static void shape_armor(float cx, float cy, float h, int piece, ImU32 c) {
    float w = h * 0.55f;
    switch (piece) {
    case 0:  // head
        ImDrawList_AddCircleFilled(g_dl, V2(cx, cy - h * 0.05f), w, c, 16);
        ImDrawList_AddRectFilled(g_dl, V2(cx - w * 0.7f, cy), V2(cx + w * 0.7f, cy + w * 0.35f), 0xff202020, 0, 0);
        break;
    case 1:  // body
        ImDrawList_AddRectFilled(g_dl, V2(cx - w, cy - w * 0.8f), V2(cx + w, cy + w), c, g_fs * 0.1f, 0);
        ImDrawList_AddRectFilled(g_dl, V2(cx - w * 0.25f, cy - w * 0.8f), V2(cx + w * 0.25f, cy - w * 0.3f), 0xff202020, 0, 0);
        break;
    case 2:  // arms
        ImDrawList_AddRectFilled(g_dl, V2(cx - w * 0.6f, cy - w), V2(cx + w * 0.6f, cy + w), c, g_fs * 0.1f, 0);
        for (int k = 0; k < 3; k++)
            ImDrawList_AddLine(g_dl, V2(cx - w * 0.6f, cy - w * 0.5f + w * 0.5f * k), V2(cx + w * 0.6f, cy - w * 0.5f + w * 0.5f * k),
                               0xff202020, 1.5f);
        break;
    case 3:  // waist
        ImDrawList_AddRectFilled(g_dl, V2(cx - w, cy - w * 0.4f), V2(cx + w, cy + w * 0.1f), c, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(cx - w, cy + w * 0.2f), V2(cx - w * 0.15f, cy + w), c, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(cx + w * 0.15f, cy + w * 0.2f), V2(cx + w, cy + w), c, 0, 0);
        break;
    default:  // legs
        ImDrawList_AddRectFilled(g_dl, V2(cx - w * 0.8f, cy - w), V2(cx - w * 0.1f, cy + w), c, 0, 0);
        ImDrawList_AddRectFilled(g_dl, V2(cx + w * 0.1f, cy - w), V2(cx + w * 0.8f, cy + w), c, 0, 0);
        break;
    }
}
// The game's own icons (its icon atlas, from the player's files each run: the port's mhxx_game_texture), grey, drawn
// in a colour as the game does (the rarity's); the shapes above until it is there. docs/icons.md: the cells.
int mhxx_game_texture(const char *arc, const char *name, uint32_t *size);
uint64_t mhxx_game_texture_id(int handle);
static int g_icons;
static uint64_t g_icon_id;
static void icons_frame(void) {
    static bool tried;
    if (!tried) {
        tried = true;
        uint32_t wh = 0;
        g_icons = mhxx_game_texture("nativeNX/arc/resident.arc", "GUI\\99_texture\\HD_cmn_icon_GSM_NOMIP", &wh);
        char l[96];
        Buf b = {l, 0, sizeof l};
        put(&b, "[SmithyTree] the game's icons: "), put(&b, g_icons ? "loaded " : "not found "), put_int(&b, (int)(wh & 0xffff)),
            put(&b, "x"), put_int(&b, (int)(wh >> 16));
        log_line(l);
    }
    g_icon_id = g_icons ? mhxx_game_texture_id(g_icons) : 0;
}
static bool icon(int r, int c, float cx, float cy, float h, ImU32 col) {  // (cell r, c of the 48 px set; 512 x 1024)
    if (!g_icon_id) return false;
    ImTextureRef_c ref = {NULL, g_icon_id};
    float u0 = (float)(c * 48) / 512.0f, v0 = (float)(416 + r * 48) / 1024.0f, s = h * 1.12f;
    ImDrawList_AddImage(g_dl, ref, V2(cx - s, cy - s), V2(cx + s, cy + s), V2(u0, v0), V2(u0 + 48.0f / 512.0f, v0 + 48.0f / 1024.0f), col);
    return true;
}
static const uint8_t WEAPON_ICON[15][2] = {{5, 5}, {5, 7}, {5, 9}, {6, 1}, {6, 5}, {0, 0}, {6, 3}, {5, 6},
                                           {6, 7}, {6, 2}, {6, 6}, {5, 8}, {6, 0}, {6, 8}, {6, 9}};
static const uint8_t ARMOR_ICON[5][2] = {{7, 0}, {7, 1}, {7, 3}, {7, 2}, {7, 4}};
static void glyph_weapon_t(int type, float cx, float cy, float h, ImU32 c) {
    if (type < 0 || type >= 15 || type == 5 || !icon(WEAPON_ICON[type][0], WEAPON_ICON[type][1], cx, cy, h, c)) shape_weapon(cx, cy, h, c);
}
static void glyph_weapon(float cx, float cy, float h, ImU32 c) { glyph_weapon_t(g_type, cx, cy, h, c); }
static void glyph_armor(float cx, float cy, float h, int piece, ImU32 c) {
    if (piece < 0 || piece > 4 || !icon(ARMOR_ICON[piece][0], ARMOR_ICON[piece][1], cx, cy, h, c)) shape_armor(cx, cy, h, piece, c);
}

static void icon_cell(float cx, float cy, float h, bool sel, bool on) {  // (a dark tile, a gold edge when chosen)
    ImDrawList_AddRectFilled(g_dl, V2(cx - h, cy - h), V2(cx + h, cy + h), on ? 0xff2a2a2a : 0xff1a1a1a, g_fs * 0.12f, 0);
    ImDrawList_AddRect(g_dl, V2(cx - h, cy - h), V2(cx + h, cy + h), sel ? 0xff30d8ff : on ? 0xff8a9aa0 : 0xff4a4a4a,
                       g_fs * 0.12f, sel ? 3.0f : 1.5f, 0);
}

static const char *const ELEMENT[10] = {"", "불", "물", "번개", "용", "얼음", "독", "마비", "수면", "폭파"};
static const ImU32 ELEMENT_COL[10] = {0xffffffff, 0xff3c6cff, 0xffffa040, 0xff30e0ff, 0xffb050c0,
                                      0xfff0e0a0, 0xffc050b0, 0xff20d0e0, 0xffd0a060, 0xff4090ff};
static const ImU32 SHARP_COL[7] = {0xff2828d8, 0xff2080f0, 0xff20e0f0, 0xff30d050, 0xfff08030, 0xfff8f8f8, 0xffd040b0};
__attribute__((unused)) static const char *const PIECE[5] = {"머리", "몸통", "팔", "허리", "다리"};

// The sharpness as the game draws it: kireajiData's entry is the whole bar (cumulative red .. purple, 450 at most); the
// weapon has only the first 150 + 25 x (its level's +7 byte) of it (Bherna Lv1: 150, as the game's bar), and
// Handicraft adds up to 50 more (drawn dim); the rest black.
static void sharpness_bar(float x, float y, float w, float h, uint32_t id, uint32_t step) {
    if (id >= g_nkire) return;
    const uint8_t *k = g_kire + 8 + id * 14;
    uint32_t base = 150 + 25 * step, prev = 0;
    if (base > 450) base = 450;
    ImDrawList_AddRectFilled(g_dl, V2(x - 2, y - 2), V2(x + w + 2, y + h + 2), 0xff101010, 0, 0);
    for (int c = 0; c < 7; c++) {
        uint32_t v = rd16(k + 2 * c);
        if (v > 450) v = 450;
        for (int part = 0; part < 2; part++) {  // (the weapon's own, then what Handicraft adds)
            uint32_t a0 = prev, a1 = v, lo = part ? base : 0, hi = part ? base + 50 : base;
            if (a0 < lo) a0 = lo;
            if (a1 > hi) a1 = hi;
            if (a1 <= a0) continue;
            ImU32 col = part ? (SHARP_COL[c] & 0x00ffffff) | 0x60000000 : SHARP_COL[c];
            ImDrawList_AddRectFilled(g_dl, V2(x + w * (float)a0 / 450.0f, y), V2(x + w * (float)a1 / 450.0f, y + h), col, 0, 0);
        }
        if (v > prev) prev = v;
    }
}
static void slots_draw(float x, float cy, int n) {  // (the game's: a ring a slot, a dash none)
    float r = g_fs * 0.32f;
    for (int i = 0; i < 3; i++) {
        float cx = x + r + i * r * 3.0f;
        if (i < n) {
            ImDrawList_AddCircleFilled(g_dl, V2(cx, cy), r, 0xff101010, 12);
            ImDrawList_AddCircle(g_dl, V2(cx, cy), r, 0xfff0f0f0, 12, 2.0f);
            ImDrawList_AddCircleFilled(g_dl, V2(cx, cy), r * 0.35f, 0xfff0f0f0, 8);
        } else
            ImDrawList_AddLine(g_dl, V2(cx - r, cy), V2(cx + r, cy), 0xfff0f0f0, 2.0f);
    }
}

// Materials as the reference's cards: "name xN ......... have M" (green / red). Returns the y after them.
static float mat_list(float x, float xr, float y, const uint8_t *e, int n, int stride, int has_key, bool *all_ok) {
    char line[160], nm[96];
    Buf b = {line, 0, sizeof line};
    int shown = 0;
    for (int k = 0; k < n; k++) {
        const uint8_t *m = e + k * stride;
        uint32_t it = rd16(m), q = m[2];
        if (!it || !q) continue;
        shown++;
        int r = it < ITEMS ? g_item_rare[it] : 1;
        ImDrawList_AddRectFilled(g_dl, V2(x, y + g_fs * 0.2f), V2(x + g_fs * 0.8f, y + g_fs), rare_colour(r), g_fs * 0.1f, 0);
        item_name(it, nm, sizeof nm);
        b.n = 0;
        if (has_key && m[3]) put(&b, "* ");
        put(&b, nm), put(&b, " x"), put_int(&b, (int)q);
        text_o(x + g_fs * 1.1f, y, 0.95f, 0xfff4f4f4, line);
        if (g_have_ok) {
            uint32_t have = g_have[it];
            b.n = 0, put(&b, "보유 "), put_int(&b, (int)have);
            text_r(xr, y, 0.95f, have >= q ? COL_HAVE : COL_LACK, line);
            if (have < q && all_ok) *all_ok = false;
        }
        y += g_fs * 1.15f;
        tan_rule(x, xr, y - g_fs * 0.08f);
    }
    if (!shown) text_o(x, y, 0.9f, 0xffe0e0e0, "(필요 소재 없음)"), y += g_fs * 1.15f;
    return y;
}

// ------------------------------------------------------------------ input
void mhxx_input_capture(uint32_t ms);
bool forge_input_isDown(uint32_t button);
bool forge_input_isPressed(uint32_t button);
enum { B_A = 0, B_B = 1, B_X = 2, B_Y = 3, B_L = 6, B_R = 7, B_ZL = 8, B_ZR = 9 };
static bool g_fresh;
static bool pressed(int b) { return g_fresh && forge_input_isPressed((uint32_t)b); }
// a direction from the d-pad or either stick: 1 right 2 left 3 up 4 down - one step a press, repeating only when held
// (in seconds: 0.35 before the first repeat, then one each 0.08 - whatever the frame rate)
static float g_time;  // (seconds, ImGui's clock)
static int pad_dir(void) {
    static int prev;
    static float next;
    int dir = 0;
    if (forge_input_isDown(14) || forge_input_isDown(18) || forge_input_isDown(22)) dir = 1;
    else if (forge_input_isDown(12) || forge_input_isDown(16) || forge_input_isDown(20)) dir = 2;
    else if (forge_input_isDown(13) || forge_input_isDown(17) || forge_input_isDown(21)) dir = 3;
    else if (forge_input_isDown(15) || forge_input_isDown(19) || forge_input_isDown(23)) dir = 4;
    if (!dir) {
        prev = 0;
        return 0;
    }
    if (dir != prev) {  // (a new press: one step now)
        prev = dir, next = g_time + 0.35f;
        return dir;
    }
    if (g_time >= next) {
        next = g_time + 0.08f;
        return dir;
    }
    return 0;
}

static char g_toast[160];
static uint32_t g_toast_frame;
static void toast(const char *s) {
    Buf b = {g_toast, 0, sizeof g_toast};
    put(&b, s);
    g_toast_frame = g_frame;
}

// ------------------------------------------------------------------ the wish list (kept while the game runs)
#define WISHES 64
typedef struct {
    uint8_t kind, type;  // kind 0 a weapon (type: its weapon type), 1 an armor piece (type: the piece)
    uint16_t id;
    uint16_t item[5];
    uint8_t qty[5];
    char name[64];
} Wish;
static Wish g_wish[WISHES];
static int g_nwish;

static int wish_find(int kind, int type, int id) {
    for (int i = 0; i < g_nwish; i++)
        if (g_wish[i].kind == kind && g_wish[i].type == type && g_wish[i].id == id) return i;
    return -1;
}
static void wish_remove(int i) {
    if (i < 0 || i >= g_nwish) return;
    for (int k = i; k + 1 < g_nwish; k++) g_wish[k] = g_wish[k + 1];
    g_nwish--;
}
static bool wish_add(int kind, int type, int id, const char *name, const uint8_t *mats, int n, int stride) {
    if (wish_find(kind, type, id) >= 0 || g_nwish >= WISHES) return false;
    Wish *w = &g_wish[g_nwish++];
    memset(w, 0, sizeof *w);
    w->kind = (uint8_t)kind, w->type = (uint8_t)type, w->id = (uint16_t)id;
    Buf b = {w->name, 0, sizeof w->name};
    put(&b, name);
    for (int k = 0, j = 0; mats && k < n && j < 5; k++) {
        const uint8_t *m = mats + k * stride;
        if (rd16(m) && m[2]) w->item[j] = (uint16_t)rd16(m), w->qty[j] = m[2], j++;
    }
    return true;
}
static int wish_percent(const Wish *w) {
    int need = 0, got = 0;
    for (int k = 0; k < 5; k++) {
        if (!w->item[k]) continue;
        int h = g_have[w->item[k]];
        need += w->qty[k], got += h < w->qty[k] ? h : w->qty[k];
    }
    if (!need) return 100;
    return (int)((float)got * 100.0f / (float)need);
}
static void wish_popups(void) {  // (a word when an entry's materials are all there)
    static uint8_t done[WISHES];
    static int n_was;
    for (int i = 0; i < g_nwish; i++) {
        bool full = wish_percent(&g_wish[i]) >= 100;
        if (full && !done[i] && i < n_was) {
            char t[120];
            Buf b = {t, 0, sizeof t};
            put(&b, "위시리스트: "), put(&b, g_wish[i].name), put(&b, " 소재를 다 모았어요!");
            toast(t);
        }
        done[i] = full;
    }
    n_was = g_nwish;
}
static int wish_ready(void) {
    int n = 0;
    for (int i = 0; i < g_nwish; i++) n += wish_percent(&g_wish[i]) >= 100;
    return n;
}

// a weapon's materials to get it: made from scratch (no parent) or derived from its parent
static const uint8_t *weapon_mats(int w, int *n, int *stride, int *key) {
    if (g_w[w].parent >= 0) {
        const uint8_t *p = proc_of((uint32_t)w, 1);
        *n = 4, *stride = 3, *key = 0;
        return p ? p + 4 : NULL;
    }
    const uint8_t *c = create_of((uint32_t)w);
    *n = 4, *stride = 4, *key = 1;
    return c ? c + 6 : NULL;
}
static void wish_weapon(int w, bool path) {
    int added = 0, removed = 0;
    for (int x = w; x >= 0 && x < WMAX; x = path ? g_w[x].parent : -1) {
        int i = wish_find(0, g_type, x);
        if (!path && i >= 0) {
            wish_remove(i), removed++;
            break;
        }
        int n, st, key;
        const uint8_t *m = weapon_mats(x, &n, &st, &key);
        char nm[64];
        weapon_name(g_type, (uint32_t)x, 0, nm, sizeof nm);
        added += wish_add(0, g_type, x, nm, m, n, st);
    }
    char t[96];
    Buf b = {t, 0, sizeof t};
    if (removed) put(&b, "위시리스트에서 뺐어요");
    else if (added) put(&b, "위시리스트에 "), put_int(&b, added), put(&b, "개 담았어요");
    else put(&b, "이미 위시리스트에 있어요");
    toast(t);
}

// ------------------------------------------------------------------ the weapon screen
static int g_cw = -1, g_clv = 1;  // the cursor: a weapon, the level its card shows
static uint16_t g_game_id, g_game_cat;
static bool g_hidden, g_wish_open;
static float g_ncol[WMAX];  // a weapon's node: its column (its rarity's; right of its parent's at least)
static int g_ncols, g_ncol_type = -1;

static bool g_spoiler = true;  // (Y: weapons whose key materials were never found show as ?)
static bool weapon_locked(int w) {
    const uint8_t *c = create_of((uint32_t)w);
    if (c) {
        for (int k = 0; k < 4; k++)
            if (rd16(c + 6 + 4 * k) && c[9 + 4 * k] && !acquired(rd16(c + 6 + 4 * k))) return true;
        return false;
    }
    const uint8_t *p = proc_of((uint32_t)w, 1);
    return p && rd16(p + 4) && !acquired(rd16(p + 4));
}
static bool hidden_w(int w) { return g_spoiler && !owned(g_type + 7, (uint32_t)w) && weapon_locked(w); }

static void layout_nodes(void) {
    if (g_ncol_type == g_type) return;
    g_ncol_type = g_type, g_ncols = 1;
    for (int i = 0; i < g_nrows; i++) {
        int w = g_rows[i];
        float c = (float)(g_w[w].rare - 1);
        if (g_w[w].parent >= 0 && c < g_ncol[g_w[w].parent] + 0.6f) c = g_ncol[g_w[w].parent] + 0.6f;
        g_ncol[w] = c;
        if ((int)c + 1 > g_ncols) g_ncols = (int)c + 1;
    }
}

static void weapon_input(void) {
    int dir = pad_dir();
    const Weapon *x = &g_w[g_cw];
    if (dir == 3 || dir == 4) {
        int r = x->row + (dir == 3 ? -1 : 1);
        if (r >= 0 && r < g_nrows) g_cw = g_rows[r], g_clv = 1;
    } else if (dir == 2 && x->parent >= 0) g_cw = x->parent, g_clv = 1;
    else if (dir == 1)
        for (int i = x->row + 1; i < g_nrows; i++)
            if (g_w[g_rows[i]].parent == g_cw) { g_cw = g_rows[i], g_clv = 1; break; }
    if (pressed(B_L)) g_clv = g_clv >= g_w[g_cw].top ? 1 : g_clv + 1;  // (L: the card's level)
    if (pressed(B_ZR)) wish_weapon(g_cw, false);
    if (pressed(B_ZL)) wish_weapon(g_cw, true);
    if (pressed(B_Y)) g_spoiler = !g_spoiler, toast(g_spoiler ? "스포일러 방지: 켬 (열쇠 소재를 못 찾은 무기는 ?)" : "스포일러 방지: 끔 (전부 보기)");
}

static void draw_weapon_tree(float x0, float y0, float x1, float y1) {
    frame(x0, y0, x1, y1, COL_LIST);
    char line[96], nm[96];
    Buf b = {line, 0, sizeof line};
    float hy = y0 + g_fs * 0.35f, hh = g_fs * 1.25f;  // (the rarities' header)
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, hy - g_fs * 0.1f), V2(x1 - 3, hy + hh), COL_LIST_HEAD, 0, 0);
    float namew = (x1 - x0) * 0.30f, gx0 = x0 + namew, gx1 = x1 - g_fs, top = hy + hh + g_fs * 0.2f;
    float colw = (gx1 - gx0) / (float)(g_ncols < 5 ? 5 : g_ncols), rowh = g_fs * 2.35f, hs = g_fs * 0.8f;
    for (int r = 1; r <= g_ncols; r++) {
        float cx = gx0 + colw * ((float)r - 0.5f);
        b.n = 0, put(&b, r == g_ncols && colw < g_fs * 3.2f ? "" : "레어 "), put_int(&b, r);
        if (colw < g_fs * 3.2f) b.n = 0, put_int(&b, r);
        float tw = text_w(line, 0.72f);
        ImDrawList_AddRect(g_dl, V2(cx - tw * 0.5f - 4, hy + 2), V2(cx + tw * 0.5f + 4, hy + hh - 3), rare_colour(r), 0, 1.0f, 0);
        text_s(cx - tw * 0.5f, hy + g_fs * 0.22f, 0.72f, rare_colour(r), line);
        ImDrawList_AddLine(g_dl, V2(cx, top), V2(cx, y1 - 4), (rare_colour(r) & 0x00ffffff) | 0x60000000, 1.5f);
    }
    int vis = (int)((y1 - top) / rowh);
    static float scroll;
    float want = (float)g_w[g_cw].row - (float)vis * 0.45f;
    if (want > (float)(g_nrows - vis)) want = (float)(g_nrows - vis);
    if (want < 0) want = 0;
    scroll += (want - scroll) * 0.25f;
#define RY(r) (top + ((float)(r) - scroll) * rowh + rowh * 0.5f)
#define NX(w) (gx0 + colw * (g_ncol[w] + 0.5f))
    ImDrawList_PushClipRect(g_dl, V2(x0 + 3, top), V2(x1 - 3, y1 - 3), true);
    for (int i = 0; i < g_nrows; i++) {  // (the rows: a stripe, the name)
        int w = g_rows[i];
        float cy = RY(i);
        if (cy < top - rowh || cy > y1 + rowh) continue;
        bool sel = w == g_cw;
        ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, cy - rowh * 0.5f + 1), V2(x1 - 3, cy + rowh * 0.5f - 1),
                                 sel ? COL_SEL : (i & 1) ? 0x10ffffff : 0x00000000, 0, 0);
        if (sel) ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, cy - rowh * 0.5f + 1), V2(x0 + 7, cy + rowh * 0.5f - 1), COL_GOLD, 0, 0);
        weapon_name(g_type, (uint32_t)w, 0, nm, sizeof nm);
        ImDrawList_PushClipRect(g_dl, V2(x0 + 3, cy - rowh), V2(gx0 - g_fs * 0.3f, cy + rowh), true);
        text_o(x0 + g_fs * 0.8f, cy - g_fs * 0.68f, 1.3f, sel ? 0xffffffff : hidden_w(w) ? 0xff909090 : 0xffeeeeee, nm);
        ImDrawList_PopClipRect(g_dl);
    }
    for (int i = 0; i < g_nrows; i++) {  // (lines: down from the parent, across to the weapon, its level on a tag)
        int w = g_rows[i];
        if (g_w[w].parent < 0) continue;
        int p = g_w[w].parent;
        float px = NX(p), py = RY(g_w[p].row), cx = NX(w), cy = RY(i);
        if ((py < top - rowh && cy < top - rowh) || (py > y1 + rowh && cy > y1 + rowh)) continue;
        bool on = false;
        for (int x = g_cw; x >= 0; x = g_w[x].parent) on |= x == w;
        ImU32 c = on ? 0xff40d8ff : 0xffb8b8b8;
        float th = on ? 3.0f : 2.0f;
        ImDrawList_AddLine(g_dl, V2(px, py + hs), V2(px, cy), c, th);
        ImDrawList_AddLine(g_dl, V2(px, cy), V2(cx - hs, cy), c, th);
        if (g_w[w].from_lv) {  // (the tag: the level the parent needs)
            float tx = px + colw * 0.32f, t = g_fs * 0.42f;
            ImDrawList_AddRectFilled(g_dl, V2(tx - t, cy - t), V2(tx + t, cy + t), on ? 0xff30a8d8 : 0xff606060, 0, 0);
            ImDrawList_AddRectFilled(g_dl, V2(tx - t * 0.45f, cy - t * 1.45f), V2(tx + t * 0.45f, cy - t), on ? 0xff30a8d8 : 0xff606060, 0, 0);
            b.n = 0, put_int(&b, g_w[w].from_lv);
            text_s(tx - text_w(line, 0.75f) * 0.5f, cy - g_fs * 0.38f, 0.75f, 0xff101010, line);
        }
    }
    for (int i = 0; i < g_nrows; i++) {  // (the nodes)
        int w = g_rows[i];
        float cx = NX(w), cy = RY(i);
        if (cy < top - rowh || cy > y1 + rowh) continue;
        bool sel = w == g_cw;
        icon_cell(cx, cy, hs, sel, true);
        if (hidden_w(w)) text_o(cx - g_fs * 0.42f, cy - g_fs * 0.72f, 1.4f, 0xffd080f0, "?");
        else glyph_weapon(cx, cy, hs * 0.85f, rare_colour(g_w[w].rare));
        if (owned(g_type + 7, (uint32_t)w)) {  // (owned: a green check)
            ImDrawList_AddLine(g_dl, V2(cx + hs * 0.35f, cy - hs * 0.75f), V2(cx + hs * 0.6f, cy - hs * 0.45f), 0xff40e060, 3.0f);
            ImDrawList_AddLine(g_dl, V2(cx + hs * 0.6f, cy - hs * 0.45f), V2(cx + hs * 1.05f, cy - hs * 1.1f), 0xff40e060, 3.0f);
        }
        if (sel) ImDrawList_AddRectFilled(g_dl, V2(cx - hs, cy + hs + 3), V2(cx + hs, cy + hs + 6), 0xff40e060, 0, 0);
        if (wish_find(0, g_type, w) >= 0) ImDrawList_AddCircleFilled(g_dl, V2(cx + hs, cy - hs), g_fs * 0.22f, 0xff30d0ff, 10);
    }
    ImDrawList_PopClipRect(g_dl);
#undef RY
#undef NX
}

static void draw_weapon_card(float x0, float y0, float x1, float y1, int w, int lv, bool *can) {
    const Weapon *wp = &g_w[w];
    if (lv < 1) lv = 1;
    if (lv > wp->top) lv = wp->top;
    const uint8_t *b0 = base_of((uint32_t)w), *e = level_of((uint32_t)w, (uint32_t)lv);
    char line[200], nm[96];
    Buf b = {line, 0, sizeof line};
    float bh = g_fs * 2.6f;
    red_bar(x0, y0, x1, y0 + bh);
    glyph_weapon(x0 + g_fs * 1.9f, y0 + bh * 0.5f, g_fs * 0.7f, rare_colour(wp->rare));
    weapon_name(g_type, (uint32_t)w, name_tier((uint32_t)w, (uint32_t)lv), nm, sizeof nm);
    text_o(x0 + g_fs * 3.0f, y0 + g_fs * 0.25f, 1.05f, 0xffffffff, nm);
    b.n = 0, put(&b, "RARE "), put_int(&b, wp->rare);
    text_o(x0 + g_fs * 3.0f, y0 + g_fs * 1.35f, 0.85f, 0xfff0f0f0, line);
    b.n = 0, put(&b, "L  Lv "), put_int(&b, lv), put(&b, " / "), put_int(&b, wp->top);
    text_r(x1 - g_fs * 1.6f, y0 + g_fs * 0.3f, 0.85f, 0xfff0f0f0, line);
    bool own = owned(g_type + 7, (uint32_t)w);
    text_r(x1 - g_fs * 1.6f, y0 + g_fs * 1.35f, 0.85f, own ? 0xff70ff70 : 0xfff0f0f0, g_owned_ok ? (own ? "보유 중" : "미보유") : "");
    float y = y0 + bh + g_fs * 0.2f, x = x0 + g_fs * 0.6f, xr = x1 - g_fs * 0.6f, xv = x + (xr - x) * 0.36f;
    float sy = y;
    tan_box(x0, y, x1, y1);
    y += g_fs * 0.25f;
    if (e) {
        bool gun = gunner(g_type);
        text_o(x, y, 0.95f, COL_HEAD, "공격력");
        b.n = 0, put_int(&b, gun ? (int)rd16(e + 6) : (int)rd16(e + 8));
        text_o(xv, y, 0.95f, 0xfff4f4f4, line);
        if (!gun) {
            int aff = (int8_t)e[11];
            b.n = 0, put(&b, "회심률 "), put(&b, aff > 0 ? "+" : ""), put_int(&b, aff), put(&b, "%");
            text_r(xr, y, 0.95f, aff < 0 ? 0xff6060ff : aff > 0 ? 0xff60f060 : 0xfff4f4f4, line);
        }
        y += g_fs * 1.2f;
        if (!gun) {
            text_o(x, y, 0.95f, COL_HEAD, "예리도");
            sharpness_bar(xv, y + g_fs * 0.25f, xr - xv, g_fs * 0.55f, e[6], e[7]);
            y += g_fs * 1.2f;
            text_o(x, y, 0.95f, COL_HEAD, "슬롯");
            slots_draw(xv, y + g_fs * 0.55f, e[LEVEL_STRIDE[g_type] - 1]);
            int el = b0 ? b0[0x11] : 0;
            if (el > 0 && el < 10 && e[12]) {
                b.n = 0, put(&b, ELEMENT[el]), put(&b, " "), put_int(&b, e[12] * 10);
                text_r(xr, y, 0.95f, ELEMENT_COL[el], line);
            }
            y += g_fs * 1.2f;
            if (e[10]) {
                b.n = 0, put(&b, "방어력 +"), put_int(&b, e[10]);
                text_o(x, y, 0.95f, 0xfff4f4f4, line), y += g_fs * 1.2f;
            }
        }
    }
    tan_rule(x0 + 4, x1 - 4, y), y += g_fs * 0.2f;
    // what it takes: made from scratch, derived from its parent, the level shown from the one before
    bool ok = g_have_ok;
    const uint8_t *c = create_of((uint32_t)w);
    if (lv == 1 && c) {
        b.n = 0, put(&b, "처음부터 생산");
        if (b0) put(&b, " - "), put_int(&b, (int)rd32(b0 + 0xc)), put(&b, "z");
        text_o(x, y, 0.95f, COL_HEAD, line), y += g_fs * 1.2f;
        y = mat_list(x, xr, y, c + 6, 4, 4, 1, wp->parent >= 0 ? NULL : &ok);
    }
    if (lv == 1 && wp->parent >= 0) {
        const uint8_t *p = proc_of((uint32_t)w, 1);
        weapon_name(g_type, (uint32_t)wp->parent, name_tier((uint32_t)wp->parent, wp->from_lv), nm, sizeof nm);
        b.n = 0, put(&b, "이전 무기에서 강화 - "), put(&b, nm), put(&b, " Lv"), put_int(&b, wp->from_lv);
        text_o(x, y, 0.95f, COL_HEAD, line), y += g_fs * 1.2f;
        if (p) y = mat_list(x, xr, y, p + 4, 4, 3, 0, &ok);
    }
    if (lv > 1) {
        const uint8_t *p = proc_of((uint32_t)w, (uint32_t)lv);
        weapon_name(g_type, (uint32_t)w, name_tier((uint32_t)w, (uint32_t)lv), nm, sizeof nm);
        b.n = 0, put(&b, "레벨 업: "), put(&b, nm), put(&b, " "), put_int(&b, lv);
        text_o(x, y, 0.95f, COL_HEAD, line), y += g_fs * 1.2f;
        if (p) y = mat_list(x, xr, y, p + 4, 4, 3, 0, &ok);
    }
    if (can) *can = ok;
    if (hidden_w(w)) {
        b.n = 0, put(&b, "잠김: ");
        if (wp->parent >= 0 && !create_of((uint32_t)w)) {
            weapon_name(g_type, (uint32_t)wp->parent, 0, nm, sizeof nm);
            put(&b, nm), put(&b, " Lv"), put_int(&b, wp->from_lv), put(&b, " 이상 + 열쇠 소재 필요");
        } else put(&b, "열쇠 소재(*)를 아직 못 찾았어요");
        text_o(x, y, 0.9f, 0xffd080f0, line), y += g_fs * 1.2f;
    } else if (ok && g_have_ok) text_o(x, y, 0.95f, 0xff40e0ff, "지금 만들 수 있어요"), y += g_fs * 1.2f;
    tan_rule(x0 + 4, x1 - 4, y), y += g_fs * 0.2f;
    // its names by level
    int s1 = wp->s1, top = wp->top;
    for (int k = 0; k < 3 && y < y1 - g_fs; k++) {
        int a = k == 0 ? 1 : k == 1 ? s1 : top, z = k == 0 ? (s1 ? s1 - 1 : top - 1) : k == 1 ? top - 1 : top;
        if (k == 1 && (!s1 || s1 > top - 1)) continue;
        if (k == 2 && top < 2) continue;
        if (k == 0 && z < 1) z = 1;
        weapon_name(g_type, (uint32_t)w, k, nm, sizeof nm);
        bool here = name_tier((uint32_t)w, (uint32_t)lv) == k;
        b.n = 0, put(&b, "Lv"), put_int(&b, a);
        if (z > a) put(&b, "-"), put_int(&b, z);
        put(&b, "  "), put(&b, nm);
        text_o(x, y, 0.85f, here ? COL_HEAD : 0xffe4e4e4, line), y += g_fs * 1.05f;
    }
    (void)sy;
    frame(x0, y0, x1, y1, 0);
}

// ------------------------------------------------------------------ the armor screen
static int g_arank = -1, g_arow, g_apiece;  // rank tab (0 low 1 high 2 G), the row in it, the piece
static uint16_t g_arow_of[AMAX];
static int g_narank;
static int rank_of(uint32_t s) {
    const uint8_t *a = aser(s);
    int r = a ? a[0x67] + 1 : 1;
    return r <= 3 ? 0 : r <= 7 ? 1 : 2;
}
static void armor_rows(int rank) {
    g_narank = 0;
    for (int i = 0; i < g_narows; i++)
        if (rank_of(g_arows[i]) == rank) g_arow_of[g_narank++] = g_arows[i];
}
static void armor_from_game(void) {
    static uint16_t from_id, from_cat;
    if (g_arank >= 0 && g_now.id == from_id && g_now.category == from_cat) return;
    from_id = g_now.id, from_cat = g_now.category;
    g_arank = rank_of(g_now.id), g_apiece = g_now.category >= 1 && g_now.category <= 5 ? g_now.category - 1 : 0, g_arow = 0;
    armor_rows(g_arank);
    for (int i = 0; i < g_narank; i++)
        if (g_arow_of[i] == g_now.id) g_arow = i;
}
static void armor_input(void) {
    int dir = pad_dir();
    if (dir == 1 && g_apiece < 4) g_apiece++;
    if (dir == 2 && g_apiece > 0) g_apiece--;
    if (dir == 3 && g_arow > 0) g_arow--;
    if (dir == 4 && g_arow + 1 < g_narank) g_arow++;
    if (pressed(B_L)) {  // (L: the rank tabs)
        g_arank = (g_arank + 1) % 3, g_arow = 0;
        armor_rows(g_arank);
    }
    uint32_t s = g_narank ? g_arow_of[g_arow] : 0;
    char nm[64];
    if (s && (pressed(B_ZR) || pressed(B_ZL))) {
        bool set = pressed(B_ZL);
        int added = 0, removed = 0;
        for (int p = set ? 0 : g_apiece; p <= (set ? 4 : g_apiece); p++) {
            const uint8_t *c = acreate(s, p);
            if (!c) continue;
            int i = wish_find(1, p, (int)s);
            if (!set && i >= 0) { wish_remove(i), removed++; continue; }
            armor_name(s, p, nm, sizeof nm);
            added += wish_add(1, p, (int)s, nm, c + 10, 5, 4);
        }
        char t[96];
        Buf b = {t, 0, sizeof t};
        if (removed) put(&b, "위시리스트에서 뺐어요");
        else if (added) put(&b, "위시리스트에 "), put_int(&b, added), put(&b, "개 담았어요");
        else put(&b, "이미 위시리스트에 있어요");
        toast(t);
    }
}

static void draw_armor_grid(float x0, float y0, float x1, float y1) {
    frame(x0, y0, x1, y1, COL_LIST);
    static const char *const RANK[3] = {"하위", "상위", "G급"};
    float ty = y0 + g_fs * 0.35f, th = g_fs * 1.3f;
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, ty - g_fs * 0.1f), V2(x1 - 3, ty + th), COL_LIST_HEAD, 0, 0);
    for (int r = 0; r < 3; r++) {
        float tx = x0 + g_fs * 0.6f + r * g_fs * 5.2f;
        bool on = r == g_arank;
        ImDrawList_AddRectFilled(g_dl, V2(tx, ty + 2), V2(tx + g_fs * 4.8f, ty + th - 2), on ? 0xff406a28 : 0xff3a2a1e, 0, 0);
        ImDrawList_AddRect(g_dl, V2(tx, ty + 2), V2(tx + g_fs * 4.8f, ty + th - 2), on ? 0xff60d060 : 0xff806040, 0, 1.0f, 0);
        text_s(tx + g_fs * 0.5f, ty + g_fs * 0.2f, 0.85f, 0xfff0f0f0, RANK[r]);
    }
    text_s(x1 - g_fs * 4.5f, ty + g_fs * 0.2f, 0.8f, 0xffc0c0c0, "L: 랭크");
    float top = ty + th + g_fs * 0.2f, rowh = g_fs * 2.45f, namew = (x1 - x0) * 0.42f, gx = x0 + namew,
          cw = (x1 - g_fs * 0.6f - gx) / 5.0f, hs = g_fs * 0.9f;
    int vis = (int)((y1 - top) / rowh);
    static float scroll;
    float want = (float)g_arow - (float)vis * 0.45f;
    if (want > (float)(g_narank - vis)) want = (float)(g_narank - vis);
    if (want < 0) want = 0;
    scroll += (want - scroll) * 0.25f;
    char nm[96];
    ImDrawList_PushClipRect(g_dl, V2(x0 + 3, top), V2(x1 - 3, y1 - 3), true);
    for (int i = 0; i < g_narank; i++) {
        float cy = top + ((float)i - scroll) * rowh + rowh * 0.5f;
        if (cy < top - rowh || cy > y1 + rowh) continue;
        uint32_t s = g_arow_of[i];
        bool sel = i == g_arow;
        ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, cy - rowh * 0.5f + 1), V2(x1 - 3, cy + rowh * 0.5f - 1),
                                 sel ? COL_SEL : (i & 1) ? 0x10ffffff : 0x00000000, 0, 0);
        int first = 0;
        while (first < 4 && !acreate(s, first)) first++;
        armor_name(s, first, nm, sizeof nm);
        ImDrawList_PushClipRect(g_dl, V2(x0 + 3, cy - rowh), V2(gx - g_fs * 0.3f, cy + rowh), true);
        text_o(x0 + g_fs * 0.8f, cy - g_fs * 0.62f, 1.15f, 0xffeeeeee, nm);
        ImDrawList_PopClipRect(g_dl);
        const uint8_t *a = aser(s);
        int rare = a ? a[0x67] + 1 : 1;
        for (int p = 0; p < 5; p++) {
            float cx = gx + cw * ((float)p + 0.5f);
            if (!acreate(s, p)) {
                ImDrawList_AddLine(g_dl, V2(cx - hs * 0.5f, cy), V2(cx + hs * 0.5f, cy), 0x80c0c0c0, 2.0f);
                continue;
            }
            bool on = sel && p == g_apiece;
            icon_cell(cx, cy, hs, on, true);
            glyph_armor(cx, cy, hs * 0.85f, p, rare_colour(rare));
            if (owned(p + 1, s)) {
                ImDrawList_AddLine(g_dl, V2(cx + hs * 0.35f, cy - hs * 0.75f), V2(cx + hs * 0.6f, cy - hs * 0.45f), 0xff40e060, 3.0f);
                ImDrawList_AddLine(g_dl, V2(cx + hs * 0.6f, cy - hs * 0.45f), V2(cx + hs * 1.05f, cy - hs * 1.1f), 0xff40e060, 3.0f);
            }
            if (wish_find(1, p, (int)s) >= 0) ImDrawList_AddCircleFilled(g_dl, V2(cx + hs, cy - hs), g_fs * 0.22f, 0xff30d0ff, 10);
        }
    }
    ImDrawList_PopClipRect(g_dl);
}

static void draw_armor_card(float x0, float y0, float x1, float y1) {
    uint32_t s = g_narank ? g_arow_of[g_arow] : 0;
    const uint8_t *a = aser(s);
    if (!a) return;
    int p = g_apiece;
    char line[160], nm[96];
    Buf b = {line, 0, sizeof line};
    const uint8_t *t = a + 0x67, *c = acreate(s, p);
    int rare = t[0] + 1, def = t[3], alv = t[1], max = def;
    if (t[2] < g_nabuild)
        for (int k = 0; k + 1 < alv && k < 20; k++) max += g_abuild[8 + t[2] * 20 + k];
    float bh = g_fs * 2.6f;
    red_bar(x0, y0, x1, y0 + bh);
    glyph_armor(x0 + g_fs * 1.9f, y0 + bh * 0.5f, g_fs * 0.75f, p, rare_colour(rare));
    armor_name(s, p, nm, sizeof nm);
    text_o(x0 + g_fs * 3.0f, y0 + g_fs * 0.25f, 1.05f, 0xffffffff, c ? nm : "-");
    b.n = 0, put(&b, "RARE "), put_int(&b, rare);
    text_o(x0 + g_fs * 3.0f, y0 + g_fs * 1.35f, 0.85f, 0xfff0f0f0, line);
    if (g_owned_ok) text_r(x1 - g_fs * 1.6f, y0 + g_fs * 1.35f, 0.85f, owned(p + 1, s) ? 0xff70ff70 : 0xfff0f0f0, owned(p + 1, s) ? "보유 중" : "미보유");
    float y = y0 + bh + g_fs * 0.2f, x = x0 + g_fs * 0.6f, xr = x1 - g_fs * 0.6f;
    tan_box(x0, y, x1, y1);
    y += g_fs * 0.25f;
    b.n = 0, put(&b, "방어력 "), put_int(&b, def), put(&b, " - "), put_int(&b, max);
    text_o(x, y, 0.95f, 0xfff4f4f4, line);
    text_o(x + (xr - x) * 0.55f, y, 0.95f, COL_HEAD, "슬롯");
    slots_draw(x + (xr - x) * 0.55f + g_fs * 2.2f, y + g_fs * 0.55f, t[5 + p]);
    y += g_fs * 1.25f;
    static const char *const RES[5] = {"불", "물", "번개", "얼음", "용"};
    static const ImU32 RES_COL[5] = {0xff3c6cff, 0xffffa040, 0xff30e0ff, 0xfff0e0a0, 0xffd060c0};
    for (int r = 0; r < 5; r++) {
        int v = (int8_t)a[0x17 + r];
        b.n = 0, put(&b, RES[r]), put(&b, " "), put(&b, v > 0 ? "+" : ""), put_int(&b, v);
        text_o(x + (xr - x) * 0.2f * (float)r, y, 0.9f, RES_COL[r], line);
    }
    y += g_fs * 1.25f;
    text_o(x, y, 0.95f, COL_HEAD, "스킬"), y += g_fs * 1.15f;
    int shown = 0;
    for (int k = 0; k < 5; k++) {
        const uint8_t *q = a + 0x1c + 15 * p + 3 * k;
        int pts = (int8_t)q[2];
        if (!rd16(q) || !pts) continue;
        skill_name(rd16(q), nm, sizeof nm);
        b.n = 0, put(&b, nm), put(&b, " "), put(&b, pts > 0 ? "+" : ""), put_int(&b, pts);
        text_o(x, y, 0.95f, pts > 0 ? 0xff90f090 : 0xff6060ff, line), y += g_fs * 1.15f;
        shown++;
    }
    if (!shown) text_o(x, y, 0.9f, 0xffe0e0e0, "(없음)"), y += g_fs * 1.15f;
    text_o(x, y, 0.95f, COL_HEAD, "생산 소재"), y += g_fs * 1.15f;
    if (c) y = mat_list(x, xr, y, c + 10, 5, 4, 1, NULL);
    frame(x0, y0, x1, y1, 0);
}

// ------------------------------------------------------------------ the wish list window (R)
static int g_wish_tab, g_wish_sel;
static void wish_input(void) {
    int dir = pad_dir();
    if (pressed(B_L) || pressed(B_ZL)) g_wish_tab = 0;
    if (pressed(B_ZR)) g_wish_tab = 1;
    if (dir == 3 && g_wish_sel > 0) g_wish_sel--;
    if (dir == 4 && g_wish_sel + 1 < g_nwish) g_wish_sel++;
    if (pressed(B_X) && g_wish_tab == 0) {
        wish_remove(g_wish_sel);
        if (g_wish_sel >= g_nwish && g_wish_sel) g_wish_sel--;
    }
    if (pressed(B_B) || pressed(B_R)) g_wish_open = false;
}
static void wish_row(float x0, float x1, float y, float h, bool sel) {
    if (sel) red_bar(x0, y, x1, y + h);
    else tan_box(x0, y, x1, y + h);
}
static void draw_wishlist(float W, float H) {
    float x0 = W * 0.155f, x1 = W * 0.845f, y0 = H * 0.055f, y1 = H * 0.935f;
    ImDrawList_AddRectFilled(g_dl, V2(0, 0), V2(W, H), 0x90000000, 0, 0);
    frame(x0, y0, x1, y1, COL_TAN);
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 3, y0 + 3), V2(x1 - 3, y0 + g_fs * 1.5f), COL_LIST_HEAD, 0, 0);
    text_o((x0 + x1) * 0.5f - text_w("위시리스트", 1.0f) * 0.5f, y0 + g_fs * 0.2f, 1.0f, 0xfff0f0f0, "위시리스트");
    float tw = (x1 - x0 - g_fs * 4) * 0.5f, ty = y0 + g_fs * 2.0f, th = g_fs * 2.0f;
    for (int k = 0; k < 2; k++) {
        float tx = x0 + g_fs * 2 + tw * (float)k;
        wish_row(tx, tx + tw - g_fs * 0.3f, ty, th, g_wish_tab == k);
        const char *l = k ? "필요 소재 합계" : "위시리스트 확인";
        text_o(tx + (tw - text_w(l, 1.0f)) * 0.5f, ty + g_fs * 0.45f, 1.0f, 0xfff0f0f0, l);
    }
    text_s(x0 + g_fs * 0.4f, ty + g_fs * 0.5f, 0.8f, 0xffffffff, "ZL");
    text_s(x1 - g_fs * 1.6f, ty + g_fs * 0.5f, 0.8f, 0xffffffff, "ZR");
    char line[160], nm[96];
    Buf b = {line, 0, sizeof line};
    float y = ty + th + g_fs * 0.8f, rh = g_fs * 2.2f;
    if (g_wish_tab == 0) {
        float lx0 = x0 + g_fs * 1.2f, lx1 = x0 + (x1 - x0) * 0.47f, rx0 = lx1 + g_fs, rx1 = x1 - g_fs * 1.2f;
        ImDrawList_AddRectFilled(g_dl, V2(lx0, y), V2(lx1, y + g_fs * 1.4f), COL_LIST_HEAD, 0, 0);
        text_o(lx0 + g_fs * 0.5f, y + g_fs * 0.2f, 0.95f, 0xfff0f0f0, "장비 목록");
        ImDrawList_AddRectFilled(g_dl, V2(rx0, y), V2(rx1, y + g_fs * 1.4f), COL_LIST_HEAD, 0, 0);
        text_o(rx0 + g_fs * 0.5f, y + g_fs * 0.2f, 0.95f, 0xfff0f0f0, "필요 소재");
        y += g_fs * 1.9f;
        if (!g_nwish) text_o(lx0, y, 1.0f, 0xffd0d0d0, "비어 있어요: 트리에서 ZR(하나) / ZL(경로 전체)로 담아요");
        int vis = (int)((y1 - g_fs * 3 - y) / rh), first = g_wish_sel - vis + 1 < 0 ? 0 : g_wish_sel - vis + 1;
        for (int i = first; i < g_nwish && i < first + vis; i++) {
            float ry = y + (float)(i - first) * rh;
            bool sel = i == g_wish_sel;
            wish_row(lx0, lx1, ry, rh - g_fs * 0.25f, sel);
            b.n = 0, put_int(&b, i + 1);
            text_o(lx0 + g_fs * 0.8f, ry + g_fs * 0.4f, 0.95f, 0xfff0f0f0, line);
            const Wish *w = &g_wish[i];
            if (w->kind) glyph_armor(lx0 + g_fs * 2.6f, ry + rh * 0.45f, g_fs * 0.6f, w->type, 0xff60d0a0);
            else glyph_weapon_t(w->type, lx0 + g_fs * 2.6f, ry + rh * 0.45f, g_fs * 0.6f, 0xffe0e0e0);
            text_o(lx0 + g_fs * 3.6f, ry + g_fs * 0.4f, 0.95f, 0xfff0f0f0, w->name);
            int pc = wish_percent(w);
            b.n = 0, put_int(&b, pc), put(&b, "%");
            text_r(lx1 - g_fs * 1.2f, ry + g_fs * 0.4f, 0.95f, pc >= 100 ? COL_HAVE : 0xfff0f0f0, line);
        }
        if (g_wish_sel < g_nwish) {
            const Wish *w = &g_wish[g_wish_sel];
            for (int k = 0; k < 5; k++) {
                if (!w->item[k]) continue;
                tan_box(rx0, y, rx1, y + rh - g_fs * 0.25f);
                item_name(w->item[k], nm, sizeof nm);
                int h = g_have[w->item[k]];
                text_o(rx0 + g_fs * 0.8f, y + g_fs * 0.4f, 0.95f, h >= w->qty[k] ? 0xfff0f0f0 : COL_LACK, nm);
                b.n = 0, put_int(&b, h), put(&b, " / "), put_int(&b, w->qty[k]);
                text_r(rx1 - g_fs * 0.8f, y + g_fs * 0.4f, 0.95f, h >= w->qty[k] ? COL_HAVE : COL_LACK, line);
                y += rh;
            }
        }
    } else {  // (all the materials the list needs, summed)
        static uint16_t item[64], need[64];
        int n = 0;
        for (int i = 0; i < g_nwish; i++)
            for (int k = 0; k < 5; k++) {
                uint16_t it = g_wish[i].item[k];
                if (!it) continue;
                int j = 0;
                while (j < n && item[j] != it) j++;
                if (j == n && n < 64) item[n] = it, need[n] = 0, n++;
                if (j < n) need[j] = (uint16_t)(need[j] + g_wish[i].qty[k]);
            }
        float lx0 = x0 + g_fs * 1.2f, lx1 = x0 + (x1 - x0) * 0.62f;
        text_o(lx0, y, 1.0f, COL_HEAD, "필요 소재 합계"), y += g_fs * 1.6f;
        int vis = (int)((y1 - g_fs * 3 - y) / rh);
        if (g_wish_sel > n - 1) g_wish_sel = n ? n - 1 : 0;
        int first = g_wish_sel - vis + 1 < 0 ? 0 : g_wish_sel - vis + 1;
        for (int j = first; j < n && j < first + vis; j++) {
            float ry = y + (float)(j - first) * rh;
            tan_box(lx0, ry, lx1, ry + rh - g_fs * 0.25f);
            item_name(item[j], nm, sizeof nm);
            int h = g_have[item[j]];
            text_o(lx0 + g_fs * 0.8f, ry + g_fs * 0.4f, 0.95f, h >= need[j] ? 0xfff0f0f0 : COL_LACK, nm);
            b.n = 0, put_int(&b, h), put(&b, " / "), put_int(&b, need[j]);
            text_r(lx1 - g_fs * 0.8f, ry + g_fs * 0.4f, 0.95f, h >= need[j] ? COL_HAVE : COL_LACK, line);
        }
    }
    text_o(x0 + g_fs * 2, y1 - g_fs * 1.8f, 0.95f, 0xfff0f0f0, "R / B: 닫기      X: 빼기      방향키: 선택");
}

// ------------------------------------------------------------------ the bottom: buttons' help, the wish list button
static void key_hint(float x, float y, const char *key, const char *what, float *xo) {
    float kw = text_w(key, 0.8f) + g_fs * 0.6f;
    ImDrawList_AddRectFilled(g_dl, V2(x, y), V2(x + kw, y + g_fs * 1.2f), 0xff101010, 0, 0);
    ImDrawList_AddRect(g_dl, V2(x, y), V2(x + kw, y + g_fs * 1.2f), 0xffd0d0d0, 0, 1.0f, 0);
    text_s(x + g_fs * 0.3f, y + g_fs * 0.15f, 0.8f, 0xffffffff, key);
    text_o(x + kw + g_fs * 0.3f, y + g_fs * 0.1f, 0.85f, 0xfff0f0f0, what);
    *xo = x + kw + g_fs * 0.6f + text_w(what, 0.85f) + g_fs * 1.2f;
}
static void bottom(float W, float H, bool weapons) {
    float y = H * 0.885f, x = W * 0.014f;
    ImDrawList_AddRectFilled(g_dl, V2(x - 4, y - 4), V2(W * 0.75f, y + g_fs * 1.35f), 0xc0000000, 0, 0);
    key_hint(x, y, "십자/스틱", "커서 이동", &x);
    key_hint(x, y, "A", "생산 / 강화", &x);
    key_hint(x, y, "ZL", weapons ? "경로 전체 담기" : "세트 전체 담기", &x);
    key_hint(x, y, "ZR", "위시리스트 담기", &x);
    key_hint(x, y, "L", weapons ? "레벨" : "랭크", &x);
    if (weapons) key_hint(x, y, "Y", g_spoiler ? "전부 보기" : "스포일러 방지", &x);
    key_hint(x, y, "B", "뒤로", &x);
    // the wish list button (the game's message box on the left: right of it)
    float bx0 = W * 0.37f, bx1 = W * 0.50f, by0 = H * 0.79f, by1 = by0 + g_fs * 2.2f;
    tan_box(bx0, by0, bx1, by1);
    char line[64];
    Buf b = {line, 0, sizeof line};
    put(&b, "위시리스트 ("), put_int(&b, g_nwish), put(&b, ")");
    int ready = wish_ready();
    if (ready) put(&b, " - "), put_int(&b, ready), put(&b, " 가능");
    float kx = bx0 + g_fs * 0.6f;
    ImDrawList_AddRectFilled(g_dl, V2(kx, by0 + g_fs * 0.5f), V2(kx + g_fs * 1.4f, by0 + g_fs * 1.7f), 0xff101010, 0, 0);
    text_s(kx + g_fs * 0.35f, by0 + g_fs * 0.6f, 0.8f, 0xffffffff, "R");
    text_o(kx + g_fs * 2.0f, by0 + g_fs * 0.55f, 0.95f, ready ? COL_HAVE : 0xfff0f0f0, line);
}


// ------------------------------------------------------------------ the confirmation (the reference's compare screen):
// before >> after, the cost, Yes / No. Yes is the game's own crafting: the screen presses the game's A on its list (the
// list's cursor is on the weapon), then its Yes, then lets its messages go - the game makes it, takes the materials.
void mhxx_input_press(uint32_t buttons, uint32_t ms);
static int g_conf, g_conf_yes, g_conf_presses, g_conf_kind, g_conf_id, g_conf_piece;
static uint16_t g_conf_state;
static float g_conf_t;
static bool g_conf_had;

// one window in MHXX's own style (its blue column heads "필요 소재 이름 / 소지수/필요수"), the 3D preview left seen
static void draw_confirm(float W, float H) {
    ImDrawList_AddRectFilled(g_dl, V2(W * 0.262f, H * 0.1f), V2(W, H * 0.9f), 0xb0000000, 0, 0);
    float x0 = W * 0.30f, x1 = W * 0.80f, y0 = H * 0.20f, y1 = H * 0.72f;
    frame(x0, y0, x1, y1, COL_TAN);
    char nm[96], line[160];
    Buf b = {line, 0, sizeof line};
    float x = x0 + g_fs * 1.0f, xr = x1 - g_fs * 1.0f, y = y0 + g_fs * 0.7f;
    // the title row: what is made
    int rare = 1, atk = -1, slots = 0, sharp = -1, sharp_step = 0, cost = -1, piece = g_conf_piece;
    const uint8_t *mats = NULL;
    int n = 0, stride = 4;
    if (g_conf_kind == 0) {
        const uint8_t *e1 = level_of((uint32_t)g_conf_id, 1), *b0 = base_of((uint32_t)g_conf_id), *c = create_of((uint32_t)g_conf_id);
        bool gun = gunner(g_type);
        rare = g_w[g_conf_id].rare, atk = e1 ? (int)(gun ? rd16(e1 + 6) : rd16(e1 + 8)) : -1;
        slots = e1 && !gun ? e1[LEVEL_STRIDE[g_type] - 1] : 0, sharp = e1 && !gun ? e1[6] : -1, sharp_step = e1 ? e1[7] : 0;
        cost = b0 ? (int)rd32(b0 + 0xc) : -1, mats = c ? c + 6 : NULL, n = 4;
        weapon_name(g_type, (uint32_t)g_conf_id, 0, nm, sizeof nm);
    } else {
        const uint8_t *a = aser((uint32_t)g_conf_id), *c = acreate((uint32_t)g_conf_id, piece);
        rare = a ? a[0x67] + 1 : 1, atk = a ? a[0x6a] : -1, slots = a ? a[0x6c + piece] : 0;
        mats = c ? c + 10 : NULL, n = 5;
        armor_name((uint32_t)g_conf_id, piece, nm, sizeof nm);
    }
    float ih = g_fs * 1.4f;
    ImDrawList_AddRectFilled(g_dl, V2(x - g_fs * 0.3f, y - g_fs * 0.2f), V2(x + ih * 2, y + ih * 1.6f), 0xff101010, 0, 0);
    if (g_conf_kind == 0) glyph_weapon(x + ih, y + ih * 0.7f, ih * 0.7f, rare_colour(rare));
    else glyph_armor(x + ih, y + ih * 0.7f, ih * 0.7f, piece, rare_colour(rare));
    text_o(x + ih * 2.5f, y - g_fs * 0.1f, 1.25f, 0xffffffff, nm);
    b.n = 0, put(&b, "RARE "), put_int(&b, rare), put(&b, "   LV 1");
    text_o(x + ih * 2.5f, y + g_fs * 1.35f, 0.9f, rare_colour(rare), line);
    y += ih * 2.0f;
    ImDrawList_AddLine(g_dl, V2(x0 + 4, y), V2(x1 - 4, y), COL_TEAL, 2.0f);
    y += g_fs * 0.4f;
    // stats, as the game's info panel: blue names, white values
    ImU32 blue = 0xffffa040;
    text_o(x, y, 1.0f, blue, g_conf_kind == 0 ? "공격력" : "방어력");
    b.n = 0, put_int(&b, atk);
    text_o(x + g_fs * 5.0f, y, 1.0f, 0xffffffff, line);
    text_o(x + (xr - x) * 0.5f, y, 1.0f, blue, "슬롯");
    slots_draw(x + (xr - x) * 0.5f + g_fs * 2.6f, y + g_fs * 0.6f, slots);
    y += g_fs * 1.4f;
    if (sharp >= 0) {
        text_o(x, y, 1.0f, blue, "예리도");
        sharpness_bar(x + g_fs * 5.0f, y + g_fs * 0.35f, (xr - x) * 0.6f, g_fs * 0.55f, (uint32_t)sharp, (uint32_t)sharp_step);
        y += g_fs * 1.4f;
    }
    ImDrawList_AddLine(g_dl, V2(x0 + 4, y), V2(x1 - 4, y), COL_TEAL, 2.0f);
    y += g_fs * 0.4f;
    // the materials: the game's columns, have / need
    text_o(x, y, 1.0f, blue, "필요 소재 이름");
    text_r(xr, y, 1.0f, blue, "소지수/필요수");
    y += g_fs * 1.35f;
    for (int k = 0; mats && k < n; k++) {
        const uint8_t *m = mats + k * stride;
        uint32_t it = rd16(m), q = m[2];
        if (!it || !q) continue;
        int r = it < ITEMS ? g_item_rare[it] : 1;
        ImDrawList_AddRectFilled(g_dl, V2(x, y + g_fs * 0.2f), V2(x + g_fs * 0.8f, y + g_fs), rare_colour(r), g_fs * 0.1f, 0);
        item_name(it, nm, sizeof nm);
        text_o(x + g_fs * 1.2f, y, 1.0f, 0xffffffff, nm);
        uint32_t have = g_have[it];
        b.n = 0, put_int(&b, (int)have), put(&b, " / "), put_int(&b, (int)q);
        text_r(xr, y, 1.0f, have >= q ? 0xffffffff : COL_LACK, line);
        y += g_fs * 1.3f;
        ImDrawList_AddLine(g_dl, V2(x, y - g_fs * 0.1f), V2(xr, y - g_fs * 0.1f), 0x40ffffff, 1.0f);
    }
    // the cost and Yes / No, at the window's foot
    float fy = y1 - g_fs * 2.6f;
    ImDrawList_AddRectFilled(g_dl, V2(x0 + 4, fy), V2(x1 - 4, y1 - 4), 0xff1c140c, 0, 0);
    if (cost >= 0) {
        b.n = 0, put(&b, "비용 "), put_int(&b, cost), put(&b, "z");
        text_o(x, fy + g_fs * 0.7f, 1.05f, COL_GOLD, line);
    }
    if (g_conf == 1) {
        static const char *const L[2] = {"생산한다", "그만둔다"};
        for (int i = 0; i < 2; i++) {
            bool on = (i == 0) == (g_conf_yes != 0);
            float bx1 = xr - (float)(1 - i) * g_fs * 7.4f, bx0 = bx1 - g_fs * 6.8f;
            ImDrawList_AddRectFilled(g_dl, V2(bx0, fy + g_fs * 0.4f), V2(bx1, fy + g_fs * 2.1f), on ? COL_RED : 0xff3a3a3a, 0, 0);
            if (on) ImDrawList_AddRect(g_dl, V2(bx0, fy + g_fs * 0.4f), V2(bx1, fy + g_fs * 2.1f), COL_GOLD, 0, 2.0f, 0);
            text_o(bx0 + (bx1 - bx0 - text_w(L[i], 1.0f)) * 0.5f, fy + g_fs * 0.7f, 1.0f, on ? 0xffffffff : 0xffb0b0b0, L[i]);
        }
    } else text_r(xr, fy + g_fs * 0.7f, 1.05f, 0xffffffff, "생산하는 중...");
}

static void conf_log(const char *what, uint16_t state) {
    char l[120];
    Buf b = {l, 0, sizeof l};
    put(&b, "[SmithyTree] crafting: "), put(&b, what), put(&b, " (the smithy's screen "), put_int(&b, state >> 8), put(&b, "/"),
        put_int(&b, state & 0xff), put(&b, ")");
    log_line(l);
}

// Runs the confirmation; true while it has the screen.
static bool confirm_step(float W, float H, uint16_t state) {
    if (!g_conf) return false;
    if (g_conf == 1) {
        int dir = pad_dir();
        if (dir == 1 || dir == 2) g_conf_yes = !g_conf_yes;
        if (pressed(B_B) || (pressed(B_A) && !g_conf_yes)) g_conf = 0;
        else if (pressed(B_A)) {
            g_conf_state = state, g_conf_t = g_time, g_conf = 2, g_conf_presses = 0;
            g_conf_had = g_conf_kind == 0 ? owned(g_type + 7, (uint32_t)g_conf_id) : owned(g_conf_piece + 1, (uint32_t)g_conf_id);
            mhxx_input_press(1u << 0, 80);  // (the game's A on its list: its own confirmation)
            conf_log("A on the game's list", state);
        }
    } else if (g_conf == 2 && g_time - g_conf_t >= 0.5f) {
        conf_log(state != g_conf_state ? "its confirmation is up: Yes" : "no new screen for its confirmation: A (Yes)", state);
        mhxx_input_press(1u << 0, 80);  // (its Yes)
        g_conf = 3, g_conf_t = g_time;
    } else if (g_conf == 3) {
        if (state == g_conf_state && g_time - g_conf_t >= 0.8f) {
            g_equip_at = 0;  // (read it now: what was just made)
            read_equip();
            bool now = g_conf_kind == 0 ? owned(g_type + 7, (uint32_t)g_conf_id) : owned(g_conf_piece + 1, (uint32_t)g_conf_id);
            conf_log(now && !g_conf_had ? "made" : "back on the list", state);
            toast(now && !g_conf_had ? "생산했어요!" : "생산이 끝났어요 (만들어지지 않았다면 소재나 돈이 모자란 거예요)");
            g_conf = 0, g_have_frame = 0;
        } else if (state != g_conf_state && g_time - g_conf_t >= 0.9f) {
            if (g_conf_presses < 3) {  // (its messages after: let them go)
                conf_log("a message: A", state);
                mhxx_input_press(1u << 0, 80), g_conf_presses++, g_conf_t = g_time;
            } else {  // (something the screen doesn't know: the game's, to the player)
                conf_log("an unknown screen: the game has the controller", state);
                g_conf = 0, g_hidden = true;
                mhxx_input_capture(0);
                return true;
            }
        }
    }
    if (g_conf) draw_confirm(W, H);
    return g_conf != 0;
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
    log_line("[SmithyTree] " VERSION " ready: the World-style smithy (weapon trees, armor grid, wish list)");
}

void forge_onUpdate(float dt) {
    (void)dt;
    g_frame++;
}

static void log_box_once(void) {
    static bool done;
    if (done || !g_have_frame) return;
    read_equip();
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
    int pieces = 0;
    for (int t = 0; t < TAGS; t++)
        for (int k = 0; k < 256; k++) pieces += __builtin_popcount(g_owned[t][k]);
    put(&b, "; equipment box: "), put(&b, g_owned_ok ? "read, " : "not read, "), put_int(&b, pieces),
        put(&b, " kinds owned; acquired flags "), put_hex(&b, g_acq);
    log_line(l);
}


void forge_onImGuiFreeRender(void) {
    ImGuiIO *io = igGetIO_Nil();
    float W = io->DisplaySize.x, H = io->DisplaySize.y;
    g_dl = igGetForegroundDrawList_Nil(), g_fs = igGetFontSize() * (H > 900 ? H / 900.0f : 1.0f);
    g_time += io->DeltaTime > 0 && io->DeltaTime < 0.25f ? io->DeltaTime : 0.0f;
    if (g_toast[0] && g_frame - g_toast_frame < 150) {  // (a word for the player, over the game too)
        float w = text_w(g_toast, 1.0f) + g_fs * 2;
        frame((W - w) * 0.5f, H * 0.03f, (W + w) * 0.5f, H * 0.03f + g_fs * 1.9f, 0xf0141416);
        text_o((W - w) * 0.5f + g_fs, H * 0.03f + g_fs * 0.45f, 1.0f, 0xff70e8ff, g_toast);
    }
    static uint32_t last;
    g_fresh = last != g_frame;  // (input: once a game frame)
    last = g_frame;
    if (!g_original || g_frame - g_seen_frame > 2 || !g_now.id) {  // (the smithy not open, or a screen without a list)
        if (g_frame - g_seen_frame > 2) g_hidden = false, g_wish_open = false, g_arank = -1;
        return;
    }
    int cat = g_now.category;
    bool weapons = cat >= 7 && cat - 7 < TYPES && cat - 7 != 5, armor = cat >= 1 && cat <= 5;
    if (!weapons && !armor) return;
    // After A or B the game has the controller (its confirmation, its Back): the screen comes back by itself when the
    // game is on this list again, after it was somewhere else.
    static uint16_t hid_at;
    static bool left;
    uint16_t state = (uint16_t)(g_now.screen << 8 | g_now.sub);
    if (g_hidden) {
        if (state != hid_at) left = true;
        else if (left) g_hidden = false;
        if (g_hidden) return;
    }
    mhxx_input_capture(150);  // (the controller is the screen's while it is up)
    read_box();
    read_equip();
    icons_frame();
    log_box_once();
    if (g_have_ok) wish_popups();
    if (g_conf) {
        g_want_id = g_conf_id;
        if (confirm_step(W, H, state)) return;
    }
    if (g_wish_open) {
        wish_input();
        draw_wishlist(W, H);
        return;
    }
    if (pressed(B_B)) {  // (B: the game's Back)
        g_hidden = true, hid_at = state, left = false;
        mhxx_input_capture(0);
        return;
    }
    if (pressed(B_R)) {
        g_wish_open = true, g_wish_sel = 0;
        return;
    }
    // the game's own 3D preview stays (its parchment on the left), so do its message box, money and buttons
    float ty0 = H * 0.185f, ty1 = H * 0.735f;
    ImDrawList_AddRectFilled(g_dl, V2(W * 0.262f, H * 0.172f), V2(W * 0.995f, ty1 + H * 0.012f), 0xff0c0a08, 0, 0);
    if (weapons) {
        if (!load_type(cat - 7)) return;
        if (g_now.category != g_game_cat || g_now.id != g_game_id || g_cw < 0 || g_cw >= (int)g_nbase || !g_w[g_cw].top) {
            g_game_cat = g_now.category, g_game_id = g_now.id;
            g_cw = g_now.id < g_nbase && g_w[g_now.id].top ? g_now.id : g_rows[0];
            g_clv = g_now.level ? g_now.level : 1;
            if (g_clv > g_w[g_cw].top) g_clv = g_w[g_cw].top;
        }
        layout_nodes();
        weapon_input();
        g_want_id = g_cw;  // (the game's list follows: its 3D preview shows the weapon)
        bool can = false;
        if (pressed(B_A)) {  // (A: the game's own A on this weapon - to make or upgrade it)
            if (g_want_result < 0) toast(g_w[g_cw].parent >= 0 && !create_of((uint32_t)g_cw) ? "파생 무기는 대장간의 '장비 강화'에서 이전 무기를 강화해서 만들어요" : "이 무기는 지금 게임 목록에 없어요 (해금 전이거나 소재 정보가 아직 없어요)");
            else if (g_now.screen == 3) g_conf = 1, g_conf_yes = 1, g_conf_kind = 0, g_conf_id = g_cw;
            else {
                g_hidden = true, hid_at = state, left = false;
                mhxx_input_capture(0);
                return;
            }
        }
        draw_weapon_tree(W * 0.268f, ty0, W * 0.705f, ty1);
        draw_weapon_card(W * 0.716f, ty0, W * 0.986f, ty1, g_cw, g_clv, &can);
    } else {
        if (!load_armor() || g_now.id >= g_naser) return;
        armor_from_game();
        armor_input();
        g_want_id = g_narank ? g_arow_of[g_arow] : -1;
        if (pressed(B_A)) {
            if (g_want_result < 0) toast("이 방어구는 지금 게임 목록에 없어요 (해금 전이거나 다른 부위 / 페이지)");
            else if (g_apiece != g_now.category - 1) toast("게임 목록은 다른 부위예요: B로 나가서 이 부위를 고른 뒤 만들 수 있어요");
            else if (g_now.screen == 3)
                g_conf = 1, g_conf_yes = 1, g_conf_kind = 1, g_conf_id = g_arow_of[g_arow], g_conf_piece = g_apiece;
            else {
                g_hidden = true, hid_at = state, left = false;
                mhxx_input_capture(0);
                return;
            }
        }
        draw_armor_grid(W * 0.268f, ty0, W * 0.705f, ty1);
        draw_armor_card(W * 0.716f, ty0, W * 0.986f, ty1);
    }
    bottom(W, H, weapons);
}
