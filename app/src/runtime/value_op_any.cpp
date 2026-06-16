// Any value op (value-op self-registration seam leaf — MultiInput bool reducer).
// TiXL authority: Operators/Lib/numbers/bool/combine/Any.cs (+ Any.t3 DefaultValue:false).
//
//   Any.cs Update():
//     var result = false;
//     foreach (var input in Input.GetCollectedTypedInputs())
//         result |= input.GetValue(context);     // OR-reduce over MultiInput<bool>
//     Result.Value = result;
//
//   Any.t3 Inputs: Input (MultiInputSlot<bool>) DefaultValue = false.
//   Ports: Input = MultiInputSlot<bool> (the variable-length bool list). Output: Result (Slot<bool>).
//
// Pure stateless reducer: behaviour is entirely the evaluate fn, registered via the ValueOp seam
// (no GPU cook). The ONE multiInput port (Input) is the only port — there is NO trailing regular
// port (unlike PickFloat's Index). So the resident gather expands every wired bool into the in[]
// prefix and count = n (nothing to subtract).
//
// EVAL-SIDE LAYOUT (pure-MultiInput, simpler than PickFloat): the resident gather (resident_eval_
// graph.cpp:96) expands the ONE multiInput port (Input) into in[0..n-1]. There is no trailing
// regular port, so in[] = [Input_0, Input_1, …, Input_{n-1}], count = n. The flat evalFloat path
// does NOT expand multiInput (one slot per port → it would only ever see the single primary), so
// the golden below MUST exercise the RESIDENT path (buildEvalGraph + evalResidentFloat), exactly
// like value_op_pickfloat.cpp / value_op_addints' multiInput sibling.
//
// FORKS (named):
//   - fork-any-bool-on-float-port: TiXL Input/Result are `bool`. This runtime has only Float value
//     ports, so each input is read as a bool via (in[i] != 0.0f) and the OR-reduced result is
//     emitted as Float 1.0f (true) / 0.0f (false). This is the bool-as-Float convention already
//     shipped for the value spine (value_eval_ops.cpp:369 `(in[1] != 0.0f)` / :412 `(in[7] !=
//     0.0f)`, Cut 32). Any non-zero Float counts as true — identical to C# `bool` once a 0|1 bool
//     slider is the only producer TiXL ever wires here.
//   - fork-any-empty-false: TiXL's foreach over zero collected inputs runs 0 times, so `result`
//     stays its initial `false` → Any({}) = false. This is TiXL FAITHFUL (not a guard) — Any.cs
//     has no anyConnected fix-up (unlike All.cs, which forces false-when-empty via `result &
//     anyConnected` because All's initial `true` would otherwise leak). For Any the natural fold
//     identity (false) already equals the empty result, so no special-case is needed. In this
//     runtime the resident gather always yields at least the lone primary slot at its default
//     (false / 0.0f), so the degenerate "no wires" case still folds to false — byte-identical to
//     TiXL empty→false. The n<1 guard returns 0.0f (false) for the same reason.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runAnySelfTest(bool injectBug);

namespace {

// in[] = [Input_0 … Input_{n-1}] (the multiInput port fully expanded; NO trailing regular port).
// Result = OR over (in[i] != 0.0f), emitted as Float 1/0  (TiXL Any.cs verbatim, bool semantics).
//   Empty fold (n<1) → false → 0.0f (fork-any-empty-false; matches TiXL loop-0-times).
float evalAnyOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  bool result = false;  // TiXL initial: var result = false;
  for (int i = 0; i < n; ++i)
    result |= (in[i] != 0.0f);  // fork-any-bool-on-float-port: nonzero Float == true
  return result ? 1.0f : 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf (no shared edit point), feeds
// valueOpSpecSink() + valueOpSelfTests() during pre-main dynamic init.
static const ValueOp _reg_any{
    // Any (TiXL Lib.numbers.bool.combine.Any): OR-reduce a MultiInput<bool>. Input = the multiInput
    // head (vary-length); it is the ONLY port. Default per Any.t3 = false (0.0f). Output emitted as
    // Float 1/0 (bool-as-Float convention). min/max 0..1 = the bool widget's 0|1 domain.
    {"Any", "Any",
     {{"Input", "Input", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalAnyOp},
    "any", runAnySelfTest};

// --- Any MATH golden (resident path — multiInput needs the resident gather) ---------------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol anyAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(a), Const(b), Const(c) → Any.Input (multiInput) }. count=3.
//   Any([a,b,c]) = (a!=0) | (b!=0) | (c!=0), as Float 1/0.
//   [0,0,0]→0 ; [0,1,0]→1 ; [1,1,1]→1 ; [0,0,0.5]→1 (nonzero=true).
// A gather that dropped extra wires (read only the primary) would mis-fold [0,0,1]→0; injectBug
// flips the all-false expectation to 1 so the tooth bites.
int runAnySelfTest(bool injectBug) {
  Symbol cst = anyAtomic("Const", {{"value", "value", "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});
  Symbol anyOp = anyAtomic("Any", {{"Input", "Input", "Float", 0.0f}},
                           {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires three Const into Any.Input (multiInput) and returns the OR-reduced out.
  auto anyOf = [&](float a, float b, float c) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[anyOp.id] = anyOp;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = a;
    SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = b;
    SymbolChild c3; c3.id = 3; c3.symbolId = "Const"; c3.overrides["value"] = c;
    SymbolChild an; an.id = 4; an.symbolId = "Any";
    root.children = {c1, c2, c3, an};
    root.connections = {
        {1, "out", 4, "Input"},  // primary multiInput source
        {2, "out", 4, "Input"},  // extra
        {3, "out", 4, "Input"},  // extra
        {4, "out", kSymbolBoundary, "out"},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // ALL-FALSE: Any([0,0,0]) = 0. injectBug asserts 1 (the wrong fold) → flips RED.
  {
    float r = anyOf(0.0f, 0.0f, 0.0f);
    float want = injectBug ? 1.0f : 0.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([0,0,0])=%.4f want=%.4f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }

  // ONE-TRUE (middle): Any([0,1,0]) = 1. Proves the EXTRA wire (not just primary) is folded.
  {
    float r = anyOf(0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([0,1,0])=%.4f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ONE-TRUE (last): Any([0,0,1]) = 1. A gather that read only the primary would mis-fold to 0.
  {
    float r = anyOf(0.0f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([0,0,1])=%.4f want=1.0000 (last wire) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ALL-TRUE: Any([1,1,1]) = 1.
  {
    float r = anyOf(1.0f, 1.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([1,1,1])=%.4f want=1.0000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-any-bool-on-float-port: any NONZERO Float counts as true. Any([0,0,0.5]) = 1.
  {
    float r = anyOf(0.0f, 0.0f, 0.5f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([0,0,0.5])=%.4f want=1.0000 (nonzero-Float=true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-any-empty-false (degenerate): the lone primary slot at its default (false / 0.0f)
  // folds to false. Any([0]) = 0 — TiXL empty→false faithful (loop runs once over a false input,
  // same result as the empty loop). Wiring a single false Const is the closest reachable form of
  // the empty-collection case (the resident gather always yields ≥1 slot).
  {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[anyOp.id] = anyOp;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 0.0f;
    SymbolChild an; an.id = 4; an.symbolId = "Any";
    root.children = {c1, an};
    root.connections = {{1, "out", 4, "Input"}, {4, "out", kSymbolBoundary, "out"}};
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    float r = it == g.outputs.end() ? -999.0f
                                    : evalResidentFloat(g, it->second.first, it->second.second, ctx);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-any] Any([0])=%.4f want=0.0000 (fork:empty->false) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
