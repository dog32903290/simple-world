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

  printf("[selftest-mathops] -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
