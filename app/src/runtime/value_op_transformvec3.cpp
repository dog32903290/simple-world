// TransformVec3 value op (value-op self-registration seam leaf — numbers/vec3 matrix mining).
// TiXL authority: Operators/Lib/numbers/vec3/TransformVec3.cs (+ TransformVec3.t3 defaults).
//
//   TransformVec3.cs Update():
//     var a = A.GetValue(context);          // Vector3
//     var m = Matrix.GetValue(context);     // Matrix4x4
//     m.Transpose();                        // ← LOAD-BEARING
//     var v4 = Vector4.Transform(new Vector4(a, 1.0f), m);
//     Result.Value = new Vector3(v4.X, v4.Y, v4.Z);
//
//   System.Numerics semantics (the parity crux):
//     Vector4.Transform(v, M) is the ROW-VECTOR transform:  result.j = Σ_i v_i · M_ij.
//     TiXL transposes m FIRST, so with m' = mᵀ:
//        result.j = Σ_i v4_i · (mᵀ)_ij = Σ_i v4_i · m_ji
//     i.e. with v4 = (ax, ay, az, 1):
//        Result.X = ax·M11 + ay·M12 + az·M13 + 1·M14
//        Result.Y = ax·M21 + ay·M22 + az·M23 + 1·M24
//        Result.Z = ax·M31 + ay·M32 + az·M33 + 1·M34
//     => the transpose converts System.Numerics' row-vector transform into a COLUMN-VECTOR
//        transform on the ORIGINAL matrix:  Result = (M · [ax,ay,az,1]ᵀ).xyz.
//     DROPPING the transpose would compute Σ_i v4_i·M_ij (= Mᵀ·v form) and change the answer for
//     any asymmetric M — that asymmetry is exactly what the RED leg exercises.
//
//   Ports (from TransformVec3.cs field order):
//     A      = InputSlot<Vector3>    (TransformVec3.cs A)
//     Matrix = InputSlot<Matrix4x4>  (TransformVec3.cs Matrix)
//     Output: Result = Slot<Vector3> (TransformVec3.cs Result)
//
//   TransformVec3.t3 DefaultValues: A = {X:1,Y:1,Z:1}, Matrix = Identity.
//
// ──────────────────────────────────────────────────────────────────────────────────────────────
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   A Matrix4x4 = 16 consecutive Float ports, row-major (fork-mat4-as-16-floats, mirror of
//   fork-vec3-as-3-floats). A Vector3 = 3 Float ports. Both are plain Float wires → purely
//   additive on the value-op registry; no new dataType, no cook-spine change.
//
//   in[] = [A.x, A.y, A.z (3), M.m11..M.m44 (16)]  (n = 19)
//   Output ports Result.x/.y/.z follow at spec indices 19/20/21.
//   Component k = outIdx - n (0=x,1=y,2=z).
//   eval: Result.k = Σ_i v4_i · M_(k,i)  with v4 = (A.x, A.y, A.z, 1)   (= M·v4, the transpose).
//
// FORKS (named):
//   - fork-transformvec3-mat4-as-16-floats: the Matrix input is 16 consecutive Float ports
//     (row-major). The transform math is byte-identical to TiXL (transpose then Vector4.Transform).
//     Not an eval fork — purely the wire-decomposition convention.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runTransformVec3SelfTest(bool injectBug);

// Pure evaluate fn (LOCAL to this leaf — no shared value_eval_ops edit; the seam stays additive).
//   in[0..2]   = A (ax, ay, az)
//   in[3..18]  = M row-major (M.m11..M.m44),  M[r*4 + c] = M_(r+1)(c+1)
//   outIdx     = output port index; component k = outIdx - n  (0=x,1=y,2=z)
//   Result.k = ax·M(k,0) + ay·M(k,1) + az·M(k,2) + 1·M(k,3)    (= row k of M dotted with [ax,ay,az,1])
//     i.e. the transposed transform: Result = M · [ax,ay,az,1]ᵀ, then take xyz.
float evalTransformVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 19) return NAN;             // not fully gathered → bite every golden
  const int k = outIdx - n;           // 0=x,1=y,2=z
  if (k < 0 || k >= 3) return 0.0f;
  const float ax = in[0], ay = in[1], az = in[2];
  const float* M = in + 3;            // 16 floats, row-major
  // Row k of M dotted with [ax,ay,az,1]:
  return ax * M[k * 4 + 0] + ay * M[k * 4 + 1] + az * M[k * 4 + 2] + 1.0f * M[k * 4 + 3];
}

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests().
// Port order MUST match evalTransformVec3's in[] read: A.x/y/z, then M.m11..M.m44, then Result.x/y/z.
// Defaults from TransformVec3.t3: A = {1,1,1}, Matrix = Identity.
static const ValueOp _reg_transformvec3{
    {"TransformVec3", "TransformVec3",
     {// A (Vector3) — Vec head arity 3 on .x (t3 default {1,1,1})
      {"A.x", "A",   "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 3},
      {"A.y", "A.y", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"A.z", "A.z", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      // Matrix (row-major; .m11 is the Vec head of arity 16 → one Inspector block; identity default)
      {"Matrix.m11", "Matrix",     "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 16},
      {"Matrix.m12", "Matrix.m12", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m13", "Matrix.m13", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m14", "Matrix.m14", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m21", "Matrix.m21", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m22", "Matrix.m22", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m23", "Matrix.m23", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m24", "Matrix.m24", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m31", "Matrix.m31", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m32", "Matrix.m32", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m33", "Matrix.m33", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m34", "Matrix.m34", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m41", "Matrix.m41", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m42", "Matrix.m42", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m43", "Matrix.m43", "Float", true, 0.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      {"Matrix.m44", "Matrix.m44", "Float", true, 1.0f, -1e9f, 1e9f, Widget::Vec, {}, false, 1},
      // Result (Vector3)
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalTransformVec3,
     "numbers.vec3"},
    "transformvec3", runTransformVec3SelfTest};

// --- TransformVec3 MATH golden -----------------------------------------------------------------
// Builds a 1-node TransformVec3 graph, sets A + the 16 matrix entries, pulls Result.x/.y/.z.
// Goldens HAND-COMPUTED from TransformVec3.cs (transpose then Vector4.Transform), NOT from sw output.
//
// CASE — rotation+translation chosen so the answer is non-degenerate AND the transpose matters:
//   M = rot Z 90° with a translation in the bottom row (System.Numerics row-major):
//        M11=0  M12=1  M13=0  M14=0
//        M21=-1 M22=0  M23=0  M24=0
//        M31=0  M32=0  M33=1  M34=0
//        M41=5  M42=7  M43=9  M44=1
//   A = (1, 2, 3).
//   Faithful result (transpose ⇒ row k of M · [1,2,3,1]):
//        Result.X = 1·M11 + 2·M12 + 3·M13 + 1·M14 = 1·0 + 2·1 + 3·0 + 0 =  2
//        Result.Y = 1·M21 + 2·M22 + 3·M23 + 1·M24 = 1·(-1)+2·0 +3·0 + 0 = -1
//        Result.Z = 1·M31 + 2·M32 + 3·M33 + 1·M34 = 0 + 0 + 3·1 + 0     =  3
//   (Note: the translation row M41/M42/M43 does NOT contribute here — that is the correct
//    System.Numerics behaviour: after transpose, translation sits in the 4th COLUMN (M14/M24/M34),
//    so a row-4 translation in the ORIGINAL M is multiplied by v4.x/y/z's 4th slot only via column 4.
//    With original-row-4 translation, those land in M41..M43 which the transpose moves to column 4 =
//    (M14',M24',M34') = (M41,M42,M43) — wait: see RED leg, which is precisely what disambiguates.)
//
//   The DROP-TRANSPOSE bug computes Σ_i v4_i·M_ij (column j of M · v4):
//        bug.X = 1·M11 + 2·M21 + 3·M31 + 1·M41 = 0 + 2·(-1) + 0 + 5 =  3   (≠ faithful 2)
//   So Result.X faithful=2 vs drop-transpose=3 → the RED leg flips the eval to no-transpose form.
int runTransformVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // M row-major: rotZ90 + bottom-row translation (5,7,9).
  float M[16] = {
      0.0f,  1.0f, 0.0f, 0.0f,   // row1
     -1.0f,  0.0f, 0.0f, 0.0f,   // row2
      0.0f,  0.0f, 1.0f, 0.0f,   // row3
      5.0f,  7.0f, 9.0f, 1.0f,   // row4 (translation)
  };
  const float ax = 1.0f, ay = 2.0f, az = 3.0f;

  // Evaluate one Result component. `noTranspose` injects the bug: compute the column-vector form
  // Σ_i v4_i·M_ij (i.e. as if m.Transpose() were dropped) by feeding M's TRANSPOSE into the node —
  // the faithful eval(M^T) == drop-transpose(M), so this exercises the exact missing-transpose path.
  auto evalT = [&](const char* outPort, bool noTranspose) -> float {
    const NodeSpec* spec = findSpec("TransformVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "TransformVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A.x"] = ax; g.node(nid)->params["A.y"] = ay; g.node(nid)->params["A.z"] = az;
    const char* mIds[16] = {"Matrix.m11","Matrix.m12","Matrix.m13","Matrix.m14",
                            "Matrix.m21","Matrix.m22","Matrix.m23","Matrix.m24",
                            "Matrix.m31","Matrix.m32","Matrix.m33","Matrix.m34",
                            "Matrix.m41","Matrix.m42","Matrix.m43","Matrix.m44"};
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) {
        // noTranspose feeds M^T → eval's faithful (transposing) math then reproduces the
        // drop-transpose result on the original M. Otherwise feed M as-is.
        float v = noTranspose ? M[c * 4 + r] : M[r * 4 + c];
        g.node(nid)->params[mIds[r * 4 + c]] = v;
      }
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  struct { const char* port; float want; } cases[] = {
      {"Result.x", 2.0f}, {"Result.y", -1.0f}, {"Result.z", 3.0f},
  };
  for (auto& c : cases) {
    float got = evalT(c.port, false);
    bool pass = std::fabs(got - c.want) < eps;
    ok = ok && pass;
    printf("[selftest-transformvec3] (rotZ90+trans · (1,2,3)).%s=%.5f want=%.5f -> %s\n",
           c.port + 7, got, c.want, pass ? "PASS" : "FAIL");
  }

  // RED leg: drop the transpose. Faithful Result.x = 2; drop-transpose form = 3. When injectBug,
  // feed M^T so the (correct, transposing) eval reproduces the no-transpose answer (3) and the
  // assertion against the faithful want (2) FAILS. This bites the transpose specifically.
  {
    float got = evalT("Result.x", injectBug /*drop transpose when buggy*/);
    float want = 2.0f;  // faithful (with-transpose); drop-transpose yields 3 → RED
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    printf("[selftest-transformvec3] transpose Result.x=%.5f want(faithful)=%.5f %s-> %s\n",
           got, want, injectBug ? "(injected drop-transpose) " : "", pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
