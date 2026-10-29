// runtime/botw_button_icons.c on a font pack tests/test_botw_button_icons.py builds (no game data):
//   probe <in.sbfarc> <out.sbfarc>   the pack with its button glyphs redrawn (TOTK_BUTTON_ICONS as set), or the error
//   probe --wanted <RomFS path>      1 when the path would be redrawn
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "botw_button_icons.h"

uint8_t *runtime_romfs_load(const char *path, size_t *size) { (void)path; (void)size; return NULL; }   // botw_item_list.c's
const char *runtime_romfs_text_lang(void) { return ""; }

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--wanted")) { printf("%d\n", botw_button_icons_wanted(argv[2])); return 0; }
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    char error[200] = ""; size_t out_size = 0;
    uint8_t *out = botw_button_icons_pack(d, (size_t)n, &out_size, error, sizeof error);
    if (!out) { printf("error: %s\n", error); return 1; }
    f = fopen(argv[2], "wb"); if (!f || fwrite(out, 1, out_size, f) != out_size) return 2;
    fclose(f); free(out); free(d);
    printf("ok\n");
    return 0;
}
