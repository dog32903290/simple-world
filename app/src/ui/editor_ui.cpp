// ui/editor_ui — toolbar / node canvas (imgui draw); the Inspector lives in ui/inspector.cpp.
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
//
// Lib-native canvas (批次 3, 照 TiXL GraphCanvas): the canvas renders the CURRENT Symbol's
// children/connections straight off doc::g_lib — no flat Graph, no projection layer.
// Composition switch (N3) = same canvas, different symbol.
#include "ui/editor_ui.h"
#include "ui/node_draw.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/audio_settings.h"
#include "app/audio_monitor.h"
#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec / pinId / pinNode (the int pin scheme rides on child ids)
#include "verify/eye/eye.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;
int g_pinnedNode = 0;  // view ⊥ graph (see editor_ui.h); set by ui/output_window.cpp

namespace {
// pin id <-> (child id, port index) — see sw::pinId().
int pinChildId(int pin) { return sw::pinNode(pin); }
int pinPortIndex(int pin) { return (pin - 1) % 100; }

const sw::PortSpec* portOfPin(int pin) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  const sw::SymbolChild* c = cur ? sw::childById(*cur, pinChildId(pin)) : nullptr;
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s) return nullptr;
  int idx = pinPortIndex(pin);
  return (idx >= 0 && idx < (int)s->ports.size()) ? &s->ports[idx] : nullptr;
}
bool pinIsInput(int pin) {
  const sw::PortSpec* p = portOfPin(pin);
  return p && p->isInput;
}

// (childId, slotId) -> absolute pin id via the spec's port index; -1 when unresolvable.
int pinOfSlot(int childId, const std::string& slotId, bool isInput) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  const sw::SymbolChild* c = cur ? sw::childById(*cur, childId) : nullptr;
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput == isInput && s->ports[i].id == slotId) return sw::pinId(childId, (int)i);
  return -1;
}

// Link ids are STATELESS: both pins packed into one 64-bit id (SymbolConnection has no id —
// rightly, TiXL wires are bare 4-tuples). Decode gives back the exact wire endpoints, so
// delete-by-link-id needs no side table. Disjoint from node/pin id ranges (>= 2^32).
uint64_t linkIdOf(int srcPin, int dstPin) {
  return ((uint64_t)(uint32_t)srcPin << 32) | (uint32_t)dstPin;
}
int linkSrcPin(uint64_t id) { return (int)(id >> 32); }
int linkDstPin(uint64_t id) { return (int)(id & 0xffffffffu); }

// Reconstruct the 4-tuple a link id names. False if either pin no longer resolves.
bool wireOfLink(uint64_t id, sw::SymbolConnection& out) {
  const sw::PortSpec* sp = portOfPin(linkSrcPin(id));
  const sw::PortSpec* dp = portOfPin(linkDstPin(id));
  if (!sp || !dp) return false;
  out.srcChild = pinChildId(linkSrcPin(id));
  out.srcSlot = sp->id;
  out.dstChild = pinChildId(linkDstPin(id));
  out.dstSlot = dp->id;
  return true;
}

// 拖動暫存：child id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;

