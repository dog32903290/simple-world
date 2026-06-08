// ui/editor_ui — toolbar / node canvas / inspector (imgui draw).
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
#include "ui/editor_ui.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
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
int pinNodeId(int pin) { return sw::pinNode(pin); }
int pinPortIndex(int pin) { return (pin - 1) % 100; }
bool pinIsInput(int pin) {
  const sw::Node* n = sw::doc::g_graph.node(pinNodeId(pin));
  if (!n) return false;
  const sw::NodeSpec* s = sw::findSpec(n->type);
  if (!s) return false;
  int idx = pinPortIndex(pin);
  return idx >= 0 && idx < (int)s->ports.size() && s->ports[idx].isInput;
}

// 拖動暫存：node id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;

// Inspector slider 拖動開始時的常數值（一次只有一個 slider 在拖）。放手時用它記 undo。
float g_paramEditBefore = 0.0f;

void addNode(const std::string& type) {
  sw::Node n;
  n.id = sw::doc::g_graph.nextId++;
  n.type = type;
  n.x = 120.0f;
  n.y = 120.0f;
  if (const sw::NodeSpec* s = sw::findSpec(type))
    for (const sw::PortSpec& p : s->ports)
      if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;
  sw::g_commands.push(std::make_unique<sw::AddNodeCommand>(sw::doc::g_graph, n));
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
    for (const std::string& t : sw::specTypes()) {
      if (ImGui::MenuItem(t.c_str())) addNode(t);
      sw::eye::recordItem(("menu:" + t).c_str());  // eye: popup item rect (drawn outside canvas)
    }
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
        // eye: record the pin label's SCREEN rect so hand can drag pin->pin.
        // GetItemRect inside Begin/EndNode is canvas-local; CanvasToScreen -> screen.
        ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
        ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
        sw::eye::recordRect(("pin:" + std::to_string(sw::pinId(node.id, (int)i))).c_str(),
                            pa.x, pa.y, pb.x, pb.y);
        ed::EndPin();
      }
    }
    if (node.type == "DrawPoints" && g_particles && g_particles->target())
      ImGui::Image(reinterpret_cast<ImTextureID>(g_particles->target()), ImVec2(200, 200));
    ed::EndNode();
    // eye: node body SCREEN rect via the node-editor's own position/size + transform.
    ImVec2 na = ed::CanvasToScreen(ed::GetNodePosition(node.id));
    ImVec2 nsz = ed::GetNodeSize(node.id);
    sw::eye::recordRect(("node:" + std::to_string(node.id)).c_str(),
                        na.x, na.y, na.x + nsz.x, na.y + nsz.y);
  }

  for (const sw::Connection& c : sw::doc::g_graph.connections) ed::Link(c.id, c.fromPin, c.toPin);

  // Create links by dragging pin -> pin (one input + one output, different nodes).
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b) && a && b) {
      int pa = (int)a.Get(), pb = (int)b.Get();
      bool ia = pinIsInput(pa), ib = pinIsInput(pb);
      if (pa != pb && ia != ib && pinNodeId(pa) != pinNodeId(pb)) {
        // Reject if the two pins' dataType differ (Float↔Float, buffer↔same-type).
        auto portTypeOf = [](int pin) -> std::string {
          const sw::Node* n = sw::doc::g_graph.node(pinNodeId(pin));
          if (!n) return "";
          const sw::NodeSpec* s = sw::findSpec(n->type);
          int idx = pinPortIndex(pin);
          return (s && idx < (int)s->ports.size()) ? s->ports[idx].dataType : "";
        };
        if (portTypeOf(pa) != portTypeOf(pb)) {
          ed::RejectNewItem();
        } else if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          const sw::Connection* old = sw::doc::g_graph.connectionToInput(to);
          if (old && old->fromPin == from) {
            // already wired to this exact source — nothing to do
          } else if (old) {
            // reconnect: remove the input's old link, add the new one, as one undo unit
            auto macro = std::make_unique<sw::MacroCommand>("Reconnect");
            macro->add(std::make_unique<sw::DeleteConnectionsCommand>(
                sw::doc::g_graph, std::vector<int>{old->id}));
            sw::Connection c{sw::doc::g_graph.nextId++, from, to};
            macro->add(std::make_unique<sw::AddConnectionCommand>(sw::doc::g_graph, c));
            sw::g_commands.push(std::move(macro));
            sw::doc::g_status = "reconnected";
          } else {
            sw::Connection c{sw::doc::g_graph.nextId++, from, to};
            sw::g_commands.push(std::make_unique<sw::AddConnectionCommand>(sw::doc::g_graph, c));
            sw::doc::g_status = "linked";
          }
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

  // Undo / Redo: Cmd+Z / Cmd+Shift+Z (macOS).
  // IMPORTANT: imgui's ConfigMacOSXBehaviors (default on __APPLE__) swaps
  // Cmd->Ctrl inside AddKeyEvent, so a physical Cmd press lands in io.KeyCtrl,
  // NOT io.KeySuper. Detect Cmd via io.KeyCtrl. (Verified by --selftest-hand.)
  {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused() && io.KeyCtrl && !io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) sw::g_commands.redo();
        else             sw::g_commands.undo();
        sw::doc::g_status = io.KeyShift ? "redo" : "undo";
        sw::doc::g_relayout = true;  // canvas re-seeds node positions from the restored graph
      }
    }
  }

  // Delete links / nodes (select + Delete key, or Backspace routed above).
  if (ed::BeginDelete()) {
    std::vector<int> delLinks, delNodes;
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid))
      if (ed::AcceptDeletedItem()) delLinks.push_back((int)lid.Get());
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid))
      if (ed::AcceptDeletedItem()) delNodes.push_back((int)nid.Get());
    ed::EndDelete();

    // 入射於被刪節點的連線交給 DeleteNodesCommand 處理，從 delLinks 去重，
    // 否則同一條線會被刪兩次（undo 也會重複還原）。
    auto incidentToDeletedNode = [&](int linkId) {
      for (const sw::Connection& c : sw::doc::g_graph.connections)
        if (c.id == linkId) {
          int fn = sw::pinNode(c.fromPin), tn = sw::pinNode(c.toPin);
          return std::find(delNodes.begin(), delNodes.end(), fn) != delNodes.end() ||
                 std::find(delNodes.begin(), delNodes.end(), tn) != delNodes.end();
        }
      return false;
    };
    std::vector<int> standaloneLinks;
    for (int id : delLinks)
      if (!incidentToDeletedNode(id)) standaloneLinks.push_back(id);

    if (!delNodes.empty() || !standaloneLinks.empty()) {
      auto macro = std::make_unique<sw::MacroCommand>("Delete");
      if (!standaloneLinks.empty())
        macro->add(std::make_unique<sw::DeleteConnectionsCommand>(sw::doc::g_graph, standaloneLinks));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteNodesCommand>(sw::doc::g_graph, delNodes));
      sw::g_commands.push(std::move(macro));
      sw::doc::g_status = "deleted";
    }
  } else {
    ed::EndDelete();
  }

  if (sw::doc::g_relayout) {  // initial / after add / after load
    for (const sw::Node& node : sw::doc::g_graph.nodes)
      ed::SetNodePosition(node.id, ImVec2(node.x, node.y));
    sw::doc::g_relayout = false;
  } else {
    bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging) {
      // 拖動中：記下還沒記過的節點的起始座標，並即時把位置反映到 graph（畫面跟手）。
      for (sw::Node& node : sw::doc::g_graph.nodes) {
        ImVec2 p = ed::GetNodePosition(node.id);
        if (g_dragStart.find(node.id) == g_dragStart.end())
          g_dragStart[node.id] = ImVec2(node.x, node.y);
        node.x = p.x;
        node.y = p.y;
      }
    } else if (!g_dragStart.empty()) {
      // 放手：把真正有位移的節點組成一個 MoveNodesCommand。
      std::vector<sw::MoveNodesCommand::Move> moves;
      for (auto& kv : g_dragStart) {
        ImVec2 now = ed::GetNodePosition(kv.first);
        if (now.x != kv.second.x || now.y != kv.second.y)
          moves.push_back({kv.first, kv.second.x, kv.second.y, now.x, now.y});
      }
      g_dragStart.clear();
      if (!moves.empty()) {
        // 命令的 doIt 會再設一次新座標（冪等），先把 graph 設回舊座標避免雙重記錄混亂。
        for (auto& m : moves)
          if (sw::Node* n = sw::doc::g_graph.node(m.id)) { n->x = m.oldX; n->y = m.oldY; }
        sw::g_commands.push(std::make_unique<sw::MoveNodesCommand>(sw::doc::g_graph, moves));
      }
    } else {
      // 沒拖動：照常把 editor 位置同步回 graph（例如程式性移動）。
      for (sw::Node& node : sw::doc::g_graph.nodes) {
        ImVec2 p = ed::GetNodePosition(node.id);
        node.x = p.x;
        node.y = p.y;
      }
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
    if (spec) {
      bool any = false;
      for (size_t i = 0; i < spec->ports.size(); ++i) {
        const sw::PortSpec& p = spec->ports[i];
        if (!(p.isInput && p.dataType == "Float")) continue;
        any = true;
        int inPin = sw::pinId(sel->id, (int)i);
        if (const sw::Connection* c = sw::doc::g_graph.connectionToInput(inPin)) {
          // Driven by a connection — grey out, show source type.
          const sw::Node* src = sw::doc::g_graph.node(sw::pinNode(c->fromPin));
          ImGui::TextDisabled("%s <- %s", p.name.c_str(), src ? src->type.c_str() : "?");
        } else {
          // Free constant — slider writes LIVE into the param map so the runtime
          // sees changes mid-drag (柏為 expects immediate feedback). One undo step
          // per drag: capture the pre-drag value on activation, record on release.
          float pre = sel->params[p.id];               // value at start of this frame
          ImGui::SliderFloat(p.name.c_str(), &sel->params[p.id], p.minV, p.maxV);
          if (ImGui::IsItemActivated()) g_paramEditBefore = pre;
          if (ImGui::IsItemDeactivatedAfterEdit() && sel->params[p.id] != g_paramEditBefore)
            sw::g_commands.push(std::make_unique<sw::SetInputValueCommand>(
                sw::doc::g_graph, sel->id, p.id, g_paramEditBefore, sel->params[p.id]));
        }
      }
      if (!any) ImGui::TextDisabled("(no editable parameters)");
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
