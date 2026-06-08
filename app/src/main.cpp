// simple_world — app entry shell (NSApplication + MTKView + Renderer + menu).
//
// Window/clear scaffold adapted from Apple's LearnMetalCpp 00-window
// (LeeTeng2001/metal-cpp-cmake). Pure C++: NS::Application + MTK::View.
// Per ARCHITECTURE.md this file is the app SHELL only — product behaviour lives
// in app/document, drawing in ui/editor_ui, verification in verify/.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"  // void* overloads via IMGUI_IMPL_METAL_CPP_EXTENSIONS
#include "imgui_node_editor.h"

#include "app/command.h"
#include "app/document.h"
#include "app/menu.h"
#include "platform/dialogs.h"
#include "runtime/attack_detector.h"
#include "runtime/audio_analyzer.h"
#include "runtime/audio_ingest.h"
#include "runtime/dispatch.h"
#include "runtime/graph.h"
#include "runtime/particle_system.h"
#include "ui/editor_ui.h"
#include "verify/eye/eye.h"
#include "verify/hand/hand.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace ed = ax::NodeEditor;

// Owned by Renderer; read by ui/editor_ui (DrawPoints preview). External linkage
// (not in the anonymous namespace) so editor_ui.cpp can `extern` it.
sw::ParticleSystem* g_particles = nullptr;

namespace {
// Editor background. Also the color --selftest will assert against later.
const MTL::ClearColor kClearColor = MTL::ClearColor::Make(0.12, 0.14, 0.18, 1.0);

// Render-loop state owned by Renderer (internal to this TU).
MTL::Library* g_shaderLib = nullptr;
uint32_t g_frameIndex = 0;
float g_time = 0.0f;

// --selftest: the app's "eye". Offscreen-render the SAME kClearColor into a
// texture we own, read the center pixel back, and assert it matches. No window,
// no screen capture: provenance is structural (we read what THIS process drew).
// codex-eyes method. injectBug=true clears a wrong color to prove the eye can
// fail (RED) before we trust a PASS (GREEN). Returns process exit code.
int runSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const int W = 64, H = 64;

  // Expected bytes derive from kClearColor itself — single source of truth with
  // the live window, so the test can't silently drift from what ships.
  const int ex = (int)std::lround(kClearColor.red * 255.0);
  const int ey = (int)std::lround(kClearColor.green * 255.0);
  const int ez = (int)std::lround(kClearColor.blue * 255.0);

  MTL::ClearColor clear = injectBug ? MTL::ClearColor::Make(0.78, 0.12, 0.12, 1.0) : kClearColor;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();  // owned
  MTL::CommandQueue* q = dev->newCommandQueue();        // owned

  // Linear RGBA8Unorm (no sRGB) so stored bytes == round(component*255).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);  // CPU-readable on Apple Silicon
  MTL::Texture* tex = dev->newTexture(td);     // owned

  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(tex);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(clear);
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rpd);
  enc->endEncoding();  // clear only
  cmd->commit();
  cmd->waitUntilCompleted();

  uint8_t px[4] = {0, 0, 0, 0};
  MTL::Region region = MTL::Region::Make2D(W / 2, H / 2, 1, 1);
  tex->getBytes(px, W * 4, region, 0);

  bool pass = std::abs(px[0] - ex) <= 2 && std::abs(px[1] - ey) <= 2 && std::abs(px[2] - ez) <= 2;
  printf("[selftest] center=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n", px[0], px[1], px[2], ex, ey, ez,
         pass ? "PASS" : "FAIL");

  tex->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}
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
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--selftest") == 0) return runSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-bug") == 0) return runSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-dispatch") == 0) return sw::runDispatchSelfTest();
    if (std::strcmp(argv[i], "--selftest-graph") == 0)
      return sw::runGraphRoundtripSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-graph-bug") == 0)
      return sw::runGraphRoundtripSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-save") == 0)
      return sw::runSaveLoadSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-save-bug") == 0)
      return sw::runSaveLoadSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-command") == 0)
      return sw::runCommandSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-command-bug") == 0)
      return sw::runCommandSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-valuecook") == 0)
      return sw::runValueCookSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-valuecook-bug") == 0)
      return sw::runValueCookSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-resolve") == 0)
      return sw::runResolveSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-resolve-bug") == 0)
      return sw::runResolveSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-attack") == 0)
      return sw::runAttackSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-attack-bug") == 0)
      return sw::runAttackSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-analyzer") == 0)
      return sw::runAudioAnalyzerSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-analyzer-bug") == 0)
      return sw::runAudioAnalyzerSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-flow") == 0)
      return sw::runParticleFlowSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-flow-bug") == 0)
      return sw::runParticleFlowSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-draw") == 0)
      return sw::runDrawPointsSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-draw-bug") == 0)
      return sw::runDrawPointsSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-eye") == 0) return sw::eye::runSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-eye-bug") == 0) return sw::eye::runSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-hand") == 0) return sw::hand::runSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-hand-bug") == 0) return sw::hand::runSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--selftest-audioingest") == 0)
      return sw::runAudioIngestSelfTest(/*injectBug=*/false);
    if (std::strcmp(argv[i], "--selftest-audioingest-bug") == 0)
      return sw::runAudioIngestSelfTest(/*injectBug=*/true);
    if (std::strcmp(argv[i], "--audio-ingest-replay") == 0)
      return sw::runAudioIngestReplay(i + 1 < argc ? argv[i + 1] : "");
  }

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
  _pMtkView->setClearColor(kClearColor);
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
    // Cook: graph params drive the runtime. evalParam = if the param's input is
    // wired, evaluate the upstream value-spine; else the stored constant. GPU
    // buffer flow is untouched — only these scalar setters read the value spine.
    g_particles->setTurbulenceAmount(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Amount", g_time, 15.0f));
    g_particles->setTurbulenceFrequency(sw::evalParam(sw::doc::g_graph, "TurbulenceForce", "Frequency", g_time, 1.2f));
    g_particles->setSpeed(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Speed", g_time, 1.0f));
    g_particles->setDrag(sw::evalParam(sw::doc::g_graph, "ParticleSystem", "Drag", g_time, 0.02f));
    ++g_frameIndex;
    const float dt = 1.0f / 60.0f;
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
