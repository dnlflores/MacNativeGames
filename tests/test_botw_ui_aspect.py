"""Actual UI vertices determine screen fills; shared canvas dimensions do not."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('clang'),'C compiler required')
class BotwUIAspect(unittest.TestCase):
    def test_copied_capture_provenance(self):
        header=ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/botw_ui_capture.h'
        aspect=header.with_name('botw_ui_aspect.h')
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            (p/'copy.c').write_text(f'''#include "{header}"
#include "{aspect}"
#include <assert.h>
int main(void){{
 totk_serial_set copies={{0}};
 int whole=botw_ui_whole_copy(1280,720,1280,720,0,0,0,0,1280,720);
 assert(whole);
 /* Rendered scene -> capture -> second copy, with neither destination itself
    rendered into. Both count as screen content; drawn back over the full view
    (a scissor may crop it) they keep screen mapping. */
 botw_ui_capture_copy(&copies,1,2,1,whole);
 botw_ui_capture_copy(&copies,2,3,0,whole);
 for(int serial=2;serial<=3;serial++){{
  int screen=botw_ui_screen_texture(1280,720,2560,1764,totk_serial_has(&copies,serial));
  botw_ui_scale scale=botw_ui_fit(2816.0/1940,botw_ui_screen_fill(1,0,screen,1,0));
  assert(screen && scale.x==1 && scale.y==1);
 }}
 /* Same-sized static artwork cannot become a capture just by being copied. */
 botw_ui_capture_copy(&copies,10,11,0,whole);
 assert(!totk_serial_has(&copies,11));
 /* A partial or offset copy, and an overwrite from static content, remove
    previous capture provenance rather than misclassifying subsequent draws. */
 assert(!botw_ui_whole_copy(1280,720,1280,720,0,0,0,0,640,720));
 assert(!botw_ui_whole_copy(1280,720,1280,720,1,0,0,0,1280,720));
 assert(!botw_ui_whole_copy(1280,720,640,360,0,0,0,0,1280,720));
 botw_ui_capture_copy(&copies,1,2,1,0);
 botw_ui_capture_copy(&copies,10,3,0,whole);
 assert(!totk_serial_has(&copies,2) && !totk_serial_has(&copies,3));
 botw_ui_capture_copy(&copies,1,2,1,whole);
 botw_ui_capture_copy(&copies,2,2,0,whole);
 assert(totk_serial_has(&copies,2));
 totk_serial_del(&copies,2); /* finalized serial / CPU upload */
 assert(!totk_serial_has(&copies,2));
 totk_serial_destroy(&copies);
 return 0;
}}''')
            binary=p/'copy'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',str(p/'copy.c'),'-lm','-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True)

    def test_layout_and_capture_proportions(self):
        header=ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/botw_ui_aspect.h'
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            (p/'probe.c').write_text(f'''#include "{header}"
#include <assert.h>
int main(void){{
 double aspects[]={{4.0/3.0,2816.0/1940.0,16.0/9.0,21.0/9.0}};
 for(unsigned i=0;i<4;i++){{
  double a=aspects[i],f=a*9.0/16.0;
  botw_ui_scale layout=botw_ui_fit(a,0),screen=botw_ui_fit(a,1);
  assert(fabs(f*layout.x/layout.y-1)<1e-9);
  /* Every ordinary pane, logo and glyph fits; none expands past screen edges. */
  assert(layout.x<=1 && layout.y<=1 && layout.x>0 && layout.y>0);
  assert(screen.x==1 && screen.y==1);
  /* Pause backgrounds: a full-view capture keeps screen mapping, also when a
     scissor crops it. */
  botw_ui_scale whole=botw_ui_fit(a,botw_ui_screen_fill(1,1,1,1,0));
  botw_ui_scale cropped=botw_ui_fit(a,botw_ui_screen_fill(1,0,1,1,0));
  assert(whole.x==1 && whole.y==1 && cropped.x==1 && cropped.y==1);
  /* A smaller pane sampling a capture (frosted quick-select slot, item card,
     pickup label) is fitted with its frame and text. */
  botw_ui_scale frosted=botw_ui_fit(a,botw_ui_screen_fill(0,1,1,1,0));
  assert(frosted.x==layout.x && frosted.y==layout.y);
  /* A sampled panel/artwork or a partial solid stays in the ordinary UI fit. */
  botw_ui_scale panel=botw_ui_fit(a,botw_ui_screen_fill(0,1,0,1,0));
  botw_ui_scale solid=botw_ui_fit(a,botw_ui_screen_fill(0,1,0,0,0));
  assert(panel.x==layout.x && panel.y==layout.y);
  assert(solid.x==layout.x && solid.y==layout.y);
  /* Full-view solid fades fill unless scissored; full-view artwork keeps its
     proportions unless its quad overhangs the view (vignettes). */
  assert(botw_ui_screen_fill(1,1,0,0,0) && !botw_ui_screen_fill(1,0,0,0,0));
  assert(!botw_ui_screen_fill(1,1,0,1,0) && botw_ui_screen_fill(1,1,0,1,1) && !botw_ui_screen_fill(1,0,0,1,1));
 }}
 /* The quick-select vignette's quad reaches 1.778 past the view vertically. */
 const float vignette[4][2]={{{{-1,-1.778f}},{{1,-1.778f}},{{-1,1.778f}},{{1,1.778f}}}};
 const float exact[4][2]={{{{-1,-1}},{{1,-1}},{{-1,1}},{{1,1}}}};
 assert(botw_ui_overhang(vignette,4) && !botw_ui_overhang(exact,4));
 float m[136]={{0}};m[0]=2.0f/1280;m[5]=2.0f/720;m[10]=m[15]=1;
 m[16]=m[21]=m[26]=1;m[132]=1280;m[133]=720;m[134]=-640;m[135]=360;
 const float quad[6][4]={{{{0,0,0,1}},{{1,0,0,1}},{{0,1,0,1}},{{1,0,0,1}},{{1,1,0,1}},{{0,1,0,1}}}};
 float xy[6][2];
 for(int i=0;i<6;i++)assert(botw_ui_project(m,quad[i],xy[i]));
 assert(botw_ui_screen_quad((const float (*)[2])xy,6,0));
 /* Same shared uniform dimensions, but the vertices describe a small icon. */
 for(int i=0;i<6;i++){{float v[4]={{quad[i][0]*.1f,quad[i][1]*.1f,0,1}};assert(botw_ui_project(m,v,xy[i]));}}
 assert(!botw_ui_screen_quad((const float (*)[2])xy,6,0));
 /* Even a full-size quad translated partly outside the view cannot count. */
 m[19]=128;for(int i=0;i<6;i++)assert(botw_ui_project(m,quad[i],xy[i]));
 assert(!botw_ui_screen_quad((const float (*)[2])xy,6,0));
 /* It is a view-sized quad slid sideways: the pause menu's page switch slides its
    screen captures this way, and the caller keeps those (only) in screen mapping. */
 assert(botw_ui_quad((const float (*)[2])xy,6,0,1));
 const float slid[4][2]={{{{1.656f,-1}},{{3.656f,-1}},{{1.656f,1}},{{3.656f,1}}}};
 assert(botw_ui_quad(slid,4,1,1) && !botw_ui_screen_quad(slid,4,1));
 /* A smaller pane slid the same way is not view-sized. */
 for(int i=0;i<6;i++){{float v[4]={{quad[i][0]*.9f,quad[i][1],0,1}};assert(botw_ui_project(m,v,xy[i]));}}
 assert(!botw_ui_quad((const float (*)[2])xy,6,0,1));
 /* Layout captures can use a perspective UI camera with constant w != 1. */
 m[19]=0;m[27]=-10;m[12]=m[13]=m[15]=0;m[14]=-1;
 m[0]*=10;m[5]*=10;
 for(int i=0;i<6;i++)assert(botw_ui_project(m,quad[i],xy[i]));
 assert(botw_ui_screen_quad((const float (*)[2])xy,6,0));
 m[27]=10;assert(!botw_ui_project(m,quad[0],xy[0]));
 /* Real quick-select capture sizes on a 2560x1764 iPad canvas. */
 assert(botw_ui_screen_texture(1280,720,2560,1764,1));
 assert(botw_ui_screen_texture(640,360,2560,1764,1));
 assert(botw_ui_screen_texture(1280,882,2560,1764,1));
 assert(!botw_ui_screen_texture(1280,720,2560,1764,0));
 assert(!botw_ui_screen_texture(840,840,2560,1764,1));
 assert(!botw_ui_screen_texture(192,235,2560,1764,1));
 const float overlap[6][2]={{{{-1,-1}},{{1,-1}},{{-1,1}},{{-1,-1}},{{1,-1}},{{1,1}}}};
 assert(!botw_ui_screen_quad(overlap,6,0));
 const float strip[4][2]={{{{-1,-1}},{{1,-1}},{{-1,1}},{{1,1}}}};
 assert(botw_ui_screen_quad(strip,4,1));
 return 0;
}}''')
            binary=p/'probe'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',str(p/'probe.c'),'-lm','-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True)
