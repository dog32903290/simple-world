// ui/editor_ui — toolbar / node canvas (imgui draw); the Inspector lives in ui/inspector.cpp.
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
//
// Lib-native canvas (批次 3, 照 TiXL GraphCanvas): the canvas renders the CURRENT Symbol's
// children/connections straight off doc::g_lib — no flat Graph, no projection layer.
// Composition switch (N3) = same canvas, different symbol.
#include "ui/editor_ui.h"
#include "ui/combine_dialog.h"
#include "ui/node_draw.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec / pinId / pinNode (the int pin scheme rides on child ids)

namespace ed = ax::NodeEditor;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;
int g_pinnedNode = 0;       // view ⊥ graph (see editor_ui.h); set by ui/output_window.cpp
bool g_navPending = false;  // see editor_ui.h (set by canvas gestures + toolbar breadcrumbs)

namespace {
// Boundary pins live in their OWN high integer band, disjoint from BOTH child node ids and
// child pins — because imgui-node-editor hashes node ids AND pin ids into one ImGui ID pool
// per canvas, so the old encoding (boundary pin = combinedIndex+1, i.e. 1..99) collided pin 1
// with child node id 1 → imgui's "conflicting ID" tooltip stole window focus → the Backspace
// delete gate (IsWindowFocused) went dead and boundary nodes couldn't be deleted. The band
// base is huge so a child id / child pin would have to reach ~1M to clash (the def cap below
// keeps the boundary index tiny, so it never approaches the band edge). NOTE: boundary pins
// must stay POSITIVE — negative ids would collide with the negative boundary NODE id scheme
// (edIdForInputDef/edIdForOutputDef). Boundary pins are UI-only (per-frame); wires/save key off
// slot strings, link ids pack two pins, so moving this base touches nothing on disk.
constexpr int kBoundaryPinBase = 1 << 20;  // 1048576
inline int boundaryPinId(int combinedIndex) { return kBoundaryPinBase + combinedIndex; }
inline bool pinIsBoundary(int pin) { return pin >= kBoundaryPinBase; }

// pin id <-> (child id, port index). Boundary pins decode to the kSymbolBoundary sentinel +
// their combined index; child pins go through the sw::pinId scheme. See sw::pinId().
int pinChildId(int pin) { return pinIsBoundary(pin) ? sw::kSymbolBoundary : sw::pinNode(pin); }
int pinPortIndex(int pin) { return pinIsBoundary(pin) ? (pin - kBoundaryPinBase) : (pin - 1) % 100; }

// What a pin IS, child or boundary. Child pins resolve through the spec; BOUNDARY pins
// (pinChildId == kSymbolBoundary == 0; ids in the high band above, disjoint from child node
// ids and child pins) resolve through the CURRENT symbol's own defs: combined index =
// inputDefs then outputDefs. Inside the symbol an inputDef is a SOURCE (isInput=false) and an
// outputDef is a SINK — TiXL's Input/Output boundary items exactly.
struct PinInfo {
  bool valid = false;
  bool isInput = false;  // canvas perspective: is this pin a sink?
  std::string slotId;
  std::string dataType;
};

PinInfo pinInfoOf(int pin) {
  PinInfo r;
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return r;
  const int childId = pinChildId(pin);
  const int idx = pinPortIndex(pin);
  if (childId == sw::kSymbolBoundary) {
    const int nIn = (int)cur->inputDefs.size();
    if (idx >= 0 && idx < nIn) {
      r = {true, /*isInput=*/false, cur->inputDefs[idx].id, cur->inputDefs[idx].dataType};
    } else if (idx >= nIn && idx < nIn + (int)cur->outputDefs.size()) {
      const sw::SlotDef& d = cur->outputDefs[idx - nIn];
      r = {true, /*isInput=*/true, d.id, d.dataType};
    }
    return r;
  }
  const sw::SymbolChild* c = sw::childById(*cur, childId);
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s || idx < 0 || idx >= (int)s->ports.size()) return r;
  const sw::PortSpec& p = s->ports[idx];
  return {true, p.isInput, p.id, p.dataType};
}
bool pinIsInput(int pin) {
  PinInfo p = pinInfoOf(pin);
  return p.valid && p.isInput;
}

