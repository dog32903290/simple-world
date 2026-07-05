// composevec3fromlist_golden — --selftest-composevec3fromlist. Cook-emit golden for ComposeVec3FromList
// (numbers/floats/conversion): a FloatsToList producer feeds ComposeVec3FromList, whose Vector3 output is
// read off the 3 Result.* extOut slots AND cross-checked via evalResidentFloat (the !evaluate readback the
// production graph uses). Proves the FloatList → Vector3 host-emit rail + the cross-frame spring-damp state.
//
// Expected values are hand-derived from ComposeVec3FromList.cs / MathUtils.cs (cited in the leaf):
//   NO-DAMP (SpringDamping=0): Result.k = Remap(list[IndexFor_k], inMin,inMax, outMin,outMax) (cs:46-48).
//     list=[10,20,30], InputRange(0,100), OutputRange(0,1), indices 0/1/2 → (0.1, 0.2, 0.3). Closed-form,
//     impl-independent (a plain linear remap) — the P5-safe anchor.
//   OUT-OF-RANGE index → NaN (cs:37-38): IndexForX=5 on a 3-element list → Result.x = NaN.
//   SEED FRAME (damping>0, frame 0): PickValue seeds damped=remapped, vel=0, then SpringDamp(target==
//     current, vel=0) returns current unchanged (springForce=0, dampingForce=0) → frame-0 output ==
//     remapped. Independent of the spring math (target==current ⇒ zero force ⇒ identity) — a real property,
//     not an A==A. list=[1], InputRange(0,1), OutputRange(0,1), damping=1 → frame-0 Result.x = 1.0.
//   DAMP STEP (damping=1, input changes 0→1 between frames): hand-computed from MathUtils.SpringDamp with
//     k=100/1.001=99.9001, sqrt(k)=9.995004, dt=1/60, seeded at damped=0/vel=0 on frame 0 (input 0.0):
//       frame 1 (input 1.0): vel=1.6650017, out=0.02775003 ;
//       frame 2 (input 1.0): vel=2.7290762, out=0.07323463.
//     These are computed OUTSIDE the impl (a Python reference in the commit), so they anchor the spring
//     math to the TiXL formula, not to sw's own output (P5-safe).
//
// injectBug arms composeVec3InjectBug() (the cook SKIPS the SpringDamp advance, emitting the RAW remapped
// value) → the damp-step legs diverge from the hand-computed spring values → RED on the REAL cook path,
// NOT by flipping expected values. --selftest-composevec3fromlist-bug must exit NON-zero. The no-damp /
// seed / out-of-range legs are unchanged by the bug (they emit remapped anyway) — the teeth are the two
// DAMP-STEP legs, whose spring trajectory the bug destroys.
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentEvalCtx / buildEvalGraph / evalResidentFloat

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "runtime/compound_graph.h"     // Symbol/SymbolChild/SymbolLibrary/SlotDef
#include "runtime/graph.h"              // pinId (unused) / NodeSpec
#include "runtime/value_op_registry.h"  // valueOpSelfTests()

namespace sw {

// From value_op_composevec3fromlist.cpp (the cook pass + its test hooks).
void cookComposeVec3Nodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx);
void resetComposeVec3State();
bool& composeVec3InjectBugRef();

int runComposeVec3FromListSelfTest(bool injectBug);

namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 2e-4f; }

Symbol cv3Atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Build a lib: FloatsToList(vals) → ComposeVec3FromList.Input, with the given params as child overrides.
// Returns the resident graph + the ComposeVec3FromList child path ("30"). `vals.empty()` omits the producer
// (unwired input). The library/graph are held by the caller across frames (state persists in the op static).
struct CV3Graph { SymbolLibrary lib; };

