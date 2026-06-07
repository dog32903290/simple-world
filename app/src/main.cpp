// simple_world — step 0 shell (Stage A: pure metal-cpp clear screen)
//
// Window/clear scaffold adapted from Apple's LearnMetalCpp 00-window
// (LeeTeng2001/metal-cpp-cmake). Pure C++: NS::Application + MTK::View, no .mm.
// Stages B/C/D (imgui, imgui-node-editor, --selftest) slot into Renderer::draw.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"  // void* overloads via IMGUI_IMPL_METAL_CPP_EXTENSIONS
#include "imgui_node_editor.h"
#include <nfd.hpp>

#include "verify/eye/eye.h"
#include "platform/dialogs.h"
#include "runtime/dispatch.h"
#include "runtime/graph.h"
#include "runtime/particle_system.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace ed = ax::NodeEditor;

namespace {
// Editor background. Also the color --selftest will assert against later.
const MTL::ClearColor kClearColor = MTL::ClearColor::Make(0.12, 0.14, 0.18, 1.0);

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

// step 0: a single node-editor context with two fake nodes + one fake link,
// just to prove the canvas is wired to imgui + Metal and is interactive.
ed::EditorContext* g_NodeEditor = nullptr;

// Live particle slice: cooked each frame into its own target texture, displayed
// inside the DrawPoints node. The texture is a viewport over real runtime output
// (ui-skin-pressure-gate:皮 downstream of real data), not fake state.
sw::ParticleSystem* g_particles = nullptr;
MTL::Library* g_shaderLib = nullptr;
uint32_t g_frameIndex = 0;
float g_time = 0.0f;

// The editorGraph — single source of truth for the canvas (nodes / connections /
// params). The canvas is DRAWN from it, the Inspector EDITS into it, the runtime
// is COOKED from it. Save/load roundtrip proven by --selftest-graph.
sw::Graph g_graph = sw::defaultParticleGraph();
int g_selectedNode = 0;     // node-editor id of the selected node, 0 = none
bool g_relayout = true;     // push graph positions to the editor (initial / add / load)
std::string g_status = "ready";

// Document state for save/load. g_documentPath empty == never-saved (Untitled).
// g_savedSnapshot is toJson() at the last successful save/open/new; comparing it
// to the live graph yields the dirty flag (see isDirty()).
std::string g_documentPath;
std::string g_savedSnapshot;
NS::Window* g_window = nullptr;  // set in applicationDidFinishLaunching, for title updates
std::string g_lastTitle;         // cache so we only setTitle when it actually changes

// pin id <-> (node id, port index) — see sw::pinId().
int pinNodeId(int pin) { return (pin - 1) / 100; }
int pinPortIndex(int pin) { return (pin - 1) % 100; }
bool pinIsInput(int pin) {
  const sw::Node* n = g_graph.node(pinNodeId(pin));
  if (!n) return false;
  const sw::NodeSpec* s = sw::findSpec(n->type);
  if (!s) return false;
  int idx = pinPortIndex(pin);
  return idx >= 0 && idx < (int)s->ports.size() && s->ports[idx].isInput;
}

bool isDirty() { return sw::toJson(g_graph) != g_savedSnapshot; }

// Forward decl: doSave is used by confirmDiscardIfDirty before it is defined.
bool doSave();

// Returns false only when the user explicitly cancels (so callers abort).
bool confirmDiscardIfDirty() {
  if (!isDirty()) return true;
  switch (sw::askUnsaved()) {
    case sw::UnsavedChoice::Save:     return doSave();   // false if Save As canceled
    case sw::UnsavedChoice::DontSave: return true;
    case sw::UnsavedChoice::Cancel:   return false;
  }
  return false;
}

// Always prompts for a location. Returns true if a file was written.
bool doSaveAs() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::SaveDialog(outPath, filters, 1, nullptr, "untitled.swproj");
  if (r != NFD_OKAY) return false;  // cancel or error
  std::string path = outPath.get();
  if (path.size() < 7 || path.substr(path.size() - 7) != ".swproj") path += ".swproj";
  std::string json = sw::toJson(g_graph);
  if (!sw::saveGraphToFile(path, g_graph)) { sw::showError("無法寫入：" + path); return false; }
  g_documentPath = path;
  g_savedSnapshot = json;
  g_status = "saved -> " + path;
  return true;
}

