/* Breath of the Wild cheats (botw_cheats.h): the cheats chosen on the iOS start screen (launcher.m: never saved, so every
   launch starts with them off) or set in the environment, TOTK_CHEAT_HEALTH (invincibility), TOTK_CHEAT_STAMINA,
   TOTK_CHEAT_MOONJUMP, TOTK_CHEAT_RUPEES, TOTK_CHEAT_ARROWS = 1, and the in-game cheat menu: hold L and R and press A.
   The menu switches the same cheats while the game runs and drops weapons, bows, shields, arrows, food, materials,
   critters and rupees in front of Link; the game's controller input is held back while it is open.

   Everything goes through the game's own data and functions (BotW 1.6.0, build 8e9978d50bdd20b4; offsets from main):
   - invincibility: the player's damage step keeps the life it had (a code patch reading a flag on the loader's data page,
     game_patches_botw.c patch_invincible), and the hearts refill as below.
   - health, stamina, rupees and arrows: the pointer chains of the community codes (github.com/blawar/switchcheats), read
     with vm_read so nothing is written before a chain exists (title screen, loading). Health and stamina refill to the
     player's own maximum at every present and every controller read; rupees and arrows are set to 999999 / 999 and stay
     in a save made while they are on, as on the console.
   - moon jump: the player's gravity factor ([[[main+0x2d1ea00]+0x198]+0x2d18]+0x130, or the same object by
     [[main+0x2c99e10]+0x8f40]; 1.0 as shipped; the community "Moonjump" codes) is -1 while L and X are both held. It only
     acts in the air, so the X press jumps first.
   - dropping an item: ksys::act::ActorCreator::requestCreateActor (main+0x1c27a30; the creator at [main+0x2d1fcc8], the
     base-proc heap at [[main+0x2d1e148]+0x28]) with an InstParamPack (vtable main+0x2a70a58) holding "@P" (the key at
     main+0x268d490), as ksys::world::ShootingStarMgr::spawnStar (main+0x1a76fb0) creates a falling star. The position is
     two metres in front of Link, a metre up: uking::act::PlayerInfo (main+0x2ca1140) +0x60 is the player actor, whose
     matrix (+0x398, 3x4 rows) gives the position and facing. The request runs on the game thread that reads the
     controller (the main loop), like the game's own.
   The item list comes from the game's files (botw_item_list.c). TOTK_CHEAT_LOG=1 logs the values every 300 frames. */
#include "botw_cheats.h"
#include "runtime.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <mach/mach.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEALTH_CHAIN = 0x2ca1a78, PLAYER_CHAIN = 0x2c9fd70, RUPEE_CHAIN = 0x2cc5fe0, ARROW_CHAIN = 0x2ca6d48, GRAVITY_CHAIN = 0x2d1ea00, GRAVITY_CHAIN_2 = 0x2c99e10,
    PLAYER_INFO = 0x2ca1140, ACTOR_CREATOR = 0x2d1fcc8, HEAP_MGR = 0x2d1e148, REQUEST_CREATE_ACTOR = 0x1c27a30,
    INST_PARAM_PACK_VTABLE = 0x2a70a58, KEY_POSITION = 0x268d490,
};
enum { PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 6, PAD_R = 1 << 7, PAD_ZL = 1 << 8, PAD_ZR = 1 << 9,
       PAD_PLUS = 1 << 10, PAD_MINUS = 1 << 11, PAD_LEFT = 1 << 12, PAD_UP = 1 << 13, PAD_RIGHT = 1 << 14, PAD_DOWN = 1 << 15 };

// ---------- game memory ----------
static int cheat_read(uint64_t a, void *out, size_t n) {
    vm_size_t got = 0;
    return a > 0x10000 && vm_read_overwrite(mach_task_self(), (vm_address_t)a, n, (vm_address_t)out, &got) == KERN_SUCCESS && got == n;
}
/* [main + base], then [p + chain[i]] for each link, then + add: the field's address, 0 when the chain is broken. */
static uint64_t cheat_chain(const uint8_t *main, uint64_t base, const uint32_t *chain, int n, uint32_t add) {
    uint64_t p;
    if (!cheat_read((uint64_t)(uintptr_t)(main + base), &p, 8)) return 0;
    for (int i = 0; i < n; i++) if (!p || !cheat_read(p + chain[i], &p, 8)) return 0;
    uint32_t probe;
    return p && cheat_read(p + add, &probe, 4) ? p + add : 0;
}
static const uint8_t *game_main(void) { return runtime_module_base("main"); }

