/* BotW UI geometry; independent of Metal and game assets. */
#pragma once
#include <math.h>
typedef struct { double x, y; } botw_ui_scale;
static inline botw_ui_scale botw_ui_fit(double aspect, int screen_fill) {
    double f = aspect * 9.0 / 16.0;
    if (screen_fill || !isfinite(f) || f <= 0 || fabs(f-1) < .001) return (botw_ui_scale){1,1};
    return f < 1 ? (botw_ui_scale){1,f} : (botw_ui_scale){1/f,1};
}
/* Draws that keep screen mapping instead of the 16:9 layout fit, all on full-view quad
   geometry: a screen capture drawn back (a scissor may crop it; pause backgrounds),
   an unscissored solid fade, and an unscissored overlay whose quad overhangs the view
   (vignettes: the game never showed their edges, which the fit would bring into view
   as bars). Smaller panes that sample a capture (frosted quick-select slots, the item
   card) belong to the layout like their frames and text, so they are fitted too. */
static inline int botw_ui_screen_fill(int full, int scissor_full, int screen, int sampled, int overhang) {
    if (!full) return 0;
    return screen || (scissor_full && (!sampled || overhang));
}
/* A quad reaching more than 10% past the view on some side. */
static inline int botw_ui_overhang(const float xy[][2], int n) {
    for (int i=0;i<n;i++) if (fabs(xy[i][0])>1.1 || fabs(xy[i][1])>1.1) return 1;
    return 0;
}
/* Which view extents a quad covers completely: bit 0 the whole width, bit 1 the whole height. */
static inline int botw_ui_span(const float xy[][2], int n) {
    double lo[2]={1e9,1e9},hi[2]={-1e9,-1e9};
    for(int i=0;i<n;i++)for(int a=0;a<2;a++){ if(!isfinite(xy[i][a]))return 0; lo[a]=fmin(lo[a],xy[i][a]); hi[a]=fmax(hi[a],xy[i][a]); }
    return (lo[0]<=-.9999 && hi[0]>=.9999) | (lo[1]<=-.9999 && hi[1]>=.9999)<<1;
}
/* A pane that spans the view's whole height (whole width) and samples screen content exactly that tall (wide): a strip
   of a screen capture, which keeps screen mapping in that direction. */
static inline int botw_ui_strip(int span, unsigned w, unsigned h, unsigned target_w, unsigned target_h) {
    return ((span & 2) && h == target_h && w < target_w) || ((span & 1) && w == target_w && h < target_h);
}
/* A layer the game sized from the screen (one side exactly the target's, the other different): drawn with the layout,
   never a vignette's static artwork. The full-screen captures (the target's exact size) are classified elsewhere. */
static inline int botw_ui_layer(unsigned w, unsigned h, unsigned target_w, unsigned target_h) {
    return w && h && ((h == target_h && w != target_w) || (w == target_w && h != target_h));
}
/* Quick-select blur/capture buffers retain the game's original 16:9 dimensions.
   Only GPU-produced images can qualify; artwork/atlases uploaded from disk cannot.
   The smallest size (1/8) is measured against the 16:9 region of the target: the shop's
   sell-screen backdrop is a 320x180 capture, which the target's own 1764 rows rejected
   (fitted, it showed as a squeezed ghost of the scene behind the menu). */
static inline int botw_ui_screen_texture(unsigned w,unsigned h,unsigned target_w,unsigned target_h,int rendered) {
    if(!rendered || !w || !h || !target_w || !target_h)return 0;
    double rw=fmin(target_w,target_h*16.0/9),rh=fmin(target_h,target_w*9.0/16);
    if(w<rw/8-.5 || h<rh/8-.5)return 0;
    double aspect=(double)w/h;
    return fabs(aspect*target_h/target_w-1)<.015 || fabs(aspect*9/16-1)<.015;
}
/* Shared dimensions describe the vertex coordinate system, not the pane bounds.
   Project the actual fetched vertices before considering a screen-copy exception. */
static inline int botw_ui_project(const float *m, const float p[4], float xy[2]) {
    double x=p[0]*m[132]+m[134], y=-p[1]*m[133]+m[135];
    double q[4]={m[16]*x+m[17]*y+m[19], m[20]*x+m[21]*y+m[23], m[24]*x+m[25]*y+m[27], 1};
    double w=m[12]*q[0]+m[13]*q[1]+m[14]*q[2]+m[15];
    if (!isfinite(w) || w<=.000001) return 0;
    for (int i=0;i<2;i++) {
        xy[i]=(float)((m[4*i]*q[0]+m[4*i+1]*q[1]+m[4*i+2]*q[2]+m[4*i+3])/w);
        if (!isfinite(xy[i])) return 0;
    }
    return 1;
}
/* A quad must cover all four NDC corners with two triangles sharing a diagonal.
   A bounding box alone also accepts overlapping triangles with uncovered pixels.
   shifted: instead of covering the view, the quad has exactly the view's size at
   any offset (a page sliding in or out). */
static inline int botw_ui_quad(const float xy[][2], int n, int strip, int shifted) {
    if ((!strip && n!=6) || (strip && n!=4)) return 0;
    double lo[2]={xy[0][0],xy[0][1]},hi[2]={xy[0][0],xy[0][1]};
    for(int i=0;i<n;i++)for(int a=0;a<2;a++) {
        if (!isfinite(xy[i][a])) return 0;
        lo[a]=fmin(lo[a],xy[i][a]);hi[a]=fmax(hi[a],xy[i][a]);
    }
    if(shifted ? fabs(hi[0]-lo[0]-2)>.001 || fabs(hi[1]-lo[1]-2)>.001
               : lo[0]>-.9999 || lo[1]>-.9999 || hi[0]<.9999 || hi[1]<.9999) return 0;
    unsigned corner[6],masks[2]={0,0};
    for(int i=0;i<n;i++) {
        unsigned c=0;
        for(int a=0;a<2;a++) {
            if(fabs(xy[i][a]-hi[a])<.0001)c|=1u<<a;
            else if(fabs(xy[i][a]-lo[a])>=.0001)return 0;
        }
        corner[i]=c;
    }
    for(int t=0;t<2;t++)for(int j=0;j<3;j++)masks[t]|=1u<<corner[strip?t+j:3*t+j];
    unsigned both=masks[0]&masks[1];
    return (masks[0]|masks[1])==15 && (both==6 || both==9) &&
           __builtin_popcount(masks[0])==3 && __builtin_popcount(masks[1])==3;
}
static inline int botw_ui_screen_quad(const float xy[][2], int n, int strip) { return botw_ui_quad(xy,n,strip,0); }