void addNode(const std::string& type) {
  sw::Symbol* cur = sw::doc::currentSymbol();
  if (!cur) return;
  sw::SymbolChild c;
  c.id = sw::nextFreeChildId(*cur);
  c.symbolId = type;
  c.x = 120.0f;
  c.y = 120.0f;
  // overrides stay EMPTY — the instance reads the definition's defaults until edited
  // (TiXL Symbol.Child semantics; the flat editor's params-prefill died with it).
  sw::g_commands.push(std::make_unique<sw::AddChildCommand>(sw::doc::g_lib, cur->id, c));
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
  // Audio input device picker (Ableton-style): list the machine's inputs and route
  // capture to the chosen one. ui -> app(audio_settings) -> platform; the pick persists.
  ImGui::SetNextItemWidth(240.0f);
  if (ImGui::BeginCombo("Audio In", sw::audio::selectedName().c_str())) {
    if (ImGui::Selectable("System Default", sw::audio::selectedUid().empty()))
      sw::audio::selectByUid("");
    for (const sw::audio::DeviceInfo& d : sw::audio::inputDevices()) {
      const bool sel = (d.uid == sw::audio::selectedUid());
      std::string label = d.name + (d.isDefault ? "  (default)" : "");
      if (ImGui::Selectable(label.c_str(), sel)) sw::audio::selectByUid(d.uid);
    }
    ImGui::EndCombo();
  }
  sw::eye::recordItem("Audio In");  // eye③: hand off this widget's screen rect
  // The live input meter now lives inside the AudioReaction node (level/hit on its face) —
  // see drawNodeCanvas; the toolbar just picks the device.
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

  sw::Symbol* cur = sw::doc::currentSymbol();

  ed::SetCurrentEditor(g_NodeEditor);
  if (hostOpen && cur) {
    ed::Begin("canvas");

  // Draw every child of the CURRENT symbol (= TiXL GraphCanvas renders Symbol children).
  // The TiXL skin (category color, port columns, type-colored slots, custom face) + eye
  // rects live in ui/node_draw. The live preview is NOT welded to any node body — it lives
  // in the Output window (ui/output_window.cpp), which shows ANY pinned node's output
  // (view ⊥ graph; OUTPUT_PIN_VIEWER_CONTRACT §4-A).
  for (const sw::SymbolChild& child : cur->children) sw::ui::drawChild(child);

  // Wires. Boundary-sentinel wires (the symbol's own external ports) get their pins in N3
  // (the root has none today); skip them rather than draw a dangling link.
  for (const sw::SymbolConnection& w : cur->connections) {
    if (w.srcChild == sw::kSymbolBoundary || w.dstChild == sw::kSymbolBoundary) continue;
    int from = pinOfSlot(w.srcChild, w.srcSlot, /*isInput=*/false);
    int to = pinOfSlot(w.dstChild, w.dstSlot, /*isInput=*/true);
    if (from < 0 || to < 0) continue;  // unresolvable wire: cook tolerance, canvas tolerance
    ed::Link(linkIdOf(from, to), from, to);
  }

  // Create links by dragging pin -> pin (one input + one output, different nodes).
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b) && a && b) {
      int pa = (int)a.Get(), pb = (int)b.Get();
      bool ia = pinIsInput(pa), ib = pinIsInput(pb);
      if (pa != pb && ia != ib && pinChildId(pa) != pinChildId(pb)) {
        // Reject if the two pins' dataType differ (Float↔Float, buffer↔same-type).
        auto portTypeOf = [](int pin) -> std::string {
          const sw::PortSpec* p = portOfPin(pin);
          return p ? p->dataType : "";
        };
        if (portTypeOf(pa) != portTypeOf(pb)) {
          ed::RejectNewItem();
        } else if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          sw::SymbolConnection nw;
          nw.srcChild = pinChildId(from);
          nw.srcSlot = portOfPin(from)->id;
          nw.dstChild = pinChildId(to);
          nw.dstSlot = portOfPin(to)->id;
          const sw::SymbolConnection* old =
              sw::connectionToInput(*cur, nw.dstChild, nw.dstSlot);
          if (old && old->srcChild == nw.srcChild && old->srcSlot == nw.srcSlot) {
            // already wired to this exact source — nothing to do
          } else if (old) {
            // reconnect: remove the input's old wire, add the new one, as one undo unit
            auto macro = std::make_unique<sw::MacroCommand>("Reconnect");
            macro->add(std::make_unique<sw::DeleteWiresCommand>(
                sw::doc::g_lib, cur->id, std::vector<sw::SymbolConnection>{*old}));
            macro->add(std::make_unique<sw::AddWireCommand>(sw::doc::g_lib, cur->id, nw));
            sw::g_commands.push(std::move(macro));
            sw::doc::g_status = "reconnected";
          } else {
            sw::g_commands.push(std::make_unique<sw::AddWireCommand>(sw::doc::g_lib, cur->id, nw));
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
        sw::doc::g_relayout = true;  // canvas re-seeds node positions from the restored lib
      }
    }
  }

  // Delete links / nodes (select + Delete key, or Backspace routed above).
  if (ed::BeginDelete()) {
    std::vector<sw::SymbolConnection> delWires;
    std::vector<int> delNodes;
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid))
      if (ed::AcceptDeletedItem()) {
        sw::SymbolConnection w;
        if (wireOfLink(lid.Get(), w)) delWires.push_back(w);
      }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid))
      if (ed::AcceptDeletedItem()) delNodes.push_back((int)nid.Get());
    ed::EndDelete();

    // 入射於被刪 child 的連線交給 DeleteChildrenCommand 處理，從 delWires 去重，
    // 否則同一條線會被刪兩次（undo 也會重複還原）。
    auto incidentToDeletedNode = [&](const sw::SymbolConnection& w) {
      return std::find(delNodes.begin(), delNodes.end(), w.srcChild) != delNodes.end() ||
             std::find(delNodes.begin(), delNodes.end(), w.dstChild) != delNodes.end();
    };
    std::vector<sw::SymbolConnection> standaloneWires;
    for (const sw::SymbolConnection& w : delWires)
      if (!incidentToDeletedNode(w)) standaloneWires.push_back(w);

    if (!delNodes.empty() || !standaloneWires.empty()) {
      auto macro = std::make_unique<sw::MacroCommand>("Delete");
      if (!standaloneWires.empty())
        macro->add(std::make_unique<sw::DeleteWiresCommand>(sw::doc::g_lib, cur->id,
                                                            standaloneWires));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteChildrenCommand>(sw::doc::g_lib, cur->id, delNodes));
      sw::g_commands.push(std::move(macro));
      sw::doc::g_status = "deleted";
    }
  } else {
    ed::EndDelete();
  }

  if (sw::doc::g_relayout) {  // initial / after add / after load
    for (const sw::SymbolChild& child : cur->children)
      ed::SetNodePosition(child.id, ImVec2(child.x, child.y));
    sw::doc::g_relayout = false;
  } else {
    bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging) {
      // 拖動中：記下還沒記過的 child 的起始座標，並即時把位置反映到 lib（畫面跟手）。
      for (sw::SymbolChild& child : cur->children) {
        ImVec2 p = ed::GetNodePosition(child.id);
        if (g_dragStart.find(child.id) == g_dragStart.end())
          g_dragStart[child.id] = ImVec2(child.x, child.y);
        child.x = p.x;
        child.y = p.y;
      }
    } else if (!g_dragStart.empty()) {
      // 放手：把真正有位移的 child 組成一個 MoveChildrenCommand。
      std::vector<sw::MoveChildrenCommand::Move> moves;
      for (auto& kv : g_dragStart) {
        ImVec2 now = ed::GetNodePosition(kv.first);
        if (now.x != kv.second.x || now.y != kv.second.y)
          moves.push_back({kv.first, kv.second.x, kv.second.y, now.x, now.y});
      }
      g_dragStart.clear();
      if (!moves.empty()) {
        // 命令的 doIt 會再設一次新座標（冪等），先把 lib 設回舊座標避免雙重記錄混亂。
        for (auto& m : moves)
          if (sw::SymbolChild* c = sw::childById(*cur, m.id)) { c->x = m.oldX; c->y = m.oldY; }
        sw::g_commands.push(std::make_unique<sw::MoveChildrenCommand>(sw::doc::g_lib, cur->id, moves));
      }
    } else {
      // 沒拖動：照常把 editor 位置同步回 lib（例如程式性移動）。
      for (sw::SymbolChild& child : cur->children) {
        ImVec2 p = ed::GetNodePosition(child.id);
        child.x = p.x;
        child.y = p.y;
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

}  // namespace sw::ui