// ---------- the cheats ----------
enum { CHEAT_INVINCIBLE, CHEAT_STAMINA, CHEAT_MOON_JUMP, CHEAT_RUPEES, CHEAT_ARROWS, CHEATS };
static const struct { const char *env, *title, *detail; } cheat_info[CHEATS] = {
    {"TOTK_CHEAT_HEALTH", "Invincibility", "Hearts stay full"},
    {"TOTK_CHEAT_STAMINA", "Unlimited stamina", "The stamina wheel stays full"},
    {"TOTK_CHEAT_MOONJUMP", "Moon jump", "Hold L, then hold X to rise"},
    {"TOTK_CHEAT_RUPEES", "Max rupees", "999,999 rupees"},
    {"TOTK_CHEAT_ARROWS", "999 arrows", "The equipped arrows stay at 999"},
};
static _Atomic int g_cheat[CHEATS], g_log;
void botw_invincible_set(int on);   /* game_patches_botw.c: the damage step keeps the life while on */
static int env_on(const char *name) { const char *e = getenv(name); return e && *e && strcmp(e, "0"); }
static void cheats_init_once(void) {
    char on[160] = "";
    for (int i = 0; i < CHEATS; i++)
        if (env_on(cheat_info[i].env)) { atomic_store(&g_cheat[i], 1); strlcat(on, *on ? ", " : " ", sizeof on); strlcat(on, cheat_info[i].title, sizeof on); }
    atomic_store(&g_log, env_on("TOTK_CHEAT_LOG"));
    botw_invincible_set(atomic_load(&g_cheat[CHEAT_INVINCIBLE]));
    fprintf(stderr, "[cheats] on at start:%s; in the game, hold L and R and press A for the cheat menu\n", *on ? on : " none");
}
static void cheats_init(void) { static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, cheats_init_once); }

/* Health and stamina refill to the player's own maximum (the community codes wrote 30 hearts and three wheels whatever the
   save had). Called at every present and every controller read. */
static void refill(const uint8_t *main) {
    static const uint32_t c80[] = {0x80}, c38[] = {0x38};
    int health = atomic_load(&g_cheat[CHEAT_INVINCIBLE]), stamina = atomic_load(&g_cheat[CHEAT_STAMINA]);
    if (!health && !stamina) return;
    uint64_t hp = cheat_chain(main, HEALTH_CHAIN, c80, 1, 0x848);      // current health, quarter hearts (one byte)
    uint64_t player = cheat_chain(main, PLAYER_CHAIN, c38, 1, 0x2a8);  // +0: heart containers (float, quarter hearts), +4 / +8: stamina
    float maxhp = 0, st[2] = {0, 0}; uint8_t cur = 0;
    if (player) { cheat_read(player, &maxhp, 4); cheat_read(player + 4, st, 8); }
    if (hp) cheat_read(hp, &cur, 1);
    if (health && hp && maxhp >= 4 && maxhp <= 120 && cur < (uint8_t)maxhp) { uint8_t v = (uint8_t)maxhp; memcpy((void *)(uintptr_t)hp, &v, 1); }
    if (stamina && player && st[1] > 0 && st[1] <= 3000 && st[0] < st[1]) memcpy((void *)(uintptr_t)(player + 4), &st[1], 4);
}

void botw_cheats_tick(uint64_t frame);
void botw_cheats_tick(uint64_t frame) {   /* at every present (gfx/nvn/hle_nvn.c) */
    cheats_init();
    int rupees = atomic_load(&g_cheat[CHEAT_RUPEES]), arrows = atomic_load(&g_cheat[CHEAT_ARROWS]), logging = atomic_load(&g_log);
    const uint8_t *main = game_main(); if (!main) return;
    refill(main);
    if (!(rupees || arrows || logging)) return;
    static const uint32_t c80[] = {0x80}, c38[] = {0x38}, crupee[] = {0xe40, 0x5b08}, carrow[] = {0x447e8};
    uint64_t rp = cheat_chain(main, RUPEE_CHAIN, crupee, 2, 0x5d90);  // rupees (s32)
    uint64_t ar = cheat_chain(main, ARROW_CHAIN, carrow, 1, 0x20);    // equipped arrows (s32; the menu shows it after reopening)
    int32_t money = 0, quiver = 0;
    if (rp) cheat_read(rp, &money, 4);
    if (ar) cheat_read(ar, &quiver, 4);
    if (logging && frame % 300 == 0) {
        uint64_t hp = cheat_chain(main, HEALTH_CHAIN, c80, 1, 0x848), player = cheat_chain(main, PLAYER_CHAIN, c38, 1, 0x2a8);
        float maxhp = 0, st[2] = {0, 0}; uint8_t cur = 0;
        if (player) { cheat_read(player, &maxhp, 4); cheat_read(player + 4, st, 8); }
        if (hp) cheat_read(hp, &cur, 1);
        fprintf(stderr, "[cheats] F%llu health %u/%g stamina %g %g rupees %d arrows %d (chains%s%s%s%s)\n", (unsigned long long)frame, cur, maxhp, st[0], st[1], money, quiver,
                hp ? " hp" : "", player ? " player" : "", rp ? " rupees" : "", ar ? " arrows" : "");
    }
    if (rupees && rp && money != 999999) { int32_t v = 999999; memcpy((void *)(uintptr_t)rp, &v, 4); }
    if (arrows && ar && quiver != 999) { int32_t v = 999; memcpy((void *)(uintptr_t)ar, &v, 4); }
}

