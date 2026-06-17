// platform/metal_compile — runtime MSL source-string compilation (Metal newLibrary(source)).
//
// ZONE: platform (native macOS interface). This is the ONE place in the whole codebase that calls
// MTL::Device::newLibrary(NS::String* source, ...) — i.e. compiles Metal Shading Language from a
// runtime-assembled string instead of a precompiled .metallib. platform is allowed to touch Metal
// (MTL::* are system framework types; this file includes no runtime/app/ui/verify source), so the
// whole source->library path lives in one platform file and never crosses a zone boundary.
//
// NAMED FORK (sw's first runtime-compile path; sw's standing philosophy precompiles every static op
// into shaders.metallib so each .metal can #include shared headers — see app/CMakeLists.txt
// "Precompiled (not runtime-compiled)"). The shader-graph *field island* is fundamentally runtime
// codegen: the MSL text does not exist until the node graph is assembled (assembleFieldMSL in
// runtime/field_graph), so it CANNOT be precompiled. This fork is scoped strictly to the field
// island — every other op stays precompiled. TiXL does the exact same split: static ops ship as
// compiled shaders; field/shader-graph ops generate HLSL at runtime and compile it on the fly
// (Operators/Lib/field/render/_/GenerateShaderGraphCode.cs + ShaderCompiler).
#pragma once

namespace MTL { class Device; class Library; class Function; }
namespace NS  { class Error; }

namespace sw {
namespace platform {

// Compile an MSL source string into an MTL::Library. Returns an OWNED library (caller release()s /
// wraps in NS::TransferPtr) on success, or nullptr on compile failure. On failure, if `err` is
// non-null it receives the NS::Error* describing the compile error (its message is logged here too).
//
// FIRST VERSION IS SYNCHRONOUS (orchestrator decision): uses the blocking newLibrary(source, opts,
// error) overload, not the async completion-handler overload. Async + a PSO/library cache keyed on
// the source hash (runtime::assembleFieldMSL's srcHash) is the Build-2 job.
MTL::Library* compileLibraryFromSource(MTL::Device* dev, const char* msl, NS::Error** err);

// --selftest-metal-compile entry. Creates the system default device, then:
//   (1) compiles a trivial valid MSL kernel -> asserts non-null library + the named function is
//       retrievable (newFunction != null),
//   (2) feeds deliberately broken MSL -> asserts nullptr library + non-null err (the error face),
//   (3) releases everything (run under ASan to prove no leak).
// injectBug forces the valid-MSL path to assert the BROKEN result (library==null) instead, proving
// the tooth BITES: a working compiler then fails the test -> RED. fn(bool) -> exit code (0/1).
int runMetalCompileSelfTest(bool injectBug);

}  // namespace platform
}  // namespace sw
