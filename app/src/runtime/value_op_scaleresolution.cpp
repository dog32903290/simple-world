// ScaleResolution value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/process/ScaleResolution.cs (+ ScaleResolution.t3).
//
//   ScaleResolution.cs Update():
//     var r = Resolution.GetValue(context);
//     var f = Factor.GetValue(context);
//     var newSize = new Int2((int)(r.Width * f.X), (int)(r.Height * f.Y));
//     if (ClampToValidTextureSize.GetValue(context)) {
//         if (newSize.Width  <= 0) newSize.Width  = 1; else if (newSize.Width  > MaxSize) newSize.Width  = MaxSize;
//         if (newSize.Height <= 0) newSize.Height = 1; else if (newSize.Height > MaxSize) newSize.Height = MaxSize;
//     }
//     Size.Value = newSize;   // output named "Size"
//
//   const int MaxSize = 16384;
//
//   Ports:
//     Resolution = InputSlot<Int2>  (default {X:0, Y:0})
//     Factor     = InputSlot<Vector2> (default {X:0.0, Y:0.0})
//     ClampToValidTextureSize = InputSlot<bool> (default false)
//   Output: Size = Slot<Int2> — two Float ports (Size.Width, Size.Height).
//
// EVAL-SIDE LAYOUT (flat — no multiInput):
//   in[] = [Resolution.Width, Resolution.Height, Factor.X, Factor.Y, Clamp]   (n=5).
//   Size.Width  = (int)(Resolution.Width  * Factor.X)  [then clamped if Clamp > 0.5].
//   Size.Height = (int)(Resolution.Height * Factor.Y).
//   Output ports Size.Width/Size.Height at spec indices n and n+1 (outIdx - n = 0 or 1).
//
// FORKS (named):
//   - fork-scaleresolution-int-on-float-port (fork-addint2-int-on-float-port precedent):
//     TiXL Resolution is Int2 (integer components); Factor is Vector2 (float). This runtime
//     stores all values as Float. The (int) truncation in the TiXL formula is replicated here:
//     newWidth = (int)((int)Resolution.Width * Factor.X) — inner (int) truncates the stored
//     float to integer before multiplying, outer (int) truncates the float product.
//     For whole-number Resolution inputs (the only values integer sliders produce), this is
//     byte-identical to TiXL's Int2 math.
//   - fork-scaleresolution-bool-as-float: TiXL ClampToValidTextureSize is bool. This runtime
//     stores it as Float (0.0 = false, 1.0 = true). Clamp is applied when in[4] > 0.5.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runScaleResolutionSelfTest(bool injectBug);

namespace {

static constexpr int kMaxTexSize = 16384;

// in[] = [Resolution.Width, Resolution.Height, Factor.X, Factor.Y, Clamp]  (n=5).
// Size.Width  = (int)(Resolution.Width  * Factor.X) [clamped if Clamp>0.5].
// Size.Height = (int)(Resolution.Height * Factor.Y) [clamped if Clamp>0.5].
// outIdx - n: 0=Width, 1=Height.
float evalScaleResolution(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int k = outIdx - n;  // 0=Width, 1=Height
  if (k < 0 || k > 1) return 0.0f;

  const int rw = (int)in[0];          // fork-scaleresolution-int-on-float-port
  const int rh = (int)in[1];
  const float fx = in[2];
  const float fy = in[3];
  const bool clamp = in[4] > 0.5f;   // fork-scaleresolution-bool-as-float

  int val = (k == 0) ? (int)(rw * fx) : (int)(rh * fy);

  if (clamp) {
    if (val <= 0) val = 1;
    else if (val > kMaxTexSize) val = kMaxTexSize;
  }

  return (float)val;
}

}  // namespace

