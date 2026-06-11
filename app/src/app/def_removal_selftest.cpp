// app/def_removal_selftest — headless RED->GREEN proof of S13 DeleteBoundaryDefs (批次 5): removing a
// compound's input/output definition收屍s every dangling wire + obsolete override across the lib, is
// undoable (照 TiXL RemoveInputsOrOutputsCommand.IsUndoable=true), survives a v2 roundtrip, and
// tolerates a tampered file that still references a removed def. Zone: app (drives the command layer +
// runtime surgery + v2 save/load — app may include all of runtime).
//
// Legs (task contract):
//   1. remove inputDef — def gone, override gone, parent wire gone, OTHER defs' wire ORDER preserved
//      (a multi-input case proves 保序), inner boundary wire gone; resident eval == a hand-built
//      lib without that def.
//   2. remove outputDef — parent's wire OUT of that output scrubbed, other outputs intact.
//   3. undo policy — the command restores the def + parent wire + override + inner wire byte-faithfully;
//      redo re-removes (我們照 TiXL: undoable, NOT history-clearing — combine's clear is its own fork).
//   4. v2 roundtrip — remove -> save -> load -> bit-stable; tolerance — tamper the file to reference a
//      removed def -> S15 local drop + warning, load does NOT die.
//   5. rejection — nonexistent def id / symbol -> lib bits (v2 json) unchanged.
// injectBug performs leg 1's removal a BUGGY way (skips the parent-wire scrub) -> the parent wire
// survives -> the "parent wire gone" assertion FAILS (teeth).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/command.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

Symbol atom(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s;
  s.id = id;
  s.name = id;
  s.atomic = true;
  s.inputDefs = std::move(ins);
  s.outputDefs = std::move(outs);
  return s;
}

Symbol compound(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs,
                std::vector<SymbolChild> children, std::vector<SymbolConnection> conns) {
  Symbol s;
  s.id = id;
  s.name = id;
  s.atomic = false;
  s.inputDefs = std::move(ins);
  s.outputDefs = std::move(outs);
  s.children = std::move(children);
  s.connections = std::move(conns);
  return s;
}

