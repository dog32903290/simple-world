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
#include "app/keymap_prefs.h"  // #11: user keymap override store (loadUserKeymap at startup)
#include "app/user_settings.h"  // #12: recent-files MRU store (loadUserSettings at startup)
#include "app/theme_registry.h"  // color-theme registry + persistence (loadThemes at startup)
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
#include "platform/window_mode.h"    // toggleOsFullScreen (F11 / View > Fullscreen seam, wired below)
#include "runtime/compound_graph.h"
#include "runtime/image_filter_op_registry.h"  // setAssetTextureDecoder (the asset-decode leaf seam)
#include "runtime/field_graph.h"               // setFieldSourceCompiler (the field source leaf seam)
#include "runtime/cmd_view_background.h"        // set/clearCommandViewBackground (Output view bg ambient)
#include "runtime/compound_save.h"  // libToJsonV2 (eye state dump)
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "selftests.h"
#include "ui/asset_browser.h"  // AssetLibrary window (resource browser + click-to-create load-op)
#include "ui/cjk_font.h"
#include "ui/connection_ops.h"  // mountConnectionVerbs (connect/disconnect hand verbs)
#include "ui/graph_dump.h"       // mountGraphDump (eye req_graph -> graph.json of current compound)
#include "ui/editor_ui.h"
#include "ui/fence_preview.h"  // fenceLastCoveredJson (eye state surface for the live .scn)
#include "ui/output_window.h"
#include "ui/timeline_window.h"
#include "ui/view_modes.h"  // P6 g_focusMode / editorChromeVisible (演出/Focus mode gating)
#include "ui/view_menu_register.h"  // registerViewMenu (native View-menu seam wiring)
#include "ui/variation_panel.h"  // P2 Variation window (snapshot pool + N-way mix + crossfader)
#include "ui/theme_editor.h"      // Color Theme Editor window (dropdown + per-field color edits)
#include "ui/theme.h"             // applyColors (apply the active user theme at startup)
#include "ui/perf_overlay.h"  // B-track gap perf-observability: FrameTime P99 grader + mini-graph
#include "app/midi_bind.h"        // P3 registerIoLiveSources (live MIDI/OSC input hook) + learnStateJson
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
// Native pixel size of the preview texture (for the Output window's aspect-correct fit + 1:1
// view modes + the WxH overlay). Keeps Metal out of the ui zone: ui asks the shell, the shell
// queries the MTL::Texture. false (and w/h untouched) when there is no texture yet.
bool previewTextureSize(int& w, int& h) {
  MTL::Texture* tex = previewTexture();
  if (!tex) return false;
  w = static_cast<int>(tex->width());
  h = static_cast<int>(tex->height());
  return w > 0 && h > 0;
}
// Per-node thumbnail seam (node_faces TexturePreviewFace): the texture the resident node at `path`
// cooked this frame, or nullptr. Same shell-owned Metal-stays-out-of-ui contract as previewTexture()
// — the ui zone asks the shell for an opaque MTL::Texture* it hands to ImGui as an ImTextureID. Only
// nodes on the currently-cooked target chain have a texture (the cook realizes one subtree per frame).
MTL::Texture* residentNodeTexture(const char* path) {
  return ::g_pointGraph ? ::g_pointGraph->residentTexFor(path) : nullptr;
}
bool residentNodeTextureSize(const char* path, int& w, int& h) {
  MTL::Texture* tex = residentNodeTexture(path);
  if (!tex) return false;
  w = static_cast<int>(tex->width());
  h = static_cast<int>(tex->height());
  return w > 0 && h > 0;
}

