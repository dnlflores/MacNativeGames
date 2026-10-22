/* Screen-copy provenance for BotW UI classification only. This must not change
   GPU ownership, CPU writeback, texture pixels or synchronization. Caller locks. */
#pragma once
#include "totk_serial_set.h"

static inline int botw_ui_whole_copy(unsigned sw, unsigned sh, unsigned dw, unsigned dh,
                                     int sx, int sy, int dx, int dy, int w, int h) {
    return sw && sh && sw==dw && sh==dh && !sx && !sy && !dx && !dy &&
           w>0 && h>0 && (unsigned)w==sw && (unsigned)h==sh;
}

static inline void botw_ui_capture_copy(totk_serial_set *copies, uint64_t src,
                                        uint64_t dst, int rendered, int whole) {
    int screen = rendered || totk_serial_has(copies, src);
    /* An overwrite from static content or a partial copy cannot retain an old
       whole-screen tag. Read the source first to permit an in-place copy. */
    totk_serial_del(copies, dst);
    if (src && dst && whole && screen) totk_serial_add(copies, dst);
}
