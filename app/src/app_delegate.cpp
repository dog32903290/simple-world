// app_delegate — AppDelegate and ViewDelegate implementations.
// Mechanically split out of main.cpp (ARCHITECTURE rule 4: single file < 400 lines).
#include "app_delegate.h"

#include <atomic>
#include <chrono>
#include <dispatch/dispatch.h>  // verify keep-alive timer (display sleep / lock stalls MTKView)

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"  // void* overloads via IMGUI_IMPL_METAL_CPP_EXTENSIONS
#include "imgui_node_editor.h"

#include "app/document.h"
#include "app/menu.h"
#include "platform/dialogs.h"   // sw::installCloseGuard
#include "selftests.h"          // sw::kBgR / kBgG / kBgB
#include "ui/cjk_font.h"
#include "ui/editor_ui.h"
#include "ui/theme.h"
#include "verify/hand/hand.h"

namespace ed = ax::NodeEditor;

// Renderer is defined in main.cpp (same TU cluster); forward-declared in the header.
// The full type is needed here for operator new/delete and constructor call.
class Renderer;

// --- verify keep-alive -------------------------------------------------------
// MTKView is driven by a CVDisplayLink, which stops when the display sleeps or the session
// locks. That froze the whole eye/hand verify loop exactly when it matters most: the agent
// driving the app unattended (the in-process hand exists so 柏為 does NOT have to sit at the
// desk). drawInMTKView stamps this every frame; a main-queue timer (installed at launch)
// redraws manually only when the stamp goes stale — the guard never fires while the display
// link ticks, so the normal path is untouched.
namespace {
std::atomic<double> g_lastDrawSec{0.0};
double nowSec() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

// Expose g_lastDrawSec to Renderer::draw() heartbeat (ViewDelegate stamps it; Renderer reads it
// indirectly through the keep-alive guard, which is in this TU). Not needed by Renderer itself.
// The stamp lives here; ViewDelegate writes it each frame via drawInMTKView.

#pragma mark - ViewDelegate
#pragma region ViewDelegate {

ViewDelegate::ViewDelegate(MTL::Device* pDevice) : MTK::ViewDelegate(), _pRenderer(new Renderer(pDevice)) {}

ViewDelegate::~ViewDelegate() { delete _pRenderer; }

void ViewDelegate::drawInMTKView(MTK::View* pView) {
  g_lastDrawSec.store(nowSec(), std::memory_order_relaxed);  // verify keep-alive heartbeat
  _pRenderer->draw(pView);
}

#pragma endregion ViewDelegate }

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
  {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't litter imgui.ini next to the binary
    // B2: restrict all floating windows (Output/Inspector/Timeline) to title-bar drag only —
    // prevents accidental body-drag while interacting with the canvas. The canvas host window
    // (drawNodeCanvas) is ImGuiWindowFlags_NoMove so this flag doesn't affect it.
    // = imgui ConfigWindowsMoveFromTitleBarOnly (bool, default false).
    io.ConfigWindowsMoveFromTitleBarOnly = true;
  }
  ImGui::StyleColorsDark();
  sw::ui::theme::apply();  // faithful TiXL default theme over the ImGui style (= T3Style.Apply; ui zone)
  sw::ui::loadCjkFont();  // merge a macOS CJK face onto the atlas so Chinese names render (ui zone)
  ImGui_ImplOSX_Init(static_cast<void*>(_pMtkView));
  ImGui_ImplMetal_Init(_pDevice);

  ed::Config cfg;
  cfg.SettingsFile = nullptr;  // don't litter NodeEditor.json next to the binary
  sw::ui::g_NodeEditor = ed::CreateEditor(&cfg);

  sw::doc::initSnapshot();  // startup matches default graph -> not dirty

  _pWindow->makeKeyAndOrderFront(nullptr);

  // verify keep-alive (see g_lastDrawSec): manual draw when the display link stalls >250ms.
  // The source is created once and lives for the app's lifetime (never released by design).
  static dispatch_source_t s_keepAlive =
      dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
  dispatch_source_set_timer(s_keepAlive, DISPATCH_TIME_NOW, NSEC_PER_SEC / 30, NSEC_PER_SEC / 100);
  dispatch_set_context(s_keepAlive, _pMtkView);
  dispatch_source_set_event_handler_f(s_keepAlive, [](void* ctx) {
    // Idle stall: redraw at the timer's pace once the display link is clearly dead (>250ms).
    // Hand-driving stall: queued hand steps drain ONE per frame, and ImGui's double-click
    // window (~0.30s real time) closes between 250ms frames — a queued `double` could never
    // register. So while hand steps are pending, pump at gesture speed; 0.05s still exceeds
    // any live display-link interval (120/60/30Hz), so the guard stays silent when frames flow.
    const double stale = nowSec() - g_lastDrawSec.load(std::memory_order_relaxed);
    if (stale > 0.25 || (sw::hand::hasPending() && stale > 0.05))
      static_cast<MTK::View*>(ctx)->draw();
  });
  dispatch_resume(s_keepAlive);

  NS::Application* pApp = reinterpret_cast<NS::Application*>(pNotification->object());
  pApp->activateIgnoringOtherApps(true);
}

bool AppDelegate::applicationShouldTerminateAfterLastWindowClosed(NS::Application* pSender) {
  return true;
}

#pragma endregion AppDelegate }
