/* BotW UI geometry; independent of Metal and game assets. */
#pragma once
#include <math.h>
typedef struct { double x, y; } botw_ui_scale;
static inline botw_ui_scale botw_ui_fit(double aspect, int screen_fill) {
    double f = aspect * 9.0 / 16.0;
    if (screen_fill || !isfinite(f) || f <= 0 || fabs(f-1) < .001) return (botw_ui_scale){1,1};
    return f < 1 ? (botw_ui_scale){1,f} : (botw_ui_scale){1/f,1};
}
/* Quick-select blur/capture buffers retain the game's original 16:9 dimensions.
   Only GPU-produced images can qualify; artwork/atlases uploaded from disk cannot. */
static inline int botw_ui_screen_texture(unsigned w,unsigned h,unsigned target_w,unsigned target_h,int rendered) {
    if(!rendered || !w || !h || !target_w || !target_h || w<target_w/8 || h<target_h/8)return 0;
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
   A bounding box alone also accepts overlapping triangles with uncovered pixels. */
static inline int botw_ui_screen_quad(const float xy[][2], int n, int strip) {
    if ((!strip && n!=6) || (strip && n!=4)) return 0;
    double lo[2]={xy[0][0],xy[0][1]},hi[2]={xy[0][0],xy[0][1]};
    for(int i=0;i<n;i++)for(int a=0;a<2;a++) {
        if (!isfinite(xy[i][a])) return 0;
        lo[a]=fmin(lo[a],xy[i][a]);hi[a]=fmax(hi[a],xy[i][a]);
    }
    if(lo[0]>-.9999 || lo[1]>-.9999 || hi[0]<.9999 || hi[1]<.9999) return 0;
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