/* Moon jump: the gravity factor is -1 while rising and 1.0 again after (written back only when this changed it). */
static void moon_jump(const uint8_t *main, int rise) {
    static int rising;
    static const uint32_t chain[] = {0x198, 0x2d18}, other[] = {0x8f40, 0x2d18};
    if (rise == rising && !rise) return;
    uint64_t g = cheat_chain(main, GRAVITY_CHAIN, chain, 2, 0x130);
    if (!g) g = cheat_chain(main, GRAVITY_CHAIN_2, other, 2, 0x130);
    if (!g) { rising = 0; return; }
    float v = rise ? -1.0f : 1.0f, now;
    if (cheat_read(g, &now, 4) && now != v) memcpy((void *)(uintptr_t)g, &v, 4);
    if (rise != rising) fprintf(stderr, "[cheats] moon jump %s\n", rise ? "rising" : "released");
    rising = rise;
}

// ---------- dropping items ----------
/* The player's position, facing (local z) and right (local x), from the actor's matrix. 0 while there is no player. */
static int player_frame(const uint8_t *main, float pos[3], float fwd[3], float right[3]) {
    uint64_t info = 0, actor = 0; float m[12];
    if (!cheat_read((uint64_t)(uintptr_t)(main + PLAYER_INFO), &info, 8) || !info || !cheat_read(info + 0x60, &actor, 8) || !actor ||
        !cheat_read(actor + 0x398, m, sizeof m)) return 0;
    for (int i = 0; i < 3; i++) { pos[i] = m[i * 4 + 3]; fwd[i] = m[i * 4 + 2]; right[i] = m[i * 4]; }
    return isfinite(pos[0]) && isfinite(pos[1]) && isfinite(pos[2]) && (pos[0] || pos[1] || pos[2]);
}
typedef struct { uint64_t vtable, proc; uint16_t count; int16_t position; uint8_t data[0xc0]; uint8_t pad[4]; } InstParamPack;
_Static_assert(sizeof(InstParamPack) == 0xd8, "ksys::act::InstParamPack");
/* Requests n of the actor in front of Link, side by side. Returns how many the game accepted, or -1 without a player. */
static int drop_items(const char *actor, int n) {
    const uint8_t *main = game_main();
    float pos[3], fwd[3], right[3];
    if (!main || !player_frame(main, pos, fwd, right)) return -1;
    uint64_t creator = 0, heap_mgr = 0, heap = 0;
    if (!cheat_read((uint64_t)(uintptr_t)(main + ACTOR_CREATOR), &creator, 8) || !creator ||
        !cheat_read((uint64_t)(uintptr_t)(main + HEAP_MGR), &heap_mgr, 8) || !heap_mgr || !cheat_read(heap_mgr + 0x28, &heap, 8) || !heap) return -1;
    typedef int (*RequestCreateActor)(uint64_t creator, const char *name, uint64_t heap, void *handle, InstParamPack *params, void *map_object, int lane);
    RequestCreateActor request = (RequestCreateActor)(uintptr_t)(main + REQUEST_CREATE_ACTOR);
    float flat = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
    if (flat < 0.1f) { fwd[0] = 0; fwd[2] = 1; flat = 1; }
    int done = 0;
    for (int i = 0; i < n; i++) {
        float side = (float)((i + 1) >> 1) * (i % 2 ? 0.7f : -0.7f);   // 0, +0.7, -0.7, +1.4, -1.4 m to the side
        float at[3] = {pos[0] + fwd[0] / flat * 2.0f + right[0] * side, pos[1] + 1.0f, pos[2] + fwd[2] / flat * 2.0f + right[2] * side};
        InstParamPack pack; memset(&pack, 0, sizeof pack);
        pack.vtable = (uint64_t)(uintptr_t)(main + INST_PARAM_PACK_VTABLE);
        uint64_t key = (uint64_t)(uintptr_t)(main + KEY_POSITION);   // "@P": key pointer, type 4 (Vec3), 12 bytes
        memcpy(pack.data, &key, 8); pack.data[8] = 4; memcpy(pack.data + 9, at, 12);
        pack.position = 21; pack.count = 1;
        done += request(creator, actor, heap, NULL, &pack, NULL, 1) & 1;
    }
    fprintf(stderr, "[cheats] dropped %s x%d (%d accepted) at %.1f %.1f %.1f\n", actor, n, done, pos[0], pos[1], pos[2]);
    return done;
}

