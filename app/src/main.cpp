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
#include "app/menu.h"
#include "platform/audio_capture.h"
#include "platform/dialogs.h"
#include "runtime/spectrum_analyzer.h"
#include "runtime/audio_reaction.h"
#include "runtime/graph.h"
#include "runtime/particle_system.h"
#include "selftests.h"
#include "ui/editor_ui.h"
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace ed = ax::NodeEditor;

// Owned by Renderer. The UI reads the live preview indirectly through previewTexture()
// below (a stable seam), so it no longer reaches into ParticleSystem directly.
sw::ParticleSystem* g_particles = nullptr;

// Stable preview seam for the UI (editor_ui samples this). Returns the live render output
// without the UI knowing which engine produced it — lane A.1 swaps the body
// (ParticleSystem monolith -> PointGraph cook) without touching editor_ui. nullptr until
// the first render.
namespace sw {
MTL::Texture* previewTexture() { return ::g_particles ? ::g_particles->target() : nullptr; }
}  // namespace sw

namespace {
// Render-loop state owned by Renderer (internal to this TU).
MTL::Library* g_shaderLib = nullptr;
uint32_t g_frameIndex = 0;
float g_time = 0.0f;

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

  // Build the live particle system from the precompiled metallib and seed it.
  NS::Error* err = nullptr;
  g_shaderLib = _pDevice->newLibrary(
      NS::String::string(SW_SHADER_METALLIB, NS::StringEncoding::UTF8StringEncoding), &err);
  if (g_shaderLib) {
    g_particles = new sw::ParticleSystem(_pDevice, g_shaderLib, /*count=*/2048, /*W=*/512, /*H=*/512);
    if (g_particles->valid()) {
      g_particles->generate(_pCommandQueue);
    } else {
      delete g_particles;
      g_particles = nullptr;
    }
  }
}

Renderer::~Renderer() {
  delete g_particles;
  g_particles = nullptr;
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

  // ---- Cook the particle slice into its own target texture (before imgui samples it) ----
  if (g_particles) {
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
    const float dt = 1.0f / 60.0f;
    // One EvaluationContext per frame (TiXL Update(EvaluationContext) style): time/frame/deltaTime.
    EvaluationContext ctx{};
    ctx.frameIndex = g_frameIndex;
    ctx.time = g_time;
    ctx.deltaTime = dt;
    const sw::SpectrumSnapshot spec = sw::audio_monitor::spectrum();  // DSP fed by the capture callback
    // Cook every AudioReaction node (TiXL parity): gather its params, run the stateful
    // algorithm on the live spectrum, write its 3 outputs into outCache for evalFloat.
    {
      static std::map<int, sw::AudioReactionState> s_arState;
      for (sw::Node& node : sw::doc::g_graph.nodes) {
        if (node.type != "AudioReaction") continue;
        auto P = [&](const char* id, float def) {
          auto it = node.params.find(id);
          return it != node.params.end() ? it->second : def;
        };
        sw::AudioReactionParams p;
        p.amplitude = P("Amplitude", 1.0f);
        p.inputBand = (int)(P("InputBand", 2.0f) + 0.5f);
        p.windowCenter = P("WindowCenter", 0.0f);
        p.windowWidth = P("WindowWidth", 1.0f);
        p.windowEdge = P("WindowEdge", 1.0f);
        p.threshold = P("Threshold", 0.5f);
        p.minTimeBetweenHits = P("MinTimeBetweenHits", 0.1f);
        p.output = (int)(P("Output", 3.0f) + 0.5f);
        p.bias = P("Bias", 1.0f);
        p.reset = P("Reset", 0.0f) > 0.5f;
        const sw::AudioReactionOut o = sw::cookAudioReaction(spec, p, g_time, s_arState[node.id]);
        node.outCache[0] = o.level;
        node.outCache[1] = o.wasHit ? 1.0f : 0.0f;
        node.outCache[2] = (float)o.hitCount;
      }
    }
    // Cook: graph params drive the runtime. evalParam = if the param's input is wired,
    // evaluate the upstream value-spine (e.g. AudioReaction); else the stored constant.
    // GPU buffer flow is untouched — only these scalar setters read the value spine.
    g_particles->setTurbulenceAmount(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Amount", ctx, 15.0f));
    g_particles->setTurbulenceFrequency(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Frequency", ctx, 1.2f));
    g_particles->setSpeed(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Speed", ctx, 1.0f));
    g_particles->setDrag(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Drag", ctx, 0.02f));
    ++g_frameIndex;
    g_time += dt;
    g_particles->update(_pCommandQueue, g_time, dt);  // TurbulenceForce + integrator
    g_particles->render(_pCommandQueue);               // DrawPoints -> target texture
  }

  // eye① clean: the pure render layer, BEFORE any imgui chrome touches it.
  if (eyeReq.clean && g_particles) sw::eye::dumpTextureRGBA(g_particles->target(), "clean.png");

  // second hand: inject ONE queued input step before NewFrame consumes IO events.
  sw::hand::applyPendingStep();

  // ---- Begin imgui frame (Metal backend uses our metal-cpp pointers directly) ----
  ImGui_ImplMetal_NewFrame(pRpd);
  ImGui_ImplOSX_NewFrame(static_cast<void*>(pView));
  ImGui::NewFrame();

  sw::eye::beginWidgetFrame();  // eye③: collect clickable widget rects this frame
  sw::ui::drawToolbar();     // New/Open/Save/Save As + Add Node (floating)
  sw::ui::drawNodeCanvas();  // main workspace, fills the viewport
  sw::ui::drawInspector();   // floats on top
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
