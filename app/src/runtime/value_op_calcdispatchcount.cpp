// CalcDispatchCount value op (value-op self-registration seam leaf — value-output-rail Phase 1).
// TiXL authority: Operators/Lib/render/_dx11/api/CalcDispatchCount.cs (+ CalcDispatchCount.t3 defaults).
//
//   CalcDispatchCount.cs Update():
//     int count = Count.GetValue(context);
//     Int3 groupSize = ThreadGroupSize.GetValue(context);
//     DispatchCount.Value = (groupSize.X > 0) ? new Int3(count / groupSize.X + 1, 1, 1) : Int3.Zero;
//
//   Ports (from CalcDispatchCount.cs field order):
//     ThreadGroupSize = InputSlot<Int3>  (CalcDispatchCount.cs line 22)
//     Count           = InputSlot<int>   (CalcDispatchCount.cs line 25)
//     Output: DispatchCount = Slot<Int3> (CalcDispatchCount.cs line 7)
//
//   CalcDispatchCount.t3 DefaultValues: ThreadGroupSize = {X:1, Y:1, Z:1}, Count = 0.
//
// This is a PURE op (no cook context): the whole result is a function of its two inputs. It
// therefore rides the existing pure-evaluate ValueOp leaf seam (like MakeResolution / Int2Components)
// — it does NOT need the extOut cook-emit pass. The value-output-rail "N scalar Float output ports"
// fork still applies to its Int3 output (3 Float ports), but no cook plumbing is involved.
//
// EVAL-SIDE LAYOUT (multi-output, mirrors AddVec3 / MakeResolution convention):
//   in[] = [ThreadGroupSize.X, ThreadGroupSize.Y, ThreadGroupSize.Z, Count]  (n=4 scalar inputs).
//   Output ports (Result.x, Result.y, Result.z) are at spec indices n, n+1, n+2.
//   Component k = outIdx - n ∈ {0=x, 1=y, 2=z}.
//
// ★ PARITY CORRECTION (load-bearing — read before trusting any "ceil per component" framing):
//   TiXL does NOT compute a per-component ceil(N/groupSize). The .cs source uses ONLY:
//     • Count (a SCALAR int — there is no Count.Y / Count.Z), and
//     • ThreadGroupSize.X (Y and Z of the group are IGNORED).
//   The result Y and Z are HARD-CODED to 1. And the division is integer floor + 1, NOT ceil:
//     X = count / groupSize.X + 1   (C# int division truncates → floor(count/gx) + 1)
//   So for Count=100, group=(8,8,1): X = 100/8 + 1 = 12 + 1 = 13, Y = 1, Z = 1  → (13, 1, 1).
//   (NOT (13, 7, 1) — that would require a per-component ceil TiXL never does. The golden below
//   is hand-derived from the .cs source, not from the plan's per-component framing.)
//
// FORKS (named):
//   - fork-vec-output-as-n-scalar-ports: TiXL wires one Slot<Int3>; this runtime wires 3 Float
//     output ports (Result.x/.y/.z). Faithful in VALUE (same numbers), forked in wire-CARDINALITY.
//     Extends the shipped input-side scalar-pack fork (AddVec3 / MakeResolution). NOT an eval fork.
//   - fork-calcdispatchcount-int-on-float-port: TiXL Count is InputSlot<int>, ThreadGroupSize is
//     InputSlot<Int3> (all ints). This runtime stores them as Float; each is (int)-truncated before
//     the integer division, matching TiXL's int-slot behaviour for whole-number inputs.
//   - fork-calcdispatchcount-floor-plus-one: the dispatch count is floor(Count/gx)+1, NOT ceil.
//     This is TiXL's literal source (CalcDispatchCount.cs:18). Faithful, named because it is a
//     surprising off-by-one vs a "correct" ceil — we clone TiXL, not the textbook.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest.
int runCalcDispatchCountSelfTest(bool injectBug);

