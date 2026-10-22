"""Execute production camera stubs on Apple silicon using synthetic memory, without game assets."""
from pathlib import Path
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == 'darwin' and platform.machine() == 'arm64' and shutil.which('clang'),
                     'Apple silicon runtime required')
class BotwAspect(unittest.TestCase):
    def test_camera_stubs(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = ROOT / 'games/totk/runtime/game_patches_botw.c'
            harness = tmp / 'probe.c'
            harness.write_text(f'''#include "{source}"
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
_Noreturn void runtime_fail(const char *s){{fprintf(stderr,"%s\\n",s);exit(1);}}
int main(void){{
 Module m={{0}};m.file_size=0x3000000;m.size=0x3004000;m.text_end=0x2200100;
 m.base=mmap(NULL,m.size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 if(m.base==MAP_FAILED)return 2;
 uint32_t div=0x1e211800,load=0xbd40b000,ret=0xd65f03c0;
 const uint32_t sites[]={{0xe0ad68,0x1271870,0x15c2148}};
 memcpy(m.base+sites[0],&div,4);memcpy(m.base+sites[1],&load,4);memcpy(m.base+sites[2],&div,4);
 for(unsigned i=0;i<3;i++)memcpy(m.base+sites[i]+4,&ret,4);
 patch_aspect(&m);
 if(mprotect(m.base,0x2204000,PROT_READ|PROT_EXEC))return 3;
 sys_icache_invalidate(m.base,0x2204000);
 float requested=runtime_aspect_now();
 const float originals[]={{16.0f/9.0f,1.777778f,0.75f,1.0f,1.451546f}};
 float camera[64]={{0}};camera[0xb0/4]=16.0f/9.0f;
 for(unsigned k=0;k<5;k++)for(unsigned i=0;i<3;i++){{
  float original=originals[k],expected=k<2 && requested?requested:original;
  camera[0xb0/4]=original;uint32_t original_bits;memcpy(&original_bits,&original,4);
  uint32_t bits;uint64_t scratch,scratch11,scratch12,before,after,flags_before,flags_after;
  void *fn=m.base+sites[i];
  __asm__ volatile("mov %x[before], sp\\n"
    "mov x0, %x[camera]\\n"
    "mov x10, #0x1234\\n"
    "mov x11, #0x2345\\n"
    "mov x12, #0x3456\\n"
    "mov x9, #0xa0000000\\n"
    "msr nzcv, x9\\n"
    "mrs %x[flags_before], nzcv\\n"
    "fmov s0, %w[original]\\n"
    "fmov s1, #1.0\\n"
    "blr %x[fn]\\n"
    "fmov %w[bits], s0\\n"
    "mov %x[scratch], x10\\n"
    "mov %x[scratch11], x11\\n"
    "mov %x[scratch12], x12\\n"
    "mrs %x[flags_after], nzcv\\n"
    "mov %x[after], sp\\n"
    :[before]"=&r"(before),[after]"=r"(after),[bits]"=r"(bits),[scratch]"=r"(scratch),[scratch11]"=r"(scratch11),[scratch12]"=r"(scratch12),[flags_before]"=&r"(flags_before),[flags_after]"=r"(flags_after)
    :[camera]"r"(camera),[fn]"r"(fn),[original]"r"(original_bits):"x0","x9","x10","x11","x12","x30","v0","v1","cc","memory");
  float result;memcpy(&result,&bits,4);
  if(fabsf(result-expected)>0.00001f || scratch!=0x1234 || scratch11!=0x2345 || scratch12!=0x3456 || before!=after || flags_before!=flags_after)return 4;
 }}
 return 0;
}}''')
            binary = tmp / 'probe'
            subprocess.run(['clang', '-std=gnu11', '-Os', '-DGAME_BOTW=1', '-DGAME_BOTW160=1',
                            '-I' + str(source.parent), str(harness), '-Wl,-dead_strip',
                            '-Wl,-undefined,dynamic_lookup', '-o', str(binary)], check=True, capture_output=True)
            for ratio in (0, 1.451546, 16/9, 21/9):
                with self.subTest(ratio=ratio):
                    subprocess.run([str(binary)], env=dict(os.environ, TOTK_ASPECT=str(ratio)),
                                   check=True, capture_output=True)
