#!/usr/bin/env python3
"""Build pinned libarchive RAR readers for iOS, or macOS for extractor tests."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

VERSION = '3.8.9'
SHA256 = '888c934f9d95648ecb9163dc8e23ab80a476ecb81a8f1154704a227b5b676dde'
URL = f'https://github.com/libarchive/libarchive/releases/download/v{VERSION}/libarchive-{VERSION}.tar.xz'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--platform', choices=['ios', 'macos'], default='ios')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f'libarchive-{VERSION}.tar.xz'
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        temporary = archive.with_suffix('.download')
        subprocess.run(['curl', '--fail', '--location', '--max-time', '120', URL, '--output', str(temporary)], check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            temporary.unlink()
            raise RuntimeError('libarchive download SHA-256 mismatch')
        temporary.replace(archive)
    source = out / f'libarchive-{VERSION}'
    if not source.is_dir():
        with tarfile.open(archive) as package:
            package.extractall(out, filter='data')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos' if args.platform == 'ios' else 'macosx', '--show-sdk-path'], text=True).strip()
    build = out / ('build-' + args.platform)
    options = ['-DBUILD_SHARED_LIBS=OFF', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64', f'-DCMAKE_OSX_SYSROOT={sdk}']
    if args.platform == 'ios':
        options += ['-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0']
    # RAR and RAR5 decode internally. Disable optional host libraries and all command-line tools.
    for feature in ['TEST', 'TAR', 'CPIO', 'CAT', 'UNZIP', 'INSTALL', 'ACL', 'XATTR', 'OPENSSL',
                    'MBEDTLS', 'NETTLE', 'LIBB2', 'LZ4', 'LZO', 'LZMA', 'ZSTD', 'BZip2',
                    'LIBXML2', 'EXPAT', 'PCREPOSIX', 'PCRE2POSIX']:
        options.append(f'-DENABLE_{feature}=OFF')
    options += ['-DENABLE_ZLIB=ON', '-DENABLE_ICONV=ON']
    subprocess.run(['cmake', '-G', 'Ninja', '-S', str(source), '-B', str(build), *options], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'archive_static', '-j', str(os.cpu_count() or 4)], check=True)
    lib = out / args.platform / 'lib'
    include = out / args.platform / 'include'
    lib.mkdir(parents=True, exist_ok=True)
    include.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build / 'libarchive/libarchive.a', lib / 'libarchive.a')
    for header in ['archive.h', 'archive_entry.h']:
        shutil.copyfile(source / 'libarchive' / header, include / header)
    shutil.copyfile(source / 'COPYING', out / args.platform / 'libarchive-COPYING.txt')
    print(f'ARCHIVE_DEPS={out / args.platform}')


if __name__ == '__main__':
    main()
