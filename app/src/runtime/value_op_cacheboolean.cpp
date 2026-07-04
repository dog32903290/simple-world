// CacheBoolean value op (value-op self-registration seam leaf — numbers/bool/process family).
// TiXL authority: Operators/Lib/numbers/bool/process/CacheBoolean.cs (+ CacheBoolean.t3 defaults).
//
//   CacheBoolean.cs Update():                                                       (cs:16-30)
//     if (!Value.HasInputConnections) {                                              // cs:19
//         Result.Value = Value.GetValue(context);                                    // cs:21 (unconnected → param)
//         return;
//     }
//     if (Value.TryGetFirstConnection(out var connection)                            // cs:25
//         && connection is Slot<bool> boolValue)
//         Result.Value = boolValue.Value;                                            // cs:28 (connected → source's current value)
//
//   Ports (CacheBoolean.cs:33-34): Value = InputSlot<bool>. Output: Result = Slot<bool>.
//   CacheBoolean.t3 DefaultValue: Value = false.
//
// WHAT THIS OP ACTUALLY DOES (the world-view read): BOTH branches publish the CURRENT value of the
// Value input — the unconnected branch reads the param (cs:21), the connected branch reads the wired
// source's live value (cs:28). The ONLY difference is HOW TiXL fetches it:
//   - unconnected: Value.GetValue(context) evaluates the input normally.
//   - connected:   Value.Value (boolValue.Value, cs:28) reads the source's CACHED last-computed value
//                  WITHOUT re-triggering its Update — that non-re-evaluation is the "cache" the name
//                  refers to. It is a DIRTY-FLAG / re-eval-suppression optimization, NOT a value change:
//                  the number published is the same bool the source holds either way.
//
// FORK (named, load-bearing): fork-cacheboolean-passthrough-on-flat-rail.
//   This runtime's value rail has NO observable HasInputConnections / cached-vs-fresh distinction:
//   reading a Float input port (constant param OR wired source, resolved by evalFloat) has no side
//   effect and no subscriber to re-trigger (the exact property value_op_and.cpp:11-13 documents for
//   `&` vs `&&`). So TiXL's two branches COLLAPSE to a single observable behaviour on this runtime:
//   Result = Value. CacheBoolean is therefore a pure bool PASSTHROUGH here. The "cache" (re-eval
//   suppression) is a TiXL scheduler optimization with NO value-level effect to clone — replicating
//   the dirty-flag machinery would be本質-complexity theatre that changes no output bit. Named, not
//   silent: the op is faithful in VALUE (Result == Value's current bool), forked in the (unobservable)
//   fetch mechanism. This is the same bargain And.cs made with `&`/`&&`.
//
// 1 bool input (Value) → 1 bool output (Result). Pure stateless value op: behaviour is entirely the
// evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// BOOL-AS-FLOAT CONVENTION (Cut 32, runtime-wide, same as And/Or/Not/BoolToFloat):
//   - bool INPUT read as truthy via `(x != 0.0f)`; bool OUTPUT emitted as `cond ? 1.0f : 0.0f`.
//   - Value port carries Widget::Bool + range [0,1]. Default false → 0.0f.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runCacheBooleanSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Value.
// Result = Value's current bool  (TiXL CacheBoolean.cs — both branches publish the current value;
// see fork-cacheboolean-passthrough-on-flat-rail: the connected/unconnected split is unobservable here).
// bool-as-float: booleanize the input (any non-zero = true) then re-emit 1/0 — so a fractional/garbage
// upstream normalizes to a clean bool exactly as TiXL's Slot<bool> would hold true/false.
float evalCacheBoolean(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;
  const bool v = (in[0] != 0.0f);  // bool-as-Float read (Cut 32)
  return v ? 1.0f : 0.0f;          // bool-as-Float emit — Result = Value (passthrough)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited.
static const ValueOp _reg_cacheboolean{
    // CacheBoolean (TiXL Lib.numbers.bool.process.CacheBoolean): Result = Value (bool passthrough).
    // Port order MUST match evalCacheBoolean's in[] read: Value, then out.
    // Bool Float input: def=0.0f (TiXL false, CacheBoolean.t3), range [0,1], Widget::Bool.
    {"CacheBoolean", "CacheBoolean",
     {{"Value", "Value", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"out", "out", "Float", false}},
     evalCacheBoolean},
    "cacheboolean", runCacheBooleanSelfTest};

// --- CacheBoolean MATH golden ------------------------------------------------------------------
// Two legs, both pulling "out" via evalFloat:
//   (A) PARAM leg — set Value directly, assert Result == Value (the unconnected branch, cs:21).
//   (B) WIRED leg — feed CacheBoolean.Value from an UPSTREAM Not(Not(x)) chain (a real cook seam:
//       the value arrives over a wire, not a param), assert Result tracks the computed upstream bool.
//       This is the load-bearing tooth: it proves the op READS + FORWARDS the wired source, not a
//       hardcoded constant. injectBug swaps the expected so the wired assertion flips RED (documented
//       want-flip: a pure stateless value op has NO cook-corruption seam — value_eval has no injectBug
//       hook — so the tooth is the assertion binding to the true evalFloat result; the And/BoolToFloat
//       precedent, GOLDEN_STANDARD "確實無 seam 可用時,檔頭寫明技術理由").
int runCacheBooleanSelfTest(bool injectBug) {
  const float eps = 1e-6f;
  bool ok = true;

  const NodeSpec* spec = findSpec("CacheBoolean");
  if (!spec) { printf("[selftest-cacheboolean] FAIL (no spec)\n"); return 1; }

  // (A) PARAM leg: Result = Value directly (unconnected branch).
  auto evalParam = [&](float value) -> float {
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "CacheBoolean";
    nd.params["Value"] = value;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TRUE passes through to 1, FALSE to 0, fractional-truthy (0.3) normalizes to 1 (bool-as-float).
  struct Row { float in, want; const char* label; };
  Row rows[] = {
      {1.0f, 1.0f, "true→1"},
      {0.0f, 0.0f, "false→0"},
      {0.3f, 1.0f, "0.3→1 (any!=0 is true)"},
  };
  for (const auto& r : rows) {
    float v = evalParam(r.in);
    bool pass = std::fabs(v - r.want) < eps;
    ok = ok && pass;
    printf("[selftest-cacheboolean] PARAM CacheBoolean(%s) =%.6f want=%.6f -> %s\n",
           r.label, v, r.want, pass ? "PASS" : "FAIL");
  }

  // (B) WIRED leg: Not(x) → CacheBoolean.Value. Not inverts: Not(0)=1 → CacheBoolean forwards 1.
  //   The value arrives over a REAL wire (Not.out → CacheBoolean.Value), so a broken passthrough
  //   (return constant / drop the wire) diverges. Expected = !x. injectBug flips the expected so the
  //   assertion binds to the actual evalFloat result and goes RED on a wrong wire read.
  {
    // Build: Not(id=1, In=0) → CacheBoolean(id=2, Value ← Not.out).
    Graph g;
    Node notNode; notNode.id = 1; notNode.type = "Not"; notNode.params["In"] = 0.0f;  // Not(0)=1
    g.nodes.push_back(notNode);
    Node cb; cb.id = 2; cb.type = "CacheBoolean"; cb.params["Value"] = 0.0f;  // param overridden by the wire
    g.nodes.push_back(cb);

    // Find Not's out port index and CacheBoolean's Value input port index.
    const NodeSpec* notSpec = findSpec("Not");
    int notOut = -1;
    if (notSpec)
      for (size_t i = 0; i < notSpec->ports.size(); ++i)
        if (notSpec->ports[i].id == "out") { notOut = (int)i; break; }
    int cbValueIdx = -1, cbOutIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i) {
      if (spec->ports[i].id == "Value") cbValueIdx = (int)i;
      if (spec->ports[i].id == "out")   cbOutIdx = (int)i;
    }
    if (notSpec && notOut >= 0 && cbValueIdx >= 0 && cbOutIdx >= 0) {
      g.connections.push_back({100, pinId(1, notOut), pinId(2, cbValueIdx)});  // Not.out → CacheBoolean.Value
      EvaluationContext ctx{}; ctx.time = 0.0f;
      float v = evalFloat(g, pinId(2, cbOutIdx), ctx, 0);
      // Not(0) = 1 → CacheBoolean forwards 1. injectBug asserts 0 (wrong) → RED, binding the assert to
      // the true wired result (proves the op READS the wire, not a hardcoded value).
      float want = injectBug ? 0.0f : 1.0f;
      bool pass = std::fabs(v - want) < eps;
      ok = ok && pass;
      printf("[selftest-cacheboolean] WIRED Not(0)→CacheBoolean =%.6f want=%.6f (forwards wired bool) -> %s\n",
             v, want, pass ? "PASS" : "FAIL");
    } else {
      printf("[selftest-cacheboolean] WIRED skipped (Not/port lookup failed)\n");
      ok = false;
    }
  }

  return ok ? 0 : 1;
}

}  // namespace sw