// Overwrites the current document; falls back to Save As when never saved.
bool doSave() {
  if (g_documentPath.empty()) return doSaveAs();
  std::string json = sw::toJson(g_graph);
  if (!sw::saveGraphToFile(g_documentPath, g_graph)) {
    sw::showError("無法寫入：" + g_documentPath);
    return false;
  }
  g_savedSnapshot = json;
  g_status = "saved -> " + g_documentPath;
  return true;
}

void doOpen() {
  if (!confirmDiscardIfDirty()) return;
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::OpenDialog(outPath, filters, 1, nullptr);
  if (r != NFD_OKAY) return;
  std::string path = outPath.get();
  sw::Graph loaded;  // load into a temp graph; only swap in on success
  if (!sw::loadGraphFromFile(path, loaded)) {
    sw::showError("無法讀取此專案檔：" + path);
    return;
  }
  g_graph = loaded;
  g_documentPath = path;
  g_savedSnapshot = sw::toJson(g_graph);
  g_relayout = true;
  g_status = "loaded <- " + path;
}

void doNew() {
  if (!confirmDiscardIfDirty()) return;
  g_graph = sw::defaultParticleGraph();
  g_documentPath.clear();
  g_savedSnapshot = sw::toJson(g_graph);
  g_relayout = true;
  g_status = "new project";
}

void updateWindowTitle() {
  if (!g_window) return;
  std::string name = g_documentPath.empty()
      ? std::string("Untitled")
      : g_documentPath.substr(g_documentPath.find_last_of('/') + 1);
  std::string title = (isDirty() ? "• " : "") + name + " — simple_world";
  if (title == g_lastTitle) return;
  g_lastTitle = title;
  g_window->setTitle(NS::String::string(title.c_str(), NS::StringEncoding::UTF8StringEncoding));
}

void addNode(const std::string& type) {
  sw::Node n;
  n.id = g_graph.nextId++;
  n.type = type;
  n.x = 120.0f;
  n.y = 120.0f;
  if (const sw::NodeSpec* s = sw::findSpec(type))
    for (const auto& p : s->params) n.params[p.id] = p.def;
  g_graph.nodes.push_back(n);
  g_relayout = true;
  g_status = "added " + type;
}

void drawToolbar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f, vp->WorkPos.y + 12.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Toolbar");
  if (ImGui::Button("New")) doNew();
  sw::eye::recordItem("New");  // eye③: hand off this widget's screen rect
  ImGui::SameLine();
  if (ImGui::Button("Open")) doOpen();
  sw::eye::recordItem("Open");
  ImGui::SameLine();
  if (ImGui::Button("Save")) doSave();
  sw::eye::recordItem("Save");
  ImGui::SameLine();
  if (ImGui::Button("Save As")) doSaveAs();
  sw::eye::recordItem("Save As");
  ImGui::SameLine();
  if (ImGui::Button("Add Node")) ImGui::OpenPopup("add_node_popup");
  sw::eye::recordItem("Add Node");
  if (ImGui::BeginPopup("add_node_popup")) {
    for (const std::string& t : sw::specTypes())
      if (ImGui::MenuItem(t.c_str())) addNode(t);
    ImGui::EndPopup();
  }
  ImGui::TextDisabled("%s", g_status.c_str());
  ImGui::End();
}