// Output-resolution-selector seam (S1-ui, TiXL OutputWindow.cs:316/411-414). The Output window's
// resolution combo writes the FRAME render size the cook seeds into RequestedResolution. Same
// shell-owned contract as previewTexture(): the ui zone owns "which preset is selected" (session
// state) and on a change calls these to drive the cook-core override that landed in 1b53b12.
// A preset → setFrameResolutionOverride; Fill → clearFrameResolutionOverride (back to window size,
// byte-identical to today). The window stays a single output graph, so there is exactly one
// override to drive — no per-graph leak. Idempotent setters on the runtime side (a plain assign /
// optional.reset), so calling on-change carries no cook churn.
void setOutputResolutionOverride(int w, int h) {
  if (::g_pointGraph && w > 0 && h > 0)
    ::g_pointGraph->setFrameResolutionOverride(
        sw::RenderResolution{static_cast<uint32_t>(w), static_cast<uint32_t>(h)});
}
void clearOutputResolutionOverride() {
  if (::g_pointGraph) ::g_pointGraph->clearFrameResolutionOverride();
}
// The Fill baseline = the graph's window resolution (TiXL's ImGui.GetWindowSize() role in
// Resolution.ComputeResolution). The aspect-ratio presets (1:1/16:9/4:3) fit against this; Fill
// returns it verbatim. false when there is no graph yet.
bool outputWindowResolution(int& w, int& h) {
  if (!::g_pointGraph) return false;
  const sw::RenderResolution r = ::g_pointGraph->windowResolution();
  w = static_cast<int>(r.w);
  h = static_cast<int>(r.h);
  return w > 0 && h > 0;
}
// Output-window VIEW BACKGROUND seam (TiXL OutputWindow._backgroundColor → EvaluationContext.BackgroundColor,
// CommandOutputUi.Recompute). The ui zone owns "which color" (session state, picker shown ONLY for a Command
// view); the shell forwards it to the runtime ambient the terminal Command executor reads. Engage for a
// Command view, clear otherwise (Texture2D / preview) → executor falls back to opaque black (byte-id).
void setOutputBackgroundColor(float r, float g, float b, float a) {
  sw::setCommandViewBackground(r, g, b, a);
}
void clearOutputBackgroundColor() { sw::clearCommandViewBackground(); }
}  // namespace sw

// P6 — Player / 演出 output mode (modes.md [core]; TiXL Player/Program.cs is a separate exe, but
// modes.md sanctions the single-binary --play flag fork: "可先做成同一 binary 的 --play / --present
// CLI flag, 不必拆 binary"). When on: ALL editor chrome is hidden and the live render texture
// (g_pointGraph->target() — the same texture previewTexture()/req_clean show) is blitted FULLSCREEN
// for the audience. Esc leaves player mode back to the editor (TiXL Program.Input.cs:50 Esc exits).
// Session-only shell state — set by the CLI flag, toggled at runtime, never serialized.
static bool g_playerMode = false;

namespace {
// Render-loop state owned by Renderer (internal to this TU).
MTL::Library* g_shaderLib = nullptr;

// --- World 1: live audio -> particles ---------------------------------------
// Capture publishes a per-frame audio level; the cook loop feeds it into the
// EvaluationContext, and the AudioReaction value node surfaces it into the graph.
// No hardcoded binding — 柏為 wires AudioReaction to a knob himself (visible in the graph).
sw::AudioCapture g_audioCapture;

// P6 fullscreen render blit (Player + Focus modes). Draws the live render target over the WHOLE
// main viewport — the same texture previewTexture()/req_clean expose, mirroring TiXL Player's
// fullscreen-texture pass (Program.RenderLoop.cs). Reuses the existing imgui Metal render path (one
// borderless window + ImGui::Image) so no new GPU pass touches the present path; when neither mode
// is on this is never called and present stays byte-identical. `behindCanvas`: focus mode draws it
// as a backdrop (the canvas still draws on top, interactive); player mode draws it as the only layer.
void drawFullscreenRender(bool behindCanvas) {
  MTL::Texture* tex = sw::previewTexture();
  if (!tex) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar;
  // Backdrop variant must not steal mouse/keyboard from the canvas above it.
  if (behindCanvas) flags |= ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##fullscreen_render", nullptr, flags)) {
    ImGui::Image(reinterpret_cast<ImTextureID>(tex), vp->Size);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}
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
          "  simple_world --play          launch in演出/Player mode (fullscreen render, no UI;\n"
          "                               Esc returns to the editor; combine with --open)\n"
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

