// MakeResolution value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/process/MakeResolution.cs (+ MakeResolution.t3 defaults).
//
//   MakeResolution.cs Update():
//     Size.Value = new Int2(Width.GetValue(context), Height.GetValue(context));
//
//   MakeResolution.t3 DefaultValues: Width=0, Height=0.
//   (TiXL comment in .cs: "Todo - deprecate?"; still ships as an active op in the live codebase.)
//
// 2 scalar int inputs (Width, Height) → 1 Int2 output. Since this runtime has no native Int2
// output type, the output is exposed as 2 Float ports: Result.Width and Result.Height.
// This is the inverse of Int2Components: Width+Height → {Width, Height} pair.
//
// EVAL-SIDE LAYOUT (multi-output, mirrors AddInt2/Int2Components convention):
//   in[] = [Width, Height]  (n=2 scalar inputs).
//   Output ports (Result.Width, Result.Height) are at spec indices n and n+1.
//   Component k = outIdx - n ∈ {0=Width, 1=Height}.
//
// FORKS (named):
//   - fork-makeresolution-int-on-float-port: TiXL Width/Height are InputSlot<int>. This runtime
//     stores all values as Float. Each input is (int)-truncated before packing into the result,
//     matching TiXL's int-slot behaviour exactly for whole-number inputs.
//   - fork-makeresolution-int2-as-2-floats: TiXL outputs an Int2 (single port). This runtime
//     exposes it as 2 Float output ports (Result.Width / Result.Height). NOT an eval fork: the
//     values are identical; only the host data model differs.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest.
int runMakeResolutionSelfTest(bool injectBug);

namespace {

// in[] = [Width, Height]  (n=2).
// Size = new Int2(Width, Height)  (TiXL MakeResolution.cs verbatim).
// fork-makeresolution-int-on-float-port: inputs (int)-truncated before packing.
// outIdx - n gives the component: 0=Width, 1=Height.
float evalMakeResolutionOp(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int k = outIdx - n;  // output component: 0=Width, 1=Height
  if (k == 0) return (float)(int)in[0];  // fork-makeresolution-int-on-float-port
  if (k == 1) return (float)(int)in[1];
  return 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_makeresolution{
    // MakeResolution (TiXL Lib.numbers.int2.process.MakeResolution):
    //   Size = new Int2(Width, Height).
    // Port order MUST match evalMakeResolutionOp's in[] read: Width, Height (inputs);
    // then Result.Width, Result.Height (outputs).
    // Defaults from MakeResolution.t3: Width=0, Height=0.
    {"MakeResolution", "MakeResolution",
     {{"Width",         "Width",         "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Slider},
      {"Height",        "Height",        "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Slider},
      {"Result.Width",  "Result.Width",  "Float", false},
      {"Result.Height", "Result.Height", "Float", false}},
     evalMakeResolutionOp},
    "makeresolution", runMakeResolutionSelfTest};

// --- MakeResolution MATH golden ----------------------------------------------------------------
// Builds a 1-node MakeResolution graph, sets Width/Height, pulls each output pin
// (Result.Width / Result.Height) via evalFloat (flat path — no multiInput, single slot per port).
// Compares against the hand-computed formula Size = Int2(Width, Height).
// injectBug asserts a wrong expected Width so the typical assertion flips RED.
int runMakeResolutionSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate MakeResolution with explicit Width/Height, pulling a named output port.
  auto evalMR = [&](float w, float h, const char* outName) -> float {
    const NodeSpec* spec = findSpec("MakeResolution");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "MakeResolution";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Width"]  = w;
    g.node(nid)->params["Height"] = h;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Width=1920, Height=1080 → Result.Width=1920, Result.Height=1080 (full HD).
  // injectBug asserts Width==0 (wrong) so the Width assertion flips RED.
  {
    float rW = evalMR(1920.0f, 1080.0f, "Result.Width");
    float wantW = injectBug ? 0.0f : 1920.0f;
    bool pass = std::fabs(rW - wantW) < eps;
    ok = ok && pass;
    printf("[selftest-makeresolution] MakeRes(1920,1080).Width=%.0f want=%.0f -> %s\n",
           rW, wantW, pass ? "PASS" : "FAIL");
  }
  {
    float rH = evalMR(1920.0f, 1080.0f, "Result.Height");
    bool pass = std::fabs(rH - 1080.0f) < eps;
    ok = ok && pass;
    printf("[selftest-makeresolution] MakeRes(1920,1080).Height=%.0f want=1080 -> %s\n",
           rH, pass ? "PASS" : "FAIL");
  }

  // SQUARE: Width=256, Height=256 → both outputs = 256.
  {
    float rW = evalMR(256.0f, 256.0f, "Result.Width");
    float rH = evalMR(256.0f, 256.0f, "Result.Height");
    bool pass = std::fabs(rW - 256.0f) < eps && std::fabs(rH - 256.0f) < eps;
    ok = ok && pass;
    printf("[selftest-makeresolution] MakeRes(256,256) W=%.0f H=%.0f want=256,256 -> %s\n",
           rW, rH, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): Width=0, Height=0 → Result.Width=0, Result.Height=0.
  {
    float rW = evalMR(0.0f, 0.0f, "Result.Width");
    float rH = evalMR(0.0f, 0.0f, "Result.Height");
    bool pass = std::fabs(rW) < eps && std::fabs(rH) < eps;
    ok = ok && pass;
    printf("[selftest-makeresolution] MakeRes(0,0) W=%.0f H=%.0f want=0,0 (t3 defaults) -> %s\n",
           rW, rH, pass ? "PASS" : "FAIL");
  }

  // TRUNCATION (fork-makeresolution-int-on-float-port): Width=7.9, Height=4.7 → Width=7, Height=4.
  // TiXL Width/Height are InputSlot<int> → truncate toward zero.
  {
    float rW = evalMR(7.9f, 4.7f, "Result.Width");
    float rH = evalMR(7.9f, 4.7f, "Result.Height");
    bool pass = std::fabs(rW - 7.0f) < eps && std::fabs(rH - 4.0f) < eps;
    ok = ok && pass;
    printf("[selftest-makeresolution] MakeRes(7.9,4.7) W=%.0f H=%.0f want=7,4 (int-trunc) -> %s\n",
           rW, rH, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