// (childId, slotId) -> absolute pin id; childId 0 = the boundary (slot among the current
// symbol's own defs). -1 when unresolvable.
int pinOfSlot(int childId, const std::string& slotId, bool isInput) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return -1;
  if (childId == sw::kSymbolBoundary) {
    const int nIn = (int)cur->inputDefs.size();
    if (!isInput) {  // a wire SOURCE at the boundary = one of the symbol's inputDefs
      for (int i = 0; i < nIn; ++i)
        if (cur->inputDefs[i].id == slotId) return boundaryPinId(i);
    } else {  // a wire SINK at the boundary = one of the symbol's outputDefs
      for (size_t j = 0; j < cur->outputDefs.size(); ++j)
        if (cur->outputDefs[j].id == slotId) return boundaryPinId(nIn + (int)j);
    }
    return -1;
  }
  const sw::SymbolChild* c = sw::childById(*cur, childId);
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
// Boundary pins decode to the kSymbolBoundary sentinel side naturally.
bool wireOfLink(uint64_t id, sw::SymbolConnection& out) {
  PinInfo sp = pinInfoOf(linkSrcPin(id));
  PinInfo dp = pinInfoOf(linkDstPin(id));
  if (!sp.valid || !dp.valid) return false;
  out.srcChild = pinChildId(linkSrcPin(id));
  out.srcSlot = sp.slotId;
  out.dstChild = pinChildId(linkDstPin(id));
  out.dstSlot = dp.slotId;
  return true;
}

// Boundary nodes carry NEGATIVE editor ids (child ids are >= 1; the boundary pin scheme
// uses childId 0, but ed needs one id PER def item — TiXL draws each input/output as its
// own movable canvas item). inputDef i -> -(i+1); outputDef j -> -(1001+j).
int edIdForInputDef(int i) { return -(i + 1); }
int edIdForOutputDef(int j) { return -(1001 + j); }
bool edIdIsBoundary(int id) { return id < 0; }
// Invert the boundary-id scheme: input defs occupy [-1000,-1], output defs <= -1001. Returns the
// def's slotId in the current symbol (empty if the index no longer resolves), and sets isInput.
std::string boundaryDefSlot(const sw::Symbol& cur, int edId, bool& isInput) {
  if (edId <= -1001) {  // output def j = -id - 1001
    isInput = false;
    int j = -edId - 1001;
    if (j >= 0 && j < (int)cur.outputDefs.size()) return cur.outputDefs[j].id;
  } else if (edId < 0) {  // input def i = -id - 1
    isInput = true;
    int i = -edId - 1;
    if (i >= 0 && i < (int)cur.inputDefs.size()) return cur.inputDefs[i].id;
  }
  return "";
}

