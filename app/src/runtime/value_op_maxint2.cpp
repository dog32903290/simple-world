// MaxInt2 value op (value-op self-registration seam leaf — Phase C numbers/int2 mining).
// TiXL authority: Operators/Lib/numbers/int2/process/MaxInt2.cs.
//
//   MaxInt2.cs Update():
//     int maxWidth = 0, maxHeight = 0;
//     foreach (var input in Sizes.GetCollectedTypedInputs()) {
//         var s = input.GetValue(context);
//         maxWidth  = Math.Max(maxWidth,  s.Width);
//         maxHeight = Math.Max(maxHeight, s.Height);
//     }
//     Sizes.DirtyFlag.Clear();
//     MaxSize.Value = new Int2(maxWidth, maxHeight);
//
//   Ports: Sizes = MultiInputSlot<Int2> (the variable-length list of Int2 values).
//   Output: MaxSize (Slot<Int2>).
//   MaxInt2.t3 DefaultValue: Sizes.X = 0, Sizes.Y = 0.
//
// BACKWARD-TRACE: The memoisation pattern (_lastIndex / _lastPrime used in GetAPrime) is NOT
// present here — MaxInt2 computes fresh every dirty cycle with no state fields. Genuinely
// stateless. The accumulator seeds from 0 (NOT Int32.MinValue as MaxInt does for plain ints).
// This means an Int2 with a component above zero wins over all sources at default — which is
// the TiXL behaviour: if all sources have Width=0, maxWidth=0. This is semantically correct
// for the resolution/size domain (negative dimensions are not meaningful in TiXL Int2 usage).
//
// Int2 multiInput gather convention (same as AddInt2's 2-float-per-source pattern, now for
// a MULTIINPUT<Int2> — each connected Int2 source contributes 2 consecutive Float slots):
//   in[] = [src0.Width, src0.Height, src1.Width, src1.Height, ..., srcK-1.Width, srcK-1.Height]
//   n = 2K (no trailing regular port — exactly the same single-multiInput convention as MaxInt).
//   The resident gather expands each Int2 source into 2 floats (Width, Height) in in[].
//
// EVAL-SIDE LAYOUT:
//   in[] = [src0.Width, src0.Height, src1.Width, src1.Height, ...]  — 2K floats for K sources.
//   Spec port layout: [Sizes (multiInput head, vecArity=2), MaxSize.Width, MaxSize.Height]
//   MaxSize.Width  = max over i of (int)in[2i]    (component 0, spec outIdx = 1)
//   MaxSize.Height = max over i of (int)in[2i+1]  (component 1, spec outIdx = 2)
//   outIdx - 1 gives the component: 0=Width, 1=Height (1 spec input port precedes outputs).
//
// FORKS (named):
//   - fork-maxint2-int-on-float-port: TiXL Width/Height are int. Runtime stores all values as
//     Float. Each component is (int)-truncated (C truncation toward zero) before the max, then
//     the result is cast back to Float. For whole-number inputs (the only kind int sliders
//     produce) this is byte-identical to TiXL's Int2 max.
//   - fork-maxint2-seed-zero: TiXL seeds maxWidth=maxHeight=0. If ALL sources have a negative
//     component, TiXL's max still starts from 0 and could exceed every source's value
//     (e.g., all sources have Width=-5 → TiXL maxWidth=0, not -5). This runtime mirrors exactly:
//     accumulator starts from 0 per TiXL .cs verbatim.
//   - fork-maxint2-int2-as-2-floats-per-source: TiXL has a native Int2 multiInput. This runtime
//     decomposes each Int2 source into 2 consecutive Float ports in the multiInput gather
//     (Width then Height), matching the AddInt2 and Int2Components conventions already shipped.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runMaxInt2SelfTest(bool injectBug);