  // `--play`: start in演出/Player mode (modes.md [core]). Just sets the flag the draw loop reads;
  // the window/GUI startup is identical — player mode is a draw-time gate, not a separate entry.
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--play") == 0) g_playerMode = true;

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
  // P3 live-control input: open the OSC + virtual-MIDI loopback transports and route incoming events
  // into the binding table (the grep-confirmed-missing app hook). A real controller / phone-OSC app —
  // or the loopback — now feeds the table; learned graph params move during cook (midibind::tick).
  // Non-fatal if a transport is unavailable (restricted env): the editor still runs.
  sw::midibind::registerIoLiveSources();
  // Wire the hand's `midi <ch> <ctrl> <val>` scenario directive to the binding-table injector (leaf
  // inversion: verify/hand owns only a fn-ptr slot, the app fills it). Lets a .scn drive a CC into
  // the learn / cook-side wire without verify depending on app. ControllerChange (kind=1).
  sw::hand::setMidiInjectHook([](int ch, int ctrl, int val) {
    sw::midibind::injectMidiForTest(/*kind=*/1, ch, ctrl, val);
  });
  // `learn <child> <slot>` arms MIDI-learn on the CURRENT composition's child (= clicking the
  // inspector MIDI button, but node-select-independent so the scenario dodges the harness gap).
  sw::hand::setLearnArmHook([](int child, const char* slot) {
    sw::midibind::beginLearn(sw::doc::currentSymbolId(), child, std::string(slot));
  });
  // `connect`/`disconnect` hand verbs -> the doc-driven wire mutation (ui/connection_ops fills the
  // verify/hand fn-ptr slots; the agent text verb then walks the SAME AddWire/Reconnect a canvas pin
  // drag would). One-line mount, like the OS-fullscreen seam below.
  sw::ui::mountConnectionVerbs();
  sw::ui::mountGraphDump();  // eye req_graph -> serialize current compound into graph.json (免座標)
  // P6 OS-fullscreen seam (leaf-inversion: ui/view_modes can't include platform/).
  // Wired here so the F11 keymap handler + View > Fullscreen menu both reach the platform call.
  sw::ui::setOsFullScreenFn(&sw::platform::toggleOsFullScreen);
  // Native View-menu seam (app/view_menu_actions): UI registers the toggle/state fns so the native
  // NSMenu View items reach the real tool-window bools without app/menu.cpp including any ui/ header.
  sw::ui::registerViewMenu();
}

