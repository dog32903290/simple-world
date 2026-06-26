// MulMatrix value op (value-op self-registration seam leaf — numbers/vec3 matrix mining).
// TiXL authority: Operators/Lib/numbers/vec3/MulMatrix.cs (+ MulMatrix.t3 defaults).
//
//   MulMatrix.cs Update():
//     var a = MatrixA.GetValue(context);
//     var b = MatrixB.GetValue(context);
//     Result.Value = Matrix4x4.Multiply(a, b);     (MulMatrix.cs line ~30)
//
//   System.Numerics.Matrix4x4.Multiply(A, B) — standard row-major matrix product:
//     C.Mij = Σ_k A.Mik · B.Mkj   (i = row 1..4, j = col 1..4)
//
//   Ports (from MulMatrix.cs field order):
//     MatrixA = InputSlot<Matrix4x4>   (MulMatrix.cs MatrixA)
//     MatrixB = InputSlot<Matrix4x4>   (MulMatrix.cs MatrixB)
//     Output: Result = Slot<Matrix4x4> (MulMatrix.cs Result)
//
//   MulMatrix.t3 DefaultValues: MatrixA = Identity, MatrixB = Identity
//     (16-float array, row-major [M11,M12,M13,M14, M21..M24, M31..M34, M41..M44];
//      identity = diagonal 1s, all else 0 — see Core/Model/SymbolPackage.TypeRegistration.cs:164).
//
// ──────────────────────────────────────────────────────────────────────────────────────────────
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   A Matrix4x4 is represented as 16 consecutive Float ports in ROW-MAJOR order, exactly mirroring
//   the established fork-vec3-as-3-floats convention (AddVec3/CrossVec3). The value rail carries
//   ONLY Float ports; a "Matrix4x4 wire" = 16 Float wires named "<base>.m11".."<base>.m44". This is
//   PURELY ADDITIVE on the value-op registry — no new dataType, no cook-spine change, no GPU upload.
//   evalFloat gathers every Float input into in[] and calls evaluate(outIdx) once per output pin.
//
//   in[] = [A.m11..A.m44 (16), B.m11..B.m44 (16)]  (n = 32; == kMaxFloatIn cap, exactly fits)
//   Output ports Result.m11..Result.m44 follow at spec indices 32..47.
//   Component (i,j) = (outIdx - n); row = comp/4, col = comp%4.
//   eval: Result.Mij = Σ_k A.Mik · B.Mkj   (Matrix4x4.Multiply, row-major).
//
// FORKS (named):
//   - fork-mulmatrix-mat4-as-16-floats: each Matrix4x4 input/output is 16 consecutive Float ports
//     (row-major). The product formula is byte-identical to System.Numerics Matrix4x4.Multiply.
//     Not an eval fork — purely the wire-decomposition convention (same as fork-vec3-as-3-floats).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runMulMatrixSelfTest(bool injectBug);

