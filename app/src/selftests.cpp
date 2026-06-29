// CLI self-test router (see selftests.h). Shell-tier file (lives at src/ root like main.cpp /
// metal_impl.cpp): may include any zone, because it only ROUTES to each subsystem's isolated
// --selftest entry.
//
// THIN READER (鐵律 7, data-driven): this file no longer holds the big central kTable that grew one
// row (and a fwd-decl) per op — that table was split, the node-registry way, into per-AREA manifest
// leaves (selftests_<area>.cpp) that self-register their rows into selftestRegistry() during pre-main
// dynamic init. Adding a selftest = add ONE row to the relevant area leaf (or, for an image-filter /
// value-op leaf, its own ImageFilterOp/ValueOp registrar) — this file is never touched. The router
// below just ITERATES the registry sink (plus the pre-existing imageFilterSelfTests() /
// valueOpSelfTests() sinks) for --selftest dispatch, --selftest-list, and --bite enumeration.
#include "selftests.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                     // findSpec — NodeSpec for --dump-nodespec
#include "runtime/dispatch.h"                  // runDispatchSelfTest (non-uniform entry)
#include "runtime/image_filter_op_registry.h"  // imageFilterSelfTests() self-registered sink
#include "runtime/value_op_registry.h"          // valueOpSelfTests() self-registered sink
#include "runtime/selftest_registry.h"          // selftestRegistry() — the area-leaf-fed sink
#include "platform/audio_capture.h"             // runAudioCaptureSmoke (non-uniform entry)
#include "platform/audio_devices.h"             // runListAudioDevices / runAudioPermissionStatus
#include "runtime/audio_ingest.h"               // runAudioIngestReplay (non-uniform entry)

namespace sw {
namespace {

// The app's "eye" color proof. Offscreen-render the SAME background color into a texture we own,
// read the center pixel back, assert it matches (codex-eyes: provenance is structural — we read what
// THIS process drew). injectBug clears a wrong color so the eye can FAIL (RED) before we trust a PASS
// (GREEN). This is the base "--selftest" (empty name) entry; it lives here, with the router, because
// it is the router's own structural eye — not an op. It is registered into the sink below.
int runColorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const int W = 64, H = 64;

  const int ex = (int)std::lround(kBgR * 255.0);
  const int ey = (int)std::lround(kBgG * 255.0);
  const int ez = (int)std::lround(kBgB * 255.0);

  MTL::ClearColor clear = injectBug ? MTL::ClearColor::Make(0.78, 0.12, 0.12, 1.0)
                                    : MTL::ClearColor::Make(kBgR, kBgG, kBgB, 1.0);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(tex);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(clear);
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(rpd)->endEncoding();  // clear only
  cmd->commit();
  cmd->waitUntilCompleted();

  uint8_t px[4] = {0, 0, 0, 0};
  tex->getBytes(px, W * 4, MTL::Region::Make2D(W / 2, H / 2, 1, 1), 0);

  bool pass = std::abs(px[0] - ex) <= 2 && std::abs(px[1] - ey) <= 2 && std::abs(px[2] - ez) <= 2;
  printf("[selftest] center=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n", px[0], px[1], px[2], ex, ey, ez,
         pass ? "PASS" : "FAIL");

  tex->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}

// --dump-nodespec <type>: print one NodeSpec's params (one per line) + the FOLDED logical param
// count, for the param-completion integrity gate (tools/nodespec_integrity.sh). "Folded logical"
// = the count that should equal the TiXL .cs [Input] count: a Vec param (Vector2/3/4) is ONE
// logical input (TiXL declares it as a single InputSlot<VectorN>), but sw spells it as a HEAD port
// (widget==Vec, vecArity>=2) followed by vecArity-1 COMPONENT ports (widget==Vec, vecArity==1) —
// so we count the head and skip the components. We also exclude sw-internal-convention ports that
// have NO TiXL [Input] behind them: every output port (isInput==false, e.g. "points"/"out"), and
// the grid-family CAPACITY "Count" port (sw sets it = CountX*CountY*CountZ; TiXL's grid nodes have
// CountX/Y/Z but no standalone Count). A non-grid generator's "Count" IS a real TiXL InputSlot<int>
// (RadialPoints/LinePoints/SpherePoints) so it is NOT excluded. Non-Float seam inputs (Mesh/
// Texture2D/Points bag) ARE real TiXL [Input]s → counted as 1 each.
int dumpNodeSpec(const char* type) {
  const sw::NodeSpec* spec = sw::findSpec(type);
  if (!spec) {
    std::fprintf(stderr, "--dump-nodespec: unknown node type '%s'\n", type);
    return 2;
  }
  // Grid-family detection: a CountX port means the standalone "Count" is the capacity convention.
  bool hasCountX = false;
  for (const auto& p : spec->ports)
    if (p.id == "CountX") { hasCountX = true; break; }

  std::printf("NodeSpec: %s (title=%s)\n", spec->type.c_str(), spec->title.c_str());
  int folded = 0;
  for (const auto& p : spec->ports) {
    const char* role = nullptr;
    if (!p.isInput) {
      role = "OUTPUT (excluded: sw output port, no TiXL [Input])";
    } else if (p.widget == sw::Widget::Vec && p.vecArity == 1) {
      role = "vec-component (excluded: folded into its Vec head)";
    } else if (hasCountX && p.id == "Count") {
      role = "capacity-Count (excluded: sw buffer-capacity convention, no TiXL [Input])";
    }
    if (role) {
      std::printf("  - %-22s [%-9s]  %s\n", p.id.c_str(), p.dataType.c_str(), role);
      continue;
    }
    // A counted logical param. Describe the widget + Vec-head arity.
    const char* w = "scalar";
    switch (p.widget) {
      case sw::Widget::Slider: w = "slider"; break;
      case sw::Widget::Enum:   w = "enum";   break;
      case sw::Widget::Bool:   w = "bool";   break;
      case sw::Widget::Vec:    w = "VEC-HEAD"; break;
    }
    if (p.widget == sw::Widget::Vec)
      std::printf("  + %-22s [%-9s]  %s/arity%d  (counts 1)\n", p.id.c_str(), p.dataType.c_str(), w,
                  p.vecArity);
    else
      std::printf("  + %-22s [%-9s]  %s  (counts 1)\n", p.id.c_str(), p.dataType.c_str(), w);
    ++folded;
  }
  std::printf("FOLDED_LOGICAL_COUNT: %d\n", folded);
  return 0;
}

}  // namespace

