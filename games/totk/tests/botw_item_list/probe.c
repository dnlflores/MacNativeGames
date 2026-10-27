// runtime/botw_item_list.c on files tests/test_botw_item_list.py builds: the cheat menu's item list (BYML ActorInfo, the
// text pack's SARC / Yaz0 / MSBT) without any game data. ROMFS_DIR holds the files under their RomFS paths, TEXT_LANG is
// the language the game would have opened. Prints the list and the reader checks as JSON.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botw_cheats.h"

uint8_t *runtime_romfs_load(const char *path, size_t *size) {
    char full[1024]; snprintf(full, sizeof full, "%s/%s", getenv("ROMFS_DIR"), path);
    FILE *f = fopen(full, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n ? (size_t)n : 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f); if (d) *size = (size_t)n; return d;
}
const char *runtime_romfs_text_lang(void) { const char *l = getenv("TEXT_LANG"); return l ? l : ""; }

static void json_str(const char *s) {
    putchar('"');
    for (; *s; s++) { if (*s == '"' || *s == '\\') putchar('\\'); putchar(*s); }
    putchar('"');
}
int main(int argc, char **argv) {
    BotwItemList list; char error[160] = "";
    if (argc > 1 && !strcmp(argv[1], "error")) {   /* expected to fail: print the reason */
        int r = botw_items_load(&list, error, sizeof error);
        printf("{\"result\":%d,\"error\":", r); json_str(error); printf("}\n");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "msbt")) {   /* msbt <file> <label>...: the texts */
        size_t n; uint8_t *d = runtime_romfs_load(argv[2], &n);
        printf("{");
        for (int i = 3; i < argc; i++) {
            char text[256]; int found = d && botw_msbt_text(d, n, argv[i], text, sizeof text);
            if (i > 3) printf(",");
            json_str(argv[i]); printf(":"); if (found) json_str(text); else printf("null");
        }
        printf("}\n"); free(d); return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "yaz0")) {   /* yaz0 <compressed> <raw>: decodes to the same bytes */
        size_t cn, rn, on = 0; uint8_t *c = runtime_romfs_load(argv[2], &cn), *r = runtime_romfs_load(argv[3], &rn);
        uint8_t *o = c ? botw_yaz0_decode(c, cn, &on) : NULL;
        uint8_t *cut = c ? botw_yaz0_decode(c, cn / 2, &on) : NULL;   /* truncated input fails instead of reading past it */
        printf("{\"same\":%s,\"truncated_rejected\":%s}\n", o && r && on == rn && !memcmp(o, r, rn) ? "true" : "false", cut ? "false" : "true");
        free(c); free(r); free(o); free(cut); return 0;
    }
    int r = botw_items_load(&list, error, sizeof error);
    printf("{\"result\":%d,\"error\":", r); json_str(error); printf(",\"items\":[");
    for (size_t i = 0; i < list.count; i++) {
        if (i) printf(",");
        printf("["); json_str(list.items[i].actor); printf(","); json_str(list.items[i].name); printf(",%d,%d]", list.items[i].kind, list.items[i].sort);
    }
    printf("]}\n");
    botw_items_free(&list);
    return 0;
}