// Pure evaluate fn (defined LOCAL to this leaf — like value_op_calcdispatchcount.cpp's
// evalCalcDispatchCountOp — so no shared value_eval_ops.{h,cpp} edit; the seam stays fully additive).
//   in[0..15]  = A row-major (A.m11..A.m44)
//   in[16..31] = B row-major (B.m11..B.m44)
//   outIdx     = output port index; component = outIdx - n  (0..15 → m11..m44, row-major)
//   Result.Mij = Σ_k A.Mik · B.Mkj   (Matrix4x4.Multiply, row-major)
float evalMulMatrix(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 32) return NAN;             // not fully wired/gathered → bite every golden
  const int comp = outIdx - n;        // 0..15 selects which Result entry
  if (comp < 0 || comp >= 16) return 0.0f;
  const int i = comp / 4;             // result row    (0..3 → M(i+1)?)
  const int j = comp % 4;             // result col
  const float* A = in;               // 16 floats, row-major: A[r*4 + c] = A.M(r+1)(c+1)
  const float* B = in + 16;          // 16 floats, row-major
  float sum = 0.0f;
  for (int k = 0; k < 4; ++k)
    sum += A[i * 4 + k] * B[k * 4 + j];  // Σ_k A.Mik · B.Mkj
  return sum;
}

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
//
// Port spec: 16 MatrixA Float inputs (Widget::Vec head arity 16 on .m11 so the Inspector draws ONE
// 4x4 block; components plain Float arity 1), 16 MatrixB inputs, then 16 Result Float outputs.
// Defaults = identity (diagonal 1.0). The vecArity-16 head is cosmetic (Inspector grouping); the
// value/save/eval spine sees 16 independent Float ports exactly like AddVec3's arity-3 head.
static const ValueOp _reg_mulmatrix{
    {"MulMatrix", "MulMatrix",
     {// MatrixA (row-major; .m11 is the Vec head of arity 16 → one Inspector block)
      {"MatrixA.m11", "MatrixA",     "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 16},
      {"MatrixA.m12", "MatrixA.m12", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m13", "MatrixA.m13", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m14", "MatrixA.m14", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m21", "MatrixA.m21", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m22", "MatrixA.m22", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m23", "MatrixA.m23", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m24", "MatrixA.m24", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m31", "MatrixA.m31", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m32", "MatrixA.m32", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m33", "MatrixA.m33", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m34", "MatrixA.m34", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m41", "MatrixA.m41", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m42", "MatrixA.m42", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m43", "MatrixA.m43", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixA.m44", "MatrixA.m44", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      // MatrixB (row-major; .m11 is the Vec head of arity 16)
      {"MatrixB.m11", "MatrixB",     "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 16},
      {"MatrixB.m12", "MatrixB.m12", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m13", "MatrixB.m13", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m14", "MatrixB.m14", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m21", "MatrixB.m21", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m22", "MatrixB.m22", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m23", "MatrixB.m23", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m24", "MatrixB.m24", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m31", "MatrixB.m31", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m32", "MatrixB.m32", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m33", "MatrixB.m33", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m34", "MatrixB.m34", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m41", "MatrixB.m41", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m42", "MatrixB.m42", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m43", "MatrixB.m43", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"MatrixB.m44", "MatrixB.m44", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      // Result (row-major) — 16 Float outputs
      {"Result.m11", "Result.m11", "Float", false}, {"Result.m12", "Result.m12", "Float", false},
      {"Result.m13", "Result.m13", "Float", false}, {"Result.m14", "Result.m14", "Float", false},
      {"Result.m21", "Result.m21", "Float", false}, {"Result.m22", "Result.m22", "Float", false},
      {"Result.m23", "Result.m23", "Float", false}, {"Result.m24", "Result.m24", "Float", false},
      {"Result.m31", "Result.m31", "Float", false}, {"Result.m32", "Result.m32", "Float", false},
      {"Result.m33", "Result.m33", "Float", false}, {"Result.m34", "Result.m34", "Float", false},
      {"Result.m41", "Result.m41", "Float", false}, {"Result.m42", "Result.m42", "Float", false},
      {"Result.m43", "Result.m43", "Float", false}, {"Result.m44", "Result.m44", "Float", false}},
     evalMulMatrix,
     "numbers.vec3"},
    "mulmatrix", runMulMatrixSelfTest};

// --- MulMatrix MATH golden ---------------------------------------------------------------------
// Builds a 1-node MulMatrix graph, sets all 32 input components, pulls a Result entry via evalFloat.
// Goldens are HAND-COMPUTED from Matrix4x4.Multiply (C.Mij = Σ_k A.Mik·B.Mkj), NOT from sw output.
//
// CASE — known rotation × known translation (the order A×B is load-bearing):
//   A = rotation about Z by 90° (row-major, System.Numerics CreateRotationZ form):
//        [ 0  1  0  0 ]      (M11=cos=0, M12=sin=1, M21=-sin=-1, M22=cos=0; rest identity)
//        [-1  0  0  0 ]
//        [ 0  0  1  0 ]
//        [ 0  0  0  1 ]
//   B = translation (tx,ty,tz)=(10,20,30). System.Numerics row-vector translation lives in
//       the BOTTOM row M41,M42,M43:
//        [ 1  0  0  0 ]
//        [ 0  1  0  0 ]
//        [ 0  0  1  0 ]
//        [10 20 30  1 ]
//   C = A×B (C.Mij = Σ_k A.Mik·B.Mkj). Since B's upper-left 3x3 is identity and translation is in
//   row 4, A×B = A with B's bottom row carried through (A row4 = (0,0,0,1) picks up B row4):
//        C row1 = A row1 · B = ( 0, 1, 0, 0 )
//        C row2 = A row2 · B = (-1, 0, 0, 0 )
//        C row3 = A row3 · B = ( 0, 0, 1, 0 )
//        C row4 = A row4 · B = (10,20,30, 1 )   (A.M44=1 selects B's row4 = translation)
//   So C.M11=0, C.M12=1, C.M21=-1, C.M33=1, C.M41=10, C.M42=20, C.M43=30, C.M44=1.
//
//   B×A would instead rotate the translation into the upper rows (different M41..M43) — the RED leg
//   swaps A↔B in the product to prove the order Matrix4x4.Multiply(A,B) is faithful.
int runMulMatrixSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // 16-float row-major identity helper
  auto setIdentity = [](float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
  };

  // Evaluate one Result entry of MulMatrix(A,B). When swapAB, feeds B as MatrixA and A as MatrixB
  // (the RED bite: proves A×B order matters — B×A gives a different translation block).
  auto evalMul = [&](const float* A, const float* B, const char* outPort, bool swapAB) -> float {
    const NodeSpec* spec = findSpec("MulMatrix");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "MulMatrix";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    const float* firstM  = swapAB ? B : A;
    const float* secondM = swapAB ? A : B;
    const char* aIds[16] = {"MatrixA.m11","MatrixA.m12","MatrixA.m13","MatrixA.m14",
                            "MatrixA.m21","MatrixA.m22","MatrixA.m23","MatrixA.m24",
                            "MatrixA.m31","MatrixA.m32","MatrixA.m33","MatrixA.m34",
                            "MatrixA.m41","MatrixA.m42","MatrixA.m43","MatrixA.m44"};
    const char* bIds[16] = {"MatrixB.m11","MatrixB.m12","MatrixB.m13","MatrixB.m14",
                            "MatrixB.m21","MatrixB.m22","MatrixB.m23","MatrixB.m24",
                            "MatrixB.m31","MatrixB.m32","MatrixB.m33","MatrixB.m34",
                            "MatrixB.m41","MatrixB.m42","MatrixB.m43","MatrixB.m44"};
    for (int i = 0; i < 16; ++i) {
      g.node(nid)->params[aIds[i]] = firstM[i];
      g.node(nid)->params[bIds[i]] = secondM[i];
    }
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // A = rot Z 90° (row-major); B = translate (10,20,30) in bottom row.
  float A[16]; setIdentity(A); A[0] = 0.0f; A[1] = 1.0f; A[4] = -1.0f; A[5] = 0.0f;
  float B[16]; setIdentity(B); B[12] = 10.0f; B[13] = 20.0f; B[14] = 30.0f;

  struct { const char* port; float want; } cases[] = {
      {"Result.m11", 0.0f},  {"Result.m12", 1.0f},  {"Result.m21", -1.0f}, {"Result.m22", 0.0f},
      {"Result.m33", 1.0f},  {"Result.m44", 1.0f},
      {"Result.m41", 10.0f}, {"Result.m42", 20.0f}, {"Result.m43", 30.0f},
  };
  for (auto& c : cases) {
    float got = evalMul(A, B, c.port, false);
    bool pass = std::fabs(got - c.want) < eps;
    ok = ok && pass;
    printf("[selftest-mulmatrix] (rotZ90 x trans(10,20,30)).%s=%.5f want=%.5f -> %s\n",
           c.port + 7, got, c.want, pass ? "PASS" : "FAIL");
  }

  // RED leg: swap A↔B. B×A rotates the translation into the upper-left block, so the translation
  // moves OUT of row4 — Result.m41 becomes 0 (not 10). When injectBug, assert the FAITHFUL value
  // (10) against the swapped (B×A) compute → mismatch → RED. This bites the A×B order specifically.
  {
    float got = evalMul(A, B, "Result.m41", injectBug /*swap when buggy*/);
    // B×A: translation row4 of A is (0,0,0,1) → row4 of B×A = A's row4 transformed... compute:
    //   (B×A).M41 = Σ_k B.M4k·A.Mk1 = B.M41·A.M11 + B.M42·A.M21 + B.M43·A.M31 + B.M44·A.M41
    //             = 10·0 + 20·(-1) + 30·0 + 1·0 = -20.   (≠ 10)
    float want = 10.0f;  // the FAITHFUL A×B value; swapped compute yields -20 → RED
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    printf("[selftest-mulmatrix] order A×B Result.m41=%.5f want(faithful)=%.5f %s-> %s\n",
           got, want, injectBug ? "(injected B×A) " : "", pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
