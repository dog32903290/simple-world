// app/rename_selftest — headless RED->GREEN proof of rename (契約 rename, 批次 6): renaming a
// compound DEFINITION flows to its spec title (Add 選單 + 所有實例顯示名 via refreshCompoundSpecs)
// and undoes; renaming an INSTANCE is isolated to that child + an empty name falls back to the def
// name (childReadableName) + undoes; a CJK instance name roundtrips byte-identically through savev2
// WITHOUT aborting the (sw-patch) parser; a tampered name field is dropped locally (S15); empty/same/
// missing renames are refused (no-op, never pushed). Zone: app (drives commands + runtime model +
// v2 save/load — app may include all of runtime).
//
// Legs (task contract):
//   1. def name — RenameSymbolCommand changes Symbol.name; refreshCompoundSpecs -> spec title updates;
//      every instance WITHOUT a custom name reads the new title; undo restores the old title.
//   2. instance name — RenameChildCommand changes ONLY that child; a sibling instance is untouched;
//      childReadableName(child, defName) == custom name; empty name -> falls back to def name; undo.
//   3. CJK roundtrip — name a child 「粒子發射器」, savev2 -> JSON -> load, the name is byte-identical
//      AND the parser does NOT abort on the non-ASCII bytes (the承重 risk this batch had to kill).
//   4. S15 tolerance — a child whose `name` field is a number (garbage) loads with the field dropped
//      (child survives, falls back to def name), file does NOT die.
//   5. refusal — empty def rename / same-name / missing symbol|child are refused() and must not mutate.
// injectBug emulates losing the writer's raw-UTF-8 name path (writes the name as a NON-string), so the
// CJK roundtrip reads back EMPTY -> the byte-identity assertion FAILS (teeth).
#include <cstdio>
#include <string>
#include <vector>

#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/graph.h"          // findSpec
#include "runtime/graph_bridge.h"   // refreshCompoundSpecs

