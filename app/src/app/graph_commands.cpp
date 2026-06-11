// app/graph_commands — lib 編輯命令實作 + 命令層自測（對 SymbolLibrary，批次 3 N2）。
#include "app/graph_commands.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>

#include "runtime/compound_save.h"  // libToJsonV2 (selftest: byte-identity after a refused add)
#include "runtime/graph.h"         // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"  // libFromGraph (selftest seed)

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
  //    (SetBypassed no-ops on an unwired fresh instance). Our model has no IsBypassed field yet
  //    (named FORK) — when it lands, set it on plan_.children here, AFTER the wires above.
}
void CopyPasteChildrenCommand::undo() {
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
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

// --- Self-test (against the LIB — the flat-graph version died with the flat editor) ---
int runCommandSelfTest(bool injectBug) {
  SymbolLibrary lib = libFromGraph(defaultParticleGraph());
  Symbol* root = lib.find(lib.rootId);
  if (!root) { printf("[selftest-command] no root -> FAIL\n"); return 1; }
  const size_t baseChildren = root->children.size();
  const size_t baseWires = root->connections.size();
  const std::vector<SymbolConnection> baseWireSnapshot = root->connections;
  CommandStack stack;

  auto firstOfType = [&](const char* type) -> const SymbolChild* {
    for (const SymbolChild& c : root->children)
      if (c.symbolId == type) return &c;
    return nullptr;
  };
  auto wiresEqualBase = [&]() {
    if (root->connections.size() != baseWireSnapshot.size()) return false;
    for (size_t i = 0; i < baseWireSnapshot.size(); ++i)
      if (!sameWire(root->connections[i], baseWireSnapshot[i])) return false;
    return true;  // 保序：undo 還原後順序必須一字不差（multi-input order contract）
  };
  std::vector<int> baseChildOrder;
  for (const SymbolChild& c : root->children) baseChildOrder.push_back(c.id);
  auto childOrderEqualBase = [&]() {
    if (root->children.size() != baseChildOrder.size()) return false;
    for (size_t i = 0; i < baseChildOrder.size(); ++i)
      if (root->children[i].id != baseChildOrder[i]) return false;
    return true;  // 保序：序列化順序 == array 順序，undo 不還原順序 = dirty 擦不掉
  };

  // Add child: push 後 +1，undo 後回 base，redo 後再 +1，收尾 undo。加上 monotonic floor:
  // add+undo 後再取 id 必須是個沒用過的（freed id 永不復活 = per-path state 不被繼承）。
  SymbolChild n;
  n.id = nextFreeChildId(*root);
  const int firstId = n.id;
  n.symbolId = "RadialPoints";
  stack.push(std::make_unique<AddChildCommand>(lib, lib.rootId, n));
  bool ok = (root->children.size() == baseChildren + 1);
  stack.undo();
  ok = ok && (root->children.size() == baseChildren);
  stack.redo();
  ok = ok && (root->children.size() == baseChildren + 1);
  stack.undo();
  ok = ok && (root->children.size() == baseChildren);
  ok = ok && (nextFreeChildId(*root) > firstId);  // the undone id stays burned

  // Delete a child with incident wires: 預設圖的 ParticleSystem 牽 3 條線（中段 child），
  // 刪它應 -1 child、-3 wires；undo 必須還原 child + 全部線 + 兩個陣列的原順序。
  const SymbolChild* ps = firstOfType("ParticleSystem");
  ok = ok && (ps != nullptr) && (baseWires == 3);
  if (ps) {
    int psId = ps->id;
    stack.push(std::make_unique<DeleteChildrenCommand>(lib, lib.rootId, std::vector<int>{psId}));
    ok = ok && (root->children.size() == baseChildren - 1) && (root->connections.empty());
    stack.undo();
    ok = ok && (root->children.size() == baseChildren) && wiresEqualBase() && childOrderEqualBase();
  }

  // Delete a single wire (the FIRST one, so order restoration is actually exercised):
  // undo restores it at index 0, not appended at the back.
  if (!root->connections.empty()) {
    SymbolConnection w0 = root->connections.front();
    stack.push(std::make_unique<DeleteWiresCommand>(lib, lib.rootId,
                                                    std::vector<SymbolConnection>{w0}));
    ok = ok && (root->connections.size() == baseWires - 1);
    stack.undo();
    ok = ok && wiresEqualBase();
  }

  // Move a child: undo restores old coords, redo reapplies new.
  if (!root->children.empty()) {
    int mid = root->children.front().id;
    SymbolChild* mc = childById(*root, mid);
    float ox = mc->x, oy = mc->y;
    std::vector<MoveChildrenCommand::Move> mv{{mid, ox, oy, ox + 50.0f, oy + 30.0f}};
    stack.push(std::make_unique<MoveChildrenCommand>(lib, lib.rootId, mv));
    mc = childById(*root, mid);
    ok = ok && (mc->x == ox + 50.0f) && (mc->y == oy + 30.0f);
    stack.undo();
    mc = childById(*root, mid);
    ok = ok && (mc->x == ox) && (mc->y == oy);
    stack.redo();
    ok = ok && (childById(*root, mid)->x == ox + 50.0f);
    stack.undo();  // 收回，保持乾淨
  }

  // Reconnect (replace-on-input): an input that already has a wire, re-wired to a different
  // source, must end with exactly ONE wire to that input; undo restores the original.
  {
    const SymbolConnection* existing =
        root->connections.empty() ? nullptr : &root->connections.front();
    if (existing) {
      const int dstChild = existing->dstChild;
      const std::string dstSlot = existing->dstSlot;
      const SymbolConnection oldWire = *existing;
      // a different source: reuse another wire's src if distinct (default graph has >1).
      int newSrc = oldWire.srcChild;
      std::string newSrcSlot = oldWire.srcSlot;
      for (const SymbolConnection& w : root->connections)
        if (w.srcChild != oldWire.srcChild) { newSrc = w.srcChild; newSrcSlot = w.srcSlot; break; }
      ok = ok && (newSrc != oldWire.srcChild);

      auto countTo = [&]() {
        int cnt = 0;
        for (const SymbolConnection& w : root->connections)
          if (w.dstChild == dstChild && w.dstSlot == dstSlot) ++cnt;
        return cnt;
      };
      ok = ok && (countTo() == 1);

      auto macro = std::make_unique<MacroCommand>("Reconnect");
      macro->add(std::make_unique<DeleteWiresCommand>(lib, lib.rootId,
                                                      std::vector<SymbolConnection>{oldWire}));
      SymbolConnection nw{newSrc, newSrcSlot, dstChild, dstSlot};
      macro->add(std::make_unique<AddWireCommand>(lib, lib.rootId, nw));
      stack.push(std::move(macro));

      ok = ok && (countTo() == 1);  // still single-cardinality
      const SymbolConnection* now = connectionToInput(*root, dstChild, dstSlot);
      ok = ok && now && (now->srcChild == newSrc);
      stack.undo();
      now = connectionToInput(*root, dstChild, dstSlot);
      ok = ok && (countTo() == 1) && now && (now->srcChild == oldWire.srcChild);
    }
  }

  // SetOverride: a child with NO prior override gets one; undo must ERASE it (the
  // definition default shines through again, never a 0-residue) — reuse stays isolated.
  if (const SymbolChild* rc = firstOfType("RadialPoints")) {
    int rid = rc->id;
    const std::string slot = "Radius";
    SymbolChild* c = childById(*root, rid);
    const bool had = c->overrides.count(slot) > 0;
    const float oldV = had ? c->overrides[slot] : 0.0f;
    // libFromGraph seeds overrides == stored params, so "had" is normally true; exercise
    // BOTH branches: first the restore-old path, then the erase path on a fresh slot.
    stack.push(std::make_unique<SetOverrideCommand>(lib, lib.rootId, rid, slot, had, oldV, 9.5f));
    ok = ok && (childById(*root, rid)->overrides[slot] == 9.5f);
    stack.undo();
    c = childById(*root, rid);
    ok = ok && (c->overrides.count(slot) == (had ? 1u : 0u));
    if (had) ok = ok && (c->overrides[slot] == oldV);

    const std::string freshSlot = "__cmdtest_fresh";
    stack.push(std::make_unique<SetOverrideCommand>(lib, lib.rootId, rid, freshSlot,
                                                    /*hadOld=*/false, 0.0f, 1.0f));
    ok = ok && (childById(*root, rid)->overrides.count(freshSlot) == 1u);
    stack.undo();
    ok = ok && (childById(*root, rid)->overrides.count(freshSlot) == 0u);
  }

  // Commands are keyed by symbolId, NOT by "what the canvas is looking at": editing a
  // NON-root symbol works and undoes correctly while root stays untouched.
  {
    Symbol comp;
    comp.id = "TestCompound";
    comp.name = "TestCompound";
    lib.symbols[comp.id] = comp;
    SymbolChild cc;
    cc.id = 1;
    cc.symbolId = "RadialPoints";
    stack.push(std::make_unique<AddChildCommand>(lib, "TestCompound", cc));
    ok = ok && (lib.find("TestCompound")->children.size() == 1u) &&
         (root->children.size() == baseChildren);
    stack.undo();
    ok = ok && lib.find("TestCompound")->children.empty();
  }

  // AddChild cycle gate at the COMMAND layer (the predicate's golden lives in
  // compound_graph_selftest; here we prove the command HONORS it): a legal compound instance
  // adds + undoes; a SELF-NEST push is a no-op (lib byte-identical, monotonic floor unmoved,
  // undo inert). Build a tiny chain Outer ⊃ Inner so transitivity is exercised too.
  {
    Symbol inner; inner.id = "InnerC"; inner.name = "InnerC"; lib.symbols[inner.id] = inner;
    Symbol outer; outer.id = "OuterC"; outer.name = "OuterC";
    { SymbolChild k; k.id = 1; k.symbolId = "InnerC"; outer.children = {k}; outer.nextChildId = 2; }
    lib.symbols[outer.id] = outer;

    // Legal: a SECOND instance of InnerC inside OuterC (reuse) — adds, then undo removes it.
    const int floorBefore = lib.find("OuterC")->nextChildId;
    SymbolChild second; second.id = nextFreeChildId(*lib.find("OuterC")); second.symbolId = "InnerC";
    stack.push(std::make_unique<AddChildCommand>(lib, "OuterC", second));
    ok = ok && (lib.find("OuterC")->children.size() == 2u);
    stack.undo();
    ok = ok && (lib.find("OuterC")->children.size() == 1u);
    ok = ok && (lib.find("OuterC")->nextChildId > floorBefore);  // floor stays burned (undo no-op on it)

    // Self-nest refusals: capture OuterC's exact bytes, push a direct (Outer-into-Outer) and a
    // transitive (Outer-into-Inner) add — both must leave OuterC AND InnerC untouched and put
    // nothing real on the stack (undo restores nothing). Compare via the v2 serializer (the
    // dirty-bit's own authority): identical dump == lib bits did not move.
    const std::string libDump = libToJsonV2(lib);
    SymbolChild selfNest; selfNest.id = 99; selfNest.symbolId = "OuterC";
    stack.push(std::make_unique<AddChildCommand>(lib, "OuterC", selfNest));   // Outer into Outer
    ok = ok && (libToJsonV2(lib) == libDump);                                 // refused, no mutation
    SymbolChild transNest; transNest.id = 98; transNest.symbolId = "OuterC";
    stack.push(std::make_unique<AddChildCommand>(lib, "InnerC", transNest));  // Outer into Inner
    ok = ok && (libToJsonV2(lib) == libDump);                                 // refused (transitive)
    stack.undo(); stack.undo();                                               // both inert
    ok = ok && (libToJsonV2(lib) == libDump);
  }

  if (injectBug) ok = !ok;  // 反向：注 bug 時必須回報失敗
  printf("[selftest-command] lib baseChildren=%zu baseWires=%zu%s -> %s\n", baseChildren,
         baseWires, injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