// ---------- the item list (loaded once, in the background) ----------
static BotwItemList g_items;
static _Atomic int g_items_state;   /* 0 not started, 1 loading, 2 ready, 3 failed */
static char g_items_error[160];
static void *items_thread(void *arg) {
    (void)arg;
    uint64_t t0 = runtime_now_ns();
    BotwItemList list; char error[160] = "";
    if (botw_items_load(&list, error, sizeof error)) {
        snprintf(g_items_error, sizeof g_items_error, "%s", error);
        fprintf(stderr, "[cheats] item list failed: %s\n", error);
        atomic_store(&g_items_state, 3);
        return NULL;
    }
    g_items = list;
    int per[BOTW_ITEM_KINDS] = {0};
    for (size_t i = 0; i < list.count; i++) per[list.items[i].kind]++;
    fprintf(stderr, "[cheats] item list: %zu items in %.0f ms (%d one-handed, %d two-handed, %d spears, %d bows, %d shields, %d arrows, %d food, %d monster parts, %d critters, %d rupees)\n",
            list.count, (runtime_now_ns() - t0) / 1e6, per[0], per[1], per[2], per[3], per[4], per[5], per[6], per[7], per[8], per[9]);
    atomic_store(&g_items_state, 2);
    return NULL;
}
static void items_start(void) {
    int idle = 0;
    if (!atomic_compare_exchange_strong(&g_items_state, &idle, 1)) return;
    pthread_t t;
    if (pthread_create(&t, NULL, items_thread, NULL)) { snprintf(g_items_error, sizeof g_items_error, "no thread"); atomic_store(&g_items_state, 3); return; }
    pthread_detach(t);
}
/* The first item of a kind and how many there are (the list is sorted by kind). */
static size_t kind_range(int kind, size_t *count) {
    size_t first = 0, n = 0;
    if (atomic_load(&g_items_state) != 2) { *count = 0; return 0; }
    while (first < g_items.count && g_items.items[first].kind < kind) first++;
    while (first + n < g_items.count && g_items.items[first + n].kind == kind) n++;
    *count = n;
    return first;
}

// ---------- the menu ----------
enum { PAGE_MAIN, PAGE_ITEMS };
enum { MAIN_ROWS = CHEATS + BOTW_ITEM_KINDS };
typedef struct {
    int open, page, row, kind, item, top;   /* main page: row; items page: kind, item (cursor), top (first row shown) */
    char toast[160]; uint64_t toast_until;
    int cheats[CHEATS];
    uint64_t version;
} Menu;
static pthread_mutex_t g_menu_mu = PTHREAD_MUTEX_INITIALIZER;
static Menu g_menu;
static _Atomic int g_menu_rows = 12;   /* item rows the panel shows (set by the drawing) */

static void toast(Menu *m, const char *text) { snprintf(m->toast, sizeof m->toast, "%s", text); m->toast_until = runtime_now_ns() + 2500000000ull; }
static void menu_activate(Menu *m, int five) {
    if (m->page == PAGE_MAIN) {
        if (m->row < CHEATS) {
            int on = !atomic_load(&g_cheat[m->row]);
            atomic_store(&g_cheat[m->row], on);
            fprintf(stderr, "[cheats] %s %s\n", cheat_info[m->row].title, on ? "on" : "off");
            if (m->row == CHEAT_INVINCIBLE) botw_invincible_set(on);
            if (m->row == CHEAT_MOON_JUMP && !on) { const uint8_t *main = game_main(); if (main) moon_jump(main, 0); }
            return;
        }
        items_start();
        m->page = PAGE_ITEMS; m->kind = m->row - CHEATS; m->item = 0; m->top = 0;
        return;
    }
    size_t count, first = kind_range(m->kind, &count);
    if (!count || m->item >= (int)count) return;
    const BotwItem *it = &g_items.items[first + (size_t)m->item];
    int n = five ? 5 : 1, done = drop_items(it->actor, n);
    char text[160];
    if (done < 0) snprintf(text, sizeof text, "Link is not in the world yet");
    else if (!done) snprintf(text, sizeof text, "The game did not drop %s", it->name);
    else if (n == 1) snprintf(text, sizeof text, "Dropped %s", it->name);
    else snprintf(text, sizeof text, "Dropped %d × %s", done, it->name);
    toast(m, text);
}
static void menu_move(Menu *m, int delta, int page_jump) {
    if (m->page == PAGE_MAIN) { m->row = (m->row + delta + MAIN_ROWS) % MAIN_ROWS; return; }
    size_t count; kind_range(m->kind, &count);
    if (!count) return;
    int n = (int)count, rows = atomic_load(&g_menu_rows), next = m->item + delta;
    if (page_jump) next = next < 0 ? 0 : next >= n ? n - 1 : next;
    else next = (next + n) % n;
    m->item = next;
    if (m->item < m->top) m->top = m->item;
    if (m->item >= m->top + rows) m->top = m->item - rows + 1;
}

