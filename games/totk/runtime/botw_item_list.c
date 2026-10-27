/* The items the BotW cheat menu can drop (botw_cheats.h), read from the game's own files at run time, so the list follows
   the player's game, language and mods: Actor/ActorInfo.product.sbyml (Yaz0 BYML) names every actor with its profile and
   sort key, and the text pack the game opened (Pack/Bootup_<language>.pack, a SARC holding the Yaz0 SARC
   Message/Msg_<language>.product.ssarc) has each item's name as "<actor>_Name" in ActorType/<profile>.msbt. Nothing from
   the game is kept in the repository; tests/test_botw_item_list.py checks the readers on files it builds itself. */
#include "botw_cheats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *runtime_romfs_load(const char *path, size_t *size);   /* runtime/romfs.c */
const char *runtime_romfs_text_lang(void);

const char *const botw_item_kind_names[BOTW_ITEM_KINDS] = {
    "One-handed weapons", "Two-handed weapons", "Spears", "Bows", "Shields", "Arrows", "Food and ingredients",
    "Monster parts and gems", "Critters", "Rupees"};

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd24(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }

uint8_t *botw_yaz0_decode(const uint8_t *s, size_t n, size_t *out_size) {
    if (n < 16 || memcmp(s, "Yaz0", 4)) return NULL;
    size_t size = be32(s + 4), i = 16, o = 0;
    uint8_t *d = malloc(size ? size : 1);
    if (!d) return NULL;
    while (o < size) {
        if (i >= n) goto bad;
        uint8_t code = s[i++];
        for (int bit = 7; bit >= 0 && o < size; bit--) {
            if (code >> bit & 1) { if (i >= n) goto bad; d[o++] = s[i++]; continue; }
            if (i + 1 >= n) goto bad;
            uint8_t a = s[i++], b = s[i++];
            size_t back = ((size_t)(a & 15) << 8 | b) + 1, len = a >> 4;
            if (!len) { if (i >= n) goto bad; len = (size_t)s[i++] + 0x12; } else len += 2;
            if (back > o) goto bad;
            for (; len && o < size; len--, o++) d[o] = d[o - back];
        }
    }
    *out_size = size;
    return d;
bad:
    free(d);
    return NULL;
}

/* Little-endian SARC (the Switch's): the member called name, by a scan of the name table. */
const uint8_t *botw_sarc_get(const uint8_t *d, size_t n, const char *name, size_t *member_size) {
    if (n < 0x20 || memcmp(d, "SARC", 4) || rd16(d + 6) != 0xfeff) return NULL;
    size_t data = rd32(d + 0xc), sfat = rd16(d + 4);
    if (sfat + 12 > n || memcmp(d + sfat, "SFAT", 4)) return NULL;
    size_t count = rd16(d + sfat + 6), nodes = sfat + rd16(d + sfat + 4), sfnt = nodes + count * 16;
    if (sfnt + 8 > n || memcmp(d + sfnt, "SFNT", 4)) return NULL;
    size_t names = sfnt + rd16(d + sfnt + 4), want = strlen(name);
    for (size_t k = 0; k < count; k++) {
        const uint8_t *e = d + nodes + k * 16;
        uint32_t attr = rd32(e + 4);
        if (!(attr >> 24)) continue;
        size_t at = names + (size_t)(attr & 0xffffff) * 4, start = data + rd32(e + 8), end = data + rd32(e + 12);
        if (at + want >= n || memcmp(d + at, name, want + 1)) continue;
        if (start > end || end > n) return NULL;
        *member_size = end - start;
        return d + start;
    }
    return NULL;
}

/* MSBT (little-endian, UTF-16): LBL1 is a hash table of labels (hash = hash * 0x492 + byte, bucket = hash % buckets)
   holding text numbers; TXT2 the texts, with 0x0e (group, type, size, size bytes) and 0x0f (4 bytes) control tags. */
