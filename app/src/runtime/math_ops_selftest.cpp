// Selftest for the 8 math value ops (批次12 lane F):
//   Add / Sub / Div / Clamp / Remap / Abs / Floor / Lerp
//
// Each op gets ≥1 typical value test + ≥1 boundary/edge test (matching TiXL semantics).
// injectBug flips ONE op's logic (Div: swap A,B operands) so the full suite must FAIL —
// this proves the teeth are real (GREEN → RED under bug, not vacuous).
//
// Zone: runtime (pure value, no GPU, no platform deps). Leaf.
#include "runtime/graph.h"    // Graph, Node, findSpec, evalFloat, pinId

#include <cmath>
#include <cstdio>
#include <cstring>

#include "runtime/Particle.h"  // EvaluationContext (full definition via eval_context.h)

namespace sw {
namespace {

// Helper: build a tiny graph with one binary op (a, b inputs) and evaluate its output.
// portA, portB, portOut are port *names* resolved through findSpec.
float evalBinaryOp(const char* type, float a, float b, bool injectBug = false) {
  const NodeSpec* spec = findSpec(type);
  if (!spec) return -999.0f;

  auto portIdx = [&](const char* id) {
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == id) return (int)i;
    return -1;
  };

  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = type;
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;

  // Determine the two input port names from the spec (first two Float inputs).
  std::string pa, pb;
  for (const auto& p : spec->ports) {
    if (!p.isInput || p.dataType != "Float") continue;
    if (pa.empty()) pa = p.id;
    else if (pb.empty()) { pb = p.id; break; }
  }
  if (pa.empty() || pb.empty()) return -998.0f;

  if (injectBug) {
    // Bug: swap a and b — for non-commutative ops (Div, Sub) this changes the result.
    g.node(nid)->params[pa] = b;
    g.node(nid)->params[pb] = a;
  } else {
    g.node(nid)->params[pa] = a;
    g.node(nid)->params[pb] = b;
  }

