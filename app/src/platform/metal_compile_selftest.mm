// platform/metal_compile_selftest.mm — --selftest-metal-compile (see metal_compile.h).
//
// Proves the one newLibrary(source) path: a valid MSL kernel compiles into a library with a
// retrievable function; broken MSL returns null with a non-null error; everything releases cleanly
// (run under ASan to confirm no leak). Compiled WITHOUT ARC (manual metal-cpp lifetime), like the
// rest of the .mm files. Mirrors image_decode_selftest.mm's shape.
#include "platform/metal_compile.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.h>
#include <Metal/Metal.hpp>

#include <cstdio>

namespace sw {
namespace platform {

namespace {

constexpr const char* kValidMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void sw_probe(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
    out[id] = 42.0;
}
)MSL";

// Undefined type + missing semicolons -> guaranteed compile error.
constexpr const char* kBrokenMsl = R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void sw_probe(device flerb* out [[buffer(0)]]) {
    out[0] = this is not valid msl
}
)MSL";

}  // namespace

int runMetalCompileSelfTest(bool injectBug) {
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::fprintf(stderr, "[selftest-metal-compile] no Metal device\n");
    return 1;
  }

  int rc = 0;

  // (1) valid MSL -> non-null library + retrievable function.
  {
    NS::Error* err = nullptr;
    MTL::Library* lib = compileLibraryFromSource(dev, kValidMsl, &err);

    // injectBug: pretend the valid compile produced nothing -> a working compiler now FAILS the
    // "valid MSL must compile" assertion (RED). Proves the tooth bites.
    bool libOk = injectBug ? false : (lib != nullptr);
    if (!libOk) {
      std::fprintf(stderr, "[selftest-metal-compile] FAIL: valid MSL did not compile%s\n",
                   injectBug ? " (injectBug)" : "");
      rc = 1;
    }

    if (lib) {
      NS::String* fnName = NS::String::string("sw_probe", NS::StringEncoding::UTF8StringEncoding);
      MTL::Function* fn = lib->newFunction(fnName);
      if (!fn) {
        std::fprintf(stderr, "[selftest-metal-compile] FAIL: function sw_probe not found\n");
        rc = 1;
      } else {
        fn->release();
      }
      lib->release();  // no leak under ASan.
    }
    if (err) err->release();
  }

  // (2) error face: broken MSL -> null library + non-null err.
  {
    NS::Error* err = nullptr;
    MTL::Library* lib = compileLibraryFromSource(dev, kBrokenMsl, &err);
    if (lib != nullptr) {
      std::fprintf(stderr, "[selftest-metal-compile] FAIL: broken MSL compiled (expected null)\n");
      lib->release();
      rc = 1;
    }
    if (err == nullptr) {
      std::fprintf(stderr, "[selftest-metal-compile] FAIL: broken MSL produced no error object\n");
      rc = 1;
    } else {
      err->release();
    }
  }

  dev->release();

  if (injectBug) {
    if (rc == 0) {
      std::fprintf(stderr,
                   "[selftest-metal-compile] FAIL: injectBug did not trip any assertion "
                   "(tooth has no bite)\n");
      return 1;
    }
    std::printf("[selftest-metal-compile] injectBug correctly RED\n");
    return 1;
  }

  if (rc == 0) std::printf("[selftest-metal-compile] PASS\n");
  return rc;
}

}  // namespace platform
}  // namespace sw
