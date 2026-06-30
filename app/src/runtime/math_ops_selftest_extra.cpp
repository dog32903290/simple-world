// Selftest leaf split out of math_ops_selftest.cpp (the file was AT its line-count cap, blocking
// the scalar Lerp/Remap coverage additions). This holds the vec2 + stateless-logic + misc-vec
// teeth ([math-batch24] AddVec2…ScaleVector2, [logic-batch27] IsGreater/Compare, [vec-batch31/32]
// DivideVector2/Vec2ToVec3/EulerToAxisAngle/PadVec2Range/RemapVec2). Moved VERBATIM — zero behavior
// change; proven by --selftest-mathops (green) + --selftest-mathops-bug (every inject tooth still bites).
// Driven from runMathOpsSelfTest() so the single "mathops" selftest name is preserved.
//
// Zone: runtime (pure value, no GPU, no platform deps). Leaf.
#include "runtime/graph.h"    // Graph, Node, findSpec, evalFloat, pinId
#include "runtime/Particle.h" // EvaluationContext

#include "runtime/math_ops_selftest_util.h"  // shared evalOpParams

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace sw {

// Vec2/logic/misc-vec teeth. Returns 0 on all-pass, 1 on any failure (own ok accumulator).
int runMathOpsExtraSelfTest(bool injectBug) {
  bool ok = true;
  float eps = 1e-5f;

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

  // [vec-batch31] BEGIN teeth — clean stateless vec value ops.
  // DivideVector2: A=(6,8) B=(2,4) U=2 → (3,4)/2 = (1.5,1.0); B.x=0 → Result.x guard 0.
  {
    float rx = evalOpParams("DivideVector2", {{"A.x", 6.0f}, {"A.y", 8.0f}, {"B.x", 2.0f}, {"B.y", 4.0f}, {"UniformScale", 2.0f}}, "Result.x");
    float ry = evalOpParams("DivideVector2", {{"A.x", 6.0f}, {"A.y", 8.0f}, {"B.x", 2.0f}, {"B.y", 4.0f}, {"UniformScale", 2.0f}}, "Result.y");
    float rz = evalOpParams("DivideVector2", {{"A.x", 6.0f}, {"B.x", 0.0f}, {"UniformScale", 2.0f}}, "Result.x");  // div-by-zero guard
    float wx = injectBug ? 3.0f : 1.5f;  // bug: forgot the /UniformScale
    bool pass = std::fabs(rx - wx) < eps && std::fabs(ry - 1.0f) < eps && std::fabs(rz - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] DivideVector2 x=%.2f(want %.2f) y=%.2f z(b=0)=%.1f -> %s\n", rx, wx, ry, rz, pass ? "PASS" : "FAIL");
  }
  // Vec2ToVec3: XY=(3,5) Z=7 → (3,5,7).
  {
    float x = evalOpParams("Vec2ToVec3", {{"XY.x", 3.0f}, {"XY.y", 5.0f}, {"Z", 7.0f}}, "Result.x");
    float y = evalOpParams("Vec2ToVec3", {{"XY.x", 3.0f}, {"XY.y", 5.0f}, {"Z", 7.0f}}, "Result.y");
    float z = evalOpParams("Vec2ToVec3", {{"XY.x", 3.0f}, {"XY.y", 5.0f}, {"Z", 7.0f}}, "Result.z");
    float wz = injectBug ? 0.0f : 7.0f;  // bug: drops Z
    bool pass = std::fabs(x - 3.0f) < eps && std::fabs(y - 5.0f) < eps && std::fabs(z - wz) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] Vec2ToVec3 (%.0f,%.0f,%.0f) wantZ=%.0f -> %s\n", x, y, z, wz, pass ? "PASS" : "FAIL");
  }
  // EulerToAxisAngle: (0,0,0)→ Axis(1,0,0) Angle 0 (gimbal guard); (PI/2,0,0)→ Axis(0,1,0) Angle PI/2.
  {
    const float HALF_PI = 1.57079633f;
    float ax0 = evalOpParams("EulerToAxisAngle", {{"Rotation.x", 0.0f}, {"Rotation.y", 0.0f}, {"Rotation.z", 0.0f}}, "Axis.x");
    float an0 = evalOpParams("EulerToAxisAngle", {{"Rotation.x", 0.0f}, {"Rotation.y", 0.0f}, {"Rotation.z", 0.0f}}, "Angle");
    float ay1 = evalOpParams("EulerToAxisAngle", {{"Rotation.x", HALF_PI}, {"Rotation.y", 0.0f}, {"Rotation.z", 0.0f}}, "Axis.y");
    float an1 = evalOpParams("EulerToAxisAngle", {{"Rotation.x", HALF_PI}, {"Rotation.y", 0.0f}, {"Rotation.z", 0.0f}}, "Angle");
    float way = injectBug ? 0.0f : 1.0f;  // bug: axis not normalized / wrong component
    bool pass = std::fabs(ax0 - 1.0f) < 1e-3f && std::fabs(an0 - 0.0f) < 1e-3f
                && std::fabs(ay1 - way) < 1e-3f && std::fabs(an1 - HALF_PI) < 1e-3f;
    ok = ok && pass;
    printf("[selftest-mathops] EulerToAxisAngle zero(ax=%.2f,an=%.2f) halfpiX(ay=%.2f want %.2f,an=%.3f) -> %s\n", ax0, an0, ay1, way, an1, pass ? "PASS" : "FAIL");
  }
  // [vec-batch31] END teeth

  // PadVec2Range: A=(0,10) U=2 → ±2 about center 5 → (-5,15). A=(4,6) ClampMinExtend=3 → (2,8).
  {
    auto pad = [&](float ax, float ay, float u, float ext, const char* out) {
      return evalOpParams("PadVec2Range", {{"A.x", ax}, {"A.y", ay}, {"UniformScale", u}, {"ClampMinExtend", ext}}, out);
    };
    float x2 = pad(0.0f, 10.0f, 2.0f, 0.0f, "Result.x");
    float y2 = pad(0.0f, 10.0f, 2.0f, 0.0f, "Result.y");
    float xe = pad(4.0f, 6.0f, 1.0f, 3.0f, "Result.x");
    float ye = pad(4.0f, 6.0f, 1.0f, 3.0f, "Result.y");
    float wx = injectBug ? 0.0f : -5.0f;  // bug: dropped UniformScale
    bool pass = std::fabs(x2 - wx) < eps && std::fabs(y2 - 15.0f) < eps && std::fabs(xe - 2.0f) < eps && std::fabs(ye - 8.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] PadVec2Range scale2=(%.1f,%.1f)(wantx %.1f) extend=(%.1f,%.1f) -> %s\n", x2, y2, wx, xe, ye, pass ? "PASS" : "FAIL");
  }
  // [vec-batch32] RemapVec2 x-component: in(0,10) out(0,100). Normal:5→50; Clamped:20→100; Modulo:15→50.
  {
    auto rmx = [&](float v, float mode) {
      return evalOpParams("RemapVec2", {{"Value.x", v}, {"RangeInMin.x", 0.0f}, {"RangeInMax.x", 10.0f},
                                        {"RangeOutMin.x", 0.0f}, {"RangeOutMax.x", 100.0f}, {"Mode", mode}}, "Result.x");
    };
    float nrm = rmx(5.0f, 0.0f), clp = rmx(20.0f, 1.0f), mod = rmx(15.0f, 2.0f);
    float wN = injectBug ? 5.0f : 50.0f;  // bug: forgot to scale to out-range
    bool pass = std::fabs(nrm - wN) < eps && std::fabs(clp - 100.0f) < eps && std::fabs(mod - 50.0f) < eps;
    ok = ok && pass;
    printf("[selftest-mathops] RemapVec2 normal=%.1f(want %.1f) clamped=%.1f mod=%.1f -> %s\n", nrm, wN, clp, mod, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
