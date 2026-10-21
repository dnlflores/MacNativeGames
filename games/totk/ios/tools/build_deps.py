#!/usr/bin/env python3
"""Build the audio and VP9 static libraries for a physical iOS device."""
import hashlib, os, subprocess, sys, tempfile
from pathlib import Path

out = Path(sys.argv[1]).resolve(); out.mkdir(parents=True, exist_ok=True)
# libvpx's configure splits its flags and paths at spaces (a game folder often has them), so work through a link without
work = out
if any(c.isspace() for c in str(out)):
    work = Path(tempfile.gettempdir())/f'mng-ios-deps-{hashlib.sha256(str(out).encode()).hexdigest()[:12]}'
    if work.is_symlink(): work.unlink()
    work.symlink_to(out)
sdk = subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'], text=True).strip()
src = work/'src'; build = work/'build'; src.mkdir(exist_ok=True); build.mkdir(exist_ok=True)
sdklink=work/'sdkroot/Platforms/iPhoneOS.platform/Developer/SDKs'/Path(sdk).name
if sdklink.is_symlink(): sdklink.unlink()
sdklink.parent.mkdir(parents=True,exist_ok=True)
if not sdklink.exists(): sdklink.symlink_to(sdk)
def run(cmd, cwd=None, env=None): subprocess.run(cmd, cwd=cwd, env=env, check=True)
def checkout(url, tag, dest):
    if not (dest/'.git').exists(): run(['git','clone','--depth','1','--branch',tag,url,str(dest)])

opus=src/'opus'; checkout('https://github.com/xiph/opus.git','v1.6.1',opus)
opus_build=build/'opus-ninja'
run(['cmake','-G','Ninja','-S',str(opus),'-B',str(opus_build),'-DCMAKE_SYSTEM_NAME=iOS',f'-DCMAKE_OSX_SYSROOT={sdklink}',
     '-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0','-DOPUS_BUILD_SHARED_LIBRARY=OFF',
     '-DOPUS_BUILD_TESTING=OFF','-DOPUS_BUILD_PROGRAMS=OFF','-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF','-DCMAKE_BUILD_TYPE=Release'])
run(['cmake','--build',str(opus_build),'--config','Release','-j'])

vpx=src/'libvpx'; checkout('https://chromium.googlesource.com/webm/libvpx.git','v1.16.0',vpx)
vpxout=build/'libvpx'; vpxout.mkdir(exist_ok=True)
bindir=build/'ios-bin'; bindir.mkdir(exist_ok=True)
real_sdk=sdk; real_tools=Path(os.environ.get('DEVELOPER_DIR',subprocess.check_output(['xcode-select','-p'],text=True).strip()))/'Toolchains/XcodeDefault.xctoolchain/usr/bin'
for tool in ('clang','clang++','ar','as','strip'):
    link=bindir/tool
    if link.is_symlink(): link.unlink()
    if not link.exists(): link.symlink_to(real_tools/tool)
wrapper=bindir/'xcrun'
wrapper.write_text(f'''#!/usr/bin/env python3
import subprocess,sys
r=subprocess.run(["/usr/bin/xcrun",*sys.argv[1:]],capture_output=True,text=True)
s=r.stdout.replace({real_sdk!r},{str(sdklink)!r}).replace({str(real_tools)!r},{str(bindir)!r})
sys.stdout.write(s);sys.stderr.write(r.stderr);sys.exit(r.returncode)
''')
wrapper.chmod(0o755)
vpx_env=dict(os.environ,PATH=str(bindir)+os.pathsep+os.environ.get('PATH',''))
run([str(vpx/'configure'),'--target=arm64-darwin-gcc','--disable-examples','--disable-tools',
     '--disable-docs','--disable-unit-tests','--disable-webm-io','--enable-pic','--disable-shared','--enable-static',
     f'--extra-cflags=-target arm64-apple-ios26.0 -isysroot {sdklink} -miphoneos-version-min=26.0'], cwd=vpxout, env=vpx_env)
run(['make','-j',str(os.cpu_count() or 4),'libvpx.a'],cwd=vpxout,env=vpx_env)
# SPIRV-Cross for the in-app shader translator (tools/totk_xlate/Library.cs): the pinned sources of the Mac's spirv-cross
# program (build_app.py SPIRV_CROSS_SOURCES / FLAGS) with ios/spvc/totk_spvc.cpp in place of its main.cpp
totk=Path(__file__).resolve().parents[2]; spvc_src=totk/'tooling/spirv-cross'; spvc_obj=build/'spirv-cross'; spvc_obj.mkdir(exist_ok=True)
spvc_flags=['-O3','-DNDEBUG','-std=c++11','-target','arm64-apple-ios26.0','-isysroot',str(sdklink),'-Wno-deprecated-declarations']
spvc_units=[spvc_src/f'{n}.cpp' for n in ('spirv_cfg','spirv_cpp','spirv_cross','spirv_cross_parsed_ir','spirv_cross_util','spirv_glsl',
                                         'spirv_hlsl','spirv_msl','spirv_parser','spirv_reflect')]+[totk/'ios/spvc/totk_spvc.cpp']
import concurrent.futures
with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
    list(pool.map(lambda u: run(['xcrun','clang++',*spvc_flags,'-I',str(spvc_src),'-c',str(u),'-o',str(spvc_obj/(u.stem+'.o'))]),spvc_units))
lib=out/'lib'; include=out/'include'; lib.mkdir(exist_ok=True); include.mkdir(exist_ok=True)
(lib/'libtotk_spvc.a').unlink(missing_ok=True)
run(['libtool','-static','-o',str(lib/'libtotk_spvc.a'),*[str(spvc_obj/(u.stem+'.o')) for u in spvc_units]])
import shutil
shutil.copy2(opus_build/'libopus.a',lib/'libopus.a')
shutil.copy2(vpxout/'libvpx.a',lib/'libvpx.a')
shutil.copytree(opus/'include',include,dirs_exist_ok=True)
shutil.copytree(vpx/'vpx',include/'vpx',dirs_exist_ok=True)
shutil.copy2(vpxout/'vpx_config.h',include/'vpx_config.h')
print(f'IOS_DEPS={out}')