namespace {

// in[] = [ThreadGroupSize.X, ThreadGroupSize.Y, ThreadGroupSize.Z, Count]  (n=4).
// DispatchCount = (gx > 0) ? Int3(count/gx + 1, 1, 1) : Int3.Zero   (CalcDispatchCount.cs:18 verbatim).
// fork-calcdispatchcount-int-on-float-port: inputs (int)-truncated before the integer division.
// fork-calcdispatchcount-floor-plus-one: count/gx is C# int division (floor), then +1.
float evalCalcDispatchCountOp(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  const int gx = (int)in[0];  // ThreadGroupSize.X (the ONLY group component TiXL reads)
  const int count = (int)in[3];
  const int k = outIdx - n;  // output component: 0=x, 1=y, 2=z
  if (gx <= 0) return 0.0f;   // groupSize.X <= 0 → Int3.Zero (all components 0)
  switch (k) {
    case 0: return (float)(count / gx + 1);  // floor(count/gx) + 1 (TiXL int div + 1)
    case 1: return 1.0f;                      // Y hard-coded to 1
    case 2: return 1.0f;                      // Z hard-coded to 1
    default: return 0.0f;
  }
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_calcdispatchcount{
    // CalcDispatchCount (TiXL Lib.render._dx11.api.CalcDispatchCount):
    //   DispatchCount = (gx>0) ? Int3(count/gx + 1, 1, 1) : Int3.Zero.
    // Port order MUST match evalCalcDispatchCountOp's in[] read:
    //   ThreadGroupSize.X/Y/Z, Count (inputs); then Result.x, Result.y, Result.z (outputs).
    // Defaults from CalcDispatchCount.t3: ThreadGroupSize = {1,1,1}, Count = 0.
    {"CalcDispatchCount", "CalcDispatchCount",
     {{"ThreadGroupSize.X", "ThreadGroupSize",   "Float", true, 1.0f, 0.0f, 65535.0f, Widget::Vec, {}, false, 3},
      {"ThreadGroupSize.Y", "ThreadGroupSize.Y", "Float", true, 1.0f, 0.0f, 65535.0f, Widget::Vec, {}, false, 1},
      {"ThreadGroupSize.Z", "ThreadGroupSize.Z", "Float", true, 1.0f, 0.0f, 65535.0f, Widget::Vec, {}, false, 1},
      {"Count",             "Count",             "Float", true, 0.0f, 0.0f, 16777216.0f},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalCalcDispatchCountOp},
    "calcdispatchcount", runCalcDispatchCountSelfTest};

// --- CalcDispatchCount MATH golden -------------------------------------------------------------
// Builds a 1-node CalcDispatchCount graph, sets ThreadGroupSize {X,Y,Z} + Count, pulls each of the
// 3 output pins (Result.x/.y/.z) via evalFloat (flat path — no multiInput). Compares against the
// hand-derived TiXL formula (CalcDispatchCount.cs:18). injectBug flips the dispatch math to a FLOOR
// (no +1) so Result.x drops by one → RED (the floor-vs-floor-plus-one tooth the plan calls for).
int runCalcDispatchCountSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  auto evalCDC = [&](float gx, float gy, float gz, float count, const char* outName) -> float {
    const NodeSpec* spec = findSpec("CalcDispatchCount");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "CalcDispatchCount";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["ThreadGroupSize.X"] = gx;
    g.node(nid)->params["ThreadGroupSize.Y"] = gy;
    g.node(nid)->params["ThreadGroupSize.Z"] = gz;
    g.node(nid)->params["Count"] = count;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL (the plan's drive values): Count=100, group=(8,8,1).
  //   X = 100/8 + 1 = 12 + 1 = 13  (NOT a per-component ceil — TiXL ignores group.Y/.Z and Count is scalar)
  //   Y = 1, Z = 1  (hard-coded).
  //   → expected (13, 1, 1).
  // injectBug: assert Result.x == 12 (the FLOOR with no +1) → the +1 op makes the real value 13 → RED.
  {
    float rx = evalCDC(8.0f, 8.0f, 1.0f, 100.0f, "Result.x");
    float ry = evalCDC(8.0f, 8.0f, 1.0f, 100.0f, "Result.y");
    float rz = evalCDC(8.0f, 8.0f, 1.0f, 100.0f, "Result.z");
    float wantX = injectBug ? 12.0f : 13.0f;  // bug: claim floor (no +1) → RED (real = 13)
    bool passX = std::fabs(rx - wantX) < eps;
    bool passY = std::fabs(ry - 1.0f) < eps;
    bool passZ = std::fabs(rz - 1.0f) < eps;
    ok = ok && passX && passY && passZ;
    printf("[selftest-calcdispatchcount] count=100 group=(8,8,1): (%.0f,%.0f,%.0f) want=(13,1,1)%s -> %s\n",
           rx, ry, rz, injectBug ? " (injectBug→want x=12)" : "",
           (passX && passY && passZ) ? "PASS" : "FAIL");
  }

  // EXACT MULTIPLE: Count=64, group=(8,_,_) → 64/8 + 1 = 8 + 1 = 9 (the floor+1 surprise: a CEIL
  // would give 8, but TiXL adds 1 even on an exact multiple). Pins fork-calcdispatchcount-floor-plus-one.
  {
    float rx = evalCDC(8.0f, 1.0f, 1.0f, 64.0f, "Result.x");
    bool pass = std::fabs(rx - 9.0f) < eps;
    ok = ok && pass;
    printf("[selftest-calcdispatchcount] count=64 group.x=8: x=%.0f want=9 (floor+1, NOT ceil=8) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // ZERO GROUP.X: groupSize.X = 0 → Int3.Zero (all components 0), even with a non-zero Count.
  {
    float rx = evalCDC(0.0f, 8.0f, 1.0f, 100.0f, "Result.x");
    float ry = evalCDC(0.0f, 8.0f, 1.0f, 100.0f, "Result.y");
    float rz = evalCDC(0.0f, 8.0f, 1.0f, 100.0f, "Result.z");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps && std::fabs(rz) < eps;
    ok = ok && pass;
    printf("[selftest-calcdispatchcount] group.x=0: (%.0f,%.0f,%.0f) want=(0,0,0) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // DEFAULTS (t3): group={1,1,1}, Count=0 → 0/1 + 1 = 1 → (1, 1, 1).
  {
    float rx = evalCDC(1.0f, 1.0f, 1.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-calcdispatchcount] t3 defaults (group=1,count=0): x=%.0f want=1 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
