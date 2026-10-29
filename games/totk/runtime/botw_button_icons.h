/* Breath of the Wild's button prompts drawn as PlayStation buttons (TOTK_BUTTON_ICONS=playstation): runtime/botw_button_icons.c,
   applied by runtime/romfs.c when the game opens a font pack or Pack/Bootup.pack. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* 1 when TOTK_BUTTON_ICONS asks for PlayStation icons and path (RomFS-relative) is a font pack, Font/Font_<region>.sbfarc,
   or Pack/Bootup.pack (the action prompt's key pictures). */
int botw_button_icons_wanted(const char *path);
/* A font pack (Yaz0 SARC) with its icon font's button glyphs redrawn, as a malloc'd Yaz0 file of the same unpacked size; or
   Pack/Bootup.pack (a SARC) with the key pictures of its Layout/Common.sblarc redrawn. NULL with a reason in error when the
   file has neither of the parts this code knows. */
uint8_t *botw_button_icons_pack(const uint8_t *data, size_t size, size_t *out_size, char *error, size_t error_size);
/* The same for one icon font (BFFNT) in place: the number of glyphs redrawn, or -1 with a reason. For tests. */
int botw_button_icons_font(uint8_t *font, size_t size, char *error, size_t error_size);
/* The same for the key pictures among a layout archive's textures (its timg/__Combined.bntx) in place. For tests. */
int botw_button_icons_layout_textures(uint8_t *bntx, size_t size, char *error, size_t error_size);
