// FloatHash value op (value-op self-registration seam leaf — REPLACES GridPosition, which is BLOCKED).
// TiXL authority: Operators/Lib/numbers/float/random/FloatHash.cs (verbatim below).
//
//   FloatHash.cs Update():
//     var floatSeed = Seed.GetValue(context);
//     var intSeed   = HashRawBitsToInt32(floatSeed);              // PCG hash of the float's raw bits
//     var makeUniqueForChild = UniqueForChild.GetValue(context);
//     var childId = SymbolChildId;                                // this node instance's GUID
//     var bigInteger = new BigInteger(childId.ToByteArray());
//     var childSeed  = makeUniqueForChild ? (uint)(bigInteger & 0xFFFFFFFF) : 0;
//     var randomValue = PcgHash((uint)(childSeed + intSeed));
//     Result.Value = (int)randomValue;
//
//   PcgHash(uint input):                                          // PCG XSH-RR output function
//     uint state = input * 747796405u + 2891336453u;             // LCG step
//     uint word  = ((state >> (int)((state >> 28) + 4u)) ^ state) * 277803737u;  // permutation
//     return (word >> 22) ^ word;                                // output transform
//   HashRawBitsToInt32(float input):
//     uint bits = reinterpret(input);  return (int)PcgHash(bits);
//
//   FloatHash.t3 DefaultValues: Seed = 0.0, UniqueForChild = true.
//
// WHY this replaces GridPosition (the work-order's #2): GridPosition.cs reads
// context.RequestedResolution.Width/Height for its aspectRatio term (load-bearing in the X position) +
// context.LocalFxTime is implied by the aspect path. The sw EvaluationContext (eval_context.h) is a
// FROZEN 16-byte GPU-synced struct {frameIndex,time,deltaTime,_pad} — it has NO RequestedResolution.
// GridPosition therefore needs a context-resolution seam that does not exist on the value rail → it is
// NOT a clean pure-value leaf. Per the work order ("已港就回報換 numbers/ 另一顆未港純值 R1"), swapped
// for FloatHash — a genuinely stateless pure-value op (deterministic integer hash, no context fields,
// no per-frame state). UNPORTED in both seam tables (grep verified).
//
// EVAL-SIDE LAYOUT: a single Float input (Seed) + a no-op-on-the-pure-path Bool input (UniqueForChild)
// → ONE Float output (the int hash dissolved to Float). Pure stateless: behaviour is entirely the
// evaluate fn, registered via the ValueOp seam (no GPU cook). in[] = [Seed, UniqueForChild].
//
// FORKS (named):
//   - fork-floathash-int-dissolve-to-float: TiXL's Result is Slot<int>; sw has no Int port type, so the
//     hash dissolves int→Float (the Cut32 convention, same as every Int-returning op already ported).
//     For hashes whose magnitude exceeds 2^24 the float mantissa rounds (e.g. (float)817759070 =
//     817759040.0) — the goldens below assert the EXACT float-dissolved value (what (float)(int) yields,
//     round-to-nearest), NOT the raw int. The int value is faithful to TiXL; the dissolve is the host
//     type constraint, byte-identical to every other Int-returning op on this runtime.
//   - fork-floathash-uniqueforchild-no-symbolchildid: TiXL's UniqueForChild=true path adds a per-INSTANCE
//     childSeed = (lower 32 bits of the node's GUID). The flat value-eval path carries no SymbolChildId
//     (a node is identified by an int id, not a GUID, and the value-eval evaluate fn gets no node identity
//     at all). So the UniqueForChild=true path has NO faithful host equivalent — there is no single
//     "correct" value (TiXL itself yields a DIFFERENT value per node instance). This port implements
//     childSeed = 0 ALWAYS (the UniqueForChild=false path), which is byte-EXACT to TiXL for
//     UniqueForChild=false. The UniqueForChild input EXISTS for port-shape parity but on this runtime
//     leaves childSeed=0 (toggling it does not change the result — a deliberate, named divergence on the
//     instance-identity path only; the pure hash math is identical). The goldens test UniqueForChild=false
//     (the byte-exact path) and assert the toggle is a no-op here.
//   - fork-floathash-seed-zero-bits: HashRawBitsToInt32(0.0f) hashes the raw bits 0x00000000 (NOT a
//     numeric-zero special case). +0.0f and -0.0f hash differently (different bit patterns) — ported
//     verbatim (the .cs warns about this explicitly).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>  // std::memcpy (bit reinterpret of float→uint32)

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runFloatHashSelfTest(bool injectBug);