CV3Graph makeLib(const std::vector<float>& vals, int ix, int iy, int iz, float inMin, float inMax,
                 float outMin, float outMax, float damping) {
  CV3Graph gg;
  SymbolLibrary& lib = gg.lib;
  lib.symbols["Const"] = cv3Atomic("Const", {{"value", "value", "Float", 0.0f}},
                                   {{"out", "out", "Float", 0.0f}});
  lib.symbols["FloatsToList"] = cv3Atomic("FloatsToList", {{"Input", "Input", "Float", 0.0f}},
                                          {{"out", "out", "FloatList", 0.0f}});
  lib.symbols["ComposeVec3FromList"] = cv3Atomic(
      "ComposeVec3FromList",
      {{"Input", "Input", "FloatList", 0.0f}, {"IndexForX", "IndexForX", "Float", 0.0f},
       {"IndexForY", "IndexForY", "Float", 0.0f}, {"IndexForZ", "IndexForZ", "Float", 0.0f},
       {"InputRange.x", "InputRange.x", "Float", 0.0f}, {"InputRange.y", "InputRange.y", "Float", 0.0f},
       {"OutputRange.x", "OutputRange.x", "Float", 0.0f}, {"OutputRange.y", "OutputRange.y", "Float", 0.0f},
       {"SpringDamping", "SpringDamping", "Float", 0.0f}},
      // Outputs declared here for the boundary wiring; evalResidentFloat resolves them off findSpec (the
      // real registry, where Result.* lead at spec indices 0/1/2 → extOut[0..2]).
      {{"Result.x", "Result.x", "Float", 0.0f}, {"Result.y", "Result.y", "Float", 0.0f},
       {"Result.z", "Result.z", "Float", 0.0f}});

  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"Result.x", "Result.x", "Float", 0.0f}, {"Result.y", "Result.y", "Float", 0.0f},
                     {"Result.z", "Result.z", "Float", 0.0f}};

  const bool wire = !vals.empty();
  if (wire) {
    SymbolChild ftl; ftl.id = 20; ftl.symbolId = "FloatsToList"; root.children.push_back(ftl);
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = 100 + (int)i; c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
  }
  SymbolChild cv3; cv3.id = 30; cv3.symbolId = "ComposeVec3FromList";
  cv3.overrides["IndexForX"] = (float)ix; cv3.overrides["IndexForY"] = (float)iy;
  cv3.overrides["IndexForZ"] = (float)iz;
  cv3.overrides["InputRange.x"] = inMin; cv3.overrides["InputRange.y"] = inMax;
  cv3.overrides["OutputRange.x"] = outMin; cv3.overrides["OutputRange.y"] = outMax;
  cv3.overrides["SpringDamping"] = damping;
  root.children.push_back(cv3);

  if (wire) {
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({100 + (int)i, "out", 20, "Input"});
    root.connections.push_back({20, "out", 30, "Input"});
  }
  const char* outP[3] = {"Result.x", "Result.y", "Result.z"};
  for (int c = 0; c < 3; ++c) root.connections.push_back({30, outP[c], kSymbolBoundary, outP[c]});

  lib.symbols[root.id] = root; lib.rootId = "Root";
  return gg;
}

// Cook one frame of `g` and return the 3 Result.* values via evalResidentFloat (== the extOut readback).
struct Vec3 { float x, y, z; };
Vec3 cookFrame(ResidentEvalGraph& g, SymbolLibrary& lib) {
  ResidentEvalCtx ctx; ctx.lib = &lib;
  cookComposeVec3Nodes(g, ctx);
  return {evalResidentFloat(g, "30", "Result.x", ctx), evalResidentFloat(g, "30", "Result.y", ctx),
          evalResidentFloat(g, "30", "Result.z", ctx)};
}

}  // namespace