static const ValueOp _reg_scaleresolution{
    // ScaleResolution (TiXL Lib.numbers.int2.process.ScaleResolution):
    //   Size = new Int2((int)(Resolution.Width * Factor.X), (int)(Resolution.Height * Factor.Y));
    //   if (ClampToValidTextureSize) clamp both components to [1, 16384].
    // Port order MUST match evalScaleResolution's in[] read:
    //   Resolution.Width, Resolution.Height, Factor.X, Factor.Y, Clamp; then Size.Width/Height.
    // Defaults from ScaleResolution.t3: Resolution={X:0,Y:0}; Factor={X:0,Y:0}; Clamp=false.
    // fork-scaleresolution-int-on-float-port: Int2 Resolution decomposed as 2 Float ports.
    // fork-scaleresolution-bool-as-float: ClampToValidTextureSize stored as Float (0=false,1=true).
    {"ScaleResolution", "ScaleResolution",
     {{"Resolution.Width",  "Resolution",        "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"Resolution.Height", "Resolution.Height", "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Factor.X",          "Factor",            "Float", true,  0.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 2},
      {"Factor.Y",          "Factor.Y",          "Float", true,  0.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 1},
      {"ClampToValidTextureSize", "ClampToValidTextureSize", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Size.Width",        "Size.Width",        "Float", false},
      {"Size.Height",       "Size.Height",       "Float", false}},
     evalScaleResolution},
    "scaleresolution", runScaleResolutionSelfTest};

// --- ScaleResolution MATH golden (flat path) ---------------------------------------------------
// Exercises the formula directly via evalFloat on a single ScaleResolution node.
// Test cases (hand-computed from TiXL formula):
//   Resolution=(1920,1080), Factor=(0.5,0.5), Clamp=false → Size=(960, 540).
//   Resolution=(800,600),   Factor=(2.0,2.0), Clamp=false → Size=(1600,1200).
//   Resolution=(100,100),   Factor=(0.0,0.0), Clamp=true  → Size=(1,1)   (clamp floor).
//   Resolution=(100,100),   Factor=(200.0,200.0), Clamp=true → Size=(16384,16384) (clamp ceil).
//   Resolution=(1920,1080), Factor=(1.5,1.5), Clamp=false → Size=(2880,1620) [truncation].
// injectBug asserts wrong Width=960.1 (fractional) for the first case → FAIL on truncation.
int runScaleResolutionSelfTest(bool injectBug) {
  const float eps = 0.5f;  // integer comparison — within 0.5 means exact int match
  bool ok = true;

  auto evalSR = [&](float rw, float rh, float fx, float fy, float clamp,
                    const char* outName) -> float {
    const NodeSpec* spec = findSpec("ScaleResolution");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ScaleResolution";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Resolution.Width"]  = rw;
    g.node(nid)->params["Resolution.Height"] = rh;
    g.node(nid)->params["Factor.X"] = fx;
    g.node(nid)->params["Factor.Y"] = fy;
    g.node(nid)->params["ClampToValidTextureSize"] = clamp;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // CASE 1: half-size (typical 1920x1080 → 960x540).
  // injectBug asserts Width=540 (Height value, as if x/y components were swapped) → FAIL.
  {
    float rw = evalSR(1920.0f, 1080.0f, 0.5f, 0.5f, 0.0f, "Size.Width");
    float want = injectBug ? 540.0f : 960.0f;  // bug: x→y component swap
    bool pass = std::fabs(rw - want) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 1920x1080 * (0.5,0.5) Width=%.1f want=%.1f -> %s\n",
           rw, want, pass?"PASS":"FAIL");
  }
  {
    float rh = evalSR(1920.0f, 1080.0f, 0.5f, 0.5f, 0.0f, "Size.Height");
    bool pass = std::fabs(rh - 540.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 1920x1080 * (0.5,0.5) Height=%.1f want=540.0 -> %s\n",
           rh, pass?"PASS":"FAIL");
  }

  // CASE 2: double-size.
  {
    float rw = evalSR(800.0f, 600.0f, 2.0f, 2.0f, 0.0f, "Size.Width");
    bool pass = std::fabs(rw - 1600.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 800x600 * (2,2) Width=%.1f want=1600.0 -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 3: Clamp floor (Factor=0 → 0, clamped to 1).
  {
    float rw = evalSR(100.0f, 100.0f, 0.0f, 0.0f, 1.0f, "Size.Width");
    bool pass = std::fabs(rw - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 100x100 * (0,0) Clamp=true Width=%.1f want=1.0 (floor) -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 4: Clamp ceiling (Factor=200 → 20000, clamped to 16384).
  {
    float rw = evalSR(100.0f, 100.0f, 200.0f, 200.0f, 1.0f, "Size.Width");
    bool pass = std::fabs(rw - 16384.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 100x100 * (200,200) Clamp=true Width=%.1f want=16384.0 (ceil) -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 5: truncation — (int)(1920 * 1.5) = (int)2880.0 = 2880 (exact, no fraction here).
  {
    float rw = evalSR(1920.0f, 1080.0f, 1.5f, 1.5f, 0.0f, "Size.Width");
    bool pass = std::fabs(rw - 2880.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 1920x1080 * (1.5,1.5) Width=%.1f want=2880.0 -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 6: non-uniform factor (asymmetric x/y).
  {
    float rh = evalSR(1920.0f, 1080.0f, 1.0f, 0.25f, 0.0f, "Size.Height");
    bool pass = std::fabs(rh - 270.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scaleresolution] 1920x1080 * (1,0.25) Height=%.1f want=270.0 (asymmetric) -> %s\n",
           rh, pass?"PASS":"FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
