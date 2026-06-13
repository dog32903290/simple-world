// app/graph_commands — lib 編輯命令實作（對 SymbolLibrary，批次 3 N2）。命令層自測
// （--selftest-command）lives in graph_commands_selftest.cpp (mechanical split, rule 4).
#include "app/graph_commands.h"

#include <algorithm>
#include <set>

#include "runtime/graph.h"         // findSpec (AddChild imports the atomic Symbol on first use)
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec (same import path)

namespace sw {
namespace {

Symbol* sym(SymbolLibrary& lib, const std::string& id) { return lib.find(id); }

bool sameWire(const SymbolConnection& a, const SymbolConnection& b) {
  return a.srcChild == b.srcChild && a.srcSlot == b.srcSlot &&
         a.dstChild == b.dstChild && a.dstSlot == b.dstSlot;
}

// Restore snapshot wires at their ORIGINAL indices (ascending = each insert position is
// already valid) — the S字段 multi-input order contract: undo leaves the array order
// exactly as before the delete.
void restoreWires(Symbol& s, const std::vector<std::pair<size_t, SymbolConnection>>& removed) {
  for (const auto& [idx, w] : removed) {
    size_t at = idx < s.connections.size() ? idx : s.connections.size();
    s.connections.insert(s.connections.begin() + at, w);
  }
}

}  // namespace

// --- AddChildCommand ---
void AddChildCommand::doIt() {
  // Defensive cycle gate: adding a COMPOUND into one of its own ancestors (or itself) would
  // self-nest, and the resident builder then SILENTLY skips that subtree (S14) = a hole on
  // canvas with no word. The normal path (toolbar) blocks BEFORE push so no no-op lands on the
  // undo stack; this early-out only covers a programmatic/stale push — lib stays byte-untouched,
  // undo is a safe no-op (nothing was added). Atomic adds never trip this (no children).
  did_ = false;
  if (addChildWouldCycle(lib_, symbolId_, child_.symbolId)) return;
  did_ = true;
  // 照 TiXL：child 永遠引用一個存在的 Symbol。第一次用到某 atomic op 型別時，從 registry
  // 匯入它的 atomic Symbol（buildEvalGraph/effectiveInput 都靠它解析 defaults）。undo 不收
  // 屍——atomic Symbol 是 registry 形狀的快取，v2 存檔只序列化 compounds，留著無害。
  if (!lib_.symbols.count(child_.symbolId))
    if (const NodeSpec* spec = findSpec(child_.symbolId))
      lib_.symbols[child_.symbolId] = atomicSymbolFromSpec(*spec);
  if (Symbol* s = sym(lib_, symbolId_)) {
    s->children.push_back(child_);
    // Monotonic floor: this id is burned forever (undo does NOT lower it — a freed id must
    // never be reused or the new child inherits dead per-path runtime state).
    if (child_.id + 1 > s->nextChildId) s->nextChildId = child_.id + 1;
  }
}
void AddChildCommand::undo() {
  if (!did_) return;  // doIt refused (cycle gate): there is nothing to remove
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto& cs = s->children;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [this](const SymbolChild& c) { return c.id == child_.id; }),
           cs.end());
}

// --- AddWireCommand ---
void AddWireCommand::doIt() {
  if (Symbol* s = sym(lib_, symbolId_)) s->connections.push_back(wire_);
}
void AddWireCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto& ws = s->connections;
  for (auto it = ws.begin(); it != ws.end(); ++it)
    if (sameWire(*it, wire_)) { ws.erase(it); return; }
}

// --- DeleteWiresCommand ---
void DeleteWiresCommand::doIt() {
  removed_.clear();
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto hit = [this](const SymbolConnection& w) {
    for (const SymbolConnection& t : wires_)
      if (sameWire(w, t)) return true;
    return false;
  };
  for (size_t i = 0; i < s->connections.size(); ++i)
    if (hit(s->connections[i])) removed_.push_back({i, s->connections[i]});
  auto& ws = s->connections;
  ws.erase(std::remove_if(ws.begin(), ws.end(), hit), ws.end());
}
void DeleteWiresCommand::undo() {
  if (Symbol* s = sym(lib_, symbolId_)) restoreWires(*s, removed_);
}

// --- DeleteChildrenCommand ---
void DeleteChildrenCommand::doIt() {
  removedChildren_.clear();
  removedWires_.clear();
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto inSet = [this](int childId) {
    return std::find(ids_.begin(), ids_.end(), childId) != ids_.end();
  };
  // 快照入射連線後刪（boundary sentinel 0 永遠不在 ids_ 裡：real child ids >= 1）。
  auto incident = [&](const SymbolConnection& w) { return inSet(w.srcChild) || inSet(w.dstChild); };
  for (size_t i = 0; i < s->connections.size(); ++i)
    if (incident(s->connections[i])) removedWires_.push_back({i, s->connections[i]});
  auto& ws = s->connections;
  ws.erase(std::remove_if(ws.begin(), ws.end(), incident), ws.end());
  // 快照 children（原 index）後刪——undo 要按原位還原，否則序列化順序變了、
  // byte-identity dirty 永遠擦不掉（refuter N2 #3），terminal 揀選也可能換人。
  auto& cs = s->children;
  for (size_t i = 0; i < cs.size(); ++i)
    if (inSet(cs[i].id)) removedChildren_.push_back({i, cs[i]});
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](const SymbolChild& c) { return inSet(c.id); }),
           cs.end());
}
void DeleteChildrenCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  for (const auto& [idx, c] : removedChildren_) {  // ascending = each insert position valid
    size_t at = idx < s->children.size() ? idx : s->children.size();
    s->children.insert(s->children.begin() + at, c);
  }
  restoreWires(*s, removedWires_);
}

