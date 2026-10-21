#!/bin/zsh
# Build the shader translation service as a static library for the iPhone / iPad app, which cannot start the Mac's
# totk_xlate --service program: the same translator, patches and Service.cs, with Library.cs's entry points
# (totk_xlate_init, totk_xlate_request) and SPIRV-Cross linked in rather than started (TOTK_XLATE_LIB; the app links
# ios/spvc/totk_spvc.cpp, built by ios/tools/build_deps.py). NativeAOT for ios-arm64 from the plain SDK
# (PublishAotUsingRuntimePack, no iOS workload).
# build_ipa.py links the copies in games/totk/prebuilt/ios-arm64 (made with this script), so building the app needs no
# .NET: libtotk_xlate.a is the translator merged with the NativeAOT runtime libraries it needs, libtotk_xlate_boot.o
# the runtime's start-up object (an object, not an archive member: nothing refers to it by name).
# Needs the .NET 10 SDK and network access the first time (NuGet packages, the NativeAOT compiler).
# usage: games/totk/tools/totk_xlate/publish_ios.sh [dotnet]   -> games/totk/prebuilt/ios-arm64/ (+ SHA256SUMS)
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h:h}
DOTNET=${1:-dotnet}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM HUP
mkdir -p "$WORK/ryujinx-src" "$WORK/xlate"
cp -R "$REPO/vendor/ryujinx/." "$WORK/ryujinx-src/"
find "$WORK/ryujinx-src/src" -maxdepth 2 \( -name obj -o -name bin \) -type d -exec rm -rf {} +
for p in "$HERE"/patches/*.patch; do patch -s -d "$WORK/ryujinx-src" -p1 --forward < "$p"; done
cp "$HERE"/*.cs "$HERE"/totk_xlate.csproj "$WORK/xlate/"
"$DOTNET" publish "$WORK/xlate/totk_xlate.csproj" -c Release -r ios-arm64 -p:PublishAot=true -p:PublishAotUsingRuntimePack=true \
  -p:InvariantGlobalization=true -p:TotkXlateLib=true -p:RyuSrc="$WORK/ryujinx-src" -o "$WORK/out"
# the NativeAOT runtime pack the publish used (its version follows the SDK)
NATIVE=$(ls -d "${NUGET_PACKAGES:-$HOME/.nuget/packages}"/microsoft.netcore.app.runtime.nativeaot.ios-arm64/*/runtimes/ios-arm64/native | tail -1)
OUT=$REPO/games/totk/prebuilt/ios-arm64
mkdir -p "$OUT"
libtool -static -o "$WORK/merged.a" "$WORK/out/totk_xlate.a" "$NATIVE"/libRuntime.WorkstationGC.a "$NATIVE"/libeventpipe-disabled.a \
  "$NATIVE"/libstandalonegc-disabled.a "$NATIVE"/libaotminipal.a "$NATIVE"/libstdc++compat.a "$NATIVE"/libSystem.Native.a \
  "$NATIVE"/libSystem.Security.Cryptography.Native.Apple.a
strip -S -o "$OUT/libtotk_xlate.a" "$WORK/merged.a"   # debug information only; the app's link keeps what it uses
cp "$NATIVE/libbootstrapperdll.o" "$OUT/libtotk_xlate_boot.o"
(cd "$OUT" && shasum -a 256 libtotk_xlate.a libtotk_xlate_boot.o > SHA256SUMS)
echo "built $OUT (runtime pack ${NATIVE:h:h:h})"; cat "$OUT/SHA256SUMS"
