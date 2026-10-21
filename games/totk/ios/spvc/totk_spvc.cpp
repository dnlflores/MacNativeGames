// TOTK (iPhone / iPad): SPIRV-Cross's own program, linked into the app instead of started, for the shader translation
// library (tools/totk_xlate/Library.cs, Service.cs SpirvCross under TOTK_XLATE_LIB). Its main() runs with the same
// arguments as the Mac's spirv-cross program, so the Metal source is the same byte for byte. Built with the other
// SPIRV-Cross sources by ios/tools/build_deps.py (the pinned tooling/spirv-cross, as build_app.py's build_spirv_cross).
//
// main.cpp's own headers are included first, so the two renames below reach only main.cpp's code: its exit() calls (bad
// arguments only, never with the fixed arguments here) throw instead of ending the app, and main() becomes callable.
#include "spirv_cpp.hpp"
#include "spirv_cross_util.hpp"
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_msl.hpp"
#include "spirv_parser.hpp"
#include "spirv_reflect.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

[[noreturn]] static void totk_spvc_exit(int code)
{
	throw std::runtime_error("spirv-cross stopped with status " + std::to_string(code));
}

#define exit totk_spvc_exit
#define main totk_spvc_main
#include "main.cpp"
#undef main
#undef exit

// One spirv-cross run; 0 on success. Its messages go to stderr as the program's do. The caller serialises runs (the
// translation lock in totk_shader_rt.m): SPIRV-Cross keeps no state between them.
extern "C" int totk_spvc_run(int argc, char **argv)
{
	try
	{
		return totk_spvc_main(argc, argv);
	}
	catch (const std::exception &e)
	{
		fprintf(stderr, "SPIRV-Cross threw an exception: %s\n", e.what());
		return EXIT_FAILURE;
	}
}
