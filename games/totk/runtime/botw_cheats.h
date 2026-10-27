/* Breath of the Wild in-game cheat menu: runtime/botw_cheats.c (the menu, the cheats, dropping items) and
   runtime/botw_item_list.c (the items it can drop, read from the game's own files). */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum {
    BOTW_ITEMS_ONE_HANDED, BOTW_ITEMS_TWO_HANDED, BOTW_ITEMS_SPEARS, BOTW_ITEMS_BOWS, BOTW_ITEMS_SHIELDS, BOTW_ITEMS_ARROWS,
    BOTW_ITEMS_FOOD, BOTW_ITEMS_MONSTER_PARTS, BOTW_ITEMS_CRITTERS, BOTW_ITEMS_RUPEES, BOTW_ITEM_KINDS
};
extern const char *const botw_item_kind_names[BOTW_ITEM_KINDS];

typedef struct { const char *actor, *name; int kind, sort; } BotwItem;
typedef struct { BotwItem *items; size_t count; char *pool; } BotwItemList;

/* Every item the menu offers, by kind and then in the game's own order: weapons, bows, shields, arrow bundles, food and
   ingredients, monster parts and gems, critters and rupees from the RomFS (mods included: Actor/ActorInfo.product.sbyml
   for the actors, the text pack the game opened for their names). Items without a name in that language are left out.
   Returns 0 and fills *list, or -1 with a reason in error. */
int botw_items_load(BotwItemList *list, char *error, size_t error_size);
void botw_items_free(BotwItemList *list);

/* The file formats, exposed for tests/botw_item_list: Yaz0 (malloc'd result or NULL), a SARC member (points into data),
   an MSBT label's text as UTF-8 (control tags dropped; 0 when absent). */
uint8_t *botw_yaz0_decode(const uint8_t *data, size_t size, size_t *out_size);
const uint8_t *botw_sarc_get(const uint8_t *data, size_t size, const char *name, size_t *member_size);
int botw_msbt_text(const uint8_t *data, size_t size, const char *label, char *out, size_t out_size);