int botw_msbt_text(const uint8_t *d, size_t n, const char *label, char *out, size_t out_size) {
    if (n < 0x20 || memcmp(d, "MsgStdBn", 8) || rd16(d + 8) != 0xfeff || !out_size) return 0;
    const uint8_t *lbl = NULL, *txt = NULL; size_t lbl_n = 0, txt_n = 0;
    for (size_t at = 0x20, k = rd16(d + 0xe); k && at + 16 <= n; k--) {
        size_t size = rd32(d + at + 4);
        if (size > n - at - 16) return 0;
        if (!memcmp(d + at, "LBL1", 4)) { lbl = d + at + 16; lbl_n = size; }
        if (!memcmp(d + at, "TXT2", 4)) { txt = d + at + 16; txt_n = size; }
        at += 16 + ((size + 15) & ~(size_t)15);
    }
    if (!lbl || !txt || lbl_n < 4 || txt_n < 4) return 0;
    uint32_t hash = 0, buckets = rd32(lbl), len = (uint32_t)strlen(label), index = UINT32_MAX;
    for (const char *c = label; *c; c++) hash = hash * 0x492 + (uint8_t)*c;
    if (!buckets || 4 + (size_t)buckets * 8 > lbl_n) return 0;
    const uint8_t *bucket = lbl + 4 + (size_t)(hash % buckets) * 8;
    size_t at = rd32(bucket + 4);
    for (uint32_t k = rd32(bucket); k && index == UINT32_MAX; k--) {
        if (at >= lbl_n) return 0;
        size_t l = lbl[at];
        if (at + 1 + l + 4 > lbl_n) return 0;
        if (l == len && !memcmp(lbl + at + 1, label, l)) index = rd32(lbl + at + 1 + l);
        at += 1 + l + 4;
    }
    if (index == UINT32_MAX || index >= rd32(txt) || 4 + (size_t)rd32(txt) * 4 > txt_n) return 0;
    size_t p = rd32(txt + 4 + (size_t)index * 4), o = 0;
    while (p + 2 <= txt_n) {
        uint32_t c = rd16(txt + p); p += 2;
        if (!c) break;
        if (c == 0x0e) { if (p + 6 > txt_n) break; p += 6 + rd16(txt + p + 4); continue; }
        if (c == 0x0f) { p += 4; continue; }
        if (c >= 0xd800 && c < 0xdc00 && p + 2 <= txt_n) {
            uint32_t lo = rd16(txt + p);
            if (lo >= 0xdc00 && lo < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00); p += 2; }
        }
        char u[4]; size_t k = 0;
        if (c < 0x80) u[k++] = (char)c;
        else if (c < 0x800) { u[k++] = (char)(0xc0 | c >> 6); u[k++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { u[k++] = (char)(0xe0 | c >> 12); u[k++] = (char)(0x80 | (c >> 6 & 63)); u[k++] = (char)(0x80 | (c & 63)); }
        else { u[k++] = (char)(0xf0 | c >> 18); u[k++] = (char)(0x80 | (c >> 12 & 63)); u[k++] = (char)(0x80 | (c >> 6 & 63)); u[k++] = (char)(0x80 | (c & 63)); }
        if (o + k >= out_size) break;
        memcpy(out + o, u, k); o += k;
    }
    out[o] = 0;
    return 1;
}

/* Little-endian BYML version 2 or 3: hash nodes (0xc1: count, then key index, type, value per entry), arrays (0xc0:
   count, types padded to 4, values) and string tables (0xc2: count + 1 offsets from the node). */
typedef struct { const uint8_t *d; size_t n, keys, strings; } Byml;
static const char *byml_str(const Byml *b, size_t table, uint32_t i) {
    if (table + 4 > b->n || b->d[table] != 0xc2 || i >= rd24(b->d + table + 1) || table + 8 + (size_t)i * 4 > b->n) return NULL;
    size_t at = table + rd32(b->d + table + 4 + (size_t)i * 4);
    return at < b->n && memchr(b->d + at, 0, b->n - at) ? (const char *)b->d + at : NULL;
}
static uint32_t byml_key(const Byml *b, const char *name) {   /* the key table's index of name, or UINT32_MAX */
    if (b->keys + 4 > b->n) return UINT32_MAX;
    for (uint32_t i = 0, count = rd24(b->d + b->keys + 1); i < count; i++) {
        const char *k = byml_str(b, b->keys, i);
        if (k && !strcmp(k, name)) return i;
    }
    return UINT32_MAX;
}
static int byml_get(const Byml *b, size_t node, uint32_t key, uint8_t *type, uint32_t *value) {
    if (node + 4 > b->n || b->d[node] != 0xc1) return 0;
    uint32_t count = rd24(b->d + node + 1);
    if (node + 4 + (size_t)count * 8 > b->n) return 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = b->d + node + 4 + (size_t)i * 8;
        if (rd24(e) == key) { *type = e[3]; *value = rd32(e + 4); return 1; }
    }
    return 0;
}
static const char *byml_get_str(const Byml *b, size_t node, uint32_t key) {
    uint8_t t; uint32_t v;
    return byml_get(b, node, key, &t, &v) && t == 0xa0 ? byml_str(b, b->strings, v) : NULL;
}

