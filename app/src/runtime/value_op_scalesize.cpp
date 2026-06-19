// ScaleSize value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/process/ScaleSize.cs (+ ScaleSize.t3 defaults).
//
//   ScaleSize.cs Update():
//     var stretch = Stretch.GetValue(context);
//     var source  = InputSize.GetValue(context);
//     var factor  = Scale.GetValue(context);
//     Result.Value = new Int2((int)(source.Width  * factor * stretch.X),
//                             (int)(source.Height * factor * stretch.Y));
//
//   Ports:
//     InputSize = InputSlot<Int2>   (default {X:0, Y:0})
//     Stretch   = InputSlot<Vector2> (default {X:1.0, Y:1.0})
//     Scale     = InputSlot<float>  (default 1.0)
//   Output: Result = Slot<Int2> — two Float ports (Result.Width, Result.Height).
//   ScaleSize.t3 defaults: InputSize={X:0,Y:0}; Stretch={X:1,Y:1}; Scale=1.0.
//
//   Relationship to ScaleResolution: ScaleResolution takes a Vector2 factor per-axis
//   (Factor.X, Factor.Y), while ScaleSize separates a uniform scalar Scale from a per-axis
//   Stretch, computing Width*Scale*Stretch.X (a 3-value product). No clamp port.
//
// EVAL-SIDE LAYOUT (flat — no multiInput):
//   in[] = [InputSize.Width, InputSize.Height, Stretch.X, Stretch.Y, Scale]   (n=5).
//   Result.Width  = (int)(InputSize.Width  * Scale * Stretch.X).
//   Result.Height = (int)(InputSize.Height * Scale * Stretch.Y).
//   Output ports at spec indices n and n+1; outIdx - n = 0=Width, 1=Height.
//
// FORKS (named):
//   - fork-scalesize-int-on-float-port (fork-addint2-int-on-float-port precedent):
//     TiXL InputSize is Int2 (integer components). This runtime stores all values as Float.
//     The formula reads source.Width as (int)in[0] before multiplying; the final (int) cast
//     truncates the product. For whole-number InputSize inputs this is byte-identical.
//   - fork-scalesize-port-order: TiXL field order in .cs is Stretch/InputSize/Scale (by attribute
//     declaration order), but the .t3 lists Scale first, then Stretch, then InputSize. The runtime
//     port order for in[] follows the declaration-order convention used by other Int2 ops:
//     InputSize (the primary Int2 source) first, then Stretch (Vec2), then Scale (scalar).
//     This matches the authoring affordance precedent (primary input first, modifiers after).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runScaleSizeSelfTest(bool injectBug);

namespace {

// in[] = [InputSize.Width, InputSize.Height, Stretch.X, Stretch.Y, Scale]  (n=5).
// Result.Width  = (int)(InputSize.Width  * Scale * Stretch.X)
// Result.Height = (int)(InputSize.Height * Scale * Stretch.Y)
// outIdx - n: 0=Width, 1=Height.
float evalScaleSize(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int k = outIdx - n;  // 0=Width, 1=Height
  if (k < 0 || k > 1) return 0.0f;

  const int sw_w = (int)in[0];  // fork-scalesize-int-on-float-port
  const int sw_h = (int)in[1];
  const float stx  = in[2];
  const float sty  = in[3];
  const float scl  = in[4];

  return (k == 0) ? (float)(int)(sw_w * scl * stx)
                  : (float)(int)(sw_h * scl * sty);
}

}  // namespace

static const ValueOp _reg_scalesize{
    // ScaleSize (TiXL Lib.numbers.int2.process.ScaleSize):
    //   Result = new Int2((int)(InputSize.Width * Scale * Stretch.X),
    //                     (int)(InputSize.Height * Scale * Stretch.Y)).
    // Port order: InputSize.Width/Height, Stretch.X/Y, Scale (inputs); Result.Width/Height (outputs).
    // Defaults from ScaleSize.t3: InputSize={X:0,Y:0}; Stretch={X:1,Y:1}; Scale=1.0.
    // fork-scalesize-int-on-float-port: InputSize stored as 2 Float ports, (int)-cast before math.
    // fork-scalesize-port-order: InputSize first (primary), Stretch second, Scale last.
    {"ScaleSize", "ScaleSize",
     {{"InputSize.Width",  "InputSize",        "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"InputSize.Height", "InputSize.Height", "Float", true,  0.0f, 0.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Stretch.X",        "Stretch",          "Float", true,  1.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 2},
      {"Stretch.Y",        "Stretch.Y",        "Float", true,  1.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 1},
      {"Scale",            "Scale",            "Float", true,  1.0f, -10.0f, 10.0f},
      {"Result.Width",     "Result.Width",     "Float", false},
      {"Result.Height",    "Result.Height",    "Float", false}},
     evalScaleSize},
    "scalesize", runScaleSizeSelfTest};