void drawNodeCanvas() {
  // The node canvas IS the main workspace: a borderless host window pinned to
  // fill the whole viewport, node editor drawn inside it. Named windows (e.g.
  // Inspector) float on top because this host uses NoBringToFrontOnFocus.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool hostOpen = ImGui::Begin("##canvas_host", nullptr, hostFlags);
  ImGui::PopStyleVar();

  ed::SetCurrentEditor(g_NodeEditor);
  if (hostOpen) {
    ed::Begin("canvas");

  // Draw every node from graph data, via its NodeSpec (title + ports).
  for (const sw::Node& node : g_graph.nodes) {
    const sw::NodeSpec* spec = sw::findSpec(node.type);
    ed::BeginNode(node.id);
    ImGui::TextUnformatted(spec ? spec->title.c_str() : node.type.c_str());
    if (spec) {
      for (size_t i = 0; i < spec->ports.size(); ++i) {
        const sw::PortSpec& p = spec->ports[i];
        ed::BeginPin(sw::pinId(node.id, (int)i),
                     p.isInput ? ed::PinKind::Input : ed::PinKind::Output);
        ImGui::TextUnformatted(p.isInput ? ("-> " + p.name).c_str() : (p.name + " ->").c_str());
        ed::EndPin();
      }
    }
    if (node.type == "DrawPoints" && g_particles && g_particles->target())
      ImGui::Image(reinterpret_cast<ImTextureID>(g_particles->target()), ImVec2(200, 200));
    ed::EndNode();
  }

  for (const sw::Connection& c : g_graph.connections) ed::Link(c.id, c.fromPin, c.toPin);

  // Create links by dragging pin -> pin (one input + one output, different nodes).
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b) && a && b) {
      int pa = (int)a.Get(), pb = (int)b.Get();
      bool ia = pinIsInput(pa), ib = pinIsInput(pb);
      if (pa != pb && ia != ib && pinNodeId(pa) != pinNodeId(pb)) {
        if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          g_graph.connections.push_back({g_graph.nextId++, from, to});
          g_status = "linked";
        }
      } else {
        ed::RejectNewItem();
      }
    }
    ed::EndCreate();
  }

  // Delete links / nodes (select + Delete key).
  if (ed::BeginDelete()) {
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid)) {
      if (ed::AcceptDeletedItem()) {
        int id = (int)lid.Get();
        auto& cs = g_graph.connections;
        cs.erase(std::remove_if(cs.begin(), cs.end(),
                                [id](const sw::Connection& c) { return c.id == id; }),
                 cs.end());
      }
    }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      if (ed::AcceptDeletedItem()) {
        int id = (int)nid.Get();
        auto& ns = g_graph.nodes;
        ns.erase(std::remove_if(ns.begin(), ns.end(), [id](const sw::Node& n) { return n.id == id; }),
                 ns.end());
        auto& cs = g_graph.connections;
        cs.erase(std::remove_if(cs.begin(), cs.end(),
                                [id](const sw::Connection& c) {
                                  return pinNodeId(c.fromPin) == id || pinNodeId(c.toPin) == id;
                                }),
                 cs.end());
      }
    }
    ed::EndDelete();
  }

  if (g_relayout) {  // initial / after add / after load: push graph positions to editor
    for (const sw::Node& node : g_graph.nodes) ed::SetNodePosition(node.id, ImVec2(node.x, node.y));
    g_relayout = false;
  } else {  // sync editor positions back to graph so Save captures dragging
    for (sw::Node& node : g_graph.nodes) {
      ImVec2 p = ed::GetNodePosition(node.id);
      node.x = p.x;
      node.y = p.y;
    }
  }

  // Capture selection (while the editor is current) for the Inspector.
  ed::NodeId sel[1];
  int nsel = ed::GetSelectedNodes(sel, 1);
  g_selectedNode = (nsel > 0) ? (int)sel[0].Get() : 0;

    ed::End();
  }
  ed::SetCurrentEditor(nullptr);

  ImGui::End();  // ##canvas_host
}

// Inspector (shell only): future home of the selected node's parameters. Kept
// honest per ui-skin-pressure-gate — NO fake parameter widgets until a NodeSpec/
// param contract exists (step 1+). For now: a selection placeholder + live FPS
// (the one real signal: proves the runtime is ticking).
void drawInspector() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 320.0f, vp->WorkPos.y + 24.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300.0f, 180.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Inspector");
  sw::Node* sel = g_graph.node(g_selectedNode);
  if (sel) {
    const sw::NodeSpec* spec = sw::findSpec(sel->type);
    ImGui::TextUnformatted(spec ? spec->title.c_str() : sel->type.c_str());
    ImGui::Separator();
    if (spec && !spec->params.empty()) {
      for (const sw::ParamSpec& p : spec->params) {
        float& v = sel->params[p.id];  // seeded from defaults at construction
        ImGui::SliderFloat(p.label.c_str(), &v, p.minV, p.maxV);
      }
    } else {
      ImGui::TextDisabled("(no editable parameters)");
    }
  } else {
    ImGui::TextDisabled("No node selected");
    ImGui::TextWrapped("Click a node in the canvas to edit its parameters.");
  }
  ImGui::Spacing();
  ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
  ImGui::End();
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

  NS::Menu* createMenuBar();

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
  if (g_NodeEditor) ed::DestroyEditor(g_NodeEditor);
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplOSX_Shutdown();
  ImGui::DestroyContext();

  if (_pMtkView) _pMtkView->release();
  if (_pWindow) _pWindow->release();
  if (_pDevice) _pDevice->release();
  delete _pViewDelegate;
}