static int item_kind(const char *profile, const char *actor) {
    static const struct { const char *profile, *prefix; int kind; } kinds[] = {
        {"WeaponSmallSword", "Weapon_Sword_", BOTW_ITEMS_ONE_HANDED}, {"WeaponLargeSword", "Weapon_Lsword_", BOTW_ITEMS_TWO_HANDED},
        {"WeaponSpear", "Weapon_Spear_", BOTW_ITEMS_SPEARS}, {"WeaponBow", "Weapon_Bow_", BOTW_ITEMS_BOWS},
        {"WeaponShield", "Weapon_Shield_", BOTW_ITEMS_SHIELDS}, {"Item", "Item_Enemy_", BOTW_ITEMS_MONSTER_PARTS},
        {"Item", "Item_Ore_", BOTW_ITEMS_MONSTER_PARTS}, {"Item", "Item_", BOTW_ITEMS_FOOD}, {"Item", "PutRupee", BOTW_ITEMS_RUPEES},
        {"CapturedActor", "Animal_Insect_", BOTW_ITEMS_CRITTERS}};
    if (!strcmp(profile, "Item") && !strncmp(actor, "Obj_", 4) && strstr(actor, "Arrow")) return BOTW_ITEMS_ARROWS;   /* bundles: Obj_ArrowBundle_A_01, Obj_FireArrow_A_03, ... */
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++)
        if (!strcmp(profile, kinds[i].profile) && !strncmp(actor, kinds[i].prefix, strlen(kinds[i].prefix))) return kinds[i].kind;
    return -1;
}

typedef struct { size_t actor, name; int kind, sort; } Pending;   /* offsets into the pool while it grows */
static const char *g_pool_base;
static int pending_order(const void *x, const void *y) {
    const Pending *a = x, *b = y;
    if (a->kind != b->kind) return a->kind - b->kind;
    if (a->sort != b->sort) return a->sort < b->sort ? -1 : 1;
    return strcmp(g_pool_base + a->name, g_pool_base + b->name);
}