// --- DeleteInputOrOutputDefCommand (S13 收屍) ---
void DeleteInputOrOutputDefCommand::doIt() {
  removed_ = RemovedSlotDef{};
  if (isInput_) removeInputDefFromLib(lib_, symbolId_, slotId_, &removed_);
  else          removeOutputDefFromLib(lib_, symbolId_, slotId_, &removed_);
}
void DeleteInputOrOutputDefCommand::undo() {
  restoreSlotDefToLib(lib_, symbolId_, removed_);
}

// --- CopyPasteChildrenCommand (copy/paste 契約 4) ---
void CopyPasteChildrenCommand::doIt() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  // 1) children first (TiXL .cs:203-261). Each new id was pre-allocated by planPaste from the
  //    monotonic floor — burn the floor as we add so it never resurrects (照 AddChild).
  for (const PastedChild& pc : plan_.children) {
    s->children.push_back(pc.child);
    if (pc.child.id + 1 > s->nextChildId) s->nextChildId = pc.child.id + 1;
  }
  // 2) wires after children (TiXL .cs:263-267). Plan order == source order (no-reverse FORK,
  //    see copy_paste.cpp planPaste) — append lays it down verbatim, multi-input order intact.
  for (const SymbolConnection& w : plan_.wires) s->connections.push_back(w);
  // 2b) 曲线: install the copied animation curves on the target Animator under the NEW childIds
  //    (= TiXL CopyAnimationsTo .cs:196-199). setCurves keyed by (newChildId, inputId).
  for (const auto& [newChildId, byInput] : plan_.curves)
    for (const auto& [inputId, arr] : byInput) s->animator.setCurves(newChildId, inputId, arr);
  // 3) [bypass seam] TiXL .cs:269-276 applies deferred bypass HERE, now that wires exist
  //    (SetBypassed no-ops on an unwired fresh instance). For each pasted child that wanted bypass,
  //    apply it ONLY if the pasted child is bypassable AND its MAIN output got a wire (= TiXL's
  //    SetBypassed guards re-checked now that the wires are in). An unwired/non-bypassable paste keeps
  //    isBypassed=false — exactly what TiXL's deferred SetBypassed yields. (S2 seam closure.)
  for (const PastedChild& pc : plan_.children) {
    if (!pc.wantBypass) continue;
    SymbolChild* c = childById(*s, pc.child.id);
    if (!c || !childIsBypassable(lib_, *c)) continue;
    const Symbol* def = lib_.find(c->symbolId);
    const std::string mainOut = (def && !def->outputDefs.empty()) ? def->outputDefs[0].id : "";
    bool wired = false;
    for (const SymbolConnection& w : s->connections)
      if (w.srcChild == c->id && w.srcSlot == mainOut) { wired = true; break; }
    if (wired) c->isBypassed = true;
  }
  // 4) annotations (R-AN #1): each clone carries a fresh id (planPaste minted it against the target +
  //    the other clones) — push them last. Undo removes them by exactly these ids.
  for (const Annotation& a : plan_.annotations) s->annotations.push_back(a);
}
void CopyPasteChildrenCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  // Pasted annotations first (mirror of the doIt tail): remove by the planned ids.
  if (!plan_.annotations.empty()) {
    std::set<std::string> annIds;
    for (const Annotation& a : plan_.annotations) annIds.insert(a.id);
    auto& as = s->annotations;
    as.erase(std::remove_if(as.begin(), as.end(),
                            [&](const Annotation& a) { return annIds.count(a.id) > 0; }),
             as.end());
  }
  // Reverse order: curves first, then wires, then children. Remove exactly the (newChildId,inputId)
  // animator entries we installed — removeChild drops the whole bucket per pasted child, so a paste
  // onto an unanimated target leaves NO殭屍 curve (the pasted ids are fresh, never pre-animated).
  for (const auto& [newChildId, byInput] : plan_.curves) s->animator.removeChild(newChildId);
  auto& ws = s->connections;
  for (const SymbolConnection& w : plan_.wires)
    for (auto it = ws.begin(); it != ws.end(); ++it)
      if (sameWire(*it, w)) { ws.erase(it); break; }
  std::set<int> newIds;
  for (const PastedChild& pc : plan_.children) newIds.insert(pc.child.id);
  auto& cs = s->children;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
                          [&](const SymbolChild& c) { return newIds.count(c.id) > 0; }),
           cs.end());
  // Monotonic floor NOT lowered (a freed id stays burned — per-path state never inherited).
}

// --- MoveChildrenCommand ---
void MoveChildrenCommand::doIt() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  for (const Move& m : moves_)
    if (SymbolChild* c = childById(*s, m.id)) { c->x = m.newX; c->y = m.newY; }
}
void MoveChildrenCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  for (const Move& m : moves_)
    if (SymbolChild* c = childById(*s, m.id)) { c->x = m.oldX; c->y = m.oldY; }
}

// --- SetOverrideCommand ---
void SetOverrideCommand::doIt() {
  Symbol* s = sym(lib_, symbolId_);
  if (SymbolChild* c = s ? childById(*s, childId_) : nullptr) c->overrides[slotId_] = new_;
}
void SetOverrideCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) return;
  if (hadOld_) c->overrides[slotId_] = old_;
  else c->overrides.erase(slotId_);
}

}  // namespace sw
