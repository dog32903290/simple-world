#include "app/export_cli.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/document.h"                      // doc::g_lib / doc::doOpenPath
#include "app/export_engine.h"                 // runExport / ExportSettings
#include "platform/glyph_outline.h"            // extractGlyphOutline (glyph seam)
#include "platform/image_decode.h"             // decodeImageToTexture (asset seam)
#include "platform/metal_compile.h"            // compileLibraryFromSource (field seam)
#include "platform/video_writer.h"             // VideoCodec
#include "runtime/field_graph.h"               // setFieldSourceCompiler
#include "runtime/glyph_points.h"              // setGlyphOutlineProvider / GlyphOutline
#include "runtime/image_filter_op_registry.h"  // setAssetTextureDecoder
#include "runtime/point_graph.h"               // registerBuiltinPointOps

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw::app {

namespace {

// Register the runtime→platform leaf seams the full cook needs (asset decode / field-MSL compile /
// glyph outline). This is a DELIBERATE MIRROR of the GUI's registration in main.mm:306-343 — the
// export runs the SAME cook, so it needs the SAME seams. Kept in lockstep by hand; a future cleanup
// could factor the shared block into one app-level installer both routers call (dossier note). NULL
// providers would silently null-out images / fields / text glyphs in a headless render, so this is
// load-bearing for any project that uses them.
void installCookSeams() {
  setAssetTextureDecoder([](MTL::Device* dev, const char* absPath, bool mipped) -> MTL::Texture* {
    return platform::decodeImageToTexture(dev, std::string(absPath), mipped);
  });
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  setGlyphOutlineProvider([](uint32_t cp, const char* fontName, float fontSize, float flatness,
                             GlyphOutline& rt) -> bool {
    platform::PlatformGlyphOutline pg;
    if (!platform::extractGlyphOutline(cp, fontName, fontSize, flatness, pg)) {
      rt.valid = false;
      return false;
    }
    rt.contours.clear();
    rt.contours.reserve(pg.contours.size());
    for (const auto& pc : pg.contours) {
      GlyphContour rc;
      rc.closed = pc.closed;
      rc.points.reserve(pc.xy.size() / 2);
      for (size_t i = 0; i + 1 < pc.xy.size(); i += 2) rc.points.push_back({pc.xy[i], pc.xy[i + 1]});
      rt.contours.push_back(std::move(rc));
    }
    rt.advance = pg.advance;
    rt.valid = pg.valid;
    return true;
  });
}

// Parse `--flag <value>` following index i. Returns the value or `def` if absent.
const char* argAfter(int argc, char** argv, const char* flag, const char* def) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  return def;
}

// Parse a bare boolean `--flag` (no following value). True iff present anywhere in argv.
bool argHasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], flag) == 0) return true;
  return false;
}

platform::VideoCodec parseCodec(const char* s, const std::string& outPath) {
  if (s) {
    if (std::strcmp(s, "h264") == 0) return platform::VideoCodec::H264;
    if (std::strcmp(s, "png") == 0) return platform::VideoCodec::PngSequence;
    if (std::strcmp(s, "prores4444") == 0) return platform::VideoCodec::ProRes4444;
  }
  // Infer from the path extension when unspecified: no ".mov" → treat as a PNG-sequence directory.
  if (outPath.size() < 4 || outPath.substr(outPath.size() - 4) != ".mov")
    return platform::VideoCodec::PngSequence;
  return platform::VideoCodec::ProRes4444;
}

}  // namespace

int runExportCli(int argc, char** argv) {
  bool hasExport = false;
  const char* outPath = nullptr;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--export") == 0) {
      hasExport = true;
      outPath = (i + 1 < argc) ? argv[i + 1] : nullptr;
      break;
    }
  if (!hasExport) return -1;  // fall through to the GUI (same protocol as the selftest router)
  if (!outPath) {
    std::fprintf(stderr, "[export] --export needs an output path\n");
    return 2;
  }

  // Load the project into the process-global doc (an export renders THIS document). Without --open
  // the current default/empty doc is rendered — still a valid (black) run for smoke testing.
  const char* openPath = argAfter(argc, argv, "--open", nullptr);
  if (openPath && !doc::doOpenPath(openPath, /*quiet=*/true)) {
    std::fprintf(stderr, "[export] failed to open %s\n", openPath);
    return 3;
  }

  ExportSettings s;
  s.outputPath = outPath;
  s.fps = atof(argAfter(argc, argv, "--fps", "30"));
  s.beginFrame = (uint32_t)atoi(argAfter(argc, argv, "--from", "0"));
  s.endFrame = (uint32_t)atoi(argAfter(argc, argv, "--to", "59"));
  s.width = (uint32_t)atoi(argAfter(argc, argv, "--width", "512"));
  s.height = (uint32_t)atoi(argAfter(argc, argv, "--height", "512"));
  s.codec = parseCodec(argAfter(argc, argv, "--codec", nullptr), s.outputPath);
  // Pre-roll escape hatch: ExportSettings::preroll defaults true (correct stateful-integrator
  // continuity for beginFrame>0); --no-preroll opts back into the old cold-start behavior.
  s.preroll = !argHasFlag(argc, argv, "--no-preroll");

  // Headless Metal bootstrap — the SAME device/metallib/op-table the goldens create (no GUI loop).
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev ? dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err) : nullptr;
  if (!dev || !mlib) {
    std::fprintf(stderr, "[export] no Metal device / metallib\n");
    if (mlib) mlib->release(); if (dev) dev->release(); pool->release();
    return 4;
  }
  registerBuiltinPointOps();
  installCookSeams();
  MTL::CommandQueue* q = dev->newCommandQueue();

  ProgressFn onProgress = [](uint32_t done, uint32_t total) -> bool {
    if (total == 0)  // pre-roll sentinel (export_engine.h ProgressFn doc): warm-up frames, not output
      std::printf("\r[export] preroll %u", done);
    else
      std::printf("\r[export] frame %u/%u", done, total);
    std::fflush(stdout);
    return true;  // headless CLI never cancels
  };
  ExportResult res = runExport(s, doc::g_lib(), dev, mlib, q, onProgress);
  std::printf("\n[export] %s: %u frames -> %s%s%s\n", res.ok ? "OK" : "FAILED", res.framesWritten,
              s.outputPath.c_str(), res.message.empty() ? "" : "  (", res.message.c_str());

  q->release(); mlib->release(); dev->release(); pool->release();
  return res.ok ? 0 : 5;
}

}  // namespace sw::app
