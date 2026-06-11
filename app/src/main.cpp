// simple_world — app entry shell (NSApplication + MTKView + Renderer + menu).
//
// Window/clear scaffold adapted from Apple's LearnMetalCpp 00-window
// (LeeTeng2001/metal-cpp-cmake). Pure C++: NS::Application + MTK::View.
// Per ARCHITECTURE.md this file is the app SHELL only — product behaviour lives
// in app/document, drawing in ui/editor_ui, verification in verify/.
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
#include "platform/audio_capture.h"
#include "platform/dialogs.h"
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/point_ops.h"
#include "selftests.h"
#include "ui/editor_ui.h"
#include "ui/output_window.h"
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

#pragma region Declarations {

class Renderer {
 public:
  explicit Renderer(MTL::Device* pDevice);
  ~Renderer();
  void draw(MTK::View* pView);

 private:
  MTL::Device* _pDevice;
  MTL::CommandQueue* _pCommandQueue;
};

class ViewDelegate : public MTK::ViewDelegate {
 public:
  explicit ViewDelegate(MTL::Device* pDevice);
  ~ViewDelegate() override;
  void drawInMTKView(MTK::View* pView) override;

 private:
  Renderer* _pRenderer;
};

class AppDelegate : public NS::ApplicationDelegate {
 public:
  ~AppDelegate() override;

  void applicationWillFinishLaunching(NS::Notification* pNotification) override;
  void applicationDidFinishLaunching(NS::Notification* pNotification) override;
  bool applicationShouldTerminateAfterLastWindowClosed(NS::Application* pSender) override;

 private:
  NS::Window* _pWindow = nullptr;
  MTK::View* _pMtkView = nullptr;
  MTL::Device* _pDevice = nullptr;
  ViewDelegate* _pViewDelegate = nullptr;
};

#pragma endregion Declarations }

int main(int argc, char* argv[]) {
  if (int rc = sw::runSelftestFromArgs(argc, argv); rc >= 0) return rc;

  NS::AutoreleasePool* pAutoreleasePool = NS::AutoreleasePool::alloc()->init();

  AppDelegate del;
  NS::Application* pApp = NS::Application::sharedApplication();
  pApp->setDelegate(&del);
  pApp->run();

  pAutoreleasePool->release();
  return 0;
}

#pragma mark - AppDelegate
#pragma region AppDelegate {

AppDelegate::~AppDelegate() {
  // Shut imgui backends down before releasing the device they reference.
  if (sw::ui::g_NodeEditor) ed::DestroyEditor(sw::ui::g_NodeEditor);
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplOSX_Shutdown();
  ImGui::DestroyContext();

  if (_pMtkView) _pMtkView->release();
  if (_pWindow) _pWindow->release();
  if (_pDevice) _pDevice->release();
  delete _pViewDelegate;
}

void AppDelegate::applicationWillFinishLaunching(NS::Notification* pNotification) {
  NS::Menu* pMenu = sw::menu::buildMainMenu();
  NS::Application* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
  pApp->setMainMenu(pMenu);
  pApp->setActivationPolicy(NS::ActivationPolicy::ActivationPolicyRegular);
}

void AppDelegate::applicationDidFinishLaunching(NS::Notification* pNotification) {
  CGRect frame = (CGRect){{100.0, 100.0}, {1024.0, 720.0}};

  _pWindow = NS::Window::alloc()->init(
      frame, NS::WindowStyleMaskClosable | NS::WindowStyleMaskTitled | NS::WindowStyleMaskResizable,
      NS::BackingStoreBuffered, false);

  _pDevice = MTL::CreateSystemDefaultDevice();

  _pMtkView = MTK::View::alloc()->init(frame, _pDevice);
  _pMtkView->setColorPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB);
  _pMtkView->setClearColor(MTL::ClearColor::Make(sw::kBgR, sw::kBgG, sw::kBgB, 1.0));
  _pMtkView->setFramebufferOnly(false);  // eye②: allow drawable readback (full.png)

  _pViewDelegate = new ViewDelegate(_pDevice);
  _pMtkView->setDelegate(_pViewDelegate);

  _pWindow->setContentView(_pMtkView);
  _pWindow->setTitle(NS::String::string("simple_world — step 0", NS::StringEncoding::UTF8StringEncoding));
  sw::doc::g_window = _pWindow;  // expose to sw::doc::updateWindowTitle()
  sw::installCloseGuard(static_cast<void*>(_pWindow),
                        []() -> bool { return sw::doc::confirmDiscardIfDirty(); });  // red-button guard

  // ---- Dear ImGui setup (OSX backend needs view.window, so init after setContentView) ----
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;  // don't litter imgui.ini next to the binary
  ImGui::StyleColorsDark();
  ImGui_ImplOSX_Init(static_cast<void*>(_pMtkView));
  ImGui_ImplMetal_Init(_pDevice);

  ed::Config cfg;
  cfg.SettingsFile = nullptr;  // don't litter NodeEditor.json next to the binary
  sw::ui::g_NodeEditor = ed::CreateEditor(&cfg);

  sw::doc::initSnapshot();  // startup matches default graph -> not dirty

  _pWindow->makeKeyAndOrderFront(nullptr);

  NS::Application* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
  pApp->activateIgnoringOtherApps(true);
}

bool AppDelegate::applicationShouldTerminateAfterLastWindowClosed(NS::Application* pSender) {
  return true;
}

#pragma endregion AppDelegate }

#pragma mark - ViewDelegate
#pragma region ViewDelegate {

ViewDelegate::ViewDelegate(MTL::Device* pDevice) : MTK::ViewDelegate(), _pRenderer(new Renderer(pDevice)) {}

ViewDelegate::~ViewDelegate() { delete _pRenderer; }

void ViewDelegate::drawInMTKView(MTK::View* pView) { _pRenderer->draw(pView); }

#pragma endregion ViewDelegate }

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
    // FOLLOWS the selected node; else the graph terminal (today's final picture, so an empty
    // selection = zero behaviour change). All three are session state (ui) — never a graph
    // edge, never in .swproj. (OUTPUT_PIN_VIEWER_CONTRACT §4-A.) Resolved HERE (the shell may
    // read ui) and passed as a plain id — app/frame_cook must not depend on ui.
    int viewTarget = sw::ui::g_pinnedNode;
    if (viewTarget == 0 || !sw::doc::g_graph.node(viewTarget))
      viewTarget = sw::ui::g_selectedNode;                              // follow selection
    if (viewTarget == 0 || !sw::doc::g_graph.node(viewTarget))
      viewTarget = g_pointGraph->defaultDrawTarget(sw::doc::g_graph);   // terminal fallback
    // The production cook (mirror rebuild + AudioReaction + RESIDENT graph cook) lives in
    // app/frame_cook.cpp — product behaviour, not shell.
    sw::framecook::run(*g_pointGraph, viewTarget);
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
  sw::ui::drawOutputWindow(); // the live preview viewport (view ⊥ graph, pinned/terminal)
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

  // eye④ state: graph + selection as json so the agent can machine-check mutations.
  if (eyeReq.state) {
    std::string s = "{\"selectedNode\": " + std::to_string(sw::ui::g_selectedNode) +
                    ", \"graph\": " + sw::toJson(sw::doc::g_graph) + "}";
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