// 拖動暫存：child id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;

}  // namespace

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

  // The symbol's OWN external ports as movable boundary items (TiXL Input/Output canvas
  // items): inputDefs are sources inside, outputDefs are sinks. node_draw renders them.
  for (size_t i = 0; i < cur->inputDefs.size(); ++i)
    sw::ui::drawBoundaryDef(cur->inputDefs[i], edIdForInputDef((int)i),
                            boundaryPinId((int)i), /*isSource=*/true);
  for (size_t j = 0; j < cur->outputDefs.size(); ++j)
    sw::ui::drawBoundaryDef(cur->outputDefs[j], edIdForOutputDef((int)j),
                            boundaryPinId((int)(cur->inputDefs.size() + j)),
                            /*isSource=*/false);

  // Wires — boundary-sentinel sides resolve to the boundary items' pins.
  for (const sw::SymbolConnection& w : cur->connections) {
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
      // NOTE: both boundary item kinds share pin-childId 0, so this same-node guard also
      // blocks a direct inputDef->outputDef passthrough wire (legal in TiXL, rare; named
      // limitation until someone needs it).
      if (pa != pb && ia != ib && pinChildId(pa) != pinChildId(pb)) {
        // Reject if the two pins' dataType differ (Float↔Float, buffer↔same-type).
        auto portTypeOf = [](int pin) -> std::string {
          PinInfo p = pinInfoOf(pin);
          return p.valid ? p.dataType : "";
        };
        if (portTypeOf(pa) != portTypeOf(pb)) {
          ed::RejectNewItem();
        } else if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          sw::SymbolConnection nw;
          nw.srcChild = pinChildId(from);
          nw.srcSlot = pinInfoOf(from).slotId;
          nw.dstChild = pinChildId(to);
          nw.dstSlot = pinInfoOf(to).slotId;
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
    std::vector<std::pair<std::string, bool>> delDefs;  // (slotId, isInput) boundary defs to remove (S13)
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid))
      if (ed::AcceptDeletedItem()) {
        sw::SymbolConnection w;
        if (wireOfLink(lid.Get(), w)) delWires.push_back(w);
      }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      // Boundary items = the symbol's own input/output defs. Deleting one = the S13 收屍 contract
      // edit (removes the def + every wire/override across the lib that referenced it). Decode which
      // def, ACCEPT so ed drops the canvas item; the macro below applies the lib surgery.
      int edId = (int)nid.Get();
      if (edIdIsBoundary(edId)) {
        bool isInput = false;
        std::string slot = boundaryDefSlot(*cur, edId, isInput);
        if (!slot.empty() && ed::AcceptDeletedItem()) delDefs.push_back({slot, isInput});
        else ed::RejectDeletedItem();  // unresolved index (mid-frame retype): keep it alive
        continue;
      }
      if (ed::AcceptDeletedItem()) delNodes.push_back(edId);
    }
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

    if (!delNodes.empty() || !standaloneWires.empty() || !delDefs.empty()) {
      auto macro = std::make_unique<sw::MacroCommand>("Delete");
      if (!standaloneWires.empty())
        macro->add(std::make_unique<sw::DeleteWiresCommand>(sw::doc::g_lib, cur->id,
                                                            standaloneWires));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteChildrenCommand>(sw::doc::g_lib, cur->id, delNodes));
      // Def removals LAST (mirror TiXL Modifications.cs:184-191: children deleted first so the def
      // scrub only touches connections still present — though our snapshot captures whatever it hits).
      for (const auto& [slot, isInput] : delDefs)
        macro->add(std::make_unique<sw::DeleteInputOrOutputDefCommand>(sw::doc::g_lib, cur->id, slot,
                                                                       isInput));
      sw::g_commands.push(std::move(macro));
      // Removing a def is a contract change — SAY so (柏為: silent edits read as broken). ASCII only.
      sw::doc::g_status = delDefs.empty() ? "deleted" : "removed boundary def (def edit, broadcasts)";
    }
  } else {
    ed::EndDelete();
  }

  // Context menu (批次 4): right-click a node -> "Combine into new symbol..." (the modal
  // itself draws after the editor scope, drawCombineDialog below).
  sw::ui::drawCanvasContextMenu();

  // Navigation gestures (TiXL MagGraph GraphStates.cs): double-click a COMPOUND child ->
  // enter its subgraph; double-click the background -> up one level. The doc refuses
  // atomic children, so plain double-clicks on operators stay inert. `cur` still points at
  // the symbol we LEFT for the rest of this frame — so the layout section below is skipped
  // this frame (g_relayout stays pending) and runs next frame against the new symbol.
  bool navThisFrame = false;
  {
    int dn = (int)ed::GetDoubleClickedNode().Get();
    if (dn > 0) {
      navThisFrame = sw::doc::pushComposition(dn);
    } else if (ed::IsBackgroundDoubleClicked()) {
      navThisFrame = sw::doc::popComposition();
    }
    if (navThisFrame) {
      g_navPending = true;
      // Pin/selection are bare child ids — across a composition switch they'd ALIAS a
      // same-id child of the new symbol (viewport silently shows the wrong node, refuter
      // N3 B2/S3). Clear immediately; ed selection cleared now too (capture below reads 0).
      g_pinnedNode = 0;
      g_selectedNode = 0;
      ed::ClearSelection();
    }
  }

  if (navThisFrame) {
    // skip layout/sync this frame (see above)
  } else if (sw::doc::g_relayout) {  // initial / after add / after load / composition switch
    for (const sw::SymbolChild& child : cur->children)
      ed::SetNodePosition(child.id, ImVec2(child.x, child.y));
    for (size_t i = 0; i < cur->inputDefs.size(); ++i)
      ed::SetNodePosition(edIdForInputDef((int)i),
                          ImVec2(cur->inputDefs[i].x, cur->inputDefs[i].y));
    for (size_t j = 0; j < cur->outputDefs.size(); ++j)
      ed::SetNodePosition(edIdForOutputDef((int)j),
                          ImVec2(cur->outputDefs[j].x, cur->outputDefs[j].y));
    sw::doc::g_relayout = false;
    if (g_navPending) {  // composition switched LAST frame; positions are seeded now
      ed::ClearSelection();           // TiXL clears selection on composition change
      ed::NavigateToContent(0.0f);    // = TiXL's saved-view fallback (frame the content)
      g_navPending = false;
    }
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

  // Boundary items: positions sync straight back to the defs (movable + persisted like
  // TiXL IInputUi.PosOnCanvas; no undo step for def moves — named asymmetry vs child moves).
  if (!navThisFrame) {
    for (size_t i = 0; i < cur->inputDefs.size(); ++i) {
      ImVec2 p = ed::GetNodePosition(edIdForInputDef((int)i));
      cur->inputDefs[i].x = p.x;
      cur->inputDefs[i].y = p.y;
    }
    for (size_t j = 0; j < cur->outputDefs.size(); ++j) {
      ImVec2 p = ed::GetNodePosition(edIdForOutputDef((int)j));
      cur->outputDefs[j].x = p.x;
      cur->outputDefs[j].y = p.y;
    }
  }

  // Capture selection (while the editor is current) for the Inspector.
  ed::NodeId sel[1];
  int nsel = ed::GetSelectedNodes(sel, 1);
  g_selectedNode = (nsel > 0) ? (int)sel[0].Get() : 0;

    ed::End();
  }
  ed::SetCurrentEditor(nullptr);

  sw::ui::drawCombineDialog();  // modal naming dialog (canvas host window scope)

  ImGui::End();  // ##canvas_host
}

}  // namespace sw::ui