namespace sw {
namespace {

// Returns the process FAIL code (1) so a leg's `return fail(...)` exits nonzero — NOT bool false,
// which would collapse to exit 0 and silently neuter the teeth.
int fail(const char* msg) {
  std::printf("[selftest] rename: FAIL — %s\n", msg);
  return 1;
}

// A lib with one atomic + one compound "Wrap" whose name we rename. The compound holds TWO children
// of the atomic so we can prove instance-name isolation between siblings.
SymbolLibrary makeLib() {
  SymbolLibrary lib;
  Symbol atom;
  atom.id = "Const";
  atom.name = "Const";
  atom.atomic = true;
  atom.inputDefs = {{"value", "value", "Float", 0.0f}};
  atom.outputDefs = {{"out", "out", "Float", 0.0f}};
  lib.symbols["Const"] = atom;

  Symbol wrap;
  wrap.id = "Wrap-1111";
  wrap.name = "Wrap";
  wrap.atomic = false;
  wrap.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild a;
  a.id = 1;
  a.symbolId = "Const";
  SymbolChild b;
  b.id = 2;
  b.symbolId = "Const";
  wrap.children = {a, b};
  wrap.nextChildId = 3;
  lib.symbols["Wrap-1111"] = wrap;
  lib.rootId = "Wrap-1111";
  return lib;
}

}  // namespace

int runRenameSelfTest(bool injectBug) {
  // ---- Leg 1: definition rename flows to the spec title + undoes ----
  {
    SymbolLibrary lib = makeLib();
    refreshCompoundSpecs(lib);
    const NodeSpec* spec0 = findSpec("Wrap-1111");
    if (!spec0 || spec0->title != "Wrap") return fail("leg1: initial spec title != 'Wrap'");

    RenameSymbolCommand cmd(lib, "Wrap-1111", "包裝");
    if (cmd.refused()) return fail("leg1: legit def rename was refused");
    cmd.doIt();
    if (lib.find("Wrap-1111")->name != "包裝") return fail("leg1: def name not changed");
    refreshCompoundSpecs(lib);  // the UI does this at frame boundary on libRevision bump
    const NodeSpec* spec1 = findSpec("Wrap-1111");
    if (!spec1 || spec1->title != "包裝") return fail("leg1: spec title did not follow def rename");
    // an instance with NO custom name reads the new def title (childReadableName fallback)
    const SymbolChild* inst = childById(*lib.find("Wrap-1111"), 1);
    if (childReadableName(*inst, spec1->title) != "包裝")
      return fail("leg1: instance display name did not follow def rename");

    cmd.undo();
    if (lib.find("Wrap-1111")->name != "Wrap") return fail("leg1: undo did not restore def name");
    refreshCompoundSpecs(lib);
    if (findSpec("Wrap-1111")->title != "Wrap") return fail("leg1: undo did not restore spec title");
  }

  // ---- Leg 2: instance rename is isolated + empty falls back to def name + undoes ----
  {
    SymbolLibrary lib = makeLib();
    Symbol& wrap = *lib.find("Wrap-1111");
    const std::string defName = wrap.name;  // "Wrap"

    RenameChildCommand cmd(lib, "Wrap-1111", 1, "左發射");
    if (cmd.refused()) return fail("leg2: legit instance rename was refused");
    cmd.doIt();
    if (childById(wrap, 1)->name != "左發射") return fail("leg2: instance name not changed");
    if (!childById(wrap, 2)->name.empty()) return fail("leg2: sibling instance was polluted");
    if (childReadableName(*childById(wrap, 1), defName) != "左發射")
      return fail("leg2: renamed child does not read its custom name");
    if (childReadableName(*childById(wrap, 2), defName) != defName)
      return fail("leg2: un-renamed sibling did not fall back to def name");

    // empty name = clear -> fall back to def name (TiXL ReadableName), and it is NOT refused
    RenameChildCommand clr(lib, "Wrap-1111", 1, "");
    if (clr.refused()) return fail("leg2: clearing instance name was wrongly refused");
    clr.doIt();
    if (!childById(wrap, 1)->name.empty()) return fail("leg2: clear did not empty the name");
    if (childReadableName(*childById(wrap, 1), defName) != defName)
      return fail("leg2: cleared child did not fall back to def name");
    clr.undo();
    if (childById(wrap, 1)->name != "左發射") return fail("leg2: undo of clear did not restore name");
    cmd.undo();
    if (!childById(wrap, 1)->name.empty()) return fail("leg2: undo of rename did not empty the name");
  }

  // ---- Leg 3: CJK instance-name roundtrip through savev2 (byte-identical, NO parser abort) ----
  {
    SymbolLibrary lib = makeLib();
    const std::string cjk = "粒子發射器";
    childById(*lib.find("Wrap-1111"), 1)->name = cjk;

    std::string json = libToJsonV2(lib);
    if (injectBug) {
      // Emulate a writer that lost the raw-UTF-8 name path: replace the quoted CJK value with a
      // bare number (no name string survives) -> the loader's is_string() guard drops it -> reads
      // back EMPTY -> the byte-identity assertion below FAILS (teeth).
      const std::string needle = "\"" + cjk + "\"";
      auto p = json.find(needle);
      if (p != std::string::npos) json.replace(p, needle.size(), "0");
    }

    SymbolLibrary back;
    std::vector<std::string> warns;
    if (!libFromJsonAny(json, back, &warns))  // <- a non-ASCII assert in the parser would ABORT here
      return fail("leg3: CJK file failed to load");
    const SymbolChild* c = childById(*back.find("Wrap-1111"), 1);
    if (!c) return fail("leg3: child lost on reload");
    if (c->name != cjk) return fail("leg3: CJK instance name did not roundtrip byte-identically");
  }

  // ---- Leg 4: S15 — a garbage (non-string) name field is dropped locally, child survives ----
  {
    SymbolLibrary lib = makeLib();
    childById(*lib.find("Wrap-1111"), 1)->name = "x";
    std::string json = libToJsonV2(lib);
    // Tamper: turn the name VALUE into a number. The child must still load (fall back to def name).
    auto p = json.find("\"name\": \"x\"");
    if (p != std::string::npos) json.replace(p, std::string("\"name\": \"x\"").size(), "\"name\": 42");
    SymbolLibrary back;
    std::vector<std::string> warns;
    if (!libFromJsonAny(json, back, &warns)) return fail("leg4: tampered file killed the whole load");
    const SymbolChild* c = childById(*back.find("Wrap-1111"), 1);
    if (!c) return fail("leg4: child dropped instead of tolerating bad name");
    if (!c->name.empty()) return fail("leg4: garbage name field was not dropped");
  }

  // ---- Leg 5: refusal — empty def rename / same name / missing target are no-ops ----
  {
    SymbolLibrary lib = makeLib();
    if (!RenameSymbolCommand(lib, "Wrap-1111", "").refused()) return fail("leg5: empty def name not refused");
    if (!RenameSymbolCommand(lib, "Wrap-1111", "Wrap").refused()) return fail("leg5: same def name not refused");
    if (!RenameSymbolCommand(lib, "Const", "x").refused()) return fail("leg5: atomic rename not refused");
    if (!RenameSymbolCommand(lib, "nope", "x").refused()) return fail("leg5: missing symbol not refused");
    if (!RenameChildCommand(lib, "Wrap-1111", 999, "x").refused()) return fail("leg5: missing child not refused");
    if (!RenameChildCommand(lib, "Wrap-1111", 1, "").refused()) {
      // child 1 starts with an empty name, so renaming it to "" IS a no-op -> must refuse
      return fail("leg5: no-op (empty->empty) child rename not refused");
    }
  }

  std::printf("[selftest] rename: %s\n", injectBug ? "FAIL not reached (bug should RED)" : "PASS");
  // injectBug must have FAILED in leg 3 already; reaching here under injectBug is itself a failure.
  return injectBug ? 1 : 0;
}

}  // namespace sw