int botw_items_load(BotwItemList *list, char *error, size_t error_size) {
    memset(list, 0, sizeof *list);
    const char *lang = runtime_romfs_text_lang();
    if (!*lang) { snprintf(error, error_size, "the game has not loaded its text yet"); return -1; }
    char path[96], member[96];
    size_t pack_n = 0, info_z = 0, info_n = 0, msg_z = 0, msg_n = 0;
    snprintf(path, sizeof path, "Pack/Bootup_%s.pack", lang);
    uint8_t *pack = runtime_romfs_load(path, &pack_n), *info_c = runtime_romfs_load("Actor/ActorInfo.product.sbyml", &info_z);
    uint8_t *info = info_c ? botw_yaz0_decode(info_c, info_z, &info_n) : NULL, *msg = NULL;
    snprintf(member, sizeof member, "Message/Msg_%s.product.ssarc", lang);
    const uint8_t *msg_c = pack ? botw_sarc_get(pack, pack_n, member, &msg_z) : NULL;
    if (msg_c) msg = botw_yaz0_decode(msg_c, msg_z, &msg_n);
    Pending *items = NULL; size_t count = 0, cap = 0, pool_n = 0, pool_cap = 0; char *pool = NULL;
    int ok = -1;
    if (!info || !msg) { snprintf(error, error_size, "%s could not be read", !info ? "Actor/ActorInfo.product.sbyml" : path); goto done; }
    Byml b = {info, info_n, 0, 0};
    if (info_n < 16 || memcmp(info, "YB", 2) || rd16(info + 2) < 2) { snprintf(error, error_size, "ActorInfo is not a little-endian BYML"); goto done; }
    b.keys = rd32(info + 4); b.strings = rd32(info + 8);
    uint32_t k_actors = byml_key(&b, "Actors"), k_name = byml_key(&b, "name"), k_profile = byml_key(&b, "profile"), k_sort = byml_key(&b, "sortKey");
    uint8_t t; uint32_t actors;
    if (!byml_get(&b, rd32(info + 12), k_actors, &t, &actors) || t != 0xc0 || actors + 4 > info_n) { snprintf(error, error_size, "ActorInfo has no actor list"); goto done; }
    uint32_t n_actors = rd24(info + actors + 1);
    size_t values = actors + 4 + (((size_t)n_actors + 3) & ~(size_t)3);
    if (values + (size_t)n_actors * 4 > info_n) { snprintf(error, error_size, "ActorInfo's actor list is cut short"); goto done; }
    struct { const char *profile; const uint8_t *data; size_t size; } texts[8] = {{0}};
    for (uint32_t i = 0; i < n_actors; i++) {
        if (info[actors + 4 + i] != 0xc1) continue;
        size_t node = rd32(info + values + (size_t)i * 4);
        const char *actor = byml_get_str(&b, node, k_name), *profile = byml_get_str(&b, node, k_profile);
        int kind = actor && profile ? item_kind(profile, actor) : -1;
        if (kind < 0) continue;
        size_t s = 0;   /* this profile's names: ActorType/<profile>.msbt */
        while (s < 8 && texts[s].profile && strcmp(texts[s].profile, profile)) s++;
        if (s == 8) continue;
        if (!texts[s].profile) {
            char name[96]; snprintf(name, sizeof name, "ActorType/%s.msbt", profile);
            texts[s].profile = profile; texts[s].data = botw_sarc_get(msg, msg_n, name, &texts[s].size);
        }
        char label[160], text[160];
        snprintf(label, sizeof label, "%s_Name", actor);
        if (!texts[s].data || !botw_msbt_text(texts[s].data, texts[s].size, label, text, sizeof text) || !*text) continue;
        size_t tl = strlen(text);
        if (tl >= 2 && !strcmp(text + tl - 2, " x")) continue;   /* "Arrow x", "Fresh Milk x": templates the game fills with a count */
        uint32_t sort = 0;
        if (!byml_get(&b, node, k_sort, &t, &sort) || (t != 0xd1 && t != 0xd3)) sort = 0;
        size_t al = strlen(actor) + 1;
        if (count == cap) { cap = cap ? cap * 2 : 512; Pending *grown = realloc(items, cap * sizeof *items); if (!grown) goto oom; items = grown; }
        if (pool_n + al + tl + 1 > pool_cap) { pool_cap = (pool_n + al + tl + 1) * 2 + 4096; char *grown = realloc(pool, pool_cap); if (!grown) goto oom; pool = grown; }
        items[count++] = (Pending){pool_n, pool_n + al, kind, (int)sort};
        memcpy(pool + pool_n, actor, al); memcpy(pool + pool_n + al, text, tl + 1); pool_n += al + tl + 1;
    }
    g_pool_base = pool;
    if (count) qsort(items, count, sizeof *items, pending_order);
    list->items = calloc(count ? count : 1, sizeof *list->items);
    if (!list->items) goto oom;
    for (size_t i = 0; i < count; i++) {   /* one entry per name and kind (the Master Sword's three forms are one) */
        const Pending *p = &items[i];
        int dup = 0;
        for (size_t j = list->count; j-- > 0 && list->items[j].kind == p->kind;) if (!strcmp(list->items[j].name, pool + p->name)) { dup = 1; break; }
        if (!dup) list->items[list->count++] = (BotwItem){pool + p->actor, pool + p->name, p->kind, p->sort};
    }
    list->pool = pool; pool = NULL;
    ok = 0;
    goto done;
oom:
    snprintf(error, error_size, "out of memory");
done:
    free(items); free(pool); free(info); free(info_c); free(msg); free(pack);
    return ok;
}

void botw_items_free(BotwItemList *list) {
    free(list->items); free(list->pool);
    memset(list, 0, sizeof *list);
}