int runComposeVec3FromListSelfTest(bool injectBug) {
  bool ok = true;
  auto rep = [&](const char* tag, float got, float want, bool pass) {
    ok = ok && pass;
    std::printf("[selftest-composevec3fromlist] %s got=%.6f want=%.6f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // === LEG A: NO-DAMP remap (P5-safe closed-form). list=[10,20,30], InputRange(0,100), OutputRange(0,1),
  //     indices 0/1/2 → (0.1, 0.2, 0.3). Damping 0 → no spring math (bug leaves this unchanged). ===
  {
    resetComposeVec3State();
    composeVec3InjectBugRef() = injectBug;
    CV3Graph gg = makeLib({10, 20, 30}, 0, 1, 2, 0, 100, 0, 1, /*damping=*/0.0f);
    ResidentEvalGraph g = buildEvalGraph(gg.lib, "Root");
    Vec3 v = cookFrame(g, gg.lib);
    composeVec3InjectBugRef() = false;
    rep("NODAMP Result.x=0.1", v.x, 0.1f, nearf(v.x, 0.1f));
    rep("NODAMP Result.y=0.2", v.y, 0.2f, nearf(v.y, 0.2f));
    rep("NODAMP Result.z=0.3", v.z, 0.3f, nearf(v.z, 0.3f));
  }

  // === LEG B: out-of-range index → NaN (cs:37-38). list=[10,20,30], IndexForX=5. ===
  {
    resetComposeVec3State();
    composeVec3InjectBugRef() = injectBug;
    CV3Graph gg = makeLib({10, 20, 30}, 5, 1, 2, 0, 100, 0, 1, /*damping=*/0.0f);
    ResidentEvalGraph g = buildEvalGraph(gg.lib, "Root");
    Vec3 v = cookFrame(g, gg.lib);
    composeVec3InjectBugRef() = false;
    bool pass = std::isnan(v.x);
    ok = ok && pass;
    std::printf("[selftest-composevec3fromlist] OOR IndexForX=5 → NaN? got=%.3f -> %s\n", v.x,
                pass ? "PASS" : "FAIL");
  }

  // === LEG C: SEED frame (damping=1, frame 0). target==current on the seed cook → output == remapped.
  //     list=[1], InputRange(0,1), OutputRange(0,1) → remapped 1.0 → frame-0 Result.x = 1.0. Property is
  //     impl-independent (zero-force identity), so the bug (which also emits remapped) LEAVES this green. ===
  {
    resetComposeVec3State();
    composeVec3InjectBugRef() = injectBug;
    CV3Graph gg = makeLib({1.0f}, 0, 0, 0, 0, 1, 0, 1, /*damping=*/1.0f);
    ResidentEvalGraph g = buildEvalGraph(gg.lib, "Root");
    Vec3 v = cookFrame(g, gg.lib);
    composeVec3InjectBugRef() = false;
    rep("SEED damping=1 frame0 Result.x=1.0(remapped)", v.x, 1.0f, nearf(v.x, 1.0f));
  }

  // === LEG D: ★ DAMP STEP (the teeth). damping=1. Frame 0 input 0.0 (seeds damped=0,vel=0); frame 1 & 2
  //     input 1.0. Hand-computed spring trajectory (Python ref in the commit): frame1=0.0277500,
  //     frame2=0.0732346. The bug SKIPS the damp → emits raw remapped 1.0 on frames 1/2 → diverges → RED.
  //     Two graphs (input 0.0 then 1.0) share the op's cross-frame state via the SAME resident path "30"
  //     across cooks — but a fresh buildEvalGraph resets extOut, not the op static. So we cook frame 0 on
  //     the 0.0 graph, then frames 1/2 on the 1.0 graph WITHOUT resetting the state store between them. ===
  {
    resetComposeVec3State();
    composeVec3InjectBugRef() = injectBug;
    // Frame 0: input 0.0 → seeds damped=0, vel=0 (remapped 0.0).
    CV3Graph g0 = makeLib({0.0f}, 0, 0, 0, 0, 1, 0, 1, /*damping=*/1.0f);
    ResidentEvalGraph rg0 = buildEvalGraph(g0.lib, "Root");
    Vec3 f0 = cookFrame(rg0, g0.lib);
    // Frame 1: input 1.0 (SAME op path "30" → same state static). remapped target 1.0.
    CV3Graph g1 = makeLib({1.0f}, 0, 0, 0, 0, 1, 0, 1, /*damping=*/1.0f);
    ResidentEvalGraph rg1 = buildEvalGraph(g1.lib, "Root");
    Vec3 f1 = cookFrame(rg1, g1.lib);
    // Frame 2: input 1.0 again (same path/state).
    CV3Graph g2 = makeLib({1.0f}, 0, 0, 0, 0, 1, 0, 1, /*damping=*/1.0f);
    ResidentEvalGraph rg2 = buildEvalGraph(g2.lib, "Root");
    Vec3 f2 = cookFrame(rg2, g2.lib);
    composeVec3InjectBugRef() = false;
    rep("DAMP frame0 (seed, input 0)=0.0", f0.x, 0.0f, nearf(f0.x, 0.0f));
    rep("DAMP frame1 (input 1)=0.0277500", f1.x, 0.0277500f, nearf(f1.x, 0.0277500f));
    rep("DAMP frame2 (input 1)=0.0732346", f2.x, 0.0732346f, nearf(f2.x, 0.0732346f));
  }

  resetComposeVec3State();  // leave the store clean for any later test in this process
  std::printf("[selftest-composevec3fromlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Golden registrar: DIRECT push into the value-op selftest sink (mirror of the PickColorFromList golden).
namespace {
struct ComposeVec3GoldenRegistrar {
  ComposeVec3GoldenRegistrar() {
    valueOpSelfTests().push_back({"composevec3fromlist", runComposeVec3FromListSelfTest});
  }
};
static const ComposeVec3GoldenRegistrar _reg_composevec3fromlist_golden;
}  // namespace

}  // namespace sw
