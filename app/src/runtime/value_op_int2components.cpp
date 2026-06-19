// Int2Components value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/process/Int2Components.cs (+ Int2Components.t3 defaults).
//
//   Int2Components.cs Update():
//     var r = Resolution.GetValue(context);
//     Width.Value = r.Width;
//     Height.Value = r.Height;
//     Length.Value = r.Width * r.Height;
//     AspectRatio.Value = (float)r.Width / (r).Height;
//
//   Int2Components.t3 DefaultValues: Resolution = {X:0, Y:0}.
//
// 1 Int2 input (Resolution) → 4 Float outputs: Width, Height, Length, AspectRatio.
// Int2 decomposed into 2 Float ports (Resolution.Width, Resolution.Height) → 4 scalar outputs.
//
// EVAL-SIDE LAYOUT (multi-output, mirrors Vector3Components / Int2Components convention):
//   in[] = [Resolution.Width, Resolution.Height]  (n=2 inputs).
//   Output ports (Width, Height, Length, AspectRatio) are at spec indices n..n+3.
//   Component k = outIdx - n ∈ {0=Width, 1=Height, 2=Length, 3=AspectRatio}.
//
// FORKS (named):
//   - fork-int2components-aspectratio-zero-guard: TiXL computes `(float)r.Width / (r).Height`
//     with no zero guard. When Height=0 this produces +Inf (C# float division). This runtime
//     matches TiXL by also returning +Inf (standard IEEE 754 float division by zero). NOT a
//     divergence from TiXL; included here to name the edge case explicitly so it is tested.
//   - fork-int2components-int-on-float-port: TiXL Resolution is InputSlot<Int2> (stores ints).
//     This runtime stores all values as Float. Width/Height are (int)-truncated from the float
//     params before the derived computations (Length, AspectRatio). For whole-number inputs
//     (the only values int sliders produce) this is byte-identical to TiXL.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest.
int runInt2ComponentsSelfTest(bool injectBug);

namespace {

// in[] = [Resolution.Width, Resolution.Height]  (n=2).
// Width  = r.Width
// Height = r.Height
// Length = r.Width * r.Height
// AspectRatio = (float)r.Width / r.Height   (fork-int2components-aspectratio-zero-guard: Height=0 → +Inf, matching TiXL)
// fork-int2components-int-on-float-port: truncate Width/Height before derived computations.
float evalInt2ComponentsOp(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int w = (int)in[0];  // fork-int2components-int-on-float-port
  const int h = (int)in[1];
  const int k = outIdx - n;  // output component: 0=Width 1=Height 2=Length 3=AspectRatio
  switch (k) {
    case 0: return (float)w;
    case 1: return (float)h;
    case 2: return (float)(w * h);
    case 3: return (float)w / (float)h;  // fork-int2components-aspectratio-zero-guard: h==0 → +Inf
    default: return 0.0f;
  }
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_int2components{
    // Int2Components (TiXL Lib.numbers.int2.process.Int2Components):
    //   Width=r.Width; Height=r.Height; Length=r.Width*r.Height; AspectRatio=(float)r.Width/r.Height.
    // Port order MUST match evalInt2ComponentsOp's in[] read:
    //   Resolution.Width, Resolution.Height (inputs); then Width, Height, Length, AspectRatio (outputs).
    // Defaults from Int2Components.t3: Resolution = {X:0, Y:0}.
    {"Int2Components", "Int2Components",
     {{"Resolution.Width",  "Resolution",        "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"Resolution.Height", "Resolution.Height", "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Width",       "Width",       "Float", false},
      {"Height",      "Height",      "Float", false},
      {"Length",      "Length",      "Float", false},
      {"AspectRatio", "AspectRatio", "Float", false}},
     evalInt2ComponentsOp},
    "int2components", runInt2ComponentsSelfTest};

// --- Int2Components MATH golden ----------------------------------------------------------------
// Builds a 1-node Int2Components graph, sets Resolution {Width, Height}, pulls each of the
// 4 output pins via evalFloat (flat path — no multiInput). Compares against hand-computed formula.
// injectBug asserts a wrong Length (wrong formula W*H) so the Length assertion flips RED.
int runInt2ComponentsSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate Int2Components, pulling a named output port.
  auto evalIC = [&](float w, float h, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Int2Components");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Int2Components";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Resolution.Width"]  = w;
    g.node(nid)->params["Resolution.Height"] = h;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Resolution = {1920, 1080}
  //   Width=1920, Height=1080, Length=1920*1080=2073600, AspectRatio=1920/1080≈1.7778.
  {
    float rW   = evalIC(1920.0f, 1080.0f, "Width");
    float rH   = evalIC(1920.0f, 1080.0f, "Height");
    float rLen = evalIC(1920.0f, 1080.0f, "Length");
    float rAR  = evalIC(1920.0f, 1080.0f, "AspectRatio");

    // injectBug asserts Length == 0 (wrong — the "forget multiplication" failure mode) → RED.
    float wantLen = injectBug ? 0.0f : 2073600.0f;

    bool passW   = std::fabs(rW   - 1920.0f)    < eps;
    bool passH   = std::fabs(rH   - 1080.0f)    < eps;
    bool passLen = std::fabs(rLen - wantLen)     < eps;
    bool passAR  = std::fabs(rAR  - (1920.0f / 1080.0f)) < 1e-3f;

    ok = ok && passW && passH && passLen && passAR;
    printf("[selftest-int2components] {1920,1080}: W=%.0f H=%.0f Len=%.0f AR=%.4f want=1920,1080,2073600,1.7778 -> %s\n",
           rW, rH, rLen, rAR, (passW && passH && passLen && passAR) ? "PASS" : "FAIL");
  }

  // SQUARE: Resolution = {256, 256} → Length=65536, AspectRatio=1.0.
  {
    float rLen = evalIC(256.0f, 256.0f, "Length");
    float rAR  = evalIC(256.0f, 256.0f, "AspectRatio");
    bool pass = std::fabs(rLen - 65536.0f) < eps && std::fabs(rAR - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-int2components] {256,256}: Len=%.0f AR=%.4f want=65536,1.0 -> %s\n",
           rLen, rAR, pass ? "PASS" : "FAIL");
  }

  // ZERO GUARD — AspectRatio with Height=0:
  // TiXL: (float)r.Width / r.Height = (float)N / 0 → +Inf (IEEE 754; TiXL does not guard).
  // fork-int2components-aspectratio-zero-guard: this runtime matches TiXL's +Inf behaviour.
  {
    float rAR = evalIC(640.0f, 0.0f, "AspectRatio");
    bool pass = std::isinf(rAR) && rAR > 0.0f;  // positive infinity
    ok = ok && pass;
    printf("[selftest-int2components] {640,0}.AspectRatio=%.6g want=+Inf (zero-guard) -> %s\n",
           rAR, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): Resolution = {0, 0} → Width=0, Height=0, Length=0, AspectRatio=NaN.
  // (0/0 = NaN in IEEE 754; TiXL inherits this.)
  {
    float rW   = evalIC(0.0f, 0.0f, "Width");
    float rLen = evalIC(0.0f, 0.0f, "Length");
    float rAR  = evalIC(0.0f, 0.0f, "AspectRatio");
    bool pass = std::fabs(rW) < eps && std::fabs(rLen) < eps && std::isnan(rAR);
    ok = ok && pass;
    printf("[selftest-int2components] {0,0}: W=%.0f Len=%.0f AR=%.6g want=0,0,NaN -> %s\n",
           rW, rLen, rAR, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