// --- ScaleSize MATH golden (flat path) ---------------------------------------------------------
// Test cases (hand-computed from TiXL formula):
//   InputSize=(1920,1080), Stretch=(1,1), Scale=1.0 → Result=(1920,1080) [identity].
//   InputSize=(1920,1080), Stretch=(1,1), Scale=0.5 → Result=(960,540)   [half].
//   InputSize=(800,600),   Stretch=(2,0.5), Scale=1.0 → Result=(1600,300) [non-uniform stretch].
//   InputSize=(100,100),   Stretch=(1,1), Scale=0.0  → Result=(0,0)       [zero scale].
//   InputSize=(100,100),   Stretch=(1.5,2.0), Scale=2.0 → Result=(300,400) [combined].
//     Width  = (int)(100 * 2.0 * 1.5) = (int)300.0 = 300.
//     Height = (int)(100 * 2.0 * 2.0) = (int)400.0 = 400.
// injectBug asserts Width≠960 (wrong value) for the half-scale case → FAIL.
int runScaleSizeSelfTest(bool injectBug) {
  const float eps = 0.5f;  // integer comparison
  bool ok = true;

  auto evalSS = [&](float iw, float ih, float sx, float sy, float scale,
                    const char* outName) -> float {
    const NodeSpec* spec = findSpec("ScaleSize");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ScaleSize";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["InputSize.Width"]  = iw;
    g.node(nid)->params["InputSize.Height"] = ih;
    g.node(nid)->params["Stretch.X"] = sx;
    g.node(nid)->params["Stretch.Y"] = sy;
    g.node(nid)->params["Scale"]     = scale;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // CASE 1: half-scale. injectBug asserts Width≠960 (wrong output) → FAIL.
  {
    float rw = evalSS(1920.0f, 1080.0f, 1.0f, 1.0f, 0.5f, "Result.Width");
    float want = injectBug ? 9999.0f : 960.0f;
    bool pass = std::fabs(rw - want) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 1920x1080 Stretch=(1,1) Scale=0.5 Width=%.1f want=%.1f -> %s\n",
           rw, want, pass?"PASS":"FAIL");
  }
  {
    float rh = evalSS(1920.0f, 1080.0f, 1.0f, 1.0f, 0.5f, "Result.Height");
    bool pass = std::fabs(rh - 540.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 1920x1080 Stretch=(1,1) Scale=0.5 Height=%.1f want=540.0 -> %s\n",
           rh, pass?"PASS":"FAIL");
  }

  // CASE 2: identity (Scale=1, Stretch=(1,1)).
  {
    float rw = evalSS(1920.0f, 1080.0f, 1.0f, 1.0f, 1.0f, "Result.Width");
    bool pass = std::fabs(rw - 1920.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 1920x1080 identity Width=%.1f want=1920.0 -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 3: non-uniform stretch (Stretch=(2.0, 0.5), Scale=1.0).
  {
    float rw = evalSS(800.0f, 600.0f, 2.0f, 0.5f, 1.0f, "Result.Width");
    float rh = evalSS(800.0f, 600.0f, 2.0f, 0.5f, 1.0f, "Result.Height");
    bool pass = std::fabs(rw - 1600.0f) < eps && std::fabs(rh - 300.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 800x600 Stretch=(2,0.5) Scale=1 W=%.1f H=%.1f want=(1600,300) -> %s\n",
           rw, rh, pass?"PASS":"FAIL");
  }

  // CASE 4: zero scale → (0, 0) no-clamp (ScaleSize has no clamping).
  {
    float rw = evalSS(100.0f, 100.0f, 1.0f, 1.0f, 0.0f, "Result.Width");
    bool pass = std::fabs(rw) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 100x100 Scale=0 Width=%.1f want=0.0 -> %s\n",
           rw, pass?"PASS":"FAIL");
  }

  // CASE 5: combined scale+stretch — (int)(100*2*1.5)=300, (int)(100*2*2)=400.
  {
    float rw = evalSS(100.0f, 100.0f, 1.5f, 2.0f, 2.0f, "Result.Width");
    float rh = evalSS(100.0f, 100.0f, 1.5f, 2.0f, 2.0f, "Result.Height");
    bool pass = std::fabs(rw - 300.0f) < eps && std::fabs(rh - 400.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalesize] 100x100 Stretch=(1.5,2) Scale=2 W=%.1f H=%.1f want=(300,400) -> %s\n",
           rw, rh, pass?"PASS":"FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
