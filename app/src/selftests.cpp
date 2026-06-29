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
// (widget==Vec, vecArity>=2) followed by vecArity-1 COMPONENT ports — so we count the head and
// POSITIONALLY consume (skip) the next N-1 ports.
//
// ★FOLD WALK = POSITIONAL CONSUME-THE-RUN (同源 Inspector ui/inspector.cpp:83-86 + animGroupForSlot
// runtime/node_registry.cpp:196-213): a Vec head at index i (widget==Vec, vecArity>=2) OWNS the
// next min(vecArity,4)-1 ports by POSITION, regardless of those component ports' own widget. The
// PRIOR rule keyed components on `widget==Vec && vecArity==1`, which DIVERGED from the codebase
// walk: field/mesh ops hand-write component ports (Center.y/.z) with NO widget set (default Slider,
// vecArity 1) — the old rule didn't recognise them as components, so each counted 1 → false EXTRA
// (SphereSDF dumped 4, true logical 2). The positional walk folds them correctly. GENERATOR-SAFE
// by construction: the 13 point generators have NO Vec head, so every port is visited individually
// — identical to the old per-port walk → 13/13 cannot regress.
//
// Also excludes sw-internal-convention ports with NO TiXL [Input] behind them: every output port
// (isInput==false), the grid-family CAPACITY "Count" port (sw sets it = CountX*CountY*CountZ; a
// non-grid generator's "Count" IS a real TiXL InputSlot<int> so it is NOT excluded), and the image
// RenderTarget output-format trio Resolution/CustomW/CustomH (sw RenderTarget convention baked onto
// every image op; TiXL keeps them in _ImageOutputFormat/.t3, not as op [Input]s — workitem C).
//
// STRUCTURAL INVARIANT (loud, stale-proof): a Vec head declares vecArity=N but the run is cut short
// by an output port / end-of-ports before N-1 components are consumed → the author forgot to lay the
// components down. (We do NOT short on the next port being a Vec head — that mirrored the authority
// walk's blind positional consume; breaking there was the image-island fold bug.) Prints
// "!! VEC-RUN-SHORT" + sets a nonzero return so the gate surfaces it instead of silently miscounting.
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
  // Image output-format trio detection: Resolution/CustomW/CustomH are sw RenderTarget conventions
  // baked onto image ops (workitem C). These are excluded ONLY when CustomW is present (i.e., the
  // spec carries the full output-format trio). Ops like GradientsToTexture have a "Resolution" port
  // that IS a real TiXL [Input] (GradientsToTexture.cs:138, InputSlot<int>) and do NOT have
  // CustomW/CustomH — they must NOT be excluded. The trio exclusion is conditional on hasCustomW.
  bool hasCustomW = false;
  for (const auto& p : spec->ports)
    if (p.id == "CustomW") { hasCustomW = true; break; }

  std::printf("NodeSpec: %s (title=%s)\n", spec->type.c_str(), spec->title.c_str());
  int folded = 0;
  bool vecRunShort = false;
  const auto& ports = spec->ports;
  for (size_t i = 0; i < ports.size(); ++i) {
    const sw::PortSpec& p = ports[i];

    // Excluded sw-convention ports (still printed for transparency, never counted).
    const char* excl = nullptr;
    if (!p.isInput) {
      excl = "OUTPUT (excluded: sw output port, no TiXL [Input])";
    } else if (hasCountX && p.id == "Count") {
      excl = "capacity-Count (excluded: sw buffer-capacity convention, no TiXL [Input])";
    } else if (hasCustomW && (p.id == "Resolution" || p.id == "CustomW" || p.id == "CustomH")) {
      excl = "output-format synthetic (excluded: image RenderTarget trio, no TiXL [Input])";
    }
    if (excl) {
      std::printf("  - %-22s [%-9s]  %s\n", p.id.c_str(), p.dataType.c_str(), excl);
      continue;
    }

    // Vec head: counts 1, then POSITIONALLY consume the next N-1 component ports (same walk as the
    // Inspector / animGroupForSlot). Do NOT look at the components' own widget.
    if (p.isInput && p.widget == sw::Widget::Vec && p.vecArity >= 2) {
      int N = p.vecArity > 4 ? 4 : p.vecArity;
      std::printf("  + %-22s [%-9s]  VEC-HEAD/arity%d  (counts 1, consumes %d component(s))\n",
                  p.id.c_str(), p.dataType.c_str(), p.vecArity, N - 1);
      ++folded;
      int consumed = 0;
      for (int k = 1; k < N; ++k) {
        size_t j = i + (size_t)k;
        // Run cut short ONLY by an output port / end-of-ports = author forgot the run. We do NOT
        // break on a component that is itself tagged Vec: the authority walk (animGroupForSlot
        // node_registry.cpp:203-207, Inspector inspector.cpp:83-86) consumes the next N-1 ports BY
        // POSITION regardless of their widget. The 10 image ops (AfterGlow/Blend/BlendWithMask/
        // BoxGradient/Combine3Images/CombineMaterialChannels2/DistortAndShade/LightRaysFx/
        // MirrorRepeat/AfterGlow2) spell a Vector4 Color as Color.x/.y/.z/.w where EACH component is
        // ALSO tagged widget==Vec with DESCENDING arity (4→3→2→1); the prior `widget==Vec &&
        // vecArity>=2` break wrongly read Color.y as an independent head → folded one Vector4 into 3
        // → false EXTRA (symbol flip: it is really MISSING). Mirroring the blind positional consume
        // folds it to 1. (field/mesh bare components have NO widget so they were already folded; this
        // change does not touch them. Generators have no Vec head so the inner loop never runs.)
        if (j >= ports.size() || !ports[j].isInput) {
          break;
        }
        std::printf("    · %-20s [%-9s]  vec-component (folded into %s)\n", ports[j].id.c_str(),
                    ports[j].dataType.c_str(), p.id.c_str());
        ++consumed;
      }
      if (consumed != N - 1) {
        std::printf("  !! VEC-RUN-SHORT: %s arity=%d expected %d component(s), consumed %d "
                    "(author forgot to lay the run)\n",
                    p.id.c_str(), p.vecArity, N - 1, consumed);
        vecRunShort = true;
      }
      i += (size_t)consumed;  // advance past the consumed components
      continue;
    }

    // A plain counted logical param. Describe the widget.
    const char* w = "scalar";
    switch (p.widget) {
      case sw::Widget::Slider: w = "slider"; break;
      case sw::Widget::Enum:   w = "enum";   break;
      case sw::Widget::Bool:   w = "bool";   break;
      case sw::Widget::Vec:    w = "VEC-HEAD"; break;  // vecArity==1 stray head: treated as scalar
    }
    std::printf("  + %-22s [%-9s]  %s  (counts 1)\n", p.id.c_str(), p.dataType.c_str(), w);
    ++folded;
  }
  std::printf("FOLDED_LOGICAL_COUNT: %d\n", folded);
  return vecRunShort ? 4 : 0;  // 4 = structural invariant tripped (distinct from findSpec-null 2)
}

// --dump-nodespec-types: print every registered NodeSpec type, one per line. Lets the integrity
// gate DERIVE an island's type set (sw types whose authoritative .cs lives under the island subtree)
// instead of hardcoding a per-island list that goes stale. Read-only enumeration of specTypes().
int dumpNodeSpecTypes() {
  for (const std::string& t : sw::specTypes()) std::printf("%s\n", t.c_str());
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
    // --dump-nodespec-types: enumerate every registered NodeSpec type (island-sweep derivation).
    if (std::strcmp(a, "--dump-nodespec-types") == 0) return dumpNodeSpecTypes();

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