/* The game's controller state for this read, after the cheat menu: L and R are held back briefly so that pressing them
   together (with A: the menu) never reaches the game, and the whole controller is the menu's while it is open. */
static struct {
    uint64_t prev, hide, held_since[2], tap_until[2], repeat_at; int pending[2], swallowed[2], moon, dir;
} in;
static int stick_dir(int32_t ly) { return ly > 16384 ? -1 : ly < -16384 ? 1 : 0; }   /* -1 up, 1 down */
static void menu_input(Menu *m, uint64_t phys, uint64_t pressed, int32_t ly, uint64_t now) {
    if ((phys & (PAD_L | PAD_R)) == (PAD_L | PAD_R) && (pressed & PAD_A)) { m->open = 0; return; }   /* L + R + A again */
    int dir = phys & PAD_UP ? -1 : phys & PAD_DOWN ? 1 : stick_dir(ly), jump = phys & PAD_LEFT ? -1 : phys & PAD_RIGHT ? 1 : 0;
    int key = dir ? dir : jump * 2;
    if (key != in.dir) { in.dir = key; in.repeat_at = now + 380000000ull; if (key) menu_move(m, dir ? dir : jump * atomic_load(&g_menu_rows), !dir); }
    else if (key && now >= in.repeat_at) { in.repeat_at = now + 70000000ull; menu_move(m, dir ? dir : jump * atomic_load(&g_menu_rows), !dir); }
    if (pressed & PAD_A) menu_activate(m, 0);
    if ((pressed & PAD_Y) && m->page == PAGE_ITEMS) menu_activate(m, 1);
    if ((pressed & (PAD_ZL | PAD_ZR)) && m->page == PAGE_ITEMS) {
        m->kind = (m->kind + (pressed & PAD_ZR ? 1 : BOTW_ITEM_KINDS - 1)) % BOTW_ITEM_KINDS; m->item = 0; m->top = 0;
    }
    if (pressed & PAD_B) { if (m->page == PAGE_ITEMS) { m->page = PAGE_MAIN; m->row = CHEATS + m->kind; } else m->open = 0; }
    if (pressed & PAD_PLUS) m->open = 0;
}