Renderer::~Renderer() {
  sw::midibind::shutdownIoLiveSources();
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
      sw::km::loadUserKeymap();  // #11: apply the user keymap JSON (if any) so rebinds take effect
      sw::settings::loadUserSettings();  // #12: load recent-files MRU (if any) for File > Open Recent
      // Color themes (= ThemeHandling.Initialize): load user themes from disk, then apply the active
      // one (UserSettings.ColorThemeName → that theme's palette, else factory = compiled-in default).
      sw::theme::loadThemes();
      sw::ui::theme::applyColors(
          sw::theme::registry().userOrFactory(sw::settings::settings().colorThemeName()).colors);
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
      return sw::viewProducerPath(sw::doc::g_lib(), sw::doc::residentPathPrefix(), id);
    };
    std::string targetPath;
    if (inCurrent(sw::ui::g_pinnedNode)) targetPath = viewPathOf(sw::ui::g_pinnedNode);
    if (targetPath.empty() && inCurrent(sw::ui::g_selectedNode))
      targetPath = viewPathOf(sw::ui::g_selectedNode);                  // follow selection
    if (targetPath.empty())
      if (int t = g_pointGraph->defaultDrawTarget(sw::doc::g_lib(), sw::doc::currentSymbolId()))
        targetPath = sw::doc::residentPathFor(t);                       // current terminal
    if (targetPath.empty())
      targetPath = std::to_string(
          g_pointGraph->defaultDrawTarget(sw::doc::g_lib(), sw::doc::g_lib().rootId));  // root terminal
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

  // Perf overlay: push this frame's delta time and recompute grade before any draw call.
  sw::ui::updatePerfOverlay(ImGui::GetIO().DeltaTime);

  sw::eye::beginWidgetFrame();  // eye③: collect clickable widget rects this frame

  // P6 — Player /演出 mode: hide ALL editor chrome AND the canvas; the live render fills the whole
  // window for the audience. Esc returns to the editor (TiXL Program.Input.cs:50). This is the only
  // draw branch — no toolbar/canvas/inspector/etc. — so nothing else can fire a shortcut here.
  if (g_playerMode) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_playerMode = false;
  }
  if (g_playerMode) {
    drawFullscreenRender(/*behindCanvas=*/false);
  } else {
    // Focus mode draws the render fullscreen BEHIND the (still-interactive) canvas first, so the
    // canvas + nodes float over the live output (TiXL FocusMode GraphImageBackground). Plain editor
    // mode skips this entirely → present path unchanged.
    if (sw::ui::g_focusMode) drawFullscreenRender(/*behindCanvas=*/true);
    sw::ui::drawNodeCanvas();  // main workspace, fills the viewport (ALWAYS drawn; runs the keymap)
    // Editor windows are gated by editorChromeVisible() (chrome shown AND not focus mode). F12 /
    // Shift+Esc collapse them; the canvas above keeps editing/navigation alive (Focus Mode is a
    // reversible in-editor state, modes.md [important]).
    if (sw::ui::editorChromeVisible()) {
      sw::ui::drawToolbar();        // New/Open/Save/Save As + Add Node (floating)
      sw::ui::drawInspector();      // floats on top
      sw::ui::drawTimelineWindow(); // S3 dope-sheet (animator lanes + playhead + key gestures)
      sw::ui::drawOutputWindow();   // the live preview viewport (view ⊥ graph, pinned/terminal)
      sw::ui::drawVariationPanel(); // P2 Variation window (snapshot pool grid + N-way mix + crossfader)
      sw::ui::drawAssetBrowser();   // AssetLibrary window (browse Lib: assets + click-to-create LoadImage)
      sw::ui::drawThemeEditor();    // Color Theme Editor (dropdown + name/author + per-field color edits)
    }
  }
  // Perf mini-graph overlay: drawn last so it floats above all other windows.
  // No-op when g_showPerfOverlay is false (default). Toggle with F10.
  sw::ui::drawPerfMiniGraph();

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
    // Cooked preview dims (S1-ui readback): the size of the texture the Output window shows this
    // frame. Fill == window size; an Output resolution-selector override retargets a Texture/
    // image-filter terminal, so a scenario can machine-assert the override actually changed the
    // render size. 0×0 when no texture cooked yet. One-line eye hook; no verify logic in business.
    int pvW = 0, pvH = 0;
    const bool havePv = sw::previewTextureSize(pvW, pvH);
    std::string preview = "{\"w\": " + std::to_string(havePv ? pvW : 0) +
                          ", \"h\": " + std::to_string(havePv ? pvH : 0) + "}";
    std::string s = "{\"selectedNode\": " + std::to_string(sw::ui::g_selectedNode) +
                    ", \"pinnedNode\": " + std::to_string(sw::ui::g_pinnedNode) +
                    ", \"preview\": " + preview +
                    ", \"compositionPath\": " + comp +
                    ", \"transport\": " + transport +
                    ", \"timelineSelection\": " + sw::ui::timelineSelectionJson() +
                    ", \"fenceActive\": " + (sw::ui::fenceActive() ? "true" : "false") +
                    ", \"fenceLastCovered\": " + sw::ui::fenceLastCoveredJson() +
                    ", \"midiLearn\": " + sw::midibind::learnStateJson(sw::doc::g_lib()) +
                    ", \"lib\": " + sw::libToJsonV2(sw::doc::g_lib()) + "}";
    sw::eye::writeText("state.json", s.c_str());
  }

  // eye⑤ graph: the CURRENT compound's children/ports/wires as graph.json so the agent can
  // learn childId/slotId (免座標) before driving `connect`. The serialization lives in
  // ui/graph_dump (it reads runtime compound + spec types eye can't); eye routes the string
  // to disk via the app-owned hook (mountGraphDump, beside mountConnectionVerbs).
  if (eyeReq.graph) sw::eye::writeGraphDump("graph.json");

  // eye② full: the whole presented frame (UI + render). Stall one frame so the
  // drawable's backing is finished before we read it back (BGRA8_sRGB).
  if (eyeReq.full && pDrawable) {
    pCmd->waitUntilCompleted();
    sw::eye::dumpDrawableBGRA(pDrawable->texture(), "full.png");
  }

  pPool->release();
}

#pragma endregion Renderer }
