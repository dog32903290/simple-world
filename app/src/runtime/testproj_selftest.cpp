// runtime/testproj_selftest — guards the CHECKED-IN compound test project
// (testdata/compound_smoke.swproj, the N4 eye/hand drill asset) against rot: it must load
// with ZERO repair warnings, and the reuse contract must hold through the REAL file —
// two instances of one definition, instance override isolated, default flowing through
// the boundary wire. Runtime leaf (compound_save + resident_eval_graph; no Metal).
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/resident_eval_graph.h"

#ifndef SW_TESTDATA_DIR
#define SW_TESTDATA_DIR "testdata"
#endif

namespace sw {

int runTestProjSelfTest(bool injectBug) {
  const std::string path = std::string(SW_TESTDATA_DIR) + "/compound_smoke.swproj";
  SymbolLibrary lib;
  std::vector<std::string> warnings;
  bool ok = loadLibFromFile(path, lib, &warnings);
  // A checked-in asset that needs S15 repairs is a rotten asset — zero warnings allowed.
  ok = ok && warnings.empty();
  for (const std::string& w : warnings) std::fprintf(stderr, "[testproj] %s\n", w.c_str());

  const Symbol* root = lib.find("Root");
  const Symbol* em = lib.find("EmitterComp");
  ok = ok && root && root->children.size() == 6 && em && !em->atomic;

  if (injectBug && em && !em->inputDefs.empty())  // teeth: pollute the DEFINITION's input
    // default — instance 1 (no override) reads it through the boundary wire, so the 3.0
    // assertion below must FAIL. (Polluting the INNER child's override would be shadowed
    // by the boundary wire — wire beats stored constant, the slot semantics this asset
    // exists to pin.)
    lib.find("EmitterComp")->inputDefs[0].def = 99.0f;

  // Inline and probe the two Emitter instances' inner RadialPoints (paths "1/1", "2/1"):
  // instance 1 reads the inputDef default through the boundary wire, instance 2 its
  // override — reuse isolation proven through the file, not a hand-built lib. The asset's
  // def is 3.0 ON PURPOSE: it differs from the registry RadialPoints default (2.0), so a
  // silently-dead boundary binding cannot pass as "default flowed through" (refuter N4).
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  const ResidentNode* r1 = g.node("1/1");
  const ResidentNode* r2 = g.node("2/1");
  ok = ok && r1 && r2;
  if (r1 && r2) {
    ResidentEvalCtx ctx;
    float v1 = resolveResidentFloatInputs(g, *r1, ctx)["Radius"];
    float v2 = resolveResidentFloatInputs(g, *r2, ctx)["Radius"];
    ok = ok && v1 == 3.0f && v2 == 4.0f;
    // Viewing a compound instance = viewing its output's producer (the shell's cook-target
    // resolution; selecting an Emitter must show its inner RadialPoints, never black).
    ok = ok && viewProducerPath(lib, "", 2) == "2/1" && viewProducerPath(lib, "", 4) == "4";

    // S14 mirror (refuter N4 #1): a MUTUAL-recursive chain that ends at an atomic must
    // return "" (the resident builder skipped that subtree — a non-empty path here would
    // bypass the shell's fallback and cook black). Build A⊃B⊃A-with-atomic-out in-code.
    {
      SymbolLibrary rec = lib;  // reuse the loaded atomics
      Symbol A, B;
      A.id = "RecA";
      A.children.push_back({1, "RecB"});
      A.children.push_back({2, "RadialPoints"});
      A.outputDefs.push_back({"out", "out", "Points"});
      A.connections.push_back({2, "points", kSymbolBoundary, "out"});
      B.id = "RecB";
      B.children.push_back({1, "RecA"});
      B.outputDefs.push_back({"out", "out", "Points"});
      B.connections.push_back({1, "out", kSymbolBoundary, "out"});
      rec.symbols["RecA"] = A;
      rec.symbols["RecB"] = B;
      rec.find("Root")->children.push_back({9, "RecA"});
      // Viewing the RecB instance from INSIDE RecA: the chain Root>RecA already contains
      // RecA; following B.out -> A would revisit it — must refuse, never invent "9/1/1/2".
      ok = ok && viewProducerPath(rec, "9/", 1) == "";
      // Viewing RecA from root: out wired straight to its atomic child -> a REAL path.
      ok = ok && viewProducerPath(rec, "", 9) == "9/2";
    }
    std::printf("[selftest-testproj] load(warn=%zu) children=%zu radius(1/1)=%.1f (2/1)=%.1f%s -> %s\n",
                warnings.size(), root ? root->children.size() : 0, v1, v2,
                injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  } else {
    std::printf("[selftest-testproj] resident paths missing -> FAIL\n");
  }
  return ok ? 0 : 1;
}

}  // namespace sw