void botw_cheat_input(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
void botw_cheat_input(uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {   /* runtime/input.c, each controller read */
    cheats_init();
    const uint8_t *main = game_main();
    uint64_t now = runtime_now_ns(), phys = *buttons, pressed = phys & ~in.prev, released = in.prev & ~phys;
    in.prev = phys;
    in.hide &= phys;
    pthread_mutex_lock(&g_menu_mu);
    Menu *m = &g_menu;
    if (m->open) {
        Menu before = *m;
        menu_input(m, phys, pressed, *ly, now);
        if (!m->open) {
            in.hide = phys; memset(in.pending, 0, sizeof in.pending); memset(in.swallowed, 0, sizeof in.swallowed); in.moon = 0;
            fprintf(stderr, "[cheats] menu closed\n");
        }
        if (memcmp(&before, m, sizeof before)) m->version++;
        pthread_mutex_unlock(&g_menu_mu);
        *buttons = 0; *lx = *ly = *rx = *ry = 0;
        if (main) moon_jump(main, 0);
        return;
    }
    int moon_on = atomic_load(&g_cheat[CHEAT_MOON_JUMP]);
    /* L (0) and R (1): a press waits up to 90 ms (L with moon jump on: until X or its release) before the game sees it;
       the other shoulder arriving meanwhile makes both the menu's (and A then opens it), X makes L the moon jump's. A
       press released while waiting reaches the game as a 50 ms tap. */
    for (int s = 0; s < 2; s++) {
        uint64_t bit = s ? PAD_R : PAD_L, other = s ? PAD_L : PAD_R;
        if (pressed & bit) { in.pending[s] = 1; in.swallowed[s] = 0; in.held_since[s] = now; }
        if (in.pending[s] && (phys & other)) { in.pending[0] = in.pending[1] = 0; in.swallowed[0] = in.swallowed[1] = 1; }
        if (!s && moon_on && in.pending[0] && (phys & PAD_X)) { in.pending[0] = 0; in.swallowed[0] = 1; in.moon = 1; }
        if (in.pending[s] && (released & bit)) { in.pending[s] = 0; in.tap_until[s] = now + 50000000ull; }
        if (in.pending[s] && !(!s && moon_on) && now - in.held_since[s] > 90000000ull) in.pending[s] = 0;   /* the game sees it from now on */
        if (released & bit) { in.swallowed[s] = 0; if (!s) in.moon = 0; }
    }
    if (in.swallowed[0] && in.swallowed[1] && (pressed & PAD_A)) {
        m->open = 1; m->version++; in.hide = phys; in.dir = 0; in.moon = 0;
        memset(in.pending, 0, sizeof in.pending); memset(in.swallowed, 0, sizeof in.swallowed);
        items_start();
        fprintf(stderr, "[cheats] menu opened\n");
    }
    pthread_mutex_unlock(&g_menu_mu);
    uint64_t game = phys & ~in.hide;
    for (int s = 0; s < 2; s++) {
        uint64_t bit = s ? PAD_R : PAD_L;
        if (in.pending[s] || in.swallowed[s]) game &= ~bit;
        if (now < in.tap_until[s]) game |= bit;
    }
    *buttons = game;
    if (main) {
        moon_jump(main, moon_on && in.moon && (phys & PAD_L) && (phys & PAD_X));
        refill(main);
    }
}

// ---------- drawing the menu (CoreText into BGRA8, premultiplied; gfx/nvn/nvn_mtl.m puts it over the picture) ----------
typedef struct { CGContextRef c; double s, h; } Pen;   /* s: pixels per point of the 1080-row design; h: bitmap height */
static CGColorRef rgba(double r, double g, double b, double a) {
    static CGColorSpaceRef space; if (!space) space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGFloat v[4] = {r, g, b, a};
    return CGColorCreate(space, v);
}
static void fill_round(Pen *p, double x, double y, double w, double h, double radius, CGColorRef color) {   /* y from the top, in points */
    CGRect r = CGRectMake(x * p->s, p->h - (y + h) * p->s, w * p->s, h * p->s);
    CGPathRef path = CGPathCreateWithRoundedRect(r, radius * p->s, radius * p->s, NULL);
    CGContextSetFillColorWithColor(p->c, color); CGContextAddPath(p->c, path); CGContextFillPath(p->c); CGPathRelease(path);
}
static void stroke_round(Pen *p, double x, double y, double w, double h, double radius, CGColorRef color, double width) {
    CGRect r = CGRectInset(CGRectMake(x * p->s, p->h - (y + h) * p->s, w * p->s, h * p->s), width * p->s / 2, width * p->s / 2);
    CGPathRef path = CGPathCreateWithRoundedRect(r, radius * p->s, radius * p->s, NULL);
    CGContextSetStrokeColorWithColor(p->c, color); CGContextSetLineWidth(p->c, width * p->s); CGContextAddPath(p->c, path); CGContextStrokePath(p->c); CGPathRelease(path);
}
static CTFontRef font(double size, int bold) {
    return CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, size, NULL);
}
/* Draws text with its baseline at y (from the top); align 0: starts at x, 1: ends at x, 2: centred on x; cut to max_w
   with an ellipsis. Returns the width drawn, in points. */
static double text(Pen *p, const char *s, double size, int bold, CGColorRef color, double x, double y, double max_w, int align) {
    CTFontRef f = font(size * p->s, bold);
    CFStringRef str = CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
    if (!str) { CFRelease(f); return 0; }
    const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName}, *vals[] = {f, color};
    CFDictionaryRef attr = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, attr);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    double w = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    if (max_w > 0 && w > max_w * p->s) {
        CFStringRef dots_s = CFStringCreateWithCString(NULL, "…", kCFStringEncodingUTF8);
        CFAttributedStringRef ell = CFAttributedStringCreate(NULL, dots_s, attr);
        CFRelease(dots_s);
        CTLineRef dots = CTLineCreateWithAttributedString(ell);
        CTLineRef cut = CTLineCreateTruncatedLine(line, max_w * p->s, kCTLineTruncationEnd, dots);
        if (cut) { CFRelease(line); line = cut; w = CTLineGetTypographicBounds(line, NULL, NULL, NULL); }
        CFRelease(dots); CFRelease(ell);
    }
    CGContextSetTextMatrix(p->c, CGAffineTransformIdentity);
    CGContextSetTextPosition(p->c, x * p->s - (align == 1 ? w : align == 2 ? w / 2 : 0), p->h - y * p->s);
    CTLineDraw(line, p->c);
    CFRelease(line); CFRelease(as); CFRelease(attr); CFRelease(str); CFRelease(f);
    return w / p->s;
}
/* A controller button as a small rounded badge with its letter, then the label: returns the x after it. */
static double hint(Pen *p, const char *button, const char *label, double x, double y, CGColorRef fg, CGColorRef badge) {
    double bw = strlen(button) > 1 ? 14 + 8.5 * (double)strlen(button) : 24;
    fill_round(p, x, y - 18, bw, 24, 6, badge);
    text(p, button, 14, 1, fg, x + bw / 2, y - 1, 0, 2);
    return x + bw + 8 + text(p, label, 16, 0, fg, x + bw + 8, y, 0, 0) + 22;
}
enum { PANEL_W = 600, TITLE_H = 96, ROW_H = 40, SECTION_H = 36, FOOTER_H = 62 };
static const int PANEL_H = TITLE_H + CHEATS * ROW_H + SECTION_H + BOTW_ITEM_KINDS * ROW_H + FOOTER_H + 12;
static void menu_draw(Pen *p, const Menu *m) {
    CGColorRef panel = rgba(0.045, 0.065, 0.08, 0.92), edge = rgba(1, 1, 1, 0.10), white = rgba(0.95, 0.97, 0.98, 1),
               dim = rgba(0.62, 0.70, 0.75, 1), accent = rgba(0.27, 0.78, 0.93, 1), sel = rgba(0.27, 0.78, 0.93, 0.20),
               dark = rgba(0.03, 0.06, 0.08, 1), badge = rgba(1, 1, 1, 0.16), off = rgba(1, 1, 1, 0.22);
    double W = PANEL_W, H = PANEL_H;
    fill_round(p, 0, 0, W, H, 18, panel);
    stroke_round(p, 0, 0, W, H, 18, edge, 1.5);
    int items_state = atomic_load(&g_items_state);
    double y = TITLE_H;
    if (m->page == PAGE_MAIN) {
        text(p, "Cheats", 30, 1, white, 28, 46, 0, 0);
        text(p, "Changes apply at once and last until the app closes", 16, 0, dim, 28, 76, W - 56, 0);
        for (int r = 0; r < MAIN_ROWS; r++) {
            if (r == CHEATS) {
                text(p, "DROP IN FRONT OF LINK", 13, 1, dim, 28, y + 24, 0, 0);
                y += SECTION_H;
            }
            if (r == m->row) { fill_round(p, 12, y + 2, W - 24, ROW_H - 4, 9, sel); fill_round(p, 12, y + 8, 4, ROW_H - 16, 2, accent); }
            if (r < CHEATS) {
                text(p, cheat_info[r].title, 21, r == m->row, white, 28, y + 27, 230, 0);
                text(p, cheat_info[r].detail, 15, 0, dim, 262, y + 26, W - 262 - 104, 0);
                double px = W - 92;
                if (m->cheats[r]) { fill_round(p, px, y + 8, 64, ROW_H - 16, 12, accent); text(p, "ON", 14, 1, dark, px + 32, y + 25, 0, 2); }
                else { stroke_round(p, px, y + 8, 64, ROW_H - 16, 12, off, 1.5); text(p, "OFF", 14, 1, dim, px + 32, y + 25, 0, 2); }
            } else {
                int kind = r - CHEATS; size_t count; kind_range(kind, &count);
                text(p, botw_item_kind_names[kind], 21, r == m->row, white, 28, y + 27, W - 160, 0);
                char right[32];
                if (items_state == 2) snprintf(right, sizeof right, "%zu  ›", count);
                else snprintf(right, sizeof right, items_state == 3 ? "–  ›" : "…  ›");
                text(p, right, 18, 0, dim, W - 30, y + 26, 0, 1);
            }
            y += ROW_H;
        }
    } else {
        size_t count, first = kind_range(m->kind, &count);
        text(p, botw_item_kind_names[m->kind], 30, 1, white, 28, 46, W - 56, 0);
        char sub[200];
        if (items_state == 3) snprintf(sub, sizeof sub, "The item list could not be read: %s", g_items_error);
        else if (items_state != 2) snprintf(sub, sizeof sub, "Reading the item list…");
        else if (!count) snprintf(sub, sizeof sub, "None in this game");
        else snprintf(sub, sizeof sub, "%d of %zu · dropped two metres in front of Link", m->item + 1, count);
        text(p, sub, 16, 0, dim, 28, 76, W - 56, 0);
        int rows = (int)((H - TITLE_H - FOOTER_H - 12) / ROW_H);
        for (int r = 0; r < rows && m->top + r < (int)count; r++) {
            int i = m->top + r;
            if (i == m->item) { fill_round(p, 12, y + 2, W - 24, ROW_H - 4, 9, sel); fill_round(p, 12, y + 8, 4, ROW_H - 16, 2, accent); }
            text(p, g_items.items[first + (size_t)i].name, 21, i == m->item, white, 28, y + 27, W - 70, 0);
            y += ROW_H;
        }
        if (count > (size_t)rows) {   /* scroll bar */
            double track = rows * ROW_H - 8, knob = fmax(24, track * rows / (double)count), at = (track - knob) * m->top / (double)(count - (size_t)rows);
            fill_round(p, W - 14, TITLE_H + 4, 4, track, 2, rgba(1, 1, 1, 0.08));
            fill_round(p, W - 14, TITLE_H + 4 + at, 4, knob, 2, rgba(1, 1, 1, 0.35));
        }
    }
    double fy = H - FOOTER_H + 6;
    fill_round(p, 12, fy - 8, W - 24, 1, 0, edge);
    if (m->toast[0] && runtime_now_ns() < m->toast_until) text(p, m->toast, 18, 1, accent, 28, fy + 30, W - 56, 0);
    else if (m->page == PAGE_MAIN) {
        double x = hint(p, "A", m->row < CHEATS ? "Switch" : "Open", 28, fy + 30, white, badge);
        x = hint(p, "B", "Close", x, fy + 30, white, badge);
        hint(p, "L+R+A", "Close", x, fy + 30, white, badge);
    } else {
        double x = hint(p, "A", "Drop 1", 28, fy + 30, white, badge);
        x = hint(p, "Y", "Drop 5", x, fy + 30, white, badge);
        x = hint(p, "ZL/ZR", "Kind", x, fy + 30, white, badge);
        hint(p, "B", "Back", x, fy + 30, white, badge);
    }
    CGColorRef all[] = {panel, edge, white, dim, accent, sel, dark, badge, off};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) CGColorRelease(all[i]);
}