// Const/Multiply atoms (Multiply.a is a multi-input so we can build a保序 case).
SymbolLibrary baseLib() {
  SymbolLibrary lib;
  lib.symbols["Const"] =
      atom("Const", {{"value", "value", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
  lib.symbols["Multiply"] = atom("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                                 {{"out", "out", "Float", 0.0f}});
  return lib;
}

float pullOut(ResidentEvalGraph& g, const ResidentEvalCtx& ctx, const char* outId = "out") {
  auto it = g.outputs.find(outId);
  if (it == g.outputs.end()) return -999.0f;
  return pullResidentFloat(g, it->second.first, it->second.second, ctx);
}

float evalRoot(const SymbolLibrary& lib) {
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  ResidentEvalCtx ctx;
  return pullOut(g, ctx);
}

bool hasWire(const Symbol& s, int sc, const char* ss, int dc, const char* ds) {
  for (const SymbolConnection& w : s.connections)
    if (w.srcChild == sc && w.srcSlot == ss && w.dstChild == dc && w.dstSlot == ds) return true;
  return false;
}

bool hasInputDef(const Symbol& s, const char* id) {
  for (const SlotDef& d : s.inputDefs)
    if (d.id == id) return true;
  return false;
}
bool hasOutputDef(const Symbol& s, const char* id) {
  for (const SlotDef& d : s.outputDefs)
    if (d.id == id) return true;
  return false;
}

}  // namespace

int runDefRemovalSelfTest(bool injectBug) {
  bool ok = true;

  // === Shared fixture: a compound "Box" with TWO inputs (g1, g2) feeding an inner Multiply, and one
  // output; used twice in root (reuse). Root wires:
  //   Const(c=5) -> Box#1.g1     (parent wire into the slot we'll delete; instance #1)
  //   Const(c=5) -> Box#1.g2     (a SURVIVING parent wire into the OTHER slot)
  //   Box#1.out  -> Mul.a        [first into multi-input Mul.a]
  //   Box#2.out  -> Mul.a        [second into multi-input Mul.a — order must survive a g1 delete]
  //   Mul.out    -> root.out
  // Box internal: boundary g1 -> innerMul.a ; boundary g2 -> innerMul.b ; innerMul.out -> boundary out.
  // Box#1 overrides g1=7 (an instance override that must be scrubbed); Box#2 leaves g1 default.
  auto makeFixture = []() {
    SymbolLibrary lib = baseLib();
    SymbolChild innerMul{1, "Multiply", {}, 0, 0};
    lib.symbols["Box"] = compound(
        "Box", {{"g1", "g1", "Float", 2.0f}, {"g2", "g2", "Float", 3.0f}},
        {{"out", "out", "Float", 0.0f}}, {innerMul},
        {{kSymbolBoundary, "g1", 1, "a"}, {kSymbolBoundary, "g2", 1, "b"},
         {1, "out", kSymbolBoundary, "out"}});
    SymbolChild c1{1, "Const", {{"value", 5.0f}}, 0, 0};
    SymbolChild b1{2, "Box", {{"g1", 7.0f}}, 0, 0};  // override on the slot we delete
    SymbolChild b2{3, "Box", {}, 0, 0};              // on the default
    SymbolChild mul{4, "Multiply", {{"b", 1.0f}}, 0, 0};
    lib.symbols["Root"] = compound(
        "Root", {}, {{"out", "out", "Float", 0.0f}}, {c1, b1, b2, mul},
        {{1, "out", 2, "g1"},   // Const -> Box#1.g1   (parent wire into deleted slot)
         {1, "out", 2, "g2"},   // Const -> Box#1.g2   (survivor)
         {2, "out", 4, "a"},    // Box#1.out -> Mul.a   (multi-input first)
         {3, "out", 4, "a"},    // Box#2.out -> Mul.a   (multi-input second — 保序 probe)
         {4, "out", kSymbolBoundary, "out"}});
    lib.rootId = "Root";
    return lib;
  };

  // --- Leg 1: remove Box.inputDef "g1" ---
  {
    SymbolLibrary lib = makeFixture();
    const float before = evalRoot(lib);  // Box#1: (5*?)... see below; just snapshot

    RemovedSlotDef snap;
    if (injectBug) {
      // BUGGY removal: drop the def + inner boundary wire + override but SKIP the parent-wire scrub.
      Symbol& box = *lib.find("Box");
      for (size_t i = 0; i < box.inputDefs.size(); ++i)
        if (box.inputDefs[i].id == "g1") { box.inputDefs.erase(box.inputDefs.begin() + i); break; }
      auto& ic = box.connections;
      ic.erase(std::remove_if(ic.begin(), ic.end(),
                              [](const SymbolConnection& c) {
                                return sourceIsSymbolInput(c) && c.srcSlot == "g1";
                              }),
               ic.end());
      lib.find("Root")->children[1].overrides.erase("g1");  // scrub override only
      // (parent wire Const->Box#1.g1 deliberately left dangling)
    } else {
      removeInputDefFromLib(lib, "Box", "g1", &snap);
    }

    Symbol& box = *lib.find("Box");
    Symbol& root = *lib.find("Root");
    ok = ok && !hasInputDef(box, "g1");                         // def gone
    ok = ok && hasInputDef(box, "g2");                          // sibling def intact
    ok = ok && root.children[1].overrides.count("g1") == 0;     // instance override scrubbed
    ok = ok && !hasWire(box, kSymbolBoundary, "g1", 1, "a");    // inner boundary wire gone
    ok = ok && hasWire(box, kSymbolBoundary, "g2", 1, "b");     // sibling inner wire intact
    ok = ok && !hasWire(root, 1, "out", 2, "g1");               // parent wire INTO deleted slot gone
    ok = ok && hasWire(root, 1, "out", 2, "g2");                // parent wire into surviving slot kept

    // 保序: the two multi-input wires into Mul.a keep relative order (Box#1 before Box#2).
    int firstA = -1, secondA = -1, seen = 0;
    for (size_t i = 0; i < root.connections.size(); ++i) {
      const SymbolConnection& w = root.connections[i];
      if (w.dstChild == 4 && w.dstSlot == "a") {
        if (seen == 0) firstA = w.srcChild;
        else if (seen == 1) secondA = w.srcChild;
        ++seen;
      }
    }
    ok = ok && seen == 2 && firstA == 2 && secondA == 3;  // Box#1 (id2) still before Box#2 (id3)

    // 刪後求值 == a hand-built lib with Box defined WITHOUT g1 from the start (equivalence).
    {
      SymbolLibrary hand = baseLib();
      SymbolChild innerMul{1, "Multiply", {}, 0, 0};
      // Box without g1: innerMul.a falls to its own default (1); only g2 remains a boundary input.
      hand.symbols["Box"] = compound("Box", {{"g2", "g2", "Float", 3.0f}},
                                     {{"out", "out", "Float", 0.0f}}, {innerMul},
                                     {{kSymbolBoundary, "g2", 1, "b"}, {1, "out", kSymbolBoundary, "out"}});
      SymbolChild c1{1, "Const", {{"value", 5.0f}}, 0, 0};
      SymbolChild b1{2, "Box", {}, 0, 0};  // override g1 gone with the def
      SymbolChild b2{3, "Box", {}, 0, 0};
      SymbolChild mul{4, "Multiply", {{"b", 1.0f}}, 0, 0};
      hand.symbols["Root"] = compound("Root", {}, {{"out", "out", "Float", 0.0f}}, {c1, b1, b2, mul},
                                      {{1, "out", 2, "g2"}, {2, "out", 4, "a"}, {3, "out", 4, "a"},
                                       {4, "out", kSymbolBoundary, "out"}});
      hand.rootId = "Root";
      ok = ok && evalRoot(lib) == evalRoot(hand);  // verified non-vacuous: both = 3.0 (Box#2 1*3 -> Mul 3*1)
    }
    (void)before;
  }

  // === The rest run with the real surgery only (bug variant already proven by leg 1). ===

  // --- Leg 2: remove Box.outputDef "out" ---
  {
    SymbolLibrary lib = makeFixture();
    // Give Box a SECOND output so we can prove the survivor stays. innerMul.out -> out2 too.
    Symbol& box = *lib.find("Box");
    box.outputDefs.push_back({"out2", "out2", "Float", 0.0f});
    box.connections.push_back({1, "out", kSymbolBoundary, "out2"});
    bool dropped = removeOutputDefFromLib(lib, "Box", "out");
    Symbol& box2 = *lib.find("Box");
    Symbol& root = *lib.find("Root");
    ok = ok && dropped;
    ok = ok && !hasOutputDef(box2, "out");                            // def gone
    ok = ok && hasOutputDef(box2, "out2");                           // survivor intact
    ok = ok && !hasWire(box2, 1, "out", kSymbolBoundary, "out");      // inner SINK boundary wire gone
    ok = ok && hasWire(box2, 1, "out", kSymbolBoundary, "out2");      // survivor inner wire intact
    ok = ok && !hasWire(root, 2, "out", 4, "a");                      // parent wire OUT of Box#1.out gone
    ok = ok && !hasWire(root, 3, "out", 4, "a");                      // parent wire OUT of Box#2.out gone
    ok = ok && hasWire(root, 1, "out", 2, "g1");                      // unrelated parent wires untouched
  }

  // --- Leg 3: undo policy (照 TiXL: undoable) ---
  {
    SymbolLibrary lib = makeFixture();
    const std::string beforeJson = libToJsonV2(lib);
    CommandStack stack;
    stack.push(std::make_unique<DeleteInputOrOutputDefCommand>(lib, "Box", "g1", /*isInput=*/true));
    ok = ok && !hasInputDef(*lib.find("Box"), "g1");                  // removed by doIt
    ok = ok && !hasWire(*lib.find("Root"), 1, "out", 2, "g1");        // parent wire scrubbed
    stack.undo();
    ok = ok && libToJsonV2(lib) == beforeJson;                       // FULL restore (def+wires+override)
    stack.redo();
    ok = ok && !hasInputDef(*lib.find("Box"), "g1");                  // redo re-removes
    ok = ok && lib.find("Root")->children[1].overrides.count("g1") == 0;
    stack.undo();
    ok = ok && libToJsonV2(lib) == beforeJson;                       // undo again -> stable
  }

  // --- Leg 3b: MIXED selection (child + boundary def) in ONE macro, TiXL order = child first
  // (Modifications.cs:184-191), so the def scrub only ever sees connections still present. Undo
  // (macro reverse) must restore both cleanly with no double-restore (the two commands capture
  // disjoint wire sets). Here: delete child Mul(id4) AND inputDef g1 together. ---
  {
    SymbolLibrary lib = makeFixture();
    const std::string beforeJson = libToJsonV2(lib);
    CommandStack stack;
    auto macro = std::make_unique<MacroCommand>("Delete");
    macro->add(std::make_unique<DeleteChildrenCommand>(lib, "Root", std::vector<int>{4}));  // child first
    macro->add(std::make_unique<DeleteInputOrOutputDefCommand>(lib, "Box", "g1", /*isInput=*/true));
    stack.push(std::move(macro));
    Symbol& root = *lib.find("Root");
    ok = ok && childById(root, 4) == nullptr;                   // Mul child gone
    ok = ok && !hasInputDef(*lib.find("Box"), "g1");            // def gone
    ok = ok && !hasWire(root, 1, "out", 2, "g1");               // def's parent wire scrubbed
    ok = ok && !hasWire(root, 2, "out", 4, "a");               // Mul-incident wire gone (child delete)
    stack.undo();
    ok = ok && libToJsonV2(lib) == beforeJson;                 // both restored, byte-identical
  }

  // --- Leg 4: v2 roundtrip + tampered-file tolerance ---
  {
    SymbolLibrary lib = makeFixture();
    removeInputDefFromLib(lib, "Box", "g1");
    std::string json = libToJsonV2(lib);
    SymbolLibrary reloaded;
    std::vector<std::string> warnings;
    bool loaded = libFromJsonAny(json, reloaded, &warnings);
    ok = ok && loaded && warnings.empty();
    ok = ok && libToJsonV2(reloaded) == json;  // bit-stable roundtrip
    ok = ok && !hasInputDef(*reloaded.find("Box"), "g1");

    // Tamper: re-introduce a wire AND an override referencing the deleted def g1, plus a stray inner
    // boundary wire from g1 — a hand-edited/older file. Loader must locally drop the dangling wires
    // AND the obsolete override (+ warn each) and NOT die. The override is NOT harmless dead-weight:
    // effectiveInput (compound_graph.cpp) returns an override BEFORE it checks the def exists, so a
    // surviving g1=7 would借屍還魂 the moment a g1 def reappeared. Defence in depth: in-memory removal
    // is transactional via removeDefFromLib; the file path is guarded by the loader scrub here.
    SymbolLibrary tampered = makeFixture();
    removeInputDefFromLib(tampered, "Box", "g1");
    // Surgically re-add the dangling references the file might still carry:
    tampered.find("Root")->connections.push_back({1, "out", 2, "g1"});  // parent wire into gone slot
    tampered.find("Box")->connections.push_back({kSymbolBoundary, "g1", 1, "a"});  // inner boundary wire
    tampered.find("Root")->children[1].overrides["g1"] = 7.0f;          // obsolete override
    std::string tjson = libToJsonV2(tampered);
    SymbolLibrary tloaded;
    std::vector<std::string> twarn;
    bool tok = libFromJsonAny(tjson, tloaded, &twarn);
    ok = ok && tok;                       // load does NOT die
    ok = ok && twarn.size() >= 3;         // two dangling wires + one obsolete override, each warned
    ok = ok && !hasWire(*tloaded.find("Root"), 1, "out", 2, "g1");      // parent dangling wire scrubbed
    ok = ok && !hasWire(*tloaded.find("Box"), kSymbolBoundary, "g1", 1, "a");  // inner dangling scrubbed
  }

  // --- Leg 4b: ZOMBIE override self-heal (refuter probe6 promoted). A stale/hand-edited file carries
  // an override on a def that no longer exists. The loader must (a) warn, (b) leave NO such override in
  // the in-memory model, (c) re-save a file with the zombie GONE — S15 "next save self-heals", which
  // before this fix was a lie for overrides (only wires self-healed). ---
  {
    SymbolLibrary tampered = makeFixture();
    removeInputDefFromLib(tampered, "Box", "g1");            // g1 def is now gone from Box
    tampered.find("Root")->children[1].overrides["g1"] = 99.0f;  // but a zombie override lingers
    std::string zjson = libToJsonV2(tampered);
    ok = ok && zjson.find("\"g1\"") != std::string::npos;   // sanity: the zombie really is in the file
    SymbolLibrary zloaded;
    std::vector<std::string> zwarn;
    bool zok = libFromJsonAny(zjson, zloaded, &zwarn);
    // injectBug models a loader that SKIPS the override scrub: re-inject the zombie post-load so the
    // "override absent" assertion below fails (teeth — uses the selftest's own injectBug, like Leg 1).
    if (injectBug && zloaded.find("Root")) zloaded.find("Root")->children[1].overrides["g1"] = 99.0f;
    bool warned = false;
    for (const std::string& w : zwarn)
      if (w.find("obsolete override") != std::string::npos && w.find("g1") != std::string::npos) warned = true;
    ok = ok && zok && warned;                                                 // (a) load ok + warned
    ok = ok && zloaded.find("Root") && zloaded.find("Root")->children[1].overrides.count("g1") == 0;  // (b) gone in-memory
    ok = ok && libToJsonV2(zloaded).find("\"g1\"") == std::string::npos;      // (c) gone on re-save (self-heal)
  }

  // --- Leg 4c: 借屍還魂 (resurrection) — refuter probe6's poison scenario as a golden, on a PURPOSE-
  // BUILT minimal lib so the probed instance is unambiguously the one root reads (the shared fixture's
  // Mul.a is multi-input, last-wire-wins -> root would read the WRONG Box and the tooth couldn't bite).
  // Box2(g1,g2) = g1*g2 ; root: Const(5) -> Box2#1.g2 ; Box2#1.out -> root.out. Remove g1's def, leave a
  // zombie g1=99 override on Box2#1, save+load, then re-add a g1 def (default 2) of the SAME id as a
  // future add-input feature would. root.out must be the def default 2*5=10, NEVER the dead 99*5=495. ---
  {
    SymbolLibrary base = baseLib();
    SymbolChild innerMul{1, "Multiply", {}, 0, 0};
    base.symbols["Box2"] = compound(
        "Box2", {{"g1", "g1", "Float", 2.0f}, {"g2", "g2", "Float", 3.0f}},
        {{"out", "out", "Float", 0.0f}}, {innerMul},
        {{kSymbolBoundary, "g1", 1, "a"}, {kSymbolBoundary, "g2", 1, "b"},
         {1, "out", kSymbolBoundary, "out"}});
    SymbolChild c1{1, "Const", {{"value", 5.0f}}, 0, 0};
    SymbolChild bx{2, "Box2", {}, 0, 0};
    base.symbols["Root"] = compound(
        "Root", {}, {{"out", "out", "Float", 0.0f}}, {c1, bx},
        {{1, "out", 2, "g2"}, {2, "out", kSymbolBoundary, "out"}});  // Const(5) -> Box2#1.g2 ; Box2#1.out -> root.out
    base.rootId = "Root";

    removeInputDefFromLib(base, "Box2", "g1");                  // g1 def gone (g2 still wired from Const)
    base.find("Root")->children[1].overrides["g1"] = 99.0f;     // zombie that must NOT survive
    std::string zjson = libToJsonV2(base);
    SymbolLibrary lib2;
    bool z2 = libFromJsonAny(zjson, lib2, nullptr);
    if (injectBug && lib2.find("Root")) lib2.find("Root")->children[1].overrides["g1"] = 99.0f;  // teeth
    // Re-introduce a g1 inputDef of the SAME id (default 2) + rewire it inward, so g1 is live again.
    if (Symbol* box = lib2.find("Box2")) {
      box->inputDefs.insert(box->inputDefs.begin(), {"g1", "g1", "Float", 2.0f});
      box->connections.push_back({kSymbolBoundary, "g1", 1, "a"});  // g1 boundary -> innerMul.a
    }
    // Box2#1.g1 effective value: must be the def default (2), NOT the resurrected 99.
    if (Symbol* root = lib2.find("Root"))
      ok = ok && z2 && effectiveInput(lib2, root->children[1], "g1", -1.0f) == 2.0f;
    // Through resident eval: root.out = Box2#1.out = g1(default 2) * g2(Const 5) = 10. Poison -> 99*5=495.
    ok = ok && evalRoot(lib2) == 10.0f;
  }

  // --- Leg 5: rejection (nonexistent def id / symbol -> lib bits unchanged) ---
  {
    SymbolLibrary lib = makeFixture();
    const std::string beforeJson = libToJsonV2(lib);
    ok = ok && !removeInputDefFromLib(lib, "Box", "nope");      // no such input def
    ok = ok && !removeOutputDefFromLib(lib, "Box", "nope");     // no such output def
    ok = ok && !removeInputDefFromLib(lib, "Ghost", "g1");      // no such symbol
    ok = ok && libToJsonV2(lib) == beforeJson;                  // byte-identical -> untouched
  }

  // No inversion: the buggy leg-1 removal (parent wire left dangling) makes the "parent wire gone"
  // assertion false -> ok=false -> FAIL/exit 1 (repo convention: the -bug variant genuinely fails).
  printf("[selftest-defremoval]%s -> %s\n", injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
