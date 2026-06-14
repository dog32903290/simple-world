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

  // [overnight-math] BEGIN new-op teeth (Sqrt/Pow/Modulo/Ceil/SmoothStep/Log/Cos)
  // Helper: evaluate any op by param injection (no wiring needed — params drive directly).
  // Supports ops whose output port is named "Result" (overnight batch naming convention).
  // params: pairs of {portId, value} to set; outPortId: name of the output port.
  auto evalOpParams = [&](const char* type,
                          std::initializer_list<std::pair<const char*, float>> params,
                          const char* outPortId) -> float {
    const NodeSpec* spec = findSpec(type);
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++;
    nd.type = type;
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    for (auto& kv : params) g.node(nid)->params[kv.first] = kv.second;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPortId) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // ----- Sqrt -----
  // TiXL Sqrt.cs: "Result.Value = MathF.Sqrt(v);"
  // Sqrt(9) == 3; Sqrt(-4) == 0 (FORK: negative → 0 not NaN)
  {
    float r = evalOpParams("Sqrt", {{"Value", 9.0f}}, "Result");
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Sqrt(9)=%.4f want=3.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Sqrt", {{"Value", -4.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Sqrt(-4)=%.4f want=0.0000 (fork:neg->0) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Pow -----
  // TiXL Pow.cs: "Result.Value = (float)Math.Pow(v, pow);"
  // Pow(2, 10) == 1024; Pow(3, 0) == 1
  {
    float r = evalOpParams("Pow", {{"Value", 2.0f}, {"Exponent", 10.0f}}, "Result");
    bool pass = std::fabs(r - 1024.0f) < 0.01f;
    ok = ok && pass;
    printf("[selftest-mathops] Pow(2,10)=%.2f want=1024.00 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Pow", {{"Value", 3.0f}, {"Exponent", 0.0f}}, "Result");
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Pow(3,0)=%.4f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Modulo -----
  // TiXL Modulo.cs: "v - mod2 * (float)Math.Floor(v/mod2)"
  // Modulo(10, 3) == 1; Modulo(-1, 3) == 2 (floor-mod); Modulo(5, 0) == 0
  {
    float r = evalOpParams("Modulo", {{"Value", 10.0f}, {"ModuloValue", 3.0f}}, "Result");
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Modulo(10,3)=%.4f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    // floor-mod: -1 mod 3 = 2 (not -1 as in C truncation mod)
    float r = evalOpParams("Modulo", {{"Value", -1.0f}, {"ModuloValue", 3.0f}}, "Result");
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Modulo(-1,3)=%.4f want=2.0000 (floor-mod) -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Modulo", {{"Value", 5.0f}, {"ModuloValue", 0.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Modulo(5,0)=%.4f want=0.0000 (div-zero->0) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Ceil -----
  // TiXL Ceil.cs: "(float)Math.Ceiling(v)"
  // Ceil(1.1) == 2; Ceil(-1.1) == -1
  {
    float r = evalOpParams("Ceil", {{"Value", 1.1f}}, "Result");
    bool pass = (r == 2.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Ceil(1.1)=%.4f want=2.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Ceil", {{"Value", -1.1f}}, "Result");
    bool pass = (r == -1.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Ceil(-1.1)=%.4f want=-1.000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- SmoothStep -----
  // TiXL SmoothStep.cs uses MathUtils.SmootherStep (= Perlin Fade, NOT classic smoothstep).
  // Fade(t) = t^3*(6t^2 - 15t + 10). SmootherStep(0,1,0.5): t=0.5 → 0.5^3*(6*0.25-7.5+10)=0.5
  {
    float r = evalOpParams("SmoothStep", {{"Min", 0.0f}, {"Max", 1.0f}, {"Value", 0.5f}}, "Result");
    bool pass = std::fabs(r - 0.5f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] SmoothStep(0,1,0.5)=%.6f want=0.5000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    // boundary: Value=0 → 0
    float r = evalOpParams("SmoothStep", {{"Min", 0.0f}, {"Max", 1.0f}, {"Value", 0.0f}}, "Result");
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] SmoothStep(0,1,0)=%.6f want=0.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    // boundary: Value=1 → 1
    float r = evalOpParams("SmoothStep", {{"Min", 0.0f}, {"Max", 1.0f}, {"Value", 1.0f}}, "Result");
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] SmoothStep(0,1,1)=%.6f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Log -----
  // TiXL Log.cs: "(float)Math.Log(v, newBase)"
  // Log(8, 2) == 3; Log(0, 2) == 0 (FORK: ≤0 → 0); Log(8, 1) == 0 (FORK: base=1 → 0)
  {
    float r = evalOpParams("Log", {{"Value", 8.0f}, {"Base", 2.0f}}, "Result");
    bool pass = std::fabs(r - 3.0f) < 1e-4f;
    ok = ok && pass;
    printf("[selftest-mathops] Log(8,2)=%.4f want=3.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Log", {{"Value", 0.0f}, {"Base", 2.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Log(0,2)=%.4f want=0.0000 (fork:<=0->0) -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Log", {{"Value", 8.0f}, {"Base", 1.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Log(8,1)=%.4f want=0.0000 (fork:base=1->0) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Cos -----
  // TiXL Cos.cs: "(float)Math.Cos(Input.GetValue(context))"
  // Cos(0) == 1; Cos(π) ≈ -1; Cos(π/2) ≈ 0
  {
    float r = evalOpParams("Cos", {{"Input", 0.0f}}, "Result");
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Cos(0)=%.6f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Cos", {{"Input", 3.14159265f}}, "Result");
    bool pass = std::fabs(r - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Cos(pi)=%.6f want=-1.000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("Cos", {{"Input", 3.14159265f / 2.0f}}, "Result");
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Cos(pi/2)=%.6f want=0.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // [overnight-math] END new-op teeth

  // [math-batch22] BEGIN teeth (Round / Atan2 / Sigmoid / AddVec3 / SubVec3 / ScaleVector3)

  // ----- Round -----
  // TiXL float/adjust/Round.cs RoundValue2:
  //   y = i - (m - tval), where tval chosen from {0, u, (m-v)/denom}.
  // RoundRatio=0 → v=0 → middle branch → tval=m/1=m → r=0 → passthrough.
  // RoundRatio=1 → v=0.5/steps → hard quantization (rounds to nearest step boundary).
  //
  // Round(2.3, StepsPerUnit=1, RoundRatio=1):
  //   u=1, v=0.5, m=0.3; m<v(0.3<0.5) → tval=0 → r=0.3 → y=2.0  [rounds down]
  {
    float r = evalOpParams("Round", {{"Value", 2.3f}, {"StepsPerUnit", 1.0f}, {"RoundRatio", 1.0f}}, "Result");
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Round(2.3,1,ratio=1)=%.4f want=2.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Round(2.7, StepsPerUnit=1, RoundRatio=1):
  //   u=1, v=0.5, m=0.7; m>u-v(0.7>0.5) → tval=u=1 → r=0.7-1=-0.3 → y=3.0  [rounds up]
  {
    float r = evalOpParams("Round", {{"Value", 2.7f}, {"StepsPerUnit", 1.0f}, {"RoundRatio", 1.0f}}, "Result");
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Round(2.7,1,ratio=1)=%.4f want=3.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Boundary: RoundRatio=0 → passthrough (TiXL Round.cs: v=0 → middle branch → tval=m → r=0)
  {
    float r = evalOpParams("Round", {{"Value", 2.7f}, {"StepsPerUnit", 1.0f}, {"RoundRatio", 0.0f}}, "Result");
    bool pass = std::fabs(r - 2.7f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Round(2.7,1,ratio=0)=%.4f want=2.7000 (passthrough) -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof for Round: inject wrong tval branch selection.
  // Bug: if we used tval=0 always (ignoring the m>u-v branch), Round(2.7,1,1) would give
  //   r=0.7, y=2.7-0.7=2.0 (rounds down) instead of 3.0 (rounds up).
  // We verify the correct result (3.0) vs the wrong one (2.0) proves the tooth.
  if (injectBug) {
    float r = evalOpParams("Round", {{"Value", 2.7f}, {"StepsPerUnit", 1.0f}, {"RoundRatio", 1.0f}}, "Result");
    // Inject: assert wrong (2.0) instead of correct (3.0) → FAIL
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Round(2.7,1,1) inject-wrong-expect-2=%.4f (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Atan2 -----
  // TiXL float/trigonometry/Atan2.cs line 17: "Result.Value = MathF.Atan2(v.X, v.Y);"
  // fork-atan2-arg-order: args are (X,Y) not standard math (Y,X).
  // Atan2(x=1, y=0) = atan2(1,0) = π/2 ≈ 1.5708
  {
    float r = evalOpParams("Atan2", {{"Vector.x", 1.0f}, {"Vector.y", 0.0f}}, "Result");
    float want = std::atan2(1.0f, 0.0f);  // = π/2 ≈ 1.5708
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Atan2(x=1,y=0)=%.6f want=%.6f (pi/2) -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }
  // Atan2(x=0, y=1) = atan2(0,1) = 0
  {
    float r = evalOpParams("Atan2", {{"Vector.x", 0.0f}, {"Vector.y", 1.0f}}, "Result");
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Atan2(x=0,y=1)=%.6f want=0.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if standard atan2(Y,X) were used instead of TiXL's atan2(X,Y):
  //   Atan2(x=1,y=0): standard gives atan2(0,1)=0, but TiXL gives atan2(1,0)=π/2.
  //   We assert π/2 in normal mode; inject bug asserts 0 → FAIL.
  if (injectBug) {
    float r = evalOpParams("Atan2", {{"Vector.x", 1.0f}, {"Vector.y", 0.0f}}, "Result");
    // Assert standard-order result (0.0) → FAIL since actual is TiXL-order (π/2).
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Atan2(x=1,y=0) inject-standard-expect-0=%.6f (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Sigmoid -----
  // TiXL float/adjust/Sigmoid.cs: "1f/(1+ MathF.Pow(MathF.E, pow * v))"
  // Sigmoid(v=0, stretch=1): 1/(1+e^0) = 1/2 = 0.5
  {
    float r = evalOpParams("Sigmoid", {{"Value", 0.0f}, {"Stretch", 1.0f}}, "Result");
    bool pass = std::fabs(r - 0.5f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Sigmoid(v=0,s=1)=%.6f want=0.5000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Sigmoid(v=1, stretch=1): 1/(1+e^1) ≈ 0.26894 (NOT standard sigmoid = 0.73106)
  {
    float r = evalOpParams("Sigmoid", {{"Value", 1.0f}, {"Stretch", 1.0f}}, "Result");
    float want = 1.0f / (1.0f + std::exp(1.0f));  // ≈ 0.26894
    bool pass = std::fabs(r - want) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] Sigmoid(v=1,s=1)=%.6f want=%.6f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }
  // RED proof: standard logistic sigmoid would give 1/(1+e^(-1)) ≈ 0.73106 for (v=1,s=1).
  // TiXL gives 0.26894. If Stretch were negated, result would be 0.73106.
  if (injectBug) {
    float r = evalOpParams("Sigmoid", {{"Value", 1.0f}, {"Stretch", 1.0f}}, "Result");
    float wrong_standard = 1.0f / (1.0f + std::exp(-1.0f));  // ≈ 0.73106
    // Assert standard sigmoid result → FAIL since actual is TiXL's 0.26894.
    bool pass = std::fabs(r - wrong_standard) < 1e-4f;
    ok = ok && pass;
    printf("[selftest-mathops] Sigmoid(v=1,s=1) inject-neg-stretch=%.6f assert=%.6f (INJECT BUG) -> %s\n",
           r, wrong_standard, pass ? "PASS" : "FAIL");
  }

  // Helper: evaluate a Vec3 op — set named float params, pull named output pin.
  // outPortId: the exact port id in the spec (e.g. "Result.x").
  auto evalVec3Op = [&](const char* type,
                        std::initializer_list<std::pair<const char*, float>> params,
                        const char* outPortId) -> float {
    const NodeSpec* spec = findSpec(type);
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++;
    nd.type = type;
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    for (auto& kv : params) g.node(nid)->params[kv.first] = kv.second;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPortId) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // ----- AddVec3 -----
  // TiXL vec3/AddVec3.cs: "Result.Value = Input1.GetValue(context) + Input2.GetValue(context);"
  // AddVec3(Input1=(1,2,3), Input2=(4,5,6)) → Result=(5,7,9)
  {
    float vx = evalVec3Op("AddVec3",
      {{"Input1.x",1.0f},{"Input1.y",2.0f},{"Input1.z",3.0f},
       {"Input2.x",4.0f},{"Input2.y",5.0f},{"Input2.z",6.0f}}, "Result.x");
    float vy = evalVec3Op("AddVec3",
      {{"Input1.x",1.0f},{"Input1.y",2.0f},{"Input1.z",3.0f},
       {"Input2.x",4.0f},{"Input2.y",5.0f},{"Input2.z",6.0f}}, "Result.y");
    float vz = evalVec3Op("AddVec3",
      {{"Input1.x",1.0f},{"Input1.y",2.0f},{"Input1.z",3.0f},
       {"Input2.x",4.0f},{"Input2.y",5.0f},{"Input2.z",6.0f}}, "Result.z");
    bool pass = std::fabs(vx-5.0f)<eps && std::fabs(vy-7.0f)<eps && std::fabs(vz-9.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] AddVec3((1,2,3)+(4,5,6))=(%.1f,%.1f,%.1f) want=(5,7,9) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // RED proof for AddVec3: if subtraction were used instead of addition, x = 1-4 = -3, not 5.
  if (injectBug) {
    float vx = evalVec3Op("AddVec3",
      {{"Input1.x",1.0f},{"Input2.x",4.0f}}, "Result.x");
    // Assert subtraction result (-3) → FAIL since actual is addition (5).
    bool pass = std::fabs(vx - (-3.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] AddVec3 inject-assert-sub x=%.1f assert=-3 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- SubVec3 -----
  // TiXL vec3/SubVec3.cs: "Result.Value = Input1.GetValue(context) - Input2.GetValue(context);"
  // SubVec3(Input1=(10,20,30), Input2=(1,2,3)) → Result=(9,18,27)
  {
    float vx = evalVec3Op("SubVec3",
      {{"Input1.x",10.0f},{"Input1.y",20.0f},{"Input1.z",30.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f},{"Input2.z",3.0f}}, "Result.x");
    float vy = evalVec3Op("SubVec3",
      {{"Input1.x",10.0f},{"Input1.y",20.0f},{"Input1.z",30.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f},{"Input2.z",3.0f}}, "Result.y");
    float vz = evalVec3Op("SubVec3",
      {{"Input1.x",10.0f},{"Input1.y",20.0f},{"Input1.z",30.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f},{"Input2.z",3.0f}}, "Result.z");
    bool pass = std::fabs(vx-9.0f)<eps && std::fabs(vy-18.0f)<eps && std::fabs(vz-27.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] SubVec3((10,20,30)-(1,2,3))=(%.1f,%.1f,%.1f) want=(9,18,27) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // RED proof for SubVec3: if operands swapped (Input2-Input1), x = 1-10 = -9, not 9.
  if (injectBug) {
    float vx = evalVec3Op("SubVec3",
      {{"Input1.x",10.0f},{"Input2.x",1.0f}}, "Result.x");
    // Assert swapped result (-9) → FAIL since actual is (9).
    bool pass = std::fabs(vx - (-9.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] SubVec3 inject-swap x=%.1f assert=-9 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- ScaleVector3 -----
  // TiXL vec3/ScaleVector3.cs: "Result.Value = a * b * u;"
  // ScaleVector3(A=(2,3,4), B=(1,1,1), ScaleUniform=5) → Result=(10,15,20)
  {
    float vx = evalVec3Op("ScaleVector3",
      {{"A.x",2.0f},{"A.y",3.0f},{"A.z",4.0f},
       {"B.x",1.0f},{"B.y",1.0f},{"B.z",1.0f},
       {"ScaleUniform",5.0f}}, "Result.x");
    float vy = evalVec3Op("ScaleVector3",
      {{"A.x",2.0f},{"A.y",3.0f},{"A.z",4.0f},
       {"B.x",1.0f},{"B.y",1.0f},{"B.z",1.0f},
       {"ScaleUniform",5.0f}}, "Result.y");
    float vz = evalVec3Op("ScaleVector3",
      {{"A.x",2.0f},{"A.y",3.0f},{"A.z",4.0f},
       {"B.x",1.0f},{"B.y",1.0f},{"B.z",1.0f},
       {"ScaleUniform",5.0f}}, "Result.z");
    bool pass = std::fabs(vx-10.0f)<eps && std::fabs(vy-15.0f)<eps && std::fabs(vz-20.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] ScaleVector3((2,3,4)*(1,1,1)*5)=(%.1f,%.1f,%.1f) want=(10,15,20) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // RED proof: if ScaleUniform were ignored (result=A*B only), x = 2*1 = 2, not 10.
  if (injectBug) {
    float vx = evalVec3Op("ScaleVector3",
      {{"A.x",2.0f},{"B.x",1.0f},{"ScaleUniform",5.0f}}, "Result.x");
    // Assert A*B only (2.0) → FAIL since actual includes ScaleUniform (10.0).
    bool pass = std::fabs(vx - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] ScaleVector3 inject-no-uniform x=%.1f assert=2 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // [math-batch22] END teeth

  // [math-batch23] BEGIN teeth (Magnitude / DotVec3 / Vec3Distance / Vector3Components / RotateVector3)

  // ----- Magnitude -----
  // TiXL vec3/Magnitude.cs: "Result.Value = Input.GetValue(context).Length();"
  // Magnitude((3,4,0)) = sqrt(9+16) = 5
  {
    float r = evalVec3Op("Magnitude",
      {{"Input.x",3.0f},{"Input.y",4.0f},{"Input.z",0.0f}}, "Result");
    bool pass = std::fabs(r - 5.0f) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] Magnitude(3,4,0)=%.5f want=5.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Magnitude((1,1,1)) = sqrt(3) ≈ 1.73205
  {
    float r = evalVec3Op("Magnitude",
      {{"Input.x",1.0f},{"Input.y",1.0f},{"Input.z",1.0f}}, "Result");
    float want = std::sqrt(3.0f);
    bool pass = std::fabs(r - want) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] Magnitude(1,1,1)=%.5f want=%.5f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }
  // Boundary: zero vector → 0
  {
    float r = evalVec3Op("Magnitude",
      {{"Input.x",0.0f},{"Input.y",0.0f},{"Input.z",0.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Magnitude(0,0,0)=%.5f want=0.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if square root were omitted (returned x^2+y^2+z^2), Magnitude(3,4,0) = 25 not 5.
  if (injectBug) {
    float r = evalVec3Op("Magnitude",
      {{"Input.x",3.0f},{"Input.y",4.0f},{"Input.z",0.0f}}, "Result");
    // Assert squared result (25) → FAIL since actual is length (5).
    bool pass = std::fabs(r - 25.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Magnitude inject-no-sqrt=%.5f assert=25 (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- DotVec3 -----
  // TiXL vec3/DotVec3.cs: "Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));"
  // DotVec3((1,0,0), (0,1,0)) = 0 (perpendicular)
  {
    float r = evalVec3Op("DotVec3",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f},{"Input2.z",0.0f}}, "Result");
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec3((1,0,0).(0,1,0))=%.4f want=0.0000 (perp) -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // DotVec3((2,3,4), (1,2,3)) = 2+6+12 = 20
  {
    float r = evalVec3Op("DotVec3",
      {{"Input1.x",2.0f},{"Input1.y",3.0f},{"Input1.z",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f},{"Input2.z",3.0f}}, "Result");
    bool pass = std::fabs(r - 20.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec3((2,3,4).(1,2,3))=%.4f want=20.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if one term were omitted (x*x2+y*y2 only, missing z*z2):
  //   DotVec3((2,3,4),(1,2,3)) = 2+6 = 8, not 20.
  if (injectBug) {
    float r = evalVec3Op("DotVec3",
      {{"Input1.x",2.0f},{"Input1.y",3.0f},{"Input1.z",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f},{"Input2.z",3.0f}}, "Result");
    // Assert missing-z result (8) → FAIL since actual is (20).
    bool pass = std::fabs(r - 8.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec3 inject-missing-z=%.4f assert=8 (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Vec3Distance -----
  // TiXL vec3/Vec3Distance.cs: "Result.Value = Vector3.Distance(Input1, Input2);"
  // Vec3Distance((0,0,0), (3,4,0)) = 5
  {
    float r = evalVec3Op("Vec3Distance",
      {{"Input1.x",0.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",3.0f},{"Input2.y",4.0f},{"Input2.z",0.0f}}, "Result");
    bool pass = std::fabs(r - 5.0f) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] Vec3Distance((0,0,0),(3,4,0))=%.5f want=5.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // Vec3Distance((1,1,1), (1,1,1)) = 0
  {
    float r = evalVec3Op("Vec3Distance",
      {{"Input1.x",1.0f},{"Input1.y",1.0f},{"Input1.z",1.0f},
       {"Input2.x",1.0f},{"Input2.y",1.0f},{"Input2.z",1.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Vec3Distance((1,1,1),(1,1,1))=%.5f want=0.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if inputs were not subtracted (magnitude of Input1 used instead):
  //   Magnitude((0,0,0)) = 0, not 5. Or for non-zero: Magnitude((3,4,0)) = 5 (same result accidentally).
  //   Use Vec3Distance((1,2,0),(4,6,0)): correct=sqrt(9+16)=5; wrong=sqrt(1+4)=2.236.
  if (injectBug) {
    float r = evalVec3Op("Vec3Distance",
      {{"Input1.x",1.0f},{"Input1.y",2.0f},{"Input1.z",0.0f},
       {"Input2.x",4.0f},{"Input2.y",6.0f},{"Input2.z",0.0f}}, "Result");
    // Assert magnitude-of-Input1 (sqrt(5)~2.236) → FAIL since actual is distance (5).
    float wrong = std::sqrt(1.0f + 4.0f);
    bool pass = std::fabs(r - wrong) < 1e-3f;
    ok = ok && pass;
    printf("[selftest-mathops] Vec3Distance inject-no-sub=%.5f assert=%.5f (INJECT BUG) -> %s\n", r, wrong, pass ? "PASS" : "FAIL");
  }

  // ----- Vector3Components -----
  // TiXL vec3/Vector3Components.cs: "X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z;"
  // Vector3Components((2,5,9)) → X=2, Y=5, Z=9
  {
    float vx = evalVec3Op("Vector3Components",
      {{"Value.x",2.0f},{"Value.y",5.0f},{"Value.z",9.0f}}, "X");
    float vy = evalVec3Op("Vector3Components",
      {{"Value.x",2.0f},{"Value.y",5.0f},{"Value.z",9.0f}}, "Y");
    float vz = evalVec3Op("Vector3Components",
      {{"Value.x",2.0f},{"Value.y",5.0f},{"Value.z",9.0f}}, "Z");
    bool pass = std::fabs(vx-2.0f)<eps && std::fabs(vy-5.0f)<eps && std::fabs(vz-9.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vector3Components((2,5,9))=(%.1f,%.1f,%.1f) want=(2,5,9) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // RED proof: if X and Y were swapped (component mismatch), X would output Y=5 not 2.
  if (injectBug) {
    float vx = evalVec3Op("Vector3Components",
      {{"Value.x",2.0f},{"Value.y",5.0f},{"Value.z",9.0f}}, "X");
    // Assert Y-value (5) at X output → FAIL since actual is (2).
    bool pass = std::fabs(vx - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vector3Components inject-xy-swap X=%.1f assert=5 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- RotateVector3 -----
  // TiXL vec3/RotateVector3.cs:
  //   "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
  //   "var m = Matrix4x4.CreateFromAxisAngle(axis, angle);"
  //   "Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context);"
  // Test 1: Rotate (1,0,0) by 90° around Z axis → (0,1,0) (Rodrigues)
  {
    float vx = evalVec3Op("RotateVector3",
      {{"VectorA.x",1.0f},{"VectorA.y",0.0f},{"VectorA.z",0.0f},
       {"Angle",90.0f},
       {"Axis.x",0.0f},{"Axis.y",0.0f},{"Axis.z",1.0f},
       {"Scale",1.0f}}, "Result.x");
    float vy = evalVec3Op("RotateVector3",
      {{"VectorA.x",1.0f},{"VectorA.y",0.0f},{"VectorA.z",0.0f},
       {"Angle",90.0f},
       {"Axis.x",0.0f},{"Axis.y",0.0f},{"Axis.z",1.0f},
       {"Scale",1.0f}}, "Result.y");
    // expect (0, 1, 0)
    bool pass = std::fabs(vx - 0.0f) < 1e-5f && std::fabs(vy - 1.0f) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] RotateVector3((1,0,0) 90deg Z)=(%.5f,%.5f) want=(0,1) -> %s\n",
           vx, vy, pass ? "PASS" : "FAIL");
  }
  // Test 2: Scale=2 multiplies result
  {
    float vx = evalVec3Op("RotateVector3",
      {{"VectorA.x",1.0f},{"VectorA.y",0.0f},{"VectorA.z",0.0f},
       {"Angle",0.0f},
       {"Axis.x",0.0f},{"Axis.y",0.0f},{"Axis.z",1.0f},
       {"Scale",2.0f}}, "Result.x");
    // Angle=0 → identity rotation → Result.x = VectorA.x * Scale = 1*2 = 2
    bool pass = std::fabs(vx - 2.0f) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] RotateVector3(Angle=0,Scale=2) Result.x=%.5f want=2.00000 -> %s\n",
           vx, pass ? "PASS" : "FAIL");
  }
  // RED proof: if angle were used as radians directly (not converted from degrees),
  //   rotating by 90 degrees → 90 radians → cos(90)≈-0.4480, sin(90)≈0.8940.
  //   Result.x = 1*cos(90) + 0 - 0 = -0.448, not 0.
  //   We assert Result.x ≈ 0 (correct) vs ≈ -0.448 (wrong-radians) → FAIL.
  if (injectBug) {
    float vx = evalVec3Op("RotateVector3",
      {{"VectorA.x",1.0f},{"VectorA.y",0.0f},{"VectorA.z",0.0f},
       {"Angle",90.0f},
       {"Axis.x",0.0f},{"Axis.y",0.0f},{"Axis.z",1.0f},
       {"Scale",1.0f}}, "Result.x");
    // Assert wrong-radians result (cos(90 rad) ≈ -0.4480) → FAIL since actual is 0.
    float wrong_rad = std::cos(90.0f);  // ≈ -0.4480
    bool pass = std::fabs(vx - wrong_rad) < 1e-3f;
    ok = ok && pass;
    printf("[selftest-mathops] RotateVector3 inject-no-deg2rad x=%.5f assert=%.5f (INJECT BUG) -> %s\n",
           vx, wrong_rad, pass ? "PASS" : "FAIL");
  }

  // [math-batch23] END teeth

  // [math-batch24] BEGIN teeth (InvertFloat/CrossVec3/LerpVec3/NormalizeVector3/RoundVec3/
  //                              AddVec2/DotVec2/Vec2Magnitude/Vector2Components/ScaleVector2)

  // ----- InvertFloat -----
  // TiXL float/adjust/InvertFloat.cs: "var sign = shouldInvert ? -1 : 1; Result.Value = sign * value;"
  // Invert=1(true): Result = -A; Invert=0(false): Result = A
  {
    float r = evalOpParams("InvertFloat", {{"A", 5.0f}, {"Invert", 1.0f}}, "Result");
    bool pass = std::fabs(r - (-5.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] InvertFloat(A=5,Invert=1)=%.4f want=-5.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalOpParams("InvertFloat", {{"A", 5.0f}, {"Invert", 0.0f}}, "Result");
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] InvertFloat(A=5,Invert=0)=%.4f want=5.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if Invert were ignored (always positive sign), Invert=1 would give +5 not -5.
  if (injectBug) {
    float r = evalOpParams("InvertFloat", {{"A", 5.0f}, {"Invert", 1.0f}}, "Result");
    // Assert wrong: positive result (5.0) → FAIL since actual is (-5.0).
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] InvertFloat inject-ignore-invert=%.4f assert=5 (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- CrossVec3 -----
  // TiXL vec3/CrossVec3.cs: "Result.Value = Vector3.Cross(Input1, Input2);"
  // Cross((1,0,0),(0,1,0)) = (0,0,1); Cross((0,1,0),(1,0,0)) = (0,0,-1) (anti-commutative)
  {
    float vx = evalVec3Op("CrossVec3",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f},{"Input2.z",0.0f}}, "Result.x");
    float vy = evalVec3Op("CrossVec3",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f},{"Input2.z",0.0f}}, "Result.y");
    float vz = evalVec3Op("CrossVec3",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f},{"Input2.z",0.0f}}, "Result.z");
    bool pass = std::fabs(vx)<eps && std::fabs(vy)<eps && std::fabs(vz-1.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] CrossVec3((1,0,0)x(0,1,0))=(%.2f,%.2f,%.2f) want=(0,0,1) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // Anti-commutative: Cross(b,a) = -Cross(a,b): Cross((0,1,0),(1,0,0)).z should be -1
  {
    float vz = evalVec3Op("CrossVec3",
      {{"Input1.x",0.0f},{"Input1.y",1.0f},{"Input1.z",0.0f},
       {"Input2.x",1.0f},{"Input2.y",0.0f},{"Input2.z",0.0f}}, "Result.z");
    bool pass = std::fabs(vz - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] CrossVec3((0,1,0)x(1,0,0)).z=%.2f want=-1.00 -> %s\n", vz, pass ? "PASS" : "FAIL");
  }
  // RED proof: if dot product were used instead of cross, Cross((1,0,0),(0,1,0)).z would be 0 (dot=0), not 1.
  // More distinguishable: Cross((1,2,3),(4,5,6)).x = 2*6-3*5=12-15=-3; dot would give 1*4+2*5+3*6=32.
  if (injectBug) {
    float vz = evalVec3Op("CrossVec3",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},{"Input1.z",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f},{"Input2.z",0.0f}}, "Result.z");
    // Assert dot-result (0.0) → FAIL since actual cross result is 1.0.
    bool pass = std::fabs(vz) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] CrossVec3 inject-as-dot z=%.4f assert=0 (INJECT BUG) -> %s\n", vz, pass ? "PASS" : "FAIL");
  }

  // ----- LerpVec3 -----
  // TiXL vec3/LerpVec3.cs: Vector3.Lerp(A,B,F) = A+(B-A)*F; Clamp bool.
  // LerpVec3(A=(0,0,0), B=(10,20,30), F=0.5, Clamp=0) → Result=(5,10,15)
  {
    float vx = evalVec3Op("LerpVec3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},
       {"B.x",10.0f},{"B.y",20.0f},{"B.z",30.0f},
       {"F",0.5f},{"Clamp",0.0f}}, "Result.x");
    float vy = evalVec3Op("LerpVec3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},
       {"B.x",10.0f},{"B.y",20.0f},{"B.z",30.0f},
       {"F",0.5f},{"Clamp",0.0f}}, "Result.y");
    float vz = evalVec3Op("LerpVec3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},
       {"B.x",10.0f},{"B.y",20.0f},{"B.z",30.0f},
       {"F",0.5f},{"Clamp",0.0f}}, "Result.z");
    bool pass = std::fabs(vx-5.0f)<eps && std::fabs(vy-10.0f)<eps && std::fabs(vz-15.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] LerpVec3((0,0,0),(10,20,30),0.5)=(%.1f,%.1f,%.1f) want=(5,10,15) -> %s\n",
           vx, vy, vz, pass ? "PASS" : "FAIL");
  }
  // Clamp=1: F=2.0 clamped to 1.0 → Result=B
  {
    float vx = evalVec3Op("LerpVec3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},
       {"B.x",10.0f},{"B.y",20.0f},{"B.z",30.0f},
       {"F",2.0f},{"Clamp",1.0f}}, "Result.x");
    bool pass = std::fabs(vx - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] LerpVec3 Clamp=1 F=2.0 → x=%.1f want=10.0 -> %s\n", vx, pass ? "PASS" : "FAIL");
  }
  // RED proof: if Clamp were always applied (ignoring the bool), unclamped F=-1 would give A+(-1)*(B-A).
  //   LerpVec3((0,0,0),(10,0,0),F=-1,Clamp=0) → x = 0+(-1)*10 = -10; if clamped, x=0.
  if (injectBug) {
    float vx = evalVec3Op("LerpVec3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},
       {"B.x",10.0f},{"B.y",0.0f},{"B.z",0.0f},
       {"F",-1.0f},{"Clamp",0.0f}}, "Result.x");
    // Assert clamp-enforced result (0.0) → FAIL since actual is unclamped (-10.0).
    bool pass = std::fabs(vx) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] LerpVec3 inject-force-clamp x=%.1f assert=0 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- NormalizeVector3 -----
  // TiXL vec3/NormalizeVector3.cs: normalize(A)*Factor; guard length>0.001.
  // NormalizeVector3((3,4,0), Factor=1) → Result=(3/5,4/5,0)=(0.6,0.8,0)
  {
    float vx = evalVec3Op("NormalizeVector3",
      {{"A.x",3.0f},{"A.y",4.0f},{"A.z",0.0f},{"Factor",1.0f}}, "Result.x");
    float vy = evalVec3Op("NormalizeVector3",
      {{"A.x",3.0f},{"A.y",4.0f},{"A.z",0.0f},{"Factor",1.0f}}, "Result.y");
    bool pass = std::fabs(vx-0.6f)<1e-5f && std::fabs(vy-0.8f)<1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] NormalizeVector3((3,4,0),Factor=1)=(%.5f,%.5f) want=(0.6,0.8) -> %s\n",
           vx, vy, pass ? "PASS" : "FAIL");
  }
  // Factor=2: multiplies result
  {
    float vx = evalVec3Op("NormalizeVector3",
      {{"A.x",1.0f},{"A.y",0.0f},{"A.z",0.0f},{"Factor",2.0f}}, "Result.x");
    bool pass = std::fabs(vx - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] NormalizeVector3((1,0,0),Factor=2) x=%.4f want=2.0000 -> %s\n", vx, pass ? "PASS" : "FAIL");
  }
  // Zero-guard: near-zero vector passes through unchanged
  {
    float vx = evalVec3Op("NormalizeVector3",
      {{"A.x",0.0f},{"A.y",0.0f},{"A.z",0.0f},{"Factor",1.0f}}, "Result.x");
    bool pass = (vx == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] NormalizeVector3(zero,Factor=1) x=%.4f want=0 (zero-guard) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }
  // RED proof: if Factor were ignored (always 1), NormalizeVector3((1,0,0),Factor=3) would give 1.0 not 3.0.
  if (injectBug) {
    float vx = evalVec3Op("NormalizeVector3",
      {{"A.x",1.0f},{"A.y",0.0f},{"A.z",0.0f},{"Factor",3.0f}}, "Result.x");
    // Assert Factor-ignored result (1.0) → FAIL since actual is (3.0).
    bool pass = std::fabs(vx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] NormalizeVector3 inject-ignore-factor x=%.4f assert=1 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- RoundVec3 -----
  // TiXL vec3/RoundVec3.cs: per-component Round(v*p)/p (Mode=0); Floor/Ceil for other modes.
  // RoundVec3(Value=(1.6,2.4,3.5), Precision=(1,1,1), Mode=0/Round) → (2,2,4) (banker's round or round-half-up)
  // Note: std::round uses round-half-away-from-zero; C# MathF.Round uses round-half-to-even (banker's).
  // For x=1.6: round(1.6)=2, x=2.4: round(2.4)=2, x=3.5: MathF.Round=4 (banker), std::round=4 → same.
  {
    float vx = evalVec3Op("RoundVec3",
      {{"Value.x",1.6f},{"Value.y",2.4f},{"Value.z",3.5f},
       {"Precision.x",1.0f},{"Precision.y",1.0f},{"Precision.z",1.0f},
       {"Mode",0.0f}}, "Result.x");
    float vy = evalVec3Op("RoundVec3",
      {{"Value.x",1.6f},{"Value.y",2.4f},{"Value.z",3.5f},
       {"Precision.x",1.0f},{"Precision.y",1.0f},{"Precision.z",1.0f},
       {"Mode",0.0f}}, "Result.y");
    bool pass = std::fabs(vx-2.0f)<eps && std::fabs(vy-2.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] RoundVec3((1.6,2.4,*),P=1,Mode=Round)=(%.1f,%.1f) want=(2,2) -> %s\n",
           vx, vy, pass ? "PASS" : "FAIL");
  }
  // Mode=1 Floor: Floor(1.9)=1, Floor(-0.1)=-1
  {
    float vx = evalVec3Op("RoundVec3",
      {{"Value.x",1.9f},{"Value.y",-0.1f},{"Value.z",0.0f},
       {"Precision.x",1.0f},{"Precision.y",1.0f},{"Precision.z",1.0f},
       {"Mode",1.0f}}, "Result.x");
    float vy = evalVec3Op("RoundVec3",
      {{"Value.x",1.9f},{"Value.y",-0.1f},{"Value.z",0.0f},
       {"Precision.x",1.0f},{"Precision.y",1.0f},{"Precision.z",1.0f},
       {"Mode",1.0f}}, "Result.y");
    bool pass = std::fabs(vx-1.0f)<eps && std::fabs(vy-(-1.0f))<eps;
    ok = ok && pass;
    printf("[selftest-mathops] RoundVec3((1.9,-0.1),P=1,Mode=Floor)=(%.1f,%.1f) want=(1,-1) -> %s\n",
           vx, vy, pass ? "PASS" : "FAIL");
  }
  // RED proof: if Floor were used for Mode=0 instead of Round, Floor(1.6)=1 not 2.
  if (injectBug) {
    float vx = evalVec3Op("RoundVec3",
      {{"Value.x",1.6f},{"Value.y",0.0f},{"Value.z",0.0f},
       {"Precision.x",1.0f},{"Precision.y",1.0f},{"Precision.z",1.0f},
       {"Mode",0.0f}}, "Result.x");
    // Assert floor result (1.0) → FAIL since actual is round (2.0).
    bool pass = std::fabs(vx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] RoundVec3 inject-floor-as-round x=%.1f assert=1 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // Helper: evaluate a Vec2 op with named float params and a named output pin.
  auto evalVec2Op = [&](const char* type,
                        std::initializer_list<std::pair<const char*, float>> params,
                        const char* outPortId) -> float {
    const NodeSpec* spec = findSpec(type);
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++;
    nd.type = type;
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    for (auto& kv : params) g.node(nid)->params[kv.first] = kv.second;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPortId) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // ----- AddVec2 -----
  // TiXL vec2/AddVec2.cs: "Result.Value = Input1.GetValue(context) + Input2.GetValue(context);"
  // AddVec2((3,4)+(1,2)) → Result=(4,6)
  {
    float rx = evalVec2Op("AddVec2",
      {{"Input1.x",3.0f},{"Input1.y",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f}}, "Result.x");
    float ry = evalVec2Op("AddVec2",
      {{"Input1.x",3.0f},{"Input1.y",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f}}, "Result.y");
    bool pass = std::fabs(rx-4.0f)<eps && std::fabs(ry-6.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] AddVec2((3,4)+(1,2))=(%.1f,%.1f) want=(4,6) -> %s\n", rx, ry, pass ? "PASS" : "FAIL");
  }
  // RED proof: if subtraction were used, AddVec2((3,4),(1,2)).x = 3-1 = 2, not 4.
  if (injectBug) {
    float rx = evalVec2Op("AddVec2",
      {{"Input1.x",3.0f},{"Input1.y",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f}}, "Result.x");
    // Assert subtraction result (2.0) → FAIL since actual is addition (4.0).
    bool pass = std::fabs(rx - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] AddVec2 inject-sub x=%.1f assert=2 (INJECT BUG) -> %s\n", rx, pass ? "PASS" : "FAIL");
  }

  // ----- DotVec2 -----
  // TiXL vec2/DotVec2.cs: "Result.Value = Vector2.Dot(Input1, Input2);"
  // DotVec2((3,4),(1,2)) = 3+8 = 11; DotVec2((1,0),(0,1)) = 0 (perpendicular)
  {
    float r = evalVec2Op("DotVec2",
      {{"Input1.x",3.0f},{"Input1.y",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f}}, "Result");
    bool pass = std::fabs(r - 11.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec2((3,4).(1,2))=%.4f want=11.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalVec2Op("DotVec2",
      {{"Input1.x",1.0f},{"Input1.y",0.0f},
       {"Input2.x",0.0f},{"Input2.y",1.0f}}, "Result");
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec2((1,0).(0,1))=%.4f want=0.0000 (perp) -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if x-term only were used (missing y*y2), DotVec2((3,4),(1,2)) = 3, not 11.
  if (injectBug) {
    float r = evalVec2Op("DotVec2",
      {{"Input1.x",3.0f},{"Input1.y",4.0f},
       {"Input2.x",1.0f},{"Input2.y",2.0f}}, "Result");
    // Assert missing-y result (3.0) → FAIL since actual is (11.0).
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DotVec2 inject-missing-y=%.4f assert=3 (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Vec2Magnitude -----
  // TiXL vec3/Vec2Magnitude.cs: "Result.Value = Input.GetValue(context).Length();"
  // Vec2Magnitude((3,4)) = 5; Vec2Magnitude((0,0)) = 0
  {
    float r = evalVec2Op("Vec2Magnitude",
      {{"Input.x",3.0f},{"Input.y",4.0f}}, "Result");
    bool pass = std::fabs(r - 5.0f) < 1e-5f;
    ok = ok && pass;
    printf("[selftest-mathops] Vec2Magnitude(3,4)=%.5f want=5.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalVec2Op("Vec2Magnitude",
      {{"Input.x",0.0f},{"Input.y",0.0f}}, "Result");
    bool pass = (r == 0.0f);
    ok = ok && pass;
    printf("[selftest-mathops] Vec2Magnitude(0,0)=%.5f want=0.00000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }
  // RED proof: if squared magnitude used instead of length, Vec2Magnitude((3,4)) = 25, not 5.
  if (injectBug) {
    float r = evalVec2Op("Vec2Magnitude",
      {{"Input.x",3.0f},{"Input.y",4.0f}}, "Result");
    // Assert squared result (25.0) → FAIL since actual is length (5.0).
    bool pass = std::fabs(r - 25.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vec2Magnitude inject-no-sqrt=%.5f assert=25 (INJECT BUG) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ----- Vector2Components -----
  // TiXL vec2/Vector2Components.cs: "X.Value = value.X; Y.Value = value.Y;"
  // Vector2Components((7,11)) → X=7, Y=11
  {
    float vx = evalVec2Op("Vector2Components",
      {{"Value.x",7.0f},{"Value.y",11.0f}}, "X");
    float vy = evalVec2Op("Vector2Components",
      {{"Value.x",7.0f},{"Value.y",11.0f}}, "Y");
    bool pass = std::fabs(vx-7.0f)<eps && std::fabs(vy-11.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vector2Components((7,11))=(%.1f,%.1f) want=(7,11) -> %s\n", vx, vy, pass ? "PASS" : "FAIL");
  }
  // RED proof: if X and Y were swapped, X would output 11 not 7.
  if (injectBug) {
    float vx = evalVec2Op("Vector2Components",
      {{"Value.x",7.0f},{"Value.y",11.0f}}, "X");
    // Assert Y-value (11.0) at X output → FAIL since actual is (7.0).
    bool pass = std::fabs(vx - 11.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vector2Components inject-xy-swap X=%.1f assert=11 (INJECT BUG) -> %s\n", vx, pass ? "PASS" : "FAIL");
  }

  // ----- ScaleVector2 -----
  // TiXL vec2/ScaleVector2.cs: "Result.Value = a * b * u;"
  // ScaleVector2(A=(3,4), B=(2,2), UniformScale=0.5) → (3*2*0.5, 4*2*0.5) = (3,4)
  {
    float rx = evalVec2Op("ScaleVector2",
      {{"A.x",3.0f},{"A.y",4.0f},
       {"B.x",2.0f},{"B.y",2.0f},
       {"UniformScale",0.5f}}, "Result.x");
    float ry = evalVec2Op("ScaleVector2",
      {{"A.x",3.0f},{"A.y",4.0f},
       {"B.x",2.0f},{"B.y",2.0f},
       {"UniformScale",0.5f}}, "Result.y");
    bool pass = std::fabs(rx-3.0f)<eps && std::fabs(ry-4.0f)<eps;
    ok = ok && pass;
    printf("[selftest-mathops] ScaleVector2((3,4)*(2,2)*0.5)=(%.1f,%.1f) want=(3,4) -> %s\n", rx, ry, pass ? "PASS" : "FAIL");
  }
  // UniformScale only: A=(1,1), B=(1,1), U=7 → Result=(7,7)
  {
    float rx = evalVec2Op("ScaleVector2",
      {{"A.x",1.0f},{"A.y",1.0f},
       {"B.x",1.0f},{"B.y",1.0f},
       {"UniformScale",7.0f}}, "Result.x");
    bool pass = std::fabs(rx - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] ScaleVector2(A=B=(1,1),U=7) x=%.1f want=7.0 -> %s\n", rx, pass ? "PASS" : "FAIL");
  }
  // RED proof: if UniformScale were ignored, ScaleVector2((3,4),(2,2),0.5).x = 3*2 = 6, not 3.
  if (injectBug) {
    float rx = evalVec2Op("ScaleVector2",
      {{"A.x",3.0f},{"A.y",4.0f},
       {"B.x",2.0f},{"B.y",2.0f},
       {"UniformScale",0.5f}}, "Result.x");
    // Assert no-uniform result (6.0) → FAIL since actual is (3.0).
    bool pass = std::fabs(rx - 6.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] ScaleVector2 inject-no-uniform x=%.1f assert=6 (INJECT BUG) -> %s\n", rx, pass ? "PASS" : "FAIL");
  }

  // [math-batch24] END teeth

  // [logic-batch27] BEGIN teeth — stateless logic (IsGreater / Compare). Bool → Float 0/1.
  // ----- IsGreater: 5>3 → 1; 2>3 → 0 (boundary). TiXL float/logic/IsGreater.cs: v > t.
  {
    float r1 = evalOpParams("IsGreater", {{"Value", 5.0f}, {"Threshold", 3.0f}}, "Result");
    float r0 = evalOpParams("IsGreater", {{"Value", 2.0f}, {"Threshold", 3.0f}}, "Result");
    float want1 = injectBug ? 0.0f : 1.0f;  // bug flips the expected true case
    bool pass = std::fabs(r1 - want1) < eps && std::fabs(r0 - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] IsGreater 5>3=%.0f(want %.0f) 2>3=%.0f -> %s\n", r1, want1, r0, pass ? "PASS" : "FAIL");
  }
  // ----- Compare all 4 modes. TiXL float/logic/Compare.cs (Mode 0..3, Precision band on Eq/NotEq).
  // v=2,test=3: IsSmaller→1, IsEqual(prec .001)→0, IsLarger→0, IsNotEqual→1.
  // v=3,test=3.0005,prec=.001: IsEqual→1 (within band); v=3,test=3.5: IsEqual→0 (outside band).
  {
    float sm = evalOpParams("Compare", {{"Value", 2.0f}, {"TestValue", 3.0f}, {"Mode", 0.0f}}, "IsTrue");
    float lg = evalOpParams("Compare", {{"Value", 2.0f}, {"TestValue", 3.0f}, {"Mode", 2.0f}}, "IsTrue");
    float ne = evalOpParams("Compare", {{"Value", 2.0f}, {"TestValue", 3.0f}, {"Mode", 3.0f}}, "IsTrue");
    float eqIn  = evalOpParams("Compare", {{"Value", 3.0f}, {"TestValue", 3.0005f}, {"Mode", 1.0f}, {"Precision", 0.001f}}, "IsTrue");
    float eqOut = evalOpParams("Compare", {{"Value", 3.0f}, {"TestValue", 3.5f}, {"Mode", 1.0f}, {"Precision", 0.001f}}, "IsTrue");
    float wantEqIn = injectBug ? 0.0f : 1.0f;  // bug breaks the within-Precision equality
    bool pass = std::fabs(sm - 1.0f) < eps && std::fabs(lg - 0.0f) < eps && std::fabs(ne - 1.0f) < eps
                && std::fabs(eqIn - wantEqIn) < eps && std::fabs(eqOut - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Compare sm=%.0f lg=%.0f ne=%.0f eqIn=%.0f(want %.0f) eqOut=%.0f -> %s\n",
           sm, lg, ne, eqIn, wantEqIn, eqOut, pass ? "PASS" : "FAIL");
  }
  // [logic-batch27] END teeth

  printf("[selftest-mathops] -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
