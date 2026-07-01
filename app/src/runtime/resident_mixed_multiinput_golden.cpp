// 骨7b MIXED-MULTIINPUT ORDER golden — the tooth that bites the loop2/loop3 flatten ordering bug.
//
// THE BUG (refuter 骨7 Finding3, CONFIRMED): resident_eval_flatten.cpp's inlineSymbol used to resolve
// wires in TWO passes — a child→child pass (old loop 2) that ran ENTIRELY BEFORE a boundary→child pass
// (old loop 3), both appending to the SAME MultiInput slot's ResidentInput::extraConns. So a slot fed by
// BOTH a child wire and a boundary wire always landed ordered [all child…, all boundary…], LOSING the
// real sym.connections declaration order. Downstream the marshal cook (point_graph_resident_buffer.cpp:
// 145-147) packs floatInputs positionally as [primary, extraConns…] straight into a GPU constant buffer,
// so a scrambled extraConns = a scrambled constant buffer = the compute shader reads shuffled constants.
// The whole mesh/modify + mesh/draw Lib family (DisplaceMesh / DeformMesh / DrawMesh …, ~224 production
// compounds) has such mixed slots.
//
// THE PROVING CASE: a real production marshal op — IntsToBuffer (its Params port is a Float multiInput,
// the mesh family's int-payload rail) — fed by an INTERLEAVED wire declaration:
//     Params <- [ child_A(=2),  boundary_Space,  child_B(=7) ]   (declaration order)
// Faithful flatten → the resident node's Params input carries primary=child_A and
//     extraConns == [ boundary_Space, child_B ]   (== declaration order minus the primary).
// The OLD two-pass flatten produced primary=child_A then extraConns == [ child_B, boundary_Space ]
// (all children appended in pass 2, then the boundary in pass 3) — the boundary and child_B SWAPPED.
//
// THE TOOTH bites ORDER, not a reduced value: it inspects the extraConns sequence directly (a Sum-style
// value assertion is commutative and would NOT catch a swap). No Metal / no marshal cook needed — the
// bug lives entirely in the CPU flatten, so inspecting the flattened graph is the tightest bite.
//
// injectBug: assert against the OLD grouped order ([child_B, boundary]) — which the FIXED single-pass
// flatten no longer produces — so the tooth goes RED, proving the fix changed behaviour in the biting
// direction (red-first). GREEN = the faithful declaration order survives the flatten.
#include "runtime/resident_eval_graph.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sw {
namespace {

Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Render one input's driver sequence (primary + extraConns) as source-node paths, so we can compare
// the ORDER textually. A boundary wire resolves to an injected "$in/…" Const path; a child wire to the
// sibling Const's resident path ("1", "3").
std::vector<std::string> driverPaths(const ResidentInput& in) {
  std::vector<std::string> v;
  v.push_back(in.srcNodePath);                 // primary
  for (const auto& ec : in.extraConns) v.push_back(ec.first);
  return v;
}

std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; }
  return s;
}

}  // namespace

int runMixedMultiInputOrderSelfTest(bool injectBug) {
  // Atomics: Const (a Float source) + IntsToBuffer (real production marshal op; Params = Float multiInput).
  Symbol cst = atomic("Const", {{"value", "value", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
  Symbol i2b = atomic("IntsToBuffer", {{"Params", "Params", "Float", 0.0f}}, {{"Buffer", "Buffer", "Buffer", 0.0f}});

  SymbolLibrary lib;
  lib.symbols[cst.id] = cst;
  lib.symbols[i2b.id] = i2b;

  // Root compound: two Const children (ids 1,3) + one IntsToBuffer child (id 2), plus a boundary INPUT
  // def "Space". The Params slot is fed INTERLEAVED: child_A(1), boundary(Space), child_B(3).
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.inputDefs = {{"Space", "Space", "Float", 0.0f}};      // boundary input (injected below)
  root.outputDefs = {{"Buffer", "Buffer", "Buffer", 0.0f}};
  SymbolChild cA; cA.id = 1; cA.symbolId = "Const"; cA.overrides["value"] = 2.0f;
  SymbolChild mb; mb.id = 2; mb.symbolId = "IntsToBuffer";
  SymbolChild cB; cB.id = 3; cB.symbolId = "Const"; cB.overrides["value"] = 7.0f;
  root.children = {cA, mb, cB};
  root.connections = {
      {1, "out", 2, "Params"},                   // child_A  -> Params (primary)
      {kSymbolBoundary, "Space", 2, "Params"},   // boundary -> Params (INTERLEAVED — must stay 2nd)
      {3, "out", 2, "Params"},                   // child_B  -> Params (must stay 3rd)
      {2, "Buffer", kSymbolBoundary, "Buffer"},  // IntsToBuffer.Buffer -> Root output
  };
  lib.symbols[root.id] = root; lib.rootId = "Root";

  // Boundary injection feeds "Space" a value → a synthetic "$in/Space#0" Const producer the boundary
  // wire resolves to as a Connection (the same thing a real parent .t3 would wire in).
  std::map<std::string, std::vector<float>> boundary = {{"Space", {5.0f}}};
  ResidentEvalGraph g = buildEvalGraph(lib, "Root", boundary);

  const ResidentNode* i2bNode = g.node("2");
  const ResidentInput* params = i2bNode ? i2bNode->input("Params") : nullptr;
  std::vector<std::string> got = params ? driverPaths(*params) : std::vector<std::string>{};

  // Faithful declaration order: primary child_A("1"), then boundary("$in/Space#0"), then child_B("3").
  std::vector<std::string> wantFaithful = {"1", "$in/Space#0", "3"};
  // OLD grouped (buggy) order: primary child_A, then all children (child_B), then boundary.
  std::vector<std::string> wantOldGrouped = {"1", "3", "$in/Space#0"};

  const std::vector<std::string>& want = injectBug ? wantOldGrouped : wantFaithful;
  bool pass = (got == want);

  printf("[selftest-mixed-multiinput] Params order got=[%s] want=[%s] (%s) -> %s\n",
         join(got).c_str(), join(want).c_str(),
         injectBug ? "injectBug: expects OLD grouped order (RED on fixed flatten)"
                   : "faithful: declaration order",
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
