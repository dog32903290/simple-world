// AddInt2 value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/basic/AddInt2.cs (+ AddInt2.t3 defaults).
//
//   AddInt2.cs Update():
//     var s1 = Input1.GetValue(context);
//     var s2 = Input2.GetValue(context);
//     Result.Value = new Int2(s1.Width + s2.Width, s1.Height + s2.Height);
//
//   AddInt2.t3 DefaultValues: Input1 = {X:0, Y:0}, Input2 = {X:0, Y:0}.
//   (TiXL Int2.X = Width component; Int2.Y = Height component.)
//
// 2 Int2 inputs → 1 Int2 output. Each Int2 is decomposed into 2 Float ports
// (Width/Height), so: 4 Float inputs → 2 Float outputs (Result.Width, Result.Height).
//
// EVAL-SIDE LAYOUT (multi-output; mirrors Vector3Components convention):
//   in[] = [Input1.Width, Input1.Height, Input2.Width, Input2.Height]  (n=4 inputs).
//   Output ports (Result.Width, Result.Height) are at spec indices n and n+1.
//   Component k = outIdx - n ∈ {0=Width, 1=Height}.
//
// FORKS (named):
//   - fork-addint2-int-on-float-port: TiXL stores Width/Height as int. This runtime stores all
//     values as Float. The addition is performed via (int) truncation of each input component
//     before adding, then the result is cast back to float. For whole-number inputs (the only
//     values int sliders produce) this is byte-identical to TiXL's Int2 addition. This matches
//     the fork already named for MultiplyInt / IntAdd / ClampInt.
//   - fork-addint2-int2-as-4-floats: TiXL has a native Int2 type. This runtime has only Float
//     ports, so each Int2 is exposed as 2 consecutive Float ports (head Widget::Int2, two slots).
//     NOT an eval fork: the addition is identical; only the data model differs.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest.
int runAddInt2SelfTest(bool injectBug);

namespace {

// in[] = [Input1.Width, Input1.Height, Input2.Width, Input2.Height]  (n=4).
// Result = new Int2(s1.Width + s2.Width, s1.Height + s2.Height)  (TiXL AddInt2.cs verbatim).
// fork-addint2-int-on-float-port: each component is (int)-truncated before addition.
// outIdx - n gives the component: 0=Width, 1=Height.
float evalAddInt2Op(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  const int w1 = (int)in[0], h1 = (int)in[1];  // fork-addint2-int-on-float-port
  const int w2 = (int)in[2], h2 = (int)in[3];
  const int k = outIdx - n;                      // component index: 0=Width, 1=Height
  if (k == 0) return (float)(w1 + w2);
  if (k == 1) return (float)(h1 + h2);
  return 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_addint2{
    // AddInt2 (TiXL Lib.numbers.int2.basic.AddInt2):
    //   Result = new Int2(Input1.Width + Input2.Width, Input1.Height + Input2.Height).
    // Port order MUST match evalAddInt2Op's in[] read:
    //   Input1.Width, Input1.Height, Input2.Width, Input2.Height (inputs); then Result.Width/Height (outputs).
    // Defaults from AddInt2.t3: Input1 = {X:0, Y:0}, Input2 = {X:0, Y:0}.
    {"AddInt2", "AddInt2",
     {{"Input1.Width",  "Input1",        "Float", true,  0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"Input1.Height", "Input1.Height", "Float", true,  0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Input2.Width",  "Input2",        "Float", true,  0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"Input2.Height", "Input2.Height", "Float", true,  0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Result.Width",  "Result.Width",  "Float", false},
      {"Result.Height", "Result.Height", "Float", false}},
     evalAddInt2Op},
    "addint2", runAddInt2SelfTest};

// --- AddInt2 MATH golden -----------------------------------------------------------------------
// Builds a 1-node AddInt2 graph, sets both Int2 component params, pulls each output pin
// (Result.Width / Result.Height) via evalFloat (flat path — AddInt2 has no multiInput, so the
// single-slot-per-port evalFloat is correct). Compares against hand-computed TiXL formula.
// injectBug asserts a wrong expected Width so the typical assertion flips RED.
int runAddInt2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate AddInt2 with explicit components, pulling a named output port.
  auto evalA2 = [&](float w1, float h1, float w2, float h2, const char* outName) -> float {
    const NodeSpec* spec = findSpec("AddInt2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AddInt2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1.Width"]  = w1;
    g.node(nid)->params["Input1.Height"] = h1;
    g.node(nid)->params["Input2.Width"]  = w2;
    g.node(nid)->params["Input2.Height"] = h2;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: (320,240) + (640,480) → Result.Width=960, Result.Height=720.
  // injectBug asserts Width==1000 (wrong answer) so the Width assertion flips RED.
  {
    float rW = evalA2(320.0f, 240.0f, 640.0f, 480.0f, "Result.Width");
    float wantW = injectBug ? 1000.0f : 960.0f;
    bool pass = std::fabs(rW - wantW) < eps;
    ok = ok && pass;
    printf("[selftest-addint2] (320+640).Width=%.0f want=%.0f -> %s\n",
           rW, wantW, pass ? "PASS" : "FAIL");
  }
  {
    float rH = evalA2(320.0f, 240.0f, 640.0f, 480.0f, "Result.Height");
    bool pass = std::fabs(rH - 720.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addint2] (240+480).Height=%.0f want=720 -> %s\n",
           rH, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: (-100, -50) + (300, 200) → Width=200, Height=150.
  {
    float rW = evalA2(-100.0f, -50.0f, 300.0f, 200.0f, "Result.Width");
    bool pass = std::fabs(rW - 200.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addint2] (-100+300).Width=%.0f want=200 -> %s\n",
           rW, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): (0,0) + (0,0) → Width=0, Height=0.
  {
    float rW = evalA2(0.0f, 0.0f, 0.0f, 0.0f, "Result.Width");
    float rH = evalA2(0.0f, 0.0f, 0.0f, 0.0f, "Result.Height");
    bool pass = std::fabs(rW) < eps && std::fabs(rH) < eps;
    ok = ok && pass;
    printf("[selftest-addint2] (0+0).Width=%.0f .Height=%.0f want=0,0 (t3 defaults) -> %s\n",
           rW, rH, pass ? "PASS" : "FAIL");
  }

  // TRUNCATION (fork-addint2-int-on-float-port): (3.9, 0) + (1.1, 0) → Width = (3+1)=4.
  // TiXL stores Width as int → truncates toward zero before adding.
  {
    float rW = evalA2(3.9f, 0.0f, 1.1f, 0.0f, "Result.Width");
    bool pass = std::fabs(rW - 4.0f) < eps;  // (int)3.9 + (int)1.1 = 3 + 1 = 4
    ok = ok && pass;
    printf("[selftest-addint2] trunc(3.9+1.1).Width=%.0f want=4 (int-truncate) -> %s\n",
           rW, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
