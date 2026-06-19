// GetAPrime value op (value-op self-registration seam leaf — Phase C numbers/int mining).
// TiXL authority: Operators/Lib/numbers/int/process/GetAPrime.cs.
//
//   GetAPrime.cs Update():
//     var index = Index.GetValue(context);
//     if (index == _lastIndex)
//         return;
//     _lastIndex = index;
//     Result.Value = ComputePrime(index);
//
//   ComputePrime(int index):
//     if (index < 1)  return -1;
//     int count = 0, n = 2;
//     while (true) {
//         if (count > 10000) return -1;      // guard against runaway
//         bool isPrime = true;
//         int limit = (int)Math.Sqrt(n);
//         for (int i = 2; i <= limit; i++) { if (n % i == 0) { isPrime = false; break; } }
//         if (isPrime) { count++; if (count == index) return n; }
//         n = (n == 2) ? 3 : n + 2;          // skip even numbers after 2
//     }
//
//   Ports: Index = InputSlot<int> (default 0). Output: Result (Slot<int>).
//   GetAPrime.t3 DefaultValue: Index = 0.
//
// BACKWARD-TRACE: _lastIndex / _lastPrime are MEMOISATION ONLY — the OUTPUT is a pure function
// of Index. No per-instance state leaks into the result; the cache only avoids re-computation
// when Index doesn't change across calls (a TiXL dirty-flag optimisation). This runtime
// re-computes every frame (stateless evaluate fn), which is faithful — same result for same input.
// Therefore GetAPrime belongs on the STATELESS value_op rail, NOT the stateful rail.
//
// EVAL-SIDE LAYOUT (simple scalar → scalar, flat evalFloat path):
//   in[] = [Index]  (n=1, single scalar input port).
//   Output: Result (one Float port, index 1 in spec).
//
// FORKS (named):
//   - fork-getaprime-index-int: TiXL Index is int; runtime has only Float ports, so Index is
//     read as (int)in[0] (truncation toward zero, matching C# explicit int cast). For whole-number
//     inputs — the only kind an int slider produces — this is byte-identical.
//   - fork-getaprime-index-zero: TiXL returns -1 for index < 1. Default t3 is Index=0 → Result=-1.
//     This runtime emits (float)-1 = -1.0f. The value spine stores -1.0f as a Float, which is
//     exact. Inspectors will display -1; any downstream int consumer will cast it back to -1.
//   - fork-getaprime-recompute-every-frame: TiXL caches with _lastIndex to skip redundant
//     ComputePrime calls. This runtime re-computes every eval (stateless fn). For practical index
//     values the inner trial-division loop is microseconds — not hot-path.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runGetAPrimeSelfTest(bool injectBug);

namespace {

// TiXL ComputePrime verbatim — pure int function, no state.
// Returns the index-th prime (1-indexed); -1 for index < 1 or index > 10000.
int computePrime(int index) {
  if (index < 1) return -1;
  int count = 0;
  int n = 2;
  while (true) {
    if (count > 10000) return -1;
    bool isPrime = true;
    int limit = (int)std::sqrt((float)n);  // (int)Math.Sqrt(n) — trial-division limit
    for (int i = 2; i <= limit; ++i) {
      if (n % i == 0) { isPrime = false; break; }
    }
    if (isPrime) {
      ++count;
      if (count == index) return n;
    }
    n = (n == 2) ? 3 : n + 2;  // skip even numbers after 2
  }
}

// in[] = [Index] (n=1). Result = (float)computePrime((int)Index).
// fork-getaprime-index-int: (int) truncation of Float input.
float evalGetAPrime(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  const int index = (n >= 1) ? (int)in[0] : 0;  // fork-getaprime-index-int
  return (float)computePrime(index);
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_getaprime{
    // GetAPrime (TiXL Lib.numbers.int.process.GetAPrime):
    //   Result = computePrime(Index) — the Index-th prime number (1-indexed).
    // Port order: Index (scalar input, default 0); Result (scalar output).
    // Default from GetAPrime.t3: Index = 0 → Result = -1 (out-of-range sentinel).
    {"GetAPrime", "GetAPrime",
     {{"Index",  "Index",  "Float", true,  0.0f, 0.0f, 10000.0f, Widget::Slider},
      {"Result", "Result", "Float", false}},
     evalGetAPrime},
    "getaprime", runGetAPrimeSelfTest};

// --- GetAPrime MATH golden (flat evalFloat path — simple scalar-in scalar-out, no multiInput) ---
// Exercises the TiXL ComputePrime formula directly:
//   Prime sequence (1-indexed): 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, ...
//     index 1 → 2
//     index 2 → 3
//     index 5 → 11
//     index 10 → 29
//     index 20 → 71
//   Index 0 → -1  (out-of-range sentinel, fork-getaprime-index-zero)
//   Index -3 → -1  (negative, fork-getaprime-index-zero, truncation via (int))
//
// injectBug asserts a WRONG prime for index=5 (e.g. 13 instead of 11) → flips RED.
int runGetAPrimeSelfTest(bool injectBug) {
  bool ok = true;
  const float eps = 0.5f;  // int values, 0.5 tolerance is exact for small primes

  // Helper: call GetAPrime with given index float.
  auto evalGAP = [&](float idx) -> float {
    const NodeSpec* spec = findSpec("GetAPrime");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "GetAPrime";
    for (const auto& p : spec->ports)
      if (p.isInput) nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Index"] = idx;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (!spec->ports[i].isInput) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // INDEX 1 → prime #1 = 2.
  {
    float r = evalGAP(1.0f);
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(1)=%.0f want=2 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // INDEX 2 → prime #2 = 3.
  {
    float r = evalGAP(2.0f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(2)=%.0f want=3 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // INDEX 5 → prime #5 = 11. injectBug asserts 13 (wrong) → flips RED.
  {
    float r = evalGAP(5.0f);
    float want = injectBug ? 13.0f : 11.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(5)=%.0f want=%.0f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }

  // INDEX 10 → prime #10 = 29.
  {
    float r = evalGAP(10.0f);
    bool pass = std::fabs(r - 29.0f) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(10)=%.0f want=29 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // INDEX 20 → prime #20 = 71.
  {
    float r = evalGAP(20.0f);
    bool pass = std::fabs(r - 71.0f) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(20)=%.0f want=71 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // INDEX 0 → -1  (fork-getaprime-index-zero: out-of-range sentinel).
  {
    float r = evalGAP(0.0f);
    bool pass = std::fabs(r - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(0)=%.0f want=-1 (out-of-range) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // INDEX -3 → -1  (fork-getaprime-index-zero: negative, (int) truncates to -3 → index<1).
  {
    float r = evalGAP(-3.0f);
    bool pass = std::fabs(r - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-getaprime] prime(-3)=%.0f want=-1 (negative) -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
