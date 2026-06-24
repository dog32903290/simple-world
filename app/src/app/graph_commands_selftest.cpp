// app/graph_commands_selftest — 命令層自測（--selftest-command，對 SymbolLibrary），split
// mechanically out of graph_commands.cpp (ARCHITECTURE rule 4). Drives the commands through
// their public header + a real CommandStack only.
#include "app/graph_commands.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/command.h"            // CommandStack / MacroCommand
#include "runtime/compound_save.h"  // libToJsonV2 (byte-identity after a refused add)
#include "runtime/graph.h"          // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"   // libFromGraph (selftest seed)

namespace sw {
namespace {

// Wire equality (4-tuple). File-local twin of the static helper in graph_commands.cpp —
// SymbolConnection has no operator== (TiXL wires are bare 4-tuples); keep the two in sync.
bool sameWire(const SymbolConnection& a, const SymbolConnection& b) {
  return a.srcChild == b.srcChild && a.srcSlot == b.srcSlot &&
         a.dstChild == b.dstChild && a.dstSlot == b.dstSlot;
}

}  // namespace

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

    // ResetOverride (Inspector "Reset to default" / jog-dial reset gesture): mirror of SetOverride.
    // Seed a known override, RESET it (doIt erases -> definition default shines through), then UNDO
    // and assert the EXACT override value is restored, then REDO erases again. This is the param-
    // reset undo round-trip the Inspector reset affordance leans on (harness-gap-immune).
    {
      const std::string rs = "Radius";
      SymbolChild* rcc = childById(*root, rid);
      rcc->overrides[rs] = 7.25f;  // a known, non-default override
      const float seeded = 7.25f;
      // refused() must be false (slot IS overridden) — TiXL greys the menu item when IsDefault.
      ResetOverrideCommand probe(lib, lib.rootId, rid, rs, /*hadOld=*/true, seeded);
      ok = ok && !probe.refused();
      stack.push(std::make_unique<ResetOverrideCommand>(lib, lib.rootId, rid, rs,
                                                        /*hadOld=*/true, seeded));
      ok = ok && (childById(*root, rid)->overrides.count(rs) == 0u);  // erased -> default
      stack.undo();
      rcc = childById(*root, rid);
      ok = ok && (rcc->overrides.count(rs) == 1u) && (rcc->overrides[rs] == seeded);  // restored
      stack.redo();
      ok = ok && (childById(*root, rid)->overrides.count(rs) == 0u);  // re-erased
      stack.undo();
      ok = ok && (childById(*root, rid)->overrides[rs] == seeded);  // back to seeded, clean
      // refused() on a never-overridden slot: nothing to reset -> caller skips push.
      ResetOverrideCommand noop(lib, lib.rootId, rid, "__never_overridden",
                                /*hadOld=*/false, 0.0f);
      ok = ok && noop.refused();
    }
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