/* For gfx/nvn/nvn_mtl.m at each present: the open menu for a picture view_w x view_h pixels as premultiplied BGRA8 pixels
   (*w x *h, rows top first, owned here until the next call) to draw at *x, *y in the picture; *version changes whenever the
   pixels do. max_w x max_h bounds the size (the texture's). Returns 0 while the menu is closed. */
int botw_cheat_overlay(int view_w, int view_h, int max_w, int max_h, const uint8_t **pixels, int *w, int *h, int *x, int *y, uint64_t *version);
int botw_cheat_overlay(int view_w, int view_h, int max_w, int max_h, const uint8_t **pixels, int *w, int *h, int *x, int *y, uint64_t *version) {
    static uint8_t *px; static int pw, ph; static uint64_t drawn = UINT64_MAX; static int drawn_items = -1;
    Menu m;
    pthread_mutex_lock(&g_menu_mu);
    if (g_menu.toast[0] && runtime_now_ns() >= g_menu.toast_until) { g_menu.toast[0] = 0; g_menu.version++; }
    for (int i = 0; i < CHEATS; i++) { int on = atomic_load(&g_cheat[i]); if (g_menu.cheats[i] != on) { g_menu.cheats[i] = on; g_menu.version++; } }
    m = g_menu;
    pthread_mutex_unlock(&g_menu_mu);
    if (!m.open) return 0;
    double s = fmin(fmax(view_h / 1080.0, 0.5), 3.0);
    s = fmin(s, fmin((double)max_w / PANEL_W, (double)max_h / PANEL_H));
    int nw = (int)ceil(PANEL_W * s), nh = (int)ceil(PANEL_H * s), items = atomic_load(&g_items_state);
    if (nw != pw || nh != ph || !px) { free(px); px = calloc((size_t)nw * (size_t)nh, 4); pw = nw; ph = nh; drawn = UINT64_MAX; if (!px) return 0; }
    if (drawn != m.version || drawn_items != items) {
        memset(px, 0, (size_t)pw * (size_t)ph * 4);
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef c = CGBitmapContextCreate(px, (size_t)pw, (size_t)ph, 8, (size_t)pw * 4, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(space);
        if (!c) return 0;
        Pen pen = {c, s, ph};
        atomic_store(&g_menu_rows, (int)((PANEL_H - TITLE_H - FOOTER_H - 12) / ROW_H));
        menu_draw(&pen, &m);
        CGContextRelease(c);
        drawn = m.version; drawn_items = items;
    }
    *pixels = px; *w = pw; *h = ph;
    *x = (int)lround(view_w * 0.035); *y = (view_h - ph) / 2;
    *version = drawn * 4 + (uint64_t)items;
    return 1;
}
