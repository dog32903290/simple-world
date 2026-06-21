// RoundVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/RoundVec3.cs (+ RoundVec3.t3 defaults).
//
//   RoundVec3.cs Update():
//     var precision = Precision.GetValue(context);           (RoundVec3.cs line 17)
//     var v = Value.GetValue(context);                       (RoundVec3.cs line 18)
//     var result = Mode.GetEnumValue<Modes>(context) switch  (RoundVec3.cs line 19)
//       {
//         Modes.Round => new Vector3(
//           MathF.Round(v.X * precision.X) / precision.X,   (RoundVec3.cs line 22)
//           MathF.Round(v.Y * precision.Y) / precision.Y,
//           MathF.Round(v.Z * precision.Z) / precision.Z),
//         Modes.Floor => new Vector3(
//           MathF.Floor(v.X * precision.X) / precision.X,   (RoundVec3.cs line 26)
//           ...),
//         Modes.Ceiling => new Vector3(
//           MathF.Ceiling(v.X * precision.X) / precision.X, (RoundVec3.cs line 31)
//           ...),
//         _ => Vector3.Zero
//       };
//     Result.Value = result;
//
//   Modes enum (RoundVec3.cs lines 42-46): Round=0, Floor=1, Ceiling=2.
//
//   Ports (from RoundVec3.cs field order):
//     Value     = InputSlot<Vector3>  (RoundVec3.cs line 48)
//     Precision = InputSlot<Vector3>  (RoundVec3.cs line 51)
//     Mode      = InputSlot<int> (mapped to Modes enum)  (RoundVec3.cs line 54)
//     Output: Result = Slot<Vector3>  (RoundVec3.cs line 8)
//
//   RoundVec3.t3 DefaultValues: Value = {0,0,0}, Precision = {1,1,1}, Mode = 0 (Round).
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   in[] = [Value.x, Value.y, Value.z, Precision.x, Precision.y, Precision.z, Mode]  (n = 7)
//   Output ports Result.x/.y/.z follow at spec indices 7/8/9.
//   Component k = outIdx - n (0=x, 1=y, 2=z).
//   eval per component: op(Value[k] * Precision[k]) / Precision[k],
//     where op = round/floor/ceil based on Mode.
//
// FORKS (named):
//   - fork-roundvec3-vec3-as-3-floats: each Vector3 input/output is 3 consecutive Float ports.
//   - fork-roundvec3-precision-zero: Precision component == 0 → return 0 (TiXL: NaN from div-by-zero).
//     Safe for GPU/UI, named divergence from TiXL undefined behavior.
//   - fork-roundvec3-mode-enum: Mode is TiXL InputSlot<int> MappedType=typeof(Modes); stored as
//     Float in our runtime, cast to int for switch. Mode=0 Round / 1 Floor / 2 Ceiling.
#include "runtime/graph.h"           // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId
#include "runtime/value_eval_ops.h"  // evalRoundVec3 (defined in value_eval_ops.cpp)

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runRoundVec3SelfTest(bool injectBug);

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_roundvec3{
    // RoundVec3 (TiXL Lib.numbers.vec3.RoundVec3):
    //   Result[k] = op(Value[k]*Precision[k]) / Precision[k], op = Round/Floor/Ceiling per Mode.
    // Port order must match evalRoundVec3's in[] read:
    //   Value.x/y/z, Precision.x/y/z, Mode, Result.x/y/z.
    // Defaults from RoundVec3.t3: Value = {0,0,0}, Precision = {1,1,1}, Mode = 0 (Round).
    {"RoundVec3", "RoundVec3",
     {{"Value.x",     "Value",       "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Value.y",     "Value.y",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Value.z",     "Value.z",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Precision.x", "Precision",   "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 3},
      {"Precision.y", "Precision.y", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
      {"Precision.z", "Precision.z", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
      {"Mode",        "Mode",        "Float", true, 0.0f, 0.0f,    2.0f,   Widget::Enum,
       {"Round", "Floor", "Ceiling"}},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalRoundVec3},
    "roundvec3", runRoundVec3SelfTest};

// --- RoundVec3 MATH golden ---------------------------------------------------------------------
// Builds 1-node RoundVec3 graph, sets components, pulls Result.x/.y/.z via evalFloat.
// Hand-computed from RoundVec3.cs lines 22/26/31: op(v * p) / p for each mode.
// injectBug asserts wrong result.x (Floor applied when Mode=Round) → flips RED.
int runRoundVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalRound = [&](float vx, float vy, float vz,
                       float px, float py, float pz,
                       float mode, const char* outPort) -> float {
    const NodeSpec* spec = findSpec("RoundVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "RoundVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value.x"] = vx; g.node(nid)->params["Value.y"] = vy;
    g.node(nid)->params["Value.z"] = vz;
    g.node(nid)->params["Precision.x"] = px; g.node(nid)->params["Precision.y"] = py;
    g.node(nid)->params["Precision.z"] = pz;
    g.node(nid)->params["Mode"] = mode;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // ROUND (Mode=0): RoundVec3(Value=(1.7, 2.3, 3.5), Precision=(1,1,1)) → (2, 2, 4).
  //   x: round(1.7*1)/1 = round(1.7) = 2; y: round(2.3) = 2; z: round(3.5) = 4 (C++ std::round ties-up).
  // injectBug: claim floor(1.7) = 1 for x → RED.
  {
    float rx = evalRound(1.7f, 2.3f, 3.5f, 1.0f, 1.0f, 1.0f, 0.0f, "Result.x");
    float want = injectBug ? 1.0f : 2.0f;  // bug: floor instead of round → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-roundvec3] Round(1.7,prec=1).x=%.5f want=%.5f (Mode=Round) -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalRound(1.7f, 2.3f, 3.5f, 1.0f, 1.0f, 1.0f, 0.0f, "Result.y");
    bool pass = std::fabs(ry - 2.0f) < eps;  // round(2.3) = 2
    ok = ok && pass;
    printf("[selftest-roundvec3] Round(2.3,prec=1).y=%.5f want=2.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalRound(1.7f, 2.3f, 3.5f, 1.0f, 1.0f, 1.0f, 0.0f, "Result.z");
    bool pass = std::fabs(rz - 4.0f) < eps;  // round(3.5) = 4
    ok = ok && pass;
    printf("[selftest-roundvec3] Round(3.5,prec=1).z=%.5f want=4.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // FLOOR (Mode=1): RoundVec3(Value=(1.9,2.9,-0.1), Precision=(1,1,1), Mode=1) → (1, 2, -1).
  //   x: floor(1.9) = 1; y: floor(2.9) = 2; z: floor(-0.1) = -1.
  {
    float rx = evalRound(1.9f, 2.9f, -0.1f, 1.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-roundvec3] Floor(1.9).x=%.5f want=1.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalRound(1.9f, 2.9f, -0.1f, 1.0f, 1.0f, 1.0f, 1.0f, "Result.z");
    bool pass = std::fabs(rz + 1.0f) < eps;  // floor(-0.1) = -1
    ok = ok && pass;
    printf("[selftest-roundvec3] Floor(-0.1).z=%.5f want=-1.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // CEILING (Mode=2): RoundVec3(Value=(1.1, 2.0, -0.9), Precision=(1,1,1), Mode=2) → (2, 2, 0).
  {
    float rx = evalRound(1.1f, 2.0f, -0.9f, 1.0f, 1.0f, 1.0f, 2.0f, "Result.x");
    bool pass = std::fabs(rx - 2.0f) < eps;  // ceil(1.1) = 2
    ok = ok && pass;
    printf("[selftest-roundvec3] Ceiling(1.1).x=%.5f want=2.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalRound(1.1f, 2.0f, -0.9f, 1.0f, 1.0f, 1.0f, 2.0f, "Result.z");
    bool pass = std::fabs(rz) < eps;  // ceil(-0.9) = 0
    ok = ok && pass;
    printf("[selftest-roundvec3] Ceiling(-0.9).z=%.5f want=0.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // PRECISION: Round(Value=(1.7,0,0), Precision=(10,1,1), Mode=0) → (2.0/10=0.2 wait no:
  //   round(1.7 * 10) / 10 = round(17) / 10 = 17 / 10 = 1.7 (no rounding needed at p=10).
  //   Use p=2: round(1.7 * 2) / 2 = round(3.4) / 2 = 3.0 / 2 = 1.5.
  {
    float rx = evalRound(1.7f, 0.0f, 0.0f, 2.0f, 1.0f, 1.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rx - 1.5f) < eps;  // round(1.7*2)/2 = 3/2 = 1.5
    ok = ok && pass;
    printf("[selftest-roundvec3] Round(1.7,prec=2).x=%.5f want=1.50000 (precision snap) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
