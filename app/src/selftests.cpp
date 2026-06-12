// CLI self-test router (see selftests.h). Shell-tier file (lives at src/ root like
// main.cpp / metal_impl.cpp): may include any zone, because it only ROUTES to each
// subsystem's isolated --selftest entry. Adding a self-test = adding one row to kTable.
#include "selftests.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "app/audio_monitor.h"
#include "app/command.h"
#include "app/document.h"  // runNavigationSelfTest (composition-path semantics)
#include "app/graph_commands.h"  // runDefRemovalSelfTest (S13 boundary-def removal)
#include "app/animation_commands.h"  // runAnimGuiSelfTest (S3 GUI 動畫命令層)
#include "app/frame_cook.h"  // framecook::runArClockSelfTest (AR 時鐘域 pin 牙)
#include "app/soundtrack.h"  // runSoundtrackSelfTest (soundtrack<->transport follow rule)
#include "platform/audio_capture.h"
#include "platform/audio_devices.h"
#include "runtime/attack_detector.h"
#include "runtime/compound_graph.h"
#include "runtime/combine.h"
#include "runtime/compound_save.h"
#include "runtime/curve.h"
#include "runtime/curve_animator.h"
#include "runtime/graph_bridge.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/audio_analyzer.h"
#include "runtime/audio_ingest.h"
#include "runtime/audio_reaction.h"
#include "runtime/dispatch.h"
#include "runtime/graph.h"
#include "runtime/particle_system.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "runtime/spectrum_analyzer.h"
#include "runtime/transport.h"
#include "ui/cjk_font.h"
#include "ui/node_style.h"
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

namespace sw {
namespace {

// The app's "eye" color proof. Offscreen-render the SAME background color into a texture
// we own, read the center pixel back, assert it matches (codex-eyes: provenance is
// structural — we read what THIS process drew). injectBug clears a wrong color so the eye
// can FAIL (RED) before we trust a PASS (GREEN). Moved verbatim out of main's shell.
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

// Data-driven table of the uniform `--selftest[-<name>]` / `--selftest[-<name>]-bug`
// pairs. fn(bool injectBug) -> process exit code. Add a self-test = add a row.
struct SelfTest {
  const char* name;       // "" -> "--selftest"; "graph" -> "--selftest-graph"
  int (*fn)(bool);
};
const SelfTest kTable[] = {
    {"", runColorSelfTest},
    {"graph", runGraphRoundtripSelfTest},
    {"save", runSaveLoadSelfTest},
    {"command", runCommandSelfTest},
    {"defremoval", runDefRemovalSelfTest},
    {"copypaste", runCopyPasteSelfTest},
    {"rename", runRenameSelfTest},
    {"childstate", runChildStateSelfTest},
    {"navigation", doc::runNavigationSelfTest},
    {"valuecook", runValueCookSelfTest},
    {"resolve", runResolveSelfTest},
    {"audionode", runAudioNodeSelfTest},
    {"attack", runAttackSelfTest},
    {"analyzer", runAudioAnalyzerSelfTest},
    {"spectrum", runSpectrumSelfTest},
    {"audioreaction", runAudioReactionSelfTest},
    {"audiomonitor", audio_monitor::runAudioMonitorSelfTest},
    {"flow", runParticleFlowSelfTest},
    {"draw", runDrawPointsSelfTest},
    {"decay", runParticleDecaySelfTest},
    {"pointgraph", runPointGraphSelfTest},
    {"residentcook", runResidentCookSelfTest},
    {"residentparity", runResidentCookParitySelfTest},
    {"bypasscook", runBypassCookSelfTest},
    {"graphbridge", runGraphBridgeSelfTest},
    {"statecount", runStateCountSelfTest},
    {"savev2", runSaveV2SelfTest},
    {"testproj", runTestProjSelfTest},
    {"combine", runCombineSelfTest},
    {"compoundspec", runCompoundSpecSelfTest},
    {"compoundmodel", runCompoundModelSelfTest},
    {"cycleguard", runCycleGuardSelfTest},
    {"transport", runTransportSelfTest},
    {"arclock", framecook::runArClockSelfTest},
    {"curve", runCurveSelfTest},
    {"animator", runCurveAnimatorSelfTest},
    {"animgui", runAnimGuiSelfTest},
    {"residenteval", runResidentEvalSelfTest},
    {"residentcache", runResidentCacheSelfTest},
    {"residentpatch", runResidentPatchSelfTest},
    {"residentlibpatch", runResidentLibPatchSelfTest},
    {"radialop", runRadialOpSelfTest},
    {"radialcenter", runRadialCenterSelfTest},
    {"drawop", runDrawOpSelfTest},
    {"simop", runSimOpSelfTest},
    {"linepoints", runLinePointsSelfTest},
    {"gridpoints", runGridPointsSelfTest},
    {"spherepoints", runSpherePointsSelfTest},
    {"transformpoints", runTransformPointsSelfTest},
    {"orientpoints", runOrientPointsSelfTest},
    {"randomizepoints", runRandomizePointsSelfTest},
    {"setpointattributes", runSetPointAttributesSelfTest},
    {"combinebuffers", runCombineBuffersSelfTest},
    {"rendertarget", runRenderTargetSelfTest},
    {"rendertargetwired", runRenderTargetWiredSelfTest},
    {"nodestyle", ui::runNodeStyleSelfTest},
    {"cjkfont", ui::runCjkFontSelfTest},
    {"eye", eye::runSelfTest},
    {"map", eye::runMapSelfTest},
    {"hand", hand::runSelfTest},
    {"audioingest", runAudioIngestSelfTest},
    {"soundtrack", soundtrack::runSoundtrackSelfTest},
};

}  // namespace

int runSelftestFromArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];

    // Uniform --selftest[-name] / -bug pairs (the table).
    for (const SelfTest& e : kTable) {
      std::string base = std::string("--selftest") + (e.name[0] ? std::string("-") + e.name : "");
      if (base == a) return e.fn(false);
      if (base + "-bug" == a) return e.fn(true);
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
