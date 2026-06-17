// platform/metal_compile.mm — see metal_compile.h. The ONE newLibrary(source) call site.
//
// Compiled WITHOUT ARC (-fno-objc-arc in CMakeLists, like eye.mm / dialogs.mm / image_decode.mm):
// we manage metal-cpp object lifetime by hand (NS::String*, MTL::CompileOptions*, MTL::Library*,
// MTL::Function*) — metal-cpp's retain/release convention, not ObjC ARC. (metal-cpp-discipline.)
#include "platform/metal_compile.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.h>
#include <Metal/Metal.hpp>

#include <cstdio>

namespace sw {
namespace platform {

MTL::Library* compileLibraryFromSource(MTL::Device* dev, const char* msl, NS::Error** err) {
  if (err) *err = nullptr;
  if (!dev || !msl) return nullptr;

  // One-shot call: wrap our own AutoreleasePool so the autoreleased NS::String/CompileOptions and
  // any error object spun up by Metal are drained here, not left for the caller (metal-cpp-discipline:
  // "callers that loop must wrap their own" — this is a one-shot, so we own the pool).
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  NS::String* src =
      NS::String::string(msl, NS::StringEncoding::UTF8StringEncoding);  // autoreleased

  // Default compile options (nullptr also works, but an explicit autoreleased options object keeps a
  // single place to set language version / fast-math later). init() returns +1; autorelease it so the
  // pool drains it — we don't hold it past this call.
  MTL::CompileOptions* opts = MTL::CompileOptions::alloc()->init();

  NS::Error* localErr = nullptr;
  // newLibrary(source, options, error) returns an OWNED (+1) library — we hand ownership to the
  // caller, so it is NOT released here and survives the pool drain (it's a +1 object, not autoreleased).
  MTL::Library* lib = dev->newLibrary(src, opts, &localErr);

  opts->release();  // we owned the +1 from alloc/init; library is independent of it now.

  if (!lib) {
    if (localErr) {
      const char* msg = localErr->localizedDescription()
                            ? localErr->localizedDescription()->utf8String()
                            : "(no message)";
      std::fprintf(stderr, "[metal_compile] newLibrary(source) failed: %s\n", msg);
      if (err) {
        localErr->retain();  // hand a +1 to the caller (survives pool drain); caller releases.
        *err = localErr;
      }
    }
    pool->release();
    return nullptr;
  }

  pool->release();  // drains src + autoreleased temporaries; lib (+1) survives.
  return lib;
}

}  // namespace platform
}  // namespace sw
