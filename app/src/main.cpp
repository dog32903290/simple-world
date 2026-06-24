// simple_world — app entry shell (NSApplication + MTKView + Renderer + menu).
//
// Window/clear scaffold adapted from Apple's LearnMetalCpp 00-window
// (LeeTeng2001/metal-cpp-cmake). Pure C++: NS::Application + MTK::View.
// Per ARCHITECTURE.md this file is the app SHELL only — product behaviour lives
// in app/document, drawing in ui/editor_ui, verification in verify/.
// AppDelegate / ViewDelegate split into app_delegate.h/.cpp (ARCHITECTURE rule 4).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"  // void* overloads via IMGUI_IMPL_METAL_CPP_EXTENSIONS
#include "imgui_node_editor.h"

#include "app/audio_settings.h"
#include "app/audio_monitor.h"
#include "app/command.h"
#include "app/document.h"
#include "app/frame_cook.h"  // the per-frame production cook (mirror + audio + resident)
#include "app/menu.h"
#include "app_delegate.h"   // AppDelegate + ViewDelegate + Renderer class definitions
#include "platform/audio_capture.h"
#include "platform/dialogs.h"
#include "platform/image_decode.h"  // decodeImageToTexture (asset-texture decode leaf seam, app-owned)
#include "platform/metal_compile.h"  // compileLibraryFromSource (field source compiler leaf seam)
#include "runtime/compound_graph.h"
#include "runtime/image_filter_op_registry.h"  // setAssetTextureDecoder (the asset-decode leaf seam)
#include "runtime/field_graph.h"               // setFieldSourceCompiler (the field source leaf seam)
#include "runtime/compound_save.h"  // libToJsonV2 (eye state dump)
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "selftests.h"
#include "ui/cjk_font.h"
#include "ui/editor_ui.h"
#include "ui/fence_preview.h"  // fenceLastCoveredJson (eye state surface for the live .scn)
#include "ui/output_window.h"
#include "ui/timeline_window.h"
#include "ui/variation_panel.h"  // P2 Variation window (snapshot pool + N-way mix + crossfader)
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace ed = ax::NodeEditor;

// The live point-operator graph runtime (lane A.1): cooks doc::g_graph each frame and renders
// the chain into its target texture. Replaces the hardcoded ParticleSystem monolith — the
// editor graph's connections now drive the GPU buffer flow (rewireable).
sw::PointGraph* g_pointGraph = nullptr;

// Stable preview seam for the UI (editor_ui samples this). The engine swap below
// (ParticleSystem monolith -> PointGraph cook) didn't touch editor_ui — exactly what the
// seam was for. nullptr until the first render.
namespace sw {
MTL::Texture* previewTexture() { return ::g_pointGraph ? ::g_pointGraph->target() : nullptr; }
}  // namespace sw

namespace {
// Render-loop state owned by Renderer (internal to this TU).
MTL::Library* g_shaderLib = nullptr;

// --- World 1: live audio -> particles ---------------------------------------
// Capture publishes a per-frame audio level; the cook loop feeds it into the
// EvaluationContext, and the AudioReaction value node surfaces it into the graph.
// No hardcoded binding — 柏為 wires AudioReaction to a knob himself (visible in the graph).
sw::AudioCapture g_audioCapture;
}  // namespace

