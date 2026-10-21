# The shader translator for iPhone and iPad, ready-made

An iOS app cannot start another program, so the iPhone / iPad build links the shader translation service into the app
instead of starting `totk_xlate --service` as the Mac does. The service is the same code as
[`../macos-arm64/totk_xlate`](../macos-arm64/README.md): the Ryujinx shader translator (`vendor/ryujinx`, MIT) with this
project's patches and `games/totk/tools/totk_xlate/Service.cs`, plus `Library.cs`'s two entry points
(`totk_xlate_init`, `totk_xlate_request`: one request line in, one reply line out). SPIRV-Cross's own `main()` is linked
in as well (`games/totk/ios/spvc/totk_spvc.cpp`, built by `ios/tools/build_deps.py` from the pinned sources) and called
with the Mac program's arguments, so a variant translated on the device is the same file as one translated on a Mac.

- `libtotk_xlate.a`: the translator compiled ahead of time with .NET NativeAOT for `ios-arm64`, merged with the NativeAOT
  runtime libraries it needs (WorkstationGC, the disabled eventpipe and standalone-GC stubs, aotminipal, System.Native,
  System.Security.Cryptography.Native.Apple), debug information stripped.
- `libtotk_xlate_boot.o`: the runtime's start-up object (an object file, so the linker keeps it).
- `SHA256SUMS`: `build_ipa.py` refuses to link files with other hashes.
- Built with `games/totk/tools/totk_xlate/publish_ios.sh` (.NET SDK 10.0.301, NativeAOT runtime pack 10.0.9).
- Checked (2026-10-20): Breath of the Wild's whole startup seed (11,148 variants, 33,159 files) prepared once with the
  Mac program and once with this service's code built the same way for macOS gave byte-identical MSL, reflection and
  variant files (only the wording of two failures' messages differs); the library took 10 s, the program 33 s.

On the device a shader variant the prepared seed does not have is translated when it is first drawn and kept in
`Documents/shader-cache`; `TOTK_SHADER_NO_TRANSLATE=1` restores the old behaviour (skip the draws, record the request for
`ios/tools/fill_shaders.py`).
