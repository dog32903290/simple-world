// ui/editor_ui — toolbar / node canvas / inspector (imgui draw).
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
#include "ui/editor_ui.h"

#include <algorithm>
#include <string>

#include "imgui.h"

#include "app/document.h"
#include "runtime/graph.h"
#include "runtime/particle_system.h"
#include "verify/eye/eye.h"

namespace ed = ax::NodeEditor;

// g_particles is owned by main/Renderer; the DrawPoints node previews its target.
extern sw::ParticleSystem* g_particles;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;

namespace {
// pin id <-> (node id, port index) — see sw::pinId().
int pinNodeId(int pin) { return (pin - 1) / 100; }
int pinPortIndex(int pin) { return (pin - 1) % 100; }
bool pinIsInput(int pin) {
  const sw::Node* n = sw::doc::g_graph.node(pinNodeId(pin));
  if (!n) return false;
  const sw::NodeSpec* s = sw::findSpec(n->type);
  if (!s) return false;
  int idx = pinPortIndex(pin);
  return idx >= 0 && idx < (int)s->ports.size() && s->ports[idx].isInput;
}

void addNode(const std::string& type) {
  sw::Node n;
  n.id = sw::doc::g_graph.nextId++;
  n.type = type;
  n.x = 120.0f;
  n.y = 120.0f;
  if (const sw::NodeSpec* s = sw::findSpec(type))
    for (const auto& p : s->params) n.params[p.id] = p.def;
  sw::doc::g_graph.nodes.push_back(n);
  sw::doc::g_relayout = true;
  sw::doc::g_status = "added " + type;
}
}  // namespace

void drawToolbar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f, vp->WorkPos.y + 12.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Toolbar");
  if (ImGui::Button("New")) sw::doc::doNew();
  sw::eye::recordItem("New");  // eye③: hand off this widget's screen rect
  ImGui::SameLine();
  if (ImGui::Button("Open")) sw::doc::doOpen();
  sw::eye::recordItem("Open");
  ImGui::SameLine();
  if (ImGui::Button("Save")) sw::doc::doSave();
  sw::eye::recordItem("Save");
  ImGui::SameLine();
  if (ImGui::Button("Save As")) sw::doc::doSaveAs();
  sw::eye::recordItem("Save As");
  ImGui::SameLine();
  if (ImGui::Button("Add Node")) ImGui::OpenPopup("add_node_popup");
  sw::eye::recordItem("Add Node");
  if (ImGui::BeginPopup("add_node_popup")) {
    for (const std::string& t : sw::specTypes())
      if (ImGui::MenuItem(t.c_str())) addNode(t);
    ImGui::EndPopup();
  }
  ImGui::TextDisabled("%s", sw::doc::g_status.c_str());
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
  for (const sw::Node& node : sw::doc::g_graph.nodes) {
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

  for (const sw::Connection& c : sw::doc::g_graph.connections) ed::Link(c.id, c.fromPin, c.toPin);

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
          sw::doc::g_graph.connections.push_back({sw::doc::g_graph.nextId++, from, to});
          sw::doc::g_status = "linked";
        }
      } else {
        ed::RejectNewItem();
      }
    }
    ed::EndCreate();
  }

  // macOS: the key labelled "delete" sends Backspace, but the node editor only
  // listens for forward-Delete (ImGuiKey_Delete). Route Backspace into the
  // editor's manual-delete queue so the BeginDelete handler below picks it up.
  if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Backspace) &&
      !ImGui::GetIO().WantTextInput) {
    ed::NodeId nodes[128];
    int nn = ed::GetSelectedNodes(nodes, 128);
    for (int i = 0; i < nn; ++i) ed::DeleteNode(nodes[i]);
    ed::LinkId links[128];
    int nl = ed::GetSelectedLinks(links, 128);
    for (int i = 0; i < nl; ++i) ed::DeleteLink(links[i]);
  }

  // Delete links / nodes (select + Delete key).
  if (ed::BeginDelete()) {
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid)) {
      if (ed::AcceptDeletedItem()) {
        int id = (int)lid.Get();
        auto& cs = sw::doc::g_graph.connections;
        cs.erase(std::remove_if(cs.begin(), cs.end(),
                                [id](const sw::Connection& c) { return c.id == id; }),
                 cs.end());
      }
    }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      if (ed::AcceptDeletedItem()) {
        int id = (int)nid.Get();
        auto& ns = sw::doc::g_graph.nodes;
        ns.erase(std::remove_if(ns.begin(), ns.end(), [id](const sw::Node& n) { return n.id == id; }),
                 ns.end());
        auto& cs = sw::doc::g_graph.connections;
        cs.erase(std::remove_if(cs.begin(), cs.end(),
                                [id](const sw::Connection& c) {
                                  return pinNodeId(c.fromPin) == id || pinNodeId(c.toPin) == id;
                                }),
                 cs.end());
      }
    }
    ed::EndDelete();
  }

  if (sw::doc::g_relayout) {  // initial / after add / after load: push positions to editor
    for (const sw::Node& node : sw::doc::g_graph.nodes) ed::SetNodePosition(node.id, ImVec2(node.x, node.y));
    sw::doc::g_relayout = false;
  } else {  // sync editor positions back to graph so Save captures dragging
    for (sw::Node& node : sw::doc::g_graph.nodes) {
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
  sw::Node* sel = sw::doc::g_graph.node(g_selectedNode);
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

}  // namespace sw::ui
