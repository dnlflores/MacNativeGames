/* BCML (.bnp) mods through UKMM (tools/ukmm_bridge/src/lib.rs, GPL-3.0-or-later). Paths are UTF-8; `romfs` is the
   folder holding the game's runtime-index.bin + romfs.bin (or romfs.bin.000, ...), `aoc` the add-on content 1 folder
   (NULL: no DLC). `lang` is the game's text language (USen, EUfr, JPja, ...). Each function returns NULL on success or
   an error message; free returned strings with totk_ukmm_free. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Converts the .bnp into the UKMM mod out_zip (work: a scratch folder); *meta_json receives the mod's metadata
   (name, version, description, option_groups) as JSON. */
char *totk_ukmm_convert(const char *bnp, const char *romfs, const char *aoc, const char *work, const char *lang,
                        const char *out_zip, char **meta_json);
/* Merges the UKMM mods of mods_json ([{"path": zip, "options": [option paths]}], lowest priority first) into out_dir:
   01007EF00011E000/romfs (base game) and 01007EF00011F001/romfs (add-on content), resource size table included. */
char *totk_ukmm_merge(const char *mods_json, const char *romfs, const char *aoc, const char *lang, const char *out_dir);
void totk_ukmm_free(char *s);
#ifdef __cplusplus
}
#endif