int main(int argc, char* argv[]) {
  if (int rc = sw::runSelftestFromArgs(argc, argv); rc >= 0) return rc;

  // INFO MODE early-exit (順手債 fix): `--help`/`-h`/`--version`/`-v` were NEVER handled flags —
  // they fell through every dispatch above and into pApp->run() (the NSApplication event loop),
  // which under a headless invocation builds the font atlas + opens a window and then spins
  // forever instead of printing usage. Any pure-information request must return BEFORE the GUI /
  // Renderer / Metal device / font-atlas work begins. Handled here, after the self-test dispatch
  // (which owns its own early exits) and before ANY app startup.
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      std::printf(
          "simple_world — native Metal node-graph runtime (TiXL parity)\n"
          "\n"
          "Usage:\n"
          "  simple_world                 launch the GUI editor\n"
          "  simple_world --open <file>   load a .swproj before the GUI starts (no dialog)\n"
          "  simple_world --help | -h     print this usage and exit\n"
          "  simple_world --version | -v  print the version and exit\n"
          "\n"
          "Self-test / headless modes:\n"
          "  simple_world --selftest-list           list every self-test name (one per line)\n"
          "  simple_world --selftest-<name>         run one self-test (append -bug for the\n"
          "                                         fault-injected negative variant)\n"
          "  simple_world --selftest-dispatch       run the compute-dispatch self-test\n"
          "  simple_world --list-audio-devices      list capture devices and exit\n"
          "  simple_world --audio-permission-status print mic-permission status and exit\n"
          "  simple_world --audio-capture-smoke <s> [out]  record a short capture smoke run\n"
          "  simple_world --audio-ingest-replay <f> replay an audio ingest fixture\n");
      return 0;
    }
    if (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-v") == 0) {
      std::printf("simple_world (native Metal runtime, TiXL parity lane)\n");
      return 0;
    }
  }

  // `--open <file.swproj>`: load a project before the GUI starts (no dialogs) — 柏為's
  // double-click-adjacent path AND the agent's only dialog-free way to load a test file
  // (the hand cannot click into an NFD modal). quiet=true: a failure must report on stderr
  // and fall through to the default doc — an NSAlert pre-NSApplication hangs forever.
  // initSnapshot later re-snapshots the loaded lib, so the file opens clean (not dirty).
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--open") == 0 && !sw::doc::doOpenPath(argv[i + 1], /*quiet=*/true))
      std::fprintf(stderr, "[open] falling back to the default project\n");

  NS::AutoreleasePool* pAutoreleasePool = NS::AutoreleasePool::alloc()->init();

  AppDelegate del;
  NS::Application* pApp = NS::Application::sharedApplication();
  pApp->setDelegate(&del);
  pApp->run();

  pAutoreleasePool->release();
  return 0;
}

#pragma mark - Renderer
#pragma region Renderer {

Renderer::Renderer(MTL::Device* pDevice) : _pDevice(pDevice->retain()) {
  _pCommandQueue = _pDevice->newCommandQueue();

  // Build the point-operator graph runtime from the precompiled metallib. registerBuiltinPointOps
  // wires the ops (RadialPoints / ParticleSystem sim / DrawPoints) into the cook table once.
  NS::Error* err = nullptr;
  g_shaderLib = _pDevice->newLibrary(
      NS::String::string(SW_SHADER_METALLIB, NS::StringEncoding::UTF8StringEncoding), &err);
  if (g_shaderLib) {
    sw::registerBuiltinPointOps();
    // Asset-texture decode leaf seam (ARCHITECTURE.md 葉子接縫): runtime exposes the fn-ptr; the app
    // owns the platform decoder and registers it here. (runtime never includes platform/ImageIO.)
    sw::setAssetTextureDecoder([](MTL::Device* dev, const char* absPath, bool mipped) -> MTL::Texture* {
      return sw::platform::decodeImageToTexture(dev, std::string(absPath), mipped);
    });
    // Field shader-graph SOURCE compiler leaf seam (runtime↛platform): runtime exposes the fn-ptr;
    // the app owns platform/metal_compile and registers it here, exactly like the asset decoder
    // above. The source-PSO cache (tex_op_cache::cachedSourcePSO) calls this to compile a
    // runtime-assembled field MSL string into a Library on first use. void* in/out = MTL::Device* /
    // MTL::Library* (runtime must not name MTL types in field_graph.h).
    sw::setFieldSourceCompiler([](void* device, const char* msl) -> void* {
      NS::Error* err = nullptr;
      return sw::platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
    });
    g_pointGraph = new sw::PointGraph(_pDevice, g_shaderLib, _pCommandQueue, /*W=*/512, /*H=*/512);
    if (!g_pointGraph->valid()) {
      delete g_pointGraph;
      g_pointGraph = nullptr;
    }
  }
}