namespace {

// PcgHash (PCG XSH-RR output function), ported verbatim from FloatHash.cs.
inline uint32_t pcgHash(uint32_t input) {
  uint32_t state = input * 747796405u + 2891336453u;                 // LCG step
  uint32_t word = ((state >> ((state >> 28) + 4u)) ^ state) * 277803737u;  // permutation
  return (word >> 22) ^ word;                                        // output transform
}

// HashRawBitsToInt32: reinterpret the float's raw bits as uint32, PCG-hash, cast to int32.
inline int32_t hashRawBitsToInt32(float input) {
  uint32_t bits;
  std::memcpy(&bits, &input, sizeof(bits));  // reinterpret (== C#'s Unsafe.As<float,uint>)
  return (int32_t)pcgHash(bits);
}

// in[] = [Seed, UniqueForChild]. childSeed = 0 always (fork-floathash-uniqueforchild-no-symbolchildid).
// Result = (int)PcgHash((uint)(0 + intSeed)), dissolved to Float (fork-floathash-int-dissolve-to-float).
float evalFloatHash(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const float seed = in[0];
  // in[1] = UniqueForChild — on this runtime it has no SymbolChildId to mix in, so childSeed stays 0.
  const int32_t intSeed = hashRawBitsToInt32(seed);
  const uint32_t randomValue = pcgHash((uint32_t)(0u + (uint32_t)intSeed));  // childSeed=0
  return (float)(int32_t)randomValue;  // (int)randomValue dissolved to Float
}

}  // namespace

// Self-registration. File-scope static ValueOp — CMake globs value_op*.cpp; no shared edit point.
static const ValueOp _reg_floathash{
    // FloatHash (TiXL Lib.numbers.float.random.FloatHash): deterministic PCG hash of a float seed → int.
    // Port order MUST match evalFloatHash's in[]: Seed, UniqueForChild (inputs), then Result (output).
    // Defaults from FloatHash.t3: Seed=0.0, UniqueForChild=true (the toggle is a no-op on this runtime —
    // see fork-floathash-uniqueforchild-no-symbolchildid; kept true for .t3 parity).
    {"FloatHash", "FloatHash",
     {{"Seed",           "Seed",           "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"UniqueForChild", "UniqueForChild", "Float", true, 1.0f, 0.0f,       1.0f,      Widget::Bool},
      {"Result",         "Result",         "Float", false}},
     evalFloatHash},
    "floathash", runFloatHashSelfTest};

// --- FloatHash MATH golden -----------------------------------------------------------------------
// Builds a 1-node FloatHash graph, sets Seed (+UniqueForChild), pulls Result via evalFloat, compares to
// the hand-computed PCG hash (Python closed-form against FloatHash.cs, NOT a self-referential cook).
// Expected values are the FLOAT-DISSOLVED ints (what (float)(int) yields, round-to-nearest). injectBug
// asserts a WRONG hash (the seed=1.0 hash on the seed=0.0 input) so the typical assertion flips RED.
int runFloatHashSelfTest(bool injectBug) {
  bool ok = true;

  auto evalH = [&](float seed, float ufc) -> float {
    const NodeSpec* spec = findSpec("FloatHash");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "FloatHash";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Seed"]           = seed;
    g.node(nid)->params["UniqueForChild"] = ufc;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // Tolerance: at hash magnitude ~1e9 one float ULP ≈ 128. We assert the EXACT float-dissolved value,
  // so eps just absorbs the harness's own float printf round-trip — 1.0 is plenty (the bug differs by
  // hundreds of millions).
  auto check = [&](const char* tag, float got, float want) {
    bool pass = std::fabs(got - want) < 1.0f;
    ok = ok && pass;
    printf("[selftest-floathash] %s got=%.1f want=%.1f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // GOLDEN 1 (t3 default seed 0.0, UniqueForChild=false → pure path): hash = 817759070 → float 817759040.
  //   injectBug asserts the seed=1.0 hash (838364390 → float 838364416) against the seed=0.0 output → RED.
  {
    float got = evalH(0.0f, 0.0f);
    float want = injectBug ? 838364416.0f : 817759040.0f;  // bug: wrong-seed hash
    check("G1 seed=0", got, want);
  }

  // GOLDEN 2: seed=1.0 → hash 838364390 → float 838364416.
  check("G2 seed=1", evalH(1.0f, 0.0f), 838364416.0f);

  // GOLDEN 3: seed=0.5 → hash 307410100 → float 307410112. (Distinct seed → distinct hash: proves the
  //   hash actually depends on the raw bits, not a constant.)
  check("G3 seed=0.5", evalH(0.5f, 0.0f), 307410112.0f);

  // GOLDEN 4: seed=-1.0 → hash 369907149 → float 369907136. (Negative seed → different bit pattern.)
  check("G4 seed=-1", evalH(-1.0f, 0.0f), 369907136.0f);

  // GOLDEN 5 (fork-floathash-uniqueforchild-no-symbolchildid): toggling UniqueForChild is a NO-OP on this
  //   runtime (no SymbolChildId to mix in) → seed=0.0 with UFC=true gives the SAME hash as UFC=false.
  check("G5 UFC=true is no-op", evalH(0.0f, 1.0f), 817759040.0f);

  return ok ? 0 : 1;
}

}  // namespace sw