NS::Menu* AppDelegate::createMenuBar() {
  using NS::StringEncoding::UTF8StringEncoding;

  NS::Menu* pMainMenu = NS::Menu::alloc()->init();
  NS::MenuItem* pAppMenuItem = NS::MenuItem::alloc()->init();
  NS::Menu* pAppMenu = NS::Menu::alloc()->init(NS::String::string("simple_world", UTF8StringEncoding));

  // Don't use localizedName(): an un-bundled CLI binary returns a misaligned/nil
  // NS::String that UBSan (correctly) flags. Use a fixed product name instead.
  NS::String* quitItemName = NS::String::string("Quit simple_world", UTF8StringEncoding);
  SEL quitCb = NS::MenuItem::registerActionCallback("appQuit", [](void*, SEL, const NS::Object* pSender) {
    if (confirmDiscardIfDirty())
      NS::Application::sharedApplication()->terminate(pSender);
  });

  NS::MenuItem* pAppQuitItem = pAppMenu->addItem(quitItemName, quitCb, NS::String::string("q", UTF8StringEncoding));
  pAppQuitItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);
  pAppMenuItem->setSubmenu(pAppMenu);

  NS::MenuItem* pWindowMenuItem = NS::MenuItem::alloc()->init();
  NS::Menu* pWindowMenu = NS::Menu::alloc()->init(NS::String::string("Window", UTF8StringEncoding));
  SEL closeWindowCb = NS::MenuItem::registerActionCallback("windowClose", [](void*, SEL, const NS::Object*) {
    if (confirmDiscardIfDirty())
      NS::Application::sharedApplication()->windows()->object<NS::Window>(0)->close();
  });
  NS::MenuItem* pCloseWindowItem =
      pWindowMenu->addItem(NS::String::string("Close Window", UTF8StringEncoding), closeWindowCb,
                           NS::String::string("w", UTF8StringEncoding));
  pCloseWindowItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);
  pWindowMenuItem->setSubmenu(pWindowMenu);

  // ---- File menu: New / Open / Save / Save As, with standard shortcuts ----
  NS::MenuItem* pFileMenuItem = NS::MenuItem::alloc()->init();
  NS::Menu* pFileMenu = NS::Menu::alloc()->init(NS::String::string("File", UTF8StringEncoding));

  SEL newCb = NS::MenuItem::registerActionCallback("fileNew", [](void*, SEL, const NS::Object*) { doNew(); });
  NS::MenuItem* pNewItem = pFileMenu->addItem(NS::String::string("New", UTF8StringEncoding), newCb,
                                              NS::String::string("n", UTF8StringEncoding));
  pNewItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL openCb = NS::MenuItem::registerActionCallback("fileOpen", [](void*, SEL, const NS::Object*) { doOpen(); });
  NS::MenuItem* pOpenItem = pFileMenu->addItem(NS::String::string("Open…", UTF8StringEncoding), openCb,
                                               NS::String::string("o", UTF8StringEncoding));
  pOpenItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL saveCb = NS::MenuItem::registerActionCallback("fileSave", [](void*, SEL, const NS::Object*) { doSave(); });
  NS::MenuItem* pSaveItem = pFileMenu->addItem(NS::String::string("Save", UTF8StringEncoding), saveCb,
                                               NS::String::string("s", UTF8StringEncoding));
  pSaveItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand);

  SEL saveAsCb = NS::MenuItem::registerActionCallback("fileSaveAs", [](void*, SEL, const NS::Object*) { doSaveAs(); });
  NS::MenuItem* pSaveAsItem = pFileMenu->addItem(NS::String::string("Save As…", UTF8StringEncoding), saveAsCb,
                                                 NS::String::string("s", UTF8StringEncoding));
  pSaveAsItem->setKeyEquivalentModifierMask(NS::EventModifierFlagCommand | NS::EventModifierFlagShift);

  pFileMenuItem->setSubmenu(pFileMenu);

  pMainMenu->addItem(pAppMenuItem);
  pMainMenu->addItem(pFileMenuItem);
  pMainMenu->addItem(pWindowMenuItem);

  pAppMenuItem->release();
  pFileMenuItem->release();
  pWindowMenuItem->release();
  pAppMenu->release();
  pFileMenu->release();
  pWindowMenu->release();

  return pMainMenu->autorelease();
}

