// app/copy_paste_selftest — headless RED->GREEN proof of copy/paste (契約 4 第三刀, 照 TiXL
// CopySymbolChildrenCommand). Zone: app (drives runtime copy_paste data semantics + the undoable
// command + v2 save for byte-identity). injectBug pollutes the external-wire-cut so a both-ends-
// external wire leaks into the clipboard -> the "external wires cut" assertion FAILS (teeth).
//
// Legs (task contract, spec 行 230):
//   1. new id + oldToNew remap, no id collision (paste into the SAME symbol -> new ids > existing).
//   2. only both-ends-internal wires survive; external wires cut.
//   3. multi-input保序: a paste of two children both feeding one multi-input keeps relative order.
//   4. per-child full state搬: overrides (= override + IsDefault) copied; output state /bypass FORK.
//   5. bypass deferred: ordering invariant children->wires->[bypass seam] (no bypass field yet, FORK).
//   6. cross-symbol paste via clipboard JSON (serialize -> parse -> paste into a DIFFERENT symbol).
//   7. undo -> libToJsonV2 byte-identity; redo re-applies; cycle gate drops a self-nesting paste.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/command.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/copy_paste.h"

namespace sw {
namespace {

Symbol atom(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
Symbol compound(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs,
                std::vector<SymbolChild> children, std::vector<SymbolConnection> conns) {
  Symbol s; s.id = id; s.name = id; s.atomic = false;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  s.children = std::move(children); s.connections = std::move(conns);
  return s;
}

bool hasWire(const Symbol& s, int sc, const char* ss, int dc, const char* ds) {
  for (const SymbolConnection& w : s.connections)
    if (w.srcChild == sc && w.srcSlot == ss && w.dstChild == dc && w.dstSlot == ds) return true;
  return false;
}
const SymbolChild* byId(const Symbol& s, int id) { return childById(s, id); }

// libToJsonV2 with the monotonic nextChildId floor lines stripped. The FORK (documented in the
// command + report): like AddChild, paste BURNS the floor and undo never lowers it (a freed paste
// id must not resurrect — it could inherit a deleted child's per-path GPU/audio state). So an
// add/paste + undo is NOT literally byte-identical on the floor field, unlike TiXL's GUID model.
// The SUBSTANTIVE content (children/wires/defs/overrides) IS byte-identical — that's what this
// proves, plus a separate explicit assertion that the floor stayed burned.
std::string jsonNoFloor(const SymbolLibrary& lib) {
  std::string s = libToJsonV2(lib), out;
  size_t i = 0;
  while (i < s.size()) {
    size_t nl = s.find('\n', i);
    if (nl == std::string::npos) nl = s.size(); else ++nl;
    std::string line = s.substr(i, nl - i);
    if (line.find("\"nextChildId\"") == std::string::npos) out += line;
    i = nl;
  }
  return out;
}

// Const/Multiply atoms. Multiply.a is a multi-input (two sources allowed) so 保序 is testable.
SymbolLibrary baseLib() {
  SymbolLibrary lib;
  lib.symbols["Const"] = atom("Const", {{"value", "value", "Float", 0.0f}},
                              {{"out", "out", "Float", 0.0f}});
  lib.symbols["Multiply"] = atom("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                                 {{"out", "out", "Float", 0.0f}});
  return lib;
}

// The BUGGY extract used by injectBug: same as the real one but it ALSO captures a wire with one
// external endpoint (the cut is skipped) — modelling a copy that drags an outside connection in.
ClipboardData buggyExtract(const Symbol& src, const std::vector<int>& ids) {
  ClipboardData clip = extractClipboard(src, ids);
  // Re-add any wire that has EXACTLY one endpoint in the selection (the kind the real cut drops).
  auto inSel = [&](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
  for (const SymbolConnection& w : src.connections)
    if (inSel(w.srcChild) != inSel(w.dstChild)) clip.wires.push_back(w);
  return clip;
}

}  // namespace

int runCopyPasteSelfTest(bool injectBug) {
  bool ok = true;
#define CHK(cond) do { bool _c=(cond); if(!_c) printf("  [FAIL] line %d: %s\n", __LINE__, #cond); ok = ok && _c; } while(0)

  // Fixture root: Const(1) -> Mul(2).a ; Mul(2).out -> Out(3).value-ish. We select {Const, Mul}:
  //   internal wire Const->Mul.a survives; external wire Mul->Out is CUT.
  //   Mul also has a second source into .a from Const2(5) to exercise multi-input 保序.
  // children: 1 Const(value=5), 2 Const(value=9), 3 Multiply(b=2), 4 Multiply(sink, external)
  // wires: 1->3.a , 2->3.a (multi-input, order 1 then 2) , 3.out->4.a (EXTERNAL: 4 not selected)
  auto makeRoot = []() {
    SymbolLibrary lib = baseLib();
    SymbolChild c1{1, "Const", {{"value", 5.0f}}, 10.0f, 20.0f};
    SymbolChild c2{2, "Const", {{"value", 9.0f}}, 10.0f, 80.0f};
    SymbolChild m3{3, "Multiply", {{"b", 2.0f}}, 200.0f, 50.0f};
    SymbolChild m4{4, "Multiply", {}, 400.0f, 50.0f};  // the EXTERNAL sink (not selected)
    lib.symbols["Root"] = compound(
        "Root", {}, {{"out", "out", "Float", 0.0f}}, {c1, c2, m3, m4},
        {{1, "out", 3, "a"},   // Const(1) -> Mul(3).a   [multi-input first]
         {2, "out", 3, "a"},   // Const(2) -> Mul(3).a   [multi-input second — 保序 probe]
         {3, "out", 4, "a"},   // Mul(3).out -> Mul(4).a [EXTERNAL: 4 not in selection -> cut]
         {4, "out", kSymbolBoundary, "out"}});
    lib.rootId = "Root";
    return lib;
  };

  const std::vector<int> selection{1, 2, 3};  // Const1, Const2, Mul3 — NOT the external sink Mul4

  // --- Leg 1+2+3+4: extract + plan + apply into the SAME symbol ---
  {
    SymbolLibrary lib = makeRoot();
    Symbol& root = *lib.find("Root");
    const std::string beforeContent = jsonNoFloor(lib);  // substantive content, floor-agnostic
    const int floorBefore = nextFreeChildId(root);

    ClipboardData clip = injectBug ? buggyExtract(root, selection) : extractClipboard(root, selection);

    // Leg 2 (extract side): only both-ends-internal wires captured. Real cut keeps EXACTLY the two
    // Const->Mul.a wires (both ends selected) and drops Mul3->Mul4 (external). Bug leaks Mul3->Mul4.
    int internalCount = 0, externalLeak = 0;
    for (const SymbolConnection& w : clip.wires) {
      bool si = std::find(selection.begin(), selection.end(), w.srcChild) != selection.end();
      bool di = std::find(selection.begin(), selection.end(), w.dstChild) != selection.end();
      if (si && di) ++internalCount; else ++externalLeak;
    }
    CHK(internalCount == 2); // the two Const->Mul.a survive
    CHK(externalLeak == 0); // <-- the TEETH assertion: bug leaks the external wire -> FAIL

    // Plan a paste anchored at (1000,1000) into Root (same symbol = duplicate).
    PastePlan plan = planPaste(lib, "Root", clip, 1000.0f, 1000.0f);

    // Leg 1: every original selected child got a NEW id, all >= the old floor, none colliding.
    CHK(plan.oldToNew.size() == 3);
    std::vector<int> newIds;
    for (const auto& kv : plan.oldToNew) {
      CHK(kv.second >= floorBefore); // allocated above the monotonic floor
      CHK(byId(root, kv.second) == nullptr); // no collision with an existing child
      newIds.push_back(kv.second);
    }
    std::sort(newIds.begin(), newIds.end());
    CHK(std::adjacent_find(newIds.begin(), newIds.end()) == newIds.end()); // ids unique

    // Apply through the undoable command.
    CommandStack stack;
    auto cmd = std::make_unique<CopyPasteChildrenCommand>(lib, "Root", plan);
    PastePlan planCopy = plan;  // keep a copy to inspect post-apply (cmd moved it)
    stack.push(std::move(cmd));

    CHK(root.children.size() == 7); // 4 original + 3 pasted

    // Leg 4: per-child full state搬. The pasted Const carrying override value=5 and the pasted
    // Mul carrying override b=2 must keep them; the pasted Mul's position == anchor + relpos.
    const int newC1 = planCopy.oldToNew[1];
    const int newM3 = planCopy.oldToNew[3];
    const SymbolChild* pc1 = byId(root, newC1);
    const SymbolChild* pm3 = byId(root, newM3);
    CHK(pc1 && pc1->overrides.count("value") && pc1->overrides.at("value") == 5.0f);
    CHK(pm3 && pm3->overrides.count("b") && pm3->overrides.at("b") == 2.0f);
    // position: Const1 was the upper-left (y=20 lowest), so its rel == (0,0) -> pastes at anchor.
    CHK(pc1 && pc1->x == 1000.0f && pc1->y == 1000.0f);

    // Leg 2 (paste side): the pasted wires are ONLY internal, remapped to new ids; NO wire touches
    // the external Mul4, and no boundary wire leaked in.
    const int newC2 = planCopy.oldToNew[2];
    CHK(hasWire(root, newC1, "out", newM3, "a")); // Const1' -> Mul3'.a
    CHK(hasWire(root, newC2, "out", newM3, "a")); // Const2' -> Mul3'.a
    int wiresIntoNewM3a = 0;
    for (const SymbolConnection& w : root.connections)
      if (w.dstChild == newM3 && w.dstSlot == "a") ++wiresIntoNewM3a;
    CHK(wiresIntoNewM3a == 2); // exactly the two internal, none external

    // Leg 3: multi-input保序. After paste, the two new wires into Mul3'.a must keep the source
    // order Const1' before Const2' (= original 1 before 2). planPaste preserves source order (no
    // reverse — our append model, see the FORK there); the command appends, so newC1 reads first.
    int firstSrc = -1, secondSrc = -1, seen = 0;
    for (const SymbolConnection& w : root.connections)
      if (w.dstChild == newM3 && w.dstSlot == "a") {
        if (seen == 0) firstSrc = w.srcChild; else if (seen == 1) secondSrc = w.srcChild; ++seen;
      }
    CHK(seen == 2 && firstSrc == newC1 && secondSrc == newC2); // 保序 preserved

    // Leg 7 (undo/redo): undo restores the substantive content byte-for-byte (children + wires gone)
    // AND keeps the floor burned (the documented FORK); redo re-applies the identical paste.
    stack.undo();
    CHK(jsonNoFloor(lib) == beforeContent);          // substantive content fully restored
    CHK(nextFreeChildId(root) >= floorBefore + 3);    // floor stays burned (ids not resurrected)
    stack.redo();
    CHK(root.children.size() == 7);                   // redo re-applies
    // redo reuses the SAME new ids (deterministic plan) — the pasted children are byte-identical to
    // the first apply, so a second undo lands on the same restored content again.
    stack.undo();
    CHK(jsonNoFloor(lib) == beforeContent);          // stable after undo again
  }

  // --- Leg 5: bypass-defer ORDERING invariant. Our model has no IsBypassed field (FORK), so we
  // assert the structural invariant the seam preserves: after doIt, every pasted wire's endpoints
  // already exist as children (wires applied AFTER children). If a future bypass step runs at the
  // seam, the wires it needs are present. (When IsBypassed lands, extend this leg to assert it.) ---
  {
    SymbolLibrary lib = makeRoot();
    ClipboardData clip = extractClipboard(*lib.find("Root"), selection);
    PastePlan plan = planPaste(lib, "Root", clip, 500.0f, 500.0f);
    CommandStack stack;
    stack.push(std::make_unique<CopyPasteChildrenCommand>(lib, "Root", plan));
    Symbol& root = *lib.find("Root");
    bool allEndpointsExist = true;
    for (const SymbolConnection& w : root.connections) {
      if (w.srcChild != kSymbolBoundary && byId(root, w.srcChild) == nullptr) allEndpointsExist = false;
      if (w.dstChild != kSymbolBoundary && byId(root, w.dstChild) == nullptr) allEndpointsExist = false;
    }
    CHK(allEndpointsExist); // children landed before wires -> bypass seam would see live wires
  }

  // --- Leg 6: cross-symbol paste via clipboard JSON ---
  {
    SymbolLibrary lib = makeRoot();
    // A second compound "Other" to paste INTO (different symbol from the copy source).
    lib.symbols["Other"] = compound("Other", {}, {{"out", "out", "Float", 0.0f}}, {}, {});
    Symbol& src = *lib.find("Root");

    ClipboardData clip = extractClipboard(src, selection);
    std::string json = clipboardToJson(clip);               // serialize (the OS-clipboard payload)
    ClipboardData parsed;
    CHK(clipboardFromJson(json, parsed)); // parse back
    CHK(parsed.children.size() == 3 && parsed.wires.size() == 2); // survived the roundtrip

    PastePlan plan = planPaste(lib, "Other", parsed, 0.0f, 0.0f);
    CommandStack stack;
    stack.push(std::make_unique<CopyPasteChildrenCommand>(lib, "Other", plan));
    Symbol& other = *lib.find("Other");
    CHK(other.children.size() == 3); // the three children landed in Other
    // The two internal wires landed too (remapped onto Other's new ids).
    int wireCount = 0;
    for (const SymbolConnection& w : other.connections) ++wireCount;
    CHK(wireCount == 2);
    // Non-our-clipboard text is a clean no-op (a foreign clipboard must not paste garbage).
    ClipboardData none;
    CHK(!clipboardFromJson("just some text", none));
    CHK(!clipboardFromJson("{\"hello\":1}", none)); // valid JSON, wrong shape
  }

  // --- Leg 7b: cycle gate drops a self-nesting paste. Build A ⊃ B; copy a B instance from A; try
  // to paste it INTO B (B-into-B = a cycle). planPaste must drop it -> empty plan -> command empty. ---
  {
    SymbolLibrary lib = baseLib();
    lib.symbols["B"] = compound("B", {}, {{"out", "out", "Float", 0.0f}},
                                {{1, "Const", {}, 0, 0}}, {});
    lib.symbols["A"] = compound("A", {}, {{"out", "out", "Float", 0.0f}},
                                {{1, "B", {}, 0, 0}}, {});  // A contains a B
    lib.rootId = "A";
    // Copy the B instance (child id 1) out of A.
    ClipboardData clip = extractClipboard(*lib.find("A"), {1});
    CHK(clip.children.size() == 1 && clip.children[0].symbolId == "B");
    // Paste into B itself -> B-into-B closes a cycle -> dropped.
    PastePlan plan = planPaste(lib, "B", clip, 0.0f, 0.0f);
    CHK(plan.children.empty()); // cycle gate dropped it
    CopyPasteChildrenCommand cmd(lib, "B", plan);
    CHK(cmd.empty()); // command is a no-op (caller skips push)
    // And a LEGAL paste into A (a SECOND B reuse) is accepted.
    PastePlan ok2 = planPaste(lib, "A", clip, 0.0f, 0.0f);
    CHK(ok2.children.size() == 1);
  }

  // No inversion: injectBug leaks the external wire (Leg 2) -> externalLeak != 0 -> ok=false -> FAIL.
  printf("[selftest-copypaste]%s -> %s\n", injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
#undef CHK
  return ok ? 0 : 1;
}

}  // namespace sw