// The base color eye test is global order 0 — it leads --selftest-list (the empty name → "--selftest").
// Registered here because its fn lives here; every other row self-registers from its area leaf.
REGISTER_SELFTESTS(/*orderBase=*/0, {"", runColorSelfTest});

int runSelftestFromArgs(int argc, char** argv) {
  const std::vector<SelftestEntry>& table = selftestRegistry();

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];

    // --selftest-list: emit one selftest name per line (the registry sink — area-leaf rows including
    // the base "" color test — plus the self-registered imageFilterSelfTests() and valueOpSelfTests()
    // sinks), so run_all_selftests.sh discovers EVERY tooth, including sink-registered leaves that
    // never appear as a source row. (The harness prefers this list and only falls back to grepping
    // when this flag is absent.)
    if (std::strcmp(a, "--selftest-list") == 0) {
      for (const SelftestEntry& e : table) std::printf("%s\n", e.name);  // "" = base color selftest
      // Sink names, skipping any already present in the registry (e.g. convertcolors is in both during
      // the self-registration transition) so the harness never double-runs the same tooth.
      for (const auto& s : imageFilterSelfTests()) {
        bool inTable = false;
        for (const SelftestEntry& e : table)
          if (std::strcmp(e.name, s.first) == 0) { inTable = true; break; }
        if (!inTable) std::printf("%s\n", s.first);
      }
      // Value-op sink names (mirror of the image-filter loop above), skipping any already in the
      // registry so the harness never double-runs the same tooth.
      for (const auto& s : valueOpSelfTests()) {
        bool inTable = false;
        for (const SelftestEntry& e : table)
          if (std::strcmp(e.name, s.first) == 0) { inTable = true; break; }
        if (!inTable) std::printf("%s\n", s.first);
      }
      return 0;
    }

    // Uniform --selftest[-name] / -bug pairs (the registry sink — area-leaf rows + the base "" test).
    for (const SelftestEntry& e : table) {
      std::string base = std::string("--selftest") + (e.name[0] ? std::string("-") + e.name : "");
      if (base == a) return e.fn(false);
      if (base + "-bug" == a) return e.fn(true);
    }

    // Self-registered IMAGE FILTER selftests (the imageFilterSelfTests() sink that each
    // point_ops_<name>.cpp ImageFilterOp registrar feeds). Reading the sink here makes an image-filter
    // leaf a TRUE zero-shared-file-edit add. (The registry sink is matched FIRST above, so any name
    // also present there wins and does not double-run.)
    for (const auto& e : imageFilterSelfTests()) {
      std::string base = std::string("--selftest-") + e.first;
      if (base == a) return e.second(false);
      if (base + "-bug" == a) return e.second(true);
    }

    // Self-registered VALUE-OP selftests (the valueOpSelfTests() sink that each value_op_<name>.cpp
    // ValueOp registrar feeds). Same dormant-half activation as the image-filter loop — a value-op
    // leaf becomes a TRUE zero-shared-file-edit add. (Registry sink matched FIRST, so any shared name
    // wins and does not double-run.)
    for (const auto& e : valueOpSelfTests()) {
      std::string base = std::string("--selftest-") + e.first;
      if (base == a) return e.second(false);
      if (base + "-bug" == a) return e.second(true);
    }

    // --dump-nodespec <type>: print one node's params + folded logical count (param-completion gate).
    if (std::strcmp(a, "--dump-nodespec") == 0) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--dump-nodespec: missing <NodeType> argument\n");
        return 2;
      }
      return dumpNodeSpec(argv[i + 1]);
    }

    // Non-uniform entries (no bug variant / take arguments).
    if (std::strcmp(a, "--selftest-dispatch") == 0) return runDispatchSelfTest();
    if (std::strcmp(a, "--audio-ingest-replay") == 0)
      return runAudioIngestReplay(i + 1 < argc ? argv[i + 1] : "");
    if (std::strcmp(a, "--audio-capture-smoke") == 0)
      return runAudioCaptureSmoke(i + 1 < argc ? atof(argv[i + 1]) : 4.0,
                                  i + 2 < argc ? argv[i + 2] : "");
    if (std::strcmp(a, "--list-audio-devices") == 0) return runListAudioDevices();
    if (std::strcmp(a, "--audio-permission-status") == 0) return runAudioPermissionStatus();

    // 順手債: an UNKNOWN --selftest* flag used to fall through and launch the GUI — which reads as
    // a hung headless run (the binary opens a window instead of printing/exiting). Reject it loudly
    // with a nonzero exit so a typo'd selftest name fails fast instead of looking dead.
    if (std::strncmp(a, "--selftest", 10) == 0) {
      std::fprintf(stderr, "unknown self-test flag: %s\n", a);
      return 2;
    }
  }
  return -1;  // no self-test flag -> launch the GUI
}

}  // namespace sw
