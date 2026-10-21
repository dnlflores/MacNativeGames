// TOTK (iPhone / iPad): the runtime shader translation service as a static library linked into the app, which cannot
// start the Mac's totk_xlate --service program. Same requests, replies and cache files as the service (Service.cs
// HandleLine); built with publish_ios.sh (NativeAOT, -p:TotkXlateLib=true) into games/totk/prebuilt/ios-arm64.
using System;
using System.Runtime.InteropServices;
using System.Text;

static class XlateLibrary
{
    // totk_xlate_init(out dir, chain, debug mode): once, before the first request
    [UnmanagedCallersOnly(EntryPoint = "totk_xlate_init")]
    static unsafe int Init(byte* outDir, byte* chain, int debugMode)
    {
        try { ShaderService.Configure(Marshal.PtrToStringUTF8((IntPtr)outDir), Marshal.PtrToStringUTF8((IntPtr)chain), debugMode); return 0; }
        catch (Exception) { return -1; }
    }

    // totk_xlate_request(request line, reply buffer, its size): the reply line, cut to fit; returns its full length
    [UnmanagedCallersOnly(EntryPoint = "totk_xlate_request")]
    static unsafe int Request(byte* line, byte* reply, int cap)
    {
        byte[] r = Encoding.UTF8.GetBytes(ShaderService.HandleLine(Marshal.PtrToStringUTF8((IntPtr)line)));
        int n = Math.Min(r.Length, cap - 1);
        if (n >= 0) { Marshal.Copy(r, 0, (IntPtr)reply, n); reply[n] = 0; }
        return r.Length;
    }
}