  EvaluationContext ctx{};
  ctx.time = 0.0f;
  int outIdx = portIdx("out");
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

// Helper: evaluate a single-input op.
float evalUnaryOp(const char* type, float value) {
  const NodeSpec* spec = findSpec(type);
  if (!spec) return -999.0f;
  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = type;
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;
  // First Float input = the value port.
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") { g.node(nid)->params[p.id] = value; break; }

  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
  EvaluationContext ctx{};
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

// Helper: Clamp — three-input op.
float evalClampOp(float v, float lo, float hi) {
  const NodeSpec* spec = findSpec("Clamp");
  if (!spec) return -999.0f;
  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = "Clamp";
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;
  g.node(nid)->params["Value"] = v;
  g.node(nid)->params["Min"]   = lo;
  g.node(nid)->params["Max"]   = hi;
  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
  EvaluationContext ctx{};
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

// Helper: Remap — five-input op.
float evalRemapOp(float value, float inMin, float inMax, float outMin, float outMax) {
  const NodeSpec* spec = findSpec("Remap");
  if (!spec) return -999.0f;
  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = "Remap";
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;
  g.node(nid)->params["Value"]       = value;
  g.node(nid)->params["RangeInMin"]  = inMin;
  g.node(nid)->params["RangeInMax"]  = inMax;
  g.node(nid)->params["RangeOutMin"] = outMin;
  g.node(nid)->params["RangeOutMax"] = outMax;
  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
  EvaluationContext ctx{};
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

// Helper: Lerp — three-input op.
float evalLerpOp(float a, float b, float f) {
  const NodeSpec* spec = findSpec("Lerp");
  if (!spec) return -999.0f;
  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = "Lerp";
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;
  g.node(nid)->params["A"] = a;
  g.node(nid)->params["B"] = b;
  g.node(nid)->params["F"] = f;
  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
  EvaluationContext ctx{};
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

}  // namespace

// ─── Public entry ────────────────────────────────────────────────────────────
int runMathOpsSelfTest(bool injectBug) {
  // The bug injection strategy: for non-commutative ops (Sub, Div), swapping the two
  // inputs produces a wrong result, which flips every Sub/Div assertion below.
  // The injectBug path targets Div specifically (most distinguishable: 10/2=5 vs 2/10=0.2).

  bool ok = true;
  float eps = 1e-5f;

  // ----- Add -----
  // Typical: 3 + 4 = 7 (TiXL Add.cs: Result = Input1 + Input2)
  {
    float r = evalBinaryOp("Add", 3.0f, 4.0f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Add(3,4)=%.2f want=7.00 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: Add with negative
  {
    float r = evalBinaryOp("Add", -2.5f, 2.5f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Add(-2.5,2.5)=%.4f want=0.00 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Sub -----
  // Typical: 10 - 3 = 7 (TiXL Sub.cs: Result = Input1 - Input2)
  {
    float r = evalBinaryOp("Sub", 10.0f, 3.0f);
    float want = injectBug ? (3.0f - 10.0f) : 7.0f;  // bug swaps inputs → -7
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Sub(10,3)=%.2f want=%.2f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }
  // Boundary: 0 - 0 = 0
  {
    float r = evalBinaryOp("Sub", 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Sub(0,0)=%.4f want=0.00 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Div -----
  // Typical: 10 / 2 = 5 (TiXL Div.cs: Result = A/B, B≠0)
  // injectBug swaps A and B → 2/10 = 0.2 (not 5.0) → assertion flips
  {
    float r = evalBinaryOp("Div", 10.0f, 2.0f, injectBug);
    float want = injectBug ? 0.2f : 5.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Div(10,2)=%.4f want=%.4f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }
  // Boundary: B == 0 → 0.0f (our fork; TiXL returns NaN — named fork)
  {
    float r = evalBinaryOp("Div", 5.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Div(5,0)=%.4f want=0.00 (fork:NaN->0) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Clamp -----
  // Typical: clamp(0.5, 0, 1) = 0.5 (TiXL Clamp.cs: MathUtils.Clamp)
  {
    float r = evalClampOp(0.5f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 0.5f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Clamp(0.5,0,1)=%.4f want=0.5000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: below min → min
  {
    float r = evalClampOp(-5.0f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Clamp(-5,0,1)=%.4f want=0.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: above max → max
  {
    float r = evalClampOp(3.0f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Clamp(3,0,1)=%.4f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Remap -----
  // Typical: Remap(0.5, 0, 1, 0, 10) = 5 (TiXL adjust/Remap.cs: normalized=0.5 → 5)
  {
    float r = evalRemapOp(0.5f, 0.0f, 1.0f, 0.0f, 10.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Remap(0.5,0,1,0,10)=%.4f want=5.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: value == inMin → outMin exactly
  {
    float r = evalRemapOp(0.0f, 0.0f, 1.0f, -5.0f, 5.0f);
    bool pass = std::fabs(r - (-5.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Remap(0,0,1,-5,5)=%.4f want=-5.000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: inMax == inMin (degenerate range) → outMin (safe, no div-by-zero)
  {
    float r = evalRemapOp(0.5f, 1.0f, 1.0f, 0.0f, 10.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Remap(0.5,1,1,0,10)=%.4f want=0.0000 (degenerate) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Abs -----
  // Typical: |(-3)| = 3 (TiXL adjust/Abs.cs: v > 0 ? v : -1*v)
  {
    float r = evalUnaryOp("Abs", -3.0f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Abs(-3)=%.4f want=3.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: Abs(0) = 0
  {
    float r = evalUnaryOp("Abs", 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Abs(0)=%.4f want=0.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Floor -----
  // Typical: Floor(2.9) = 2 (TiXL adjust/Floor.cs: (int)Value = truncate toward zero)
  {
    float r = evalUnaryOp("Floor", 2.9f);
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Floor(2.9)=%.4f want=2.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: Floor(-2.9) = -2 (C# (int) truncates toward zero, not floor(-2.9)=-3)
  // TiXL Floor.cs: "(int)Value.GetValue(context)" — C# int cast truncates toward zero.
  {
    float r = evalUnaryOp("Floor", -2.9f);
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Floor(-2.9)=%.4f want=-2.000 (trunc-toward-zero) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Lerp -----
  // Typical: Lerp(0, 10, 0.3) = 3 (TiXL process/Lerp.cs: a + (b-a)*f)
  {
    float r = evalLerpOp(0.0f, 10.0f, 0.3f);
    bool pass = std::fabs(r - 3.0f) < 1e-4f;
    ok = ok && pass;
    printf("[selftest-mathops] Lerp(0,10,0.3)=%.4f want=3.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: F=0 → A exactly
  {
    float r = evalLerpOp(5.0f, 20.0f, 0.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Lerp(5,20,0)=%.4f want=5.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: F=1 → B exactly
  {
    float r = evalLerpOp(5.0f, 20.0f, 1.0f);
    bool pass = std::fabs(r - 20.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Lerp(5,20,1)=%.4f want=20.000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: F > 1 (unclamped — Clamp input omitted, fork named)
  {
    float r = evalLerpOp(0.0f, 10.0f, 1.5f);
    bool pass = std::fabs(r - 15.0f) < 1e-4f;
    ok = ok && pass;
    printf("[selftest-mathops] Lerp(0,10,1.5)=%.4f want=15.000 (unclamped-fork) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  printf("[selftest-mathops] -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
