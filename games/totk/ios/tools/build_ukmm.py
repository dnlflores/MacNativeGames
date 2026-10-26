#!/usr/bin/env python3
"""Build the BCML / UKMM bridge (tools/ukmm_bridge) for iOS, or macOS for tests: fetches UKMM at a pinned commit,
applies tools/ukmm_bridge/ukmm.patch, adds the bridge to UKMM's workspace (its Cargo.lock pins the dependencies) and
builds libtotk_ukmm.a. Needs rustup's cargo with the aarch64-apple-ios target (CARGO, default ~/.cargo/bin/cargo):
  curl https://sh.rustup.rs -sSf | sh -s -- -y --no-modify-path --profile minimal --target aarch64-apple-ios
UKMM and the bridge are GPL-3.0-or-later (tools/ukmm_bridge/LICENSE)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

REPO = 'https://github.com/NiceneNerd/ukmm.git'
COMMIT = 'c0eae84d8d73375eebb2175e2faa8b5ae324ee1a'
BRIDGE = Path(__file__).resolve().parents[2] / 'tools' / 'ukmm_bridge'


def git(*args, cwd):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def checkout(source: Path):
    if not (source / '.git').is_dir():
        source.mkdir(parents=True, exist_ok=True)
        git('init', '-q', cwd=source)
        git('remote', 'add', 'origin', REPO, cwd=source)
    if subprocess.run(['git', 'cat-file', '-e', COMMIT + '^{commit}'], cwd=source, capture_output=True).returncode:
        git('fetch', '-q', '--depth', '1', 'origin', COMMIT, cwd=source)
    git('checkout', '-q', '--force', COMMIT, cwd=source)
    git('clean', '-q', '-fdx', '-e', 'target', cwd=source)
    git('apply', str(BRIDGE / 'ukmm.patch'), cwd=source)
    crate = source / 'crates' / 'totk-bridge'
    if crate.is_symlink() or crate.is_file():
        crate.unlink()
    shutil.rmtree(crate, ignore_errors=True)
    shutil.copytree(BRIDGE, crate, ignore=shutil.ignore_patterns('target', '*.patch', '__pycache__'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--platform', choices=['ios', 'macos'], default='ios')
    args = parser.parse_args()
    out = args.out.resolve()
    source = out / 'ukmm'
    checkout(source)
    cargo = os.environ.get('CARGO', str(Path.home() / '.cargo/bin/cargo'))
    target = 'aarch64-apple-ios' if args.platform == 'ios' else 'aarch64-apple-darwin'
    # The C parts (zlib, unrar) follow SDKROOT: the caller's may be the Mac SDK (build_ipa.py), so it is set per platform.
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos' if args.platform == 'ios' else 'macosx', '--show-sdk-path'], text=True).strip()
    env = {k: v for k, v in os.environ.items() if k not in ('CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS')}
    env.update(SDKROOT=sdk, IPHONEOS_DEPLOYMENT_TARGET='26.0', MACOSX_DEPLOYMENT_TARGET='14.0')
    # roead builds zlib-ng with a bare "cmake ." inside its git checkout: its own Cargo home per platform keeps that
    # checkout apart, and CMake (3.21+) takes the iOS toolchain from the environment when it configures it.
    env['CARGO_HOME'] = str(out / f'cargo-home-{args.platform}')
    rustup = Path(cargo).with_name('rustup')   # the rustup toolchain's compiler (with the iOS standard library), not one on PATH
    if rustup.exists():
        env['RUSTC'] = subprocess.check_output([str(rustup), 'which', 'rustc'], text=True).strip()
    if args.platform == 'ios':
        toolchain = out / 'ios-toolchain.cmake'
        # CMAKE_SYSTEM_PROCESSOR: zlib-ng's architecture check comes out empty when cross-compiling without it
        toolchain.write_text('set(CMAKE_SYSTEM_NAME iOS)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\nset(CMAKE_OSX_ARCHITECTURES arm64)\n'
                             f'set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0)\nset(CMAKE_OSX_SYSROOT "{sdk}")\n'
                             'set(WITH_NEON OFF CACHE BOOL "")\nset(WITH_ACLE OFF CACHE BOOL "")\nset(ZLIB_ENABLE_TESTS OFF CACHE BOOL "")\n')
        env['CMAKE_TOOLCHAIN_FILE'] = str(toolchain)
    subprocess.run([cargo, 'build', '--release', '--target', target, '-p', 'totk-bridge', '--lib'], cwd=source, env=env, check=True)
    lib = out / args.platform / 'lib'
    lib.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / 'target' / target / 'release' / 'libtotk_ukmm.a', lib / 'libtotk_ukmm.a')
    shutil.copyfile(BRIDGE / 'totk_ukmm.h', out / args.platform / 'totk_ukmm.h')
    print(f'UKMM_DEPS={out / args.platform}')


if __name__ == '__main__':
    main()