Renderer::~Renderer() {
  delete g_pointGraph;
  g_pointGraph = nullptr;
  if (g_shaderLib) {
    g_shaderLib->release();
    g_shaderLib = nullptr;
  }
  _pCommandQueue->release();
  _pDevice->release();
}

void Renderer::draw(MTK::View* pView) {
  // Per-frame autorelease pool: MTK getters below are autoreleased (Iron Rule 2).
  NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();

  // eye: did the agent ask for a capture this frame? (cheap stat of 3 sentinels)
  sw::eye::Request eyeReq = sw::eye::poll();
  sw::hand::poll();  // second hand: read agent input commands queued for this frame

  MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();
  if (pRpd == nullptr) {
    pPool->release();
    return;
  }

  // ---- Cook the point-operator graph into its target texture (before imgui samples it) ----
  if (g_pointGraph) {
    // World 1: capture the chosen audio input. audio_settings owns the device pick
    // (persisted by UID); loadPrefs() applies the saved device on the first frame, and
    // takePendingChange() fires whenever 柏為 picks a new device in the toolbar -> restart
    // capture on it. Permission is requested in start(); the engine starts async after
    // 柏為 grants. The tap feeds the per-band SpectrumAnalyzer; each AudioReaction node
    // cooks from that snapshot into its outCache below (Level/WasHit/HitCount).
    static bool s_audioPrefsLoaded = false;
    if (!s_audioPrefsLoaded) {
      // Register the DSP sink before the first start() (the audio thread reads it once
      // streaming begins → set-before-start is race-free). Keeps capture a runtime-free leaf.
      g_audioCapture.setBlockCallback(&sw::audio_monitor::onBlock, nullptr);
      sw::audio::loadPrefs();
      s_audioPrefsLoaded = true;
    }
    unsigned int audioDev = 0;
    if (sw::audio::takePendingChange(audioDev)) g_audioCapture.start(audioDev);
    // view ⊥ graph (TiXL ViewSelectionPinning): the viewport shows the PINNED node; else it
    // FOLLOWS the selected node; else the CURRENT symbol's terminal; else the ROOT terminal
    // — entering a compound with no realizable child keeps showing the whole composition's
    // picture instead of going black (TiXL: navigation never blanks the output window).
    // All session state (ui) — never a graph edge, never in .swproj.
    // (OUTPUT_PIN_VIEWER_CONTRACT §4-A.) Resolved HERE (the shell may read ui) into a
    // RESIDENT PATH — app/frame_cook must not depend on ui.
    const sw::Symbol* curSym = sw::doc::currentSymbolConst();
    auto inCurrent = [&](int id) { return id != 0 && curSym && sw::childById(*curSym, id); };
    // A COMPOUND child inlines away — its resident path doesn't exist. Viewing it means
    // viewing its primary output's PRODUCER (viewProducerPath; atomic children resolve to
    // themselves). Empty result (no output / unwired) falls through to the terminals.
    auto viewPathOf = [&](int id) {
      return sw::viewProducerPath(sw::doc::g_lib, sw::doc::residentPathPrefix(), id);
    };
    std::string targetPath;
    if (inCurrent(sw::ui::g_pinnedNode)) targetPath = viewPathOf(sw::ui::g_pinnedNode);
    if (targetPath.empty() && inCurrent(sw::ui::g_selectedNode))
      targetPath = viewPathOf(sw::ui::g_selectedNode);                  // follow selection
    if (targetPath.empty())
      if (int t = g_pointGraph->defaultDrawTarget(sw::doc::g_lib, sw::doc::currentSymbolId()))
        targetPath = sw::doc::residentPathFor(t);                       // current terminal
    if (targetPath.empty())
      targetPath = std::to_string(
          g_pointGraph->defaultDrawTarget(sw::doc::g_lib, sw::doc::g_lib.rootId));  // root terminal
    // The production cook (projection rebuild + AudioReaction + RESIDENT graph cook) lives
    // in app/frame_cook.cpp — product behaviour, not shell.
    sw::framecook::run(*g_pointGraph, targetPath);
  }

  // eye① clean: the pure render layer, BEFORE any imgui chrome touches it.
  if (eyeReq.clean && g_pointGraph) sw::eye::dumpTextureRGBA(g_pointGraph->target(), "clean.png");

  // second hand: inject ONE queued input step before NewFrame consumes IO events.
  sw::hand::applyPendingStep();

  // ---- Begin imgui frame (Metal backend uses our metal-cpp pointers directly) ----
  ImGui_ImplMetal_NewFrame(pRpd);
  ImGui_ImplOSX_NewFrame(static_cast<void*>(pView));
  ImGui::NewFrame();

  sw::eye::beginWidgetFrame();  // eye③: collect clickable widget rects this frame
  sw::ui::drawToolbar();     // New/Open/Save/Save As + Add Node (floating)
  sw::ui::drawNodeCanvas();  // main workspace, fills the viewport
  sw::ui::drawInspector();    // floats on top
  sw::ui::drawTimelineWindow(); // S3 dope-sheet (animator lanes + playhead + key gestures)
  sw::ui::drawOutputWindow(); // the live preview viewport (view ⊥ graph, pinned/terminal)
  sw::ui::drawVariationPanel(); // P2 Variation window (snapshot pool grid + N-way mix + crossfader)
  sw::doc::updateWindowTitle();  // filename + dirty star; no-op when unchanged

  ImGui::Render();

  MTL::CommandBuffer* pCmd = _pCommandQueue->commandBuffer();
  MTL::RenderCommandEncoder* pEnc = pCmd->renderCommandEncoder(pRpd);
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), pCmd, pEnc);
  pEnc->endEncoding();
  CA::MetalDrawable* pDrawable = pView->currentDrawable();
  pCmd->presentDrawable(pDrawable);
  pCmd->commit();

  // eye③ map: widget rects -> top-left screen points (live window geometry).
  if (eyeReq.map) sw::eye::writeWidgetMap(static_cast<void*>(pView), "map.json");

  // eye④ state: lib + composition + selection as json so the agent can machine-check
  // mutations. (Was {selectedNode, graph:<flat>} — lib-native since 批次 3 N2; the lib json
  // serializes compounds only, so the root compound carries the canvas children/wires.)
  if (eyeReq.state) {
    std::string comp = "[";
    for (size_t i = 0; i < sw::doc::g_compositionPath.size(); ++i)
      comp += (i ? ", " : "") + std::to_string(sw::doc::g_compositionPath[i]);
    comp += "]";
    // Transport state (S5): the verify agent reads the two-clock playhead here (one-line hook;
    // the values come from app/frame_cook, no verify logic in business code — 鐵律 3).
    std::string transport =
        "{\"playing\": " + std::string(sw::framecook::transportPlaying() ? "true" : "false") +
        ", \"position\": " + std::to_string(sw::framecook::transportPosition()) +
        ", \"fxTime\": " + std::to_string(sw::framecook::transportFxTime()) +
        ", \"rate\": " + std::to_string(sw::framecook::transportRate()) +
        ", \"bpm\": " + std::to_string(sw::framecook::transportBpm()) + "}";
    std::string s = "{\"selectedNode\": " + std::to_string(sw::ui::g_selectedNode) +
                    ", \"pinnedNode\": " + std::to_string(sw::ui::g_pinnedNode) +
                    ", \"compositionPath\": " + comp +
                    ", \"transport\": " + transport +
                    ", \"timelineSelection\": " + sw::ui::timelineSelectionJson() +
                    ", \"fenceActive\": " + (sw::ui::fenceActive() ? "true" : "false") +
                    ", \"fenceLastCovered\": " + sw::ui::fenceLastCoveredJson() +
                    ", \"lib\": " + sw::libToJsonV2(sw::doc::g_lib) + "}";
    sw::eye::writeText("state.json", s.c_str());
  }

  // eye② full: the whole presented frame (UI + render). Stall one frame so the
  // drawable's backing is finished before we read it back (BGRA8_sRGB).
  if (eyeReq.full && pDrawable) {
    pCmd->waitUntilCompleted();
    sw::eye::dumpDrawableBGRA(pDrawable->texture(), "full.png");
  }

  pPool->release();
}

#pragma endregion Renderer }