namespace {

// in[] = [src0.Width, src0.Height, src1.Width, src1.Height, ...]  n = 2K (K Int2 sources).
// Spec port layout: [Sizes (head, multiInput, vecArity=2), MaxSize.Width (out), MaxSize.Height (out)]
//   outIdx == 1 → MaxSize.Width  (comp = outIdx - 1 = 0)
//   outIdx == 2 → MaxSize.Height (comp = outIdx - 1 = 1)
// The multiInput head "Sizes" occupies ONE spec input port → output ports begin at spec index 1.
// Accumulator seeds from 0 (fork-maxint2-seed-zero, per TiXL .cs verbatim).
float evalMaxInt2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;               // defensive: need at least one Int2 source (2 floats)
  const int comp = outIdx - 1;          // 0 = Width, 1 = Height (1 spec input port before outputs)
  if (comp < 0 || comp > 1) return 0.0f;
  int maxVal = 0;  // fork-maxint2-seed-zero: seeds from 0, not Int32.MinValue
  for (int i = comp; i < n; i += 2)    // stride 2: Width at even indices, Height at odd
    maxVal = std::max(maxVal, (int)in[i]);  // fork-maxint2-int-on-float-port: trunc toward zero
  return (float)maxVal;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_maxint2{
    // MaxInt2 (TiXL Lib.numbers.int2.process.MaxInt2):
    //   MaxSize = new Int2(max of all Widths, max of all Heights) across connected Sizes inputs.
    // Port order: Sizes (multiInput Int2 head, vecArity=2); MaxSize.Width, MaxSize.Height (outputs).
    // Defaults from MaxInt2.t3: Sizes.X=0, Sizes.Y=0.
    // fork-maxint2-int2-as-2-floats-per-source: each Int2 source = 2 Float ports in multiInput gather.
    // ONE multiInput head port "Sizes" with vecArity=2 (matching PickVector2's "Input" with vecArity=2).
    // All Width/Height floats from all sources wire to "Sizes"; the resident gather interleaves them
    // into in[] = [src0.W, src0.H, src1.W, src1.H, ...].
    {"MaxInt2", "MaxInt2",
     {{"Sizes", "Sizes", "Float", true, 0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 2,
       /*multiInput=*/true},
      {"MaxSize.Width",  "MaxSize.Width",  "Float", false},
      {"MaxSize.Height", "MaxSize.Height", "Float", false}},
     evalMaxInt2},
    "maxint2", runMaxInt2SelfTest};

// --- MaxInt2 MATH golden (resident path — multiInput Int2 needs the resident gather) -----------
// Each Int2 source is represented as 2 consecutive Const nodes wired into the Sizes multiInput
// (Width then Height), matching the vecArity=2 convention. evalResidentFloat is called for each
// output component.
namespace {
Symbol mx2Atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const sources (pairs Width/Height) → MaxInt2.Sizes (multiInput Int2) }
//   3 sources: A=(320,240), B=(1920,100), C=(800,1080).
//   Expected: maxWidth = max(320,1920,800) = 1920 (from B)
//             maxHeight = max(240,100,1080) = 1080 (from C)
//   → MaxSize.Width=1920, MaxSize.Height=1080.
//
// injectBug asserts Width=320 (wrong — reads only the first source) → RED.
//
// Extra cases:
//   1 source: (640,480) → MaxSize=(640,480)  (single source; also tests default-slot path).
//   ALL equal: (256,256), (256,256) → MaxSize=(256,256).
//   SEED-ZERO FORK: (0,0), (-5,-3) → maxWidth=max(0,0,-5)=0, maxHeight=max(0,0,-3)=0.
//     (TiXL seed=0 dominates negative components; fork-maxint2-seed-zero explicitly tested.)
int runMaxInt2SelfTest(bool injectBug) {
  Symbol cst = mx2Atomic("Const",
                          {{"value", "value", "Float", 0.0f}},
                          {{"out",   "out",   "Float", 0.0f}});
  Symbol mx2 = mx2Atomic("MaxInt2",
                          {{"Sizes", "Sizes", "Float", 0.0f}},
                          {{"MaxSize.Width",  "MaxSize.Width",  "Float", 0.0f},
                           {"MaxSize.Height", "MaxSize.Height", "Float", 0.0f}});

  // Helper: compute max Int2 from a list of (width,height) pairs; return named output.
  auto maxOf = [&](std::vector<std::pair<float,float>> sources, const char* outName) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[mx2.id] = mx2;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"MaxSize.Width",  "MaxSize.Width",  "Float", 0.0f},
                       {"MaxSize.Height", "MaxSize.Height", "Float", 0.0f}};
    // The root output ports mirror the MaxInt2 output port names.
    const int mxChildId = 200;
    SymbolChild mc; mc.id = mxChildId; mc.symbolId = "MaxInt2";
    root.children.clear();
    // Each source: 2 Const nodes (Width, Height), IDs 2i+1 (Width) and 2i+2 (Height).
    for (size_t i = 0; i < sources.size(); ++i) {
      SymbolChild cw; cw.id = (int)(2*i+1); cw.symbolId = "Const"; cw.overrides["value"] = sources[i].first;
      SymbolChild ch; ch.id = (int)(2*i+2); ch.symbolId = "Const"; ch.overrides["value"] = sources[i].second;
      root.children.push_back(cw);
      root.children.push_back(ch);
    }
    root.children.push_back(mc);
    root.connections.clear();
    // Wire each (Width, Height) pair into the Sizes multiInput head port.
    // vecArity=2: each Int2 source sends 2 consecutive wires to "Sizes" (same convention as
    // PickVector2 wiring pairs to "Input"). The resident gather collects them interleaved:
    // [src0.Width, src0.Height, src1.Width, src1.Height, ...].
    for (size_t i = 0; i < sources.size(); ++i) {
      root.connections.push_back({(int)(2*i+1), "out", mxChildId, "Sizes"});  // Width wire
      root.connections.push_back({(int)(2*i+2), "out", mxChildId, "Sizes"});  // Height wire (same port!)
    }
    root.connections.push_back({mxChildId, outName, kSymbolBoundary, outName});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find(outName);
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 0.5f;  // int results, 0.5 tolerance is exact

  // TYPICAL: 3 sources A=(320,240), B=(1920,100), C=(800,1080).
  // maxWidth=1920 (from B), maxHeight=1080 (from C).
  // injectBug asserts Width=320 (first source only, ignores rest) → RED.
  {
    float rW = maxOf({{320,240},{1920,100},{800,1080}}, "MaxSize.Width");
    float wantW = injectBug ? 320.0f : 1920.0f;
    bool pass = std::fabs(rW - wantW) < eps;
    ok = ok && pass;
    printf("[selftest-maxint2] Max3.Width=%.0f want=%.0f -> %s\n",
           rW, wantW, pass ? "PASS" : "FAIL");
  }
  {
    float rH = maxOf({{320,240},{1920,100},{800,1080}}, "MaxSize.Height");
    bool pass = std::fabs(rH - 1080.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint2] Max3.Height=%.0f want=1080 -> %s\n", rH, pass ? "PASS" : "FAIL");
  }

  // SINGLE source: (640,480) → MaxSize=(640,480).
  {
    float rW = maxOf({{640,480}}, "MaxSize.Width");
    float rH = maxOf({{640,480}}, "MaxSize.Height");
    bool pass = std::fabs(rW - 640.0f) < eps && std::fabs(rH - 480.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint2] Max1=(%.0f,%.0f) want=(640,480) -> %s\n", rW, rH, pass ? "PASS" : "FAIL");
  }

  // ALL EQUAL: (256,256),(256,256) → MaxSize=(256,256).
  {
    float rW = maxOf({{256,256},{256,256}}, "MaxSize.Width");
    bool pass = std::fabs(rW - 256.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint2] MaxEqual.Width=%.0f want=256 -> %s\n", rW, pass ? "PASS" : "FAIL");
  }

  // SEED-ZERO FORK (fork-maxint2-seed-zero): sources (0,0) and (-5,-3).
  // TiXL seeds from 0. maxWidth=max(0,0,-5)=0, maxHeight=max(0,0,-3)=0.
  // (Sources with negative components do NOT win over the seed.)
  {
    float rW = maxOf({{0,0},{-5,-3}}, "MaxSize.Width");
    float rH = maxOf({{0,0},{-5,-3}}, "MaxSize.Height");
    bool pass = std::fabs(rW - 0.0f) < eps && std::fabs(rH - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint2] MaxSeedZero=(%.0f,%.0f) want=(0,0) fork-seed-zero -> %s\n",
           rW, rH, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