void AppDelegate::applicationWillFinishLaunching(NS::Notification* pNotification) {
  NS::Menu* pMenu = createMenuBar();
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
  g_window = _pWindow;  // expose to updateWindowTitle()
  sw::installCloseGuard(static_cast<void*>(g_window),
                        []() -> bool { return confirmDiscardIfDirty(); });  // red-button guard

  // ---- Dear ImGui setup (OSX backend needs view.window, so init after setContentView) ----
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;  // don't litter imgui.ini next to the binary
  ImGui::StyleColorsDark();
  ImGui_ImplOSX_Init(static_cast<void*>(_pMtkView));
  ImGui_ImplMetal_Init(_pDevice);

  ed::Config cfg;
  cfg.SettingsFile = nullptr;  // don't litter NodeEditor.json next to the binary
  g_NodeEditor = ed::CreateEditor(&cfg);

  g_savedSnapshot = sw::toJson(g_graph);  // startup matches default graph -> not dirty

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

  MTL::RenderPassDescriptor* pRpd = pView->currentRenderPassDescriptor();
  if (pRpd == nullptr) {
    pPool->release();
    return;
  }

  // ---- Cook the particle slice into its own target texture (before imgui samples it) ----
  if (g_particles) {
    // Cook: graph params drive the runtime (皮 downstream of real graph data).
    g_particles->setTurbulenceAmount(g_graph.param("TurbulenceForce", "Amount", 15.0f));
    g_particles->setTurbulenceFrequency(g_graph.param("TurbulenceForce", "Frequency", 1.2f));
    g_particles->setSpeed(g_graph.param("ParticleSystem", "Speed", 1.0f));
    g_particles->setDrag(g_graph.param("ParticleSystem", "Drag", 0.02f));
    ++g_frameIndex;
    const float dt = 1.0f / 60.0f;
    g_time += dt;
    g_particles->update(_pCommandQueue, g_time, dt);  // TurbulenceForce + integrator
    g_particles->render(_pCommandQueue);               // DrawPoints -> target texture
  }

  // eye① clean: the pure render layer, BEFORE any imgui chrome touches it.
  if (eyeReq.clean && g_particles) sw::eye::dumpTextureRGBA(g_particles->target(), "clean.png");

  // ---- Begin imgui frame (Metal backend uses our metal-cpp pointers directly) ----
  ImGui_ImplMetal_NewFrame(pRpd);
  ImGui_ImplOSX_NewFrame(static_cast<void*>(pView));
  ImGui::NewFrame();

  sw::eye::beginWidgetFrame();  // eye③: collect clickable widget rects this frame
  drawToolbar();     // Save / Load / Add Node (floating)
  drawNodeCanvas();  // main workspace, fills the viewport
  drawInspector();   // floats on top
  updateWindowTitle();  // filename + dirty star; no-op when unchanged

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

  // eye② full: the whole presented frame (UI + render). Stall one frame so the
  // drawable's backing is finished before we read it back (BGRA8_sRGB).
  if (eyeReq.full && pDrawable) {
    pCmd->waitUntilCompleted();
    sw::eye::dumpDrawableBGRA(pDrawable->texture(), "full.png");
  }

  pPool->release();
}

#pragma endregion Renderer }
