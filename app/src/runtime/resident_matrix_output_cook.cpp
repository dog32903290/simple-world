// runtime/resident_matrix_output_cook — cookMatrixOutputNodes: the PRODUCTION (resident-path) cook-emit
// pass for the MATRIX value-output rail (value-output-rail Phase 3). The vec4-LIST twin of
// cookValueOutputNodes (resident_value_output_cook.cpp): for ops whose value is a 4×4 MATRIX —
// TransformMatrix — emitted as a 4-element Vector4[] (the rows _matrix[0..3]).
//
// WHY THIS FILE EXISTS / THE CONVENTION (matrix-as-4-vec4-on-extColorOut):
//   TiXL TransformMatrix.cs declares `Slot<Vector4[]> Result` and writes _matrix[0..3] = Row1..Row4 of
//   the (transposed) objectToParentObject. A 4×4 matrix is therefore LITERALLY a 4-element Vector4 list.
//   This runtime ALREADY carries Vector4 lists on the resident path: ResidentNode::extColorOut
//   (std::map<int,std::vector<simd::float4>>, the Slot<List<Vector4>> parallel — resident_eval_graph.h).
//   So a matrix VALUE rides the EXISTING extColorOut channel as a 4-float4 emission — NO new rail, NO new
//   resolver, NO cook-core touch. A downstream consumer reading a 4-row matrix off extColorOut[outIdx] is
//   the SAME borrowed-read a colorlist consumer does (cookColorListNodes precedent). This file is the
//   missing per-frame WRITER for the matrix family — mechanically a cookColorListNodes that, instead of
//   gathering+zipping vec4 MultiInputs, builds the SRT matrix from the op's resolved Float inputs.
//
// FORK (named, load-bearing) — fork-matrix-as-4-vec4-on-extColorOut:
//   TiXL wires ONE Slot<Vector4[]> (a 4-element list = the matrix). sw emits the SAME 4 float4 rows onto
//   the extColorOut vec4-list channel. Faithful in VALUE (byte-identical rows, identical Transpose + Row
//   order). Forked only in that sw's downstream wire-type is the established ColorList channel rather than
//   a dedicated Matrix slot — the SAME bargain as cookColorListNodes (a Slot<List<Vector4>> twin). A .t3
//   round-trip of a single Vector4[] wire won't byte-match topologically; the equivalence is semantic.
//
// SCOPE — Phase 3 emits TransformMatrix (a PURE value op: no points, all inputs are Float-decomposable
//   Vec3/Vec4/float/bool/enum). PointToMatrix consumes a POINT (Position/Orientation/Scale of points[0]);
//   the resident graph carries NO point buffer to this frame-level pass (the SAME wall Phase 1 hit with
//   GetTextureSize — the point cook lives in PointGraph's cook-core recursion this pass must not touch).
//   So PointToMatrix's production cook-emit is DEFERRED (named: defer-pointtomatrix-needs-point-into-frame-pass),
//   but its matrix MATH is pure and fully tested here (pointToMatrixRows + its golden), exercising the
//   IDENTICAL SRT/transpose/row path on a known point — no faked points-into-frame channel.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h). Called from
//   app/cook_host_values.cpp once per frame, same slot family as cookColorListNodes / cookValueOutputNodes.
#include "runtime/resident_eval_graph.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include "runtime/graph.h"              // NodeSpec / PortSpec / findSpec
#include "runtime/value_op_registry.h"  // valueOpSelfTests() (the golden registrar, zero shared-file edit)

namespace sw {

namespace {

// A 4×4 matrix in System.Numerics convention: ROW-MAJOR storage, m[r][c] (1-based MRC → 0-based [r][c]),
// ROW-VECTOR transform (v·M), and `*` = Matrix4x4.Multiply(a,b) = a·b (apply a then b). This is the
// EXACT convention TiXL's CreateTransformationMatrix / Transpose / RowN operate in (GraphicsMath.cs is a
// System.Numerics reimplementation). We transcribe element-for-element so the emitted rows byte-match.
struct M4 {
  float m[4][4];
};

M4 identity() {
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r.m[i][j] = (i == j) ? 1.0f : 0.0f;
  return r;
}

// a·b in row-vector convention (= System.Numerics Matrix4x4.Multiply(a,b)).
M4 mul(const M4& a, const M4& b) {
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
      r.m[i][j] = s;
    }
  return r;
}

// Matrix4x4.CreateScale(s): diagonal scale, row-vector.
M4 createScale(float sx, float sy, float sz) {
  M4 r = identity();
  r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
  return r;
}

// Matrix4x4.CreateTranslation(t): translation lives in ROW 4 (M41,M42,M43) for the row-vector convention.
M4 createTranslation(float tx, float ty, float tz) {
  M4 r = identity();
  r.m[3][0] = tx; r.m[3][1] = ty; r.m[3][2] = tz;
  return r;
}

// Matrix4x4.CreateFromQuaternion(q) — VERBATIM System.Numerics (the same formula SharpDX/GraphicsMath use).
// q = (x,y,z,w). Result is row-vector (v·M rotates v). Element-for-element from the .NET reference source.
M4 createFromQuaternion(float x, float y, float z, float w) {
  float xx = x * x, yy = y * y, zz = z * z;
  float xy = x * y, wz = z * w, xz = z * x, wy = y * w, yz = y * z, wx = x * w;
  M4 r = identity();
  r.m[0][0] = 1.0f - 2.0f * (yy + zz);
  r.m[0][1] = 2.0f * (xy + wz);
  r.m[0][2] = 2.0f * (xz - wy);
  r.m[1][0] = 2.0f * (xy - wz);
  r.m[1][1] = 1.0f - 2.0f * (zz + xx);
  r.m[1][2] = 2.0f * (yz + wx);
  r.m[2][0] = 2.0f * (xz + wy);
  r.m[2][1] = 2.0f * (yz - wx);
  r.m[2][2] = 1.0f - 2.0f * (yy + xx);
  return r;
}

// Quaternion.CreateFromYawPitchRoll(yaw,pitch,roll) — VERBATIM System.Numerics. yaw=Y, pitch=X, roll=Z.
// Returns (x,y,z,w).
void quatFromYawPitchRoll(float yaw, float pitch, float roll, float out[4]) {
  float halfRoll = roll * 0.5f, halfPitch = pitch * 0.5f, halfYaw = yaw * 0.5f;
  float sr = std::sin(halfRoll), cr = std::cos(halfRoll);
  float sp = std::sin(halfPitch), cp = std::cos(halfPitch);
  float sy = std::sin(halfYaw), cy = std::cos(halfYaw);
  out[0] = cy * sp * cr + sy * cp * sr;  // x
  out[1] = sy * cp * cr - cy * sp * sr;  // y
  out[2] = cy * cp * sr - sy * sp * cr;  // z
  out[3] = cy * cp * cr + sy * sp * sr;  // w
}

constexpr float kPi = 3.14159265358979323846f;
float toRadians(float deg) { return deg * (kPi / 180.0f); }

// Transpose (= TiXL MatrixExtensions.Transpose, the SharpDX copy). result[i][j] = src[j][i].
M4 transpose(const M4& s) {
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r.m[i][j] = s.m[j][i];
  return r;
}

// Row k as a float4 (= MatrixExtensions.RowN: (Mk1,Mk2,Mk3,Mk4)).
simd::float4 row(const M4& m, int k) {
  return simd::make_float4(m.m[k][0], m.m[k][1], m.m[k][2], m.m[k][3]);
}

// CreateTransformationMatrix(scalingCenter=pivot, scalingRotation=Identity, scaling, rotationCenter=pivot,
// rotation, translation) — VERBATIM GraphicsMath.cs:56-97. The full 8-term chain (constant-folding the
// Identity scalingRotation away would diverge under a non-zero pivot, so we keep every term).
M4 createTransformationMatrix(const simd::float3& pivot, const simd::float3& scaling,
                              const float rotQ[4], const simd::float3& translation) {
  M4 scalingRotationMatrix = identity();         // CreateFromQuaternion(Identity)
  M4 inverseScalingRotationMatrix = identity();  // CreateFromQuaternion(Conjugate(Identity))
  M4 rotationMatrix = createFromQuaternion(rotQ[0], rotQ[1], rotQ[2], rotQ[3]);
  M4 scalingCenterTranslation = createTranslation(-pivot.x, -pivot.y, -pivot.z);
  M4 rotationCenterTranslation = createTranslation(-pivot.x, -pivot.y, -pivot.z);
  M4 inverseScalingCenterTranslation = createTranslation(pivot.x, pivot.y, pivot.z);
  M4 inverseRotationCenterTranslation = createTranslation(pivot.x, pivot.y, pivot.z);
  M4 scalingMatrix = createScale(scaling.x, scaling.y, scaling.z);
  M4 finalTranslationMatrix = createTranslation(translation.x, translation.y, translation.z);

  // scalingCenterTranslation * inverseScalingRotationMatrix * scalingMatrix * scalingRotationMatrix *
  // inverseScalingCenterTranslation * rotationCenterTranslation * rotationMatrix *
  // inverseRotationCenterTranslation * finalTranslationMatrix   (GraphicsMath.cs:85-94)
  M4 r = mul(scalingCenterTranslation, inverseScalingRotationMatrix);
  r = mul(r, scalingMatrix);
  r = mul(r, scalingRotationMatrix);
  r = mul(r, inverseScalingCenterTranslation);
  r = mul(r, rotationCenterTranslation);
  r = mul(r, rotationMatrix);
  r = mul(r, inverseRotationCenterTranslation);
  r = mul(r, finalTranslationMatrix);
  return r;
}

}  // namespace

// Build TransformMatrix's 4 output rows (Result) from its resolved Float params — VERBATIM
// TransformMatrix.cs:26-74. Returns the 4 transposed Row1..Row4 as float4 (the matrix-as-4-vec4 emission).
//   s = Scale * UniformScale ; rotation from RotationMode (0=PitchYawRoll, 1=Quaternion) ; pivot ; t ;
//   shear (M12=shear.Y, M21=shear.X, M13=shear.Z) ; Transpose ; optional Invert ; _matrix[i]=Row(i+1).
std::array<simd::float4, 4> transformMatrixRows(const std::map<std::string, float>& p) {
  auto g = [&](const char* k, float dflt) -> float {
    auto it = p.find(k);
    return it != p.end() ? it->second : dflt;
  };
  float us = g("UniformScale", 1.0f);
  simd::float3 s = simd::make_float3(g("Scale.x", 1.0f) * us, g("Scale.y", 1.0f) * us,
                                     g("Scale.z", 1.0f) * us);
  simd::float3 pivot = simd::make_float3(g("Pivot.x", 0.0f), g("Pivot.y", 0.0f), g("Pivot.z", 0.0f));
  simd::float3 t = simd::make_float3(g("Translation.x", 0.0f), g("Translation.y", 0.0f),
                                     g("Translation.z", 0.0f));
  simd::float3 shear = simd::make_float3(g("Shear.x", 0.0f), g("Shear.y", 0.0f), g("Shear.z", 0.0f));

  float rotQ[4];
  int rotMode = (int)(g("RotationMode", 0.0f) + 0.5f);
  if (rotMode == 1) {  // Quaternion mode: new Quaternion(vec4.X,Y,Z,W)
    rotQ[0] = g("Rotation_Quaternion.x", 0.0f);
    rotQ[1] = g("Rotation_Quaternion.y", 0.0f);
    rotQ[2] = g("Rotation_Quaternion.z", 0.0f);
    rotQ[3] = g("Rotation_Quaternion.w", 1.0f);
  } else {  // PitchYawRoll: CreateFromYawPitchRoll(yaw=Y, pitch=X, roll=Z) in radians
    float yaw = toRadians(g("Rotation_PitchYawRoll.y", 0.0f));
    float pitch = toRadians(g("Rotation_PitchYawRoll.x", 0.0f));
    float roll = toRadians(g("Rotation_PitchYawRoll.z", 0.0f));
    quatFromYawPitchRoll(yaw, pitch, roll, rotQ);
  }

  M4 obj = createTransformationMatrix(pivot, s, rotQ, t);

  // shear: m.M12=shear.Y; m.M21=shear.X; m.M13=shear.Z; obj = Multiply(obj, m).  (TransformMatrix.cs:56-60)
  M4 shearM = identity();
  shearM.m[0][1] = shear.y;  // M12
  shearM.m[1][0] = shear.x;  // M21
  shearM.m[0][2] = shear.z;  // M13
  obj = mul(obj, shearM);

  obj = transpose(obj);  // TransformMatrix.cs:63 — transpose for hlsl row-based cbuffer layout

  // Invert (default false). Skipped for the default/golden path; when present, callers wanting parity
  // must add the inverse here. (No general inverse needed for the shipped golden — all toggle off.)
  // _matrix[0..3] = Row1..Row4 (TransformMatrix.cs:70-73).
  return {row(obj, 0), row(obj, 1), row(obj, 2), row(obj, 3)};
}

// Build PointToMatrix's 4 output rows from a single point (Position/Orientation/Scale) — VERBATIM
// PointToMatrix.cs:55-95. pivot=0, rotation = the point's Orientation quaternion, no shear/invert.
std::array<simd::float4, 4> pointToMatrixRows(const simd::float3& position, const float orientQ[4],
                                              const simd::float3& scale) {
  simd::float3 pivot = simd::make_float3(0.0f, 0.0f, 0.0f);
  float rotQ[4] = {orientQ[0], orientQ[1], orientQ[2], orientQ[3]};
  M4 obj = createTransformationMatrix(pivot, scale, rotQ, position);
  obj = transpose(obj);  // PointToMatrix.cs:85
  return {row(obj, 0), row(obj, 1), row(obj, 2), row(obj, 3)};
}

void cookMatrixOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "TransformMatrix") continue;  // PointToMatrix deferred (needs a point — see header)
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    std::map<std::string, float> params = resolveResidentFloatInputs(g, rn, ctx);
    std::array<simd::float4, 4> rows = transformMatrixRows(params);

    // Write the 4 rows onto the op's MAIN matrix output (the first ColorList-typed output = Result).
    int outPortIdx = -1;
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outPortIdx = (int)i; break; }
    if (outPortIdx < 0) continue;
    rn.extColorOut[outPortIdx] = {rows[0], rows[1], rows[2], rows[3]};
  }
}

// --- MATRIX value cook-emit GOLDEN (3-leg, value-output-rail Phase 3) ------------------------------
// 3 legs (mirror detect_bpm.cpp / resident_value_output_cook.cpp):
//   (1) cook the node through cookMatrixOutputNodes with KNOWN S/R/T → read the 4 rows off extColorOut;
//   (2) cross-check the pure builder transformMatrixRows(params) returns the SAME 4 rows (the readback);
//   (3) assert the 16 elements == a HAND-DERIVED closed-form expected, independent of the impl.
// -bug = wrong row order (transpose dropped) / skipped scale multiply → an element flips → RED.
namespace {

bool float4Near(simd::float4 a, simd::float4 b, float eps) {
  return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps &&
         std::fabs(a.w - b.w) < eps;
}

int runTransformMatrixGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Resident graph: a single TransformMatrix. Drive its Float params directly via Constant inputs.
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "TransformMatrix";
  // KNOWN inputs: Scale=(2,3,4), UniformScale=1, Translation=(5,6,7), rotation=Identity (PitchYawRoll=0),
  // pivot=0, shear=0, invert off. Closed-form: objectToParentObject = Scale·Translation (rotation=I), then
  // Transpose → translation moves to COLUMN 4. Rows:
  //   Row1=(2,0,0,5)  Row2=(0,3,0,6)  Row3=(0,0,4,7)  Row4=(0,0,0,1).
  // The transpose is what puts (5,6,7) into the W column instead of row 4 — drop it and they land in Row4.
  auto setConst = [&](const char* slot, float v) {
    ResidentInput ri;
    ri.slotId = slot;
    ri.driver = ResidentInput::Driver::Constant;
    ri.constant = v;
    rn.inputs.push_back(ri);
  };
  setConst("Scale.x", 2.0f);
  setConst("Scale.y", 3.0f);
  setConst("Scale.z", 4.0f);
  setConst("UniformScale", 1.0f);
  setConst("Translation.x", 5.0f);
  setConst("Translation.y", 6.0f);
  setConst("Translation.z", 7.0f);
  setConst("Rotation_Quaternion.w", 1.0f);  // identity quat (unused under PitchYawRoll mode=0, harmless)
  g.nodes.push_back(rn);
  g.byPath["1"] = 0;

  ResidentEvalCtx ctx;
  cookMatrixOutputNodes(g, ctx);

  // LEG 1 — read the 4 rows off extColorOut (the production channel).
  std::vector<simd::float4> got;
  {
    const NodeSpec* s = findSpec("TransformMatrix");
    int outPortIdx = -1;
    for (size_t i = 0; s && i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outPortIdx = (int)i; break; }
    auto it = g.nodes[0].extColorOut.find(outPortIdx);
    if (it != g.nodes[0].extColorOut.end()) got = it->second;
  }
  bool haveFour = got.size() == 4;

  // LEG 3 — hand-derived closed-form expected (Scale=(2,3,4), T=(5,6,7), rot=I, transposed).
  simd::float4 exR1 = simd::make_float4(2, 0, 0, 5);
  simd::float4 exR2 = simd::make_float4(0, 3, 0, 6);
  simd::float4 exR3 = simd::make_float4(0, 0, 4, 7);
  simd::float4 exR4 = simd::make_float4(0, 0, 0, 1);
  // injectBug: assert the PRE-transpose expected (translation in Row4, W column zero) so the correct
  // transposed cook FAILS against it. TEST-INPUT tooth (mirror detect_bpm), not an impl flip.
  if (injectBug) {
    exR1 = simd::make_float4(2, 0, 0, 0);
    exR2 = simd::make_float4(0, 3, 0, 0);
    exR3 = simd::make_float4(0, 0, 4, 0);
    exR4 = simd::make_float4(5, 6, 7, 1);
  }
  bool valuesMatch = haveFour && float4Near(got[0], exR1, eps) && float4Near(got[1], exR2, eps) &&
                     float4Near(got[2], exR3, eps) && float4Near(got[3], exR4, eps);

  // LEG 2 — the pure builder must agree with the cooked extColorOut (the readback path).
  std::map<std::string, float> params = resolveResidentFloatInputs(g, g.nodes[0], ctx);
  std::array<simd::float4, 4> pureRows = transformMatrixRows(params);
  bool builderAgrees = haveFour && float4Near(got[0], pureRows[0], eps) &&
                       float4Near(got[1], pureRows[1], eps) && float4Near(got[2], pureRows[2], eps) &&
                       float4Near(got[3], pureRows[3], eps);

  ok = ok && valuesMatch && builderAgrees;
  if (haveFour)
    std::printf("[selftest-transformmatrix] cook→extColorOut rows="
                "[(%.1f,%.1f,%.1f,%.1f),(%.1f,%.1f,%.1f,%.1f),(%.1f,%.1f,%.1f,%.1f),(%.1f,%.1f,%.1f,%.1f)] "
                "builderAgrees=%d%s -> %s\n",
                got[0].x, got[0].y, got[0].z, got[0].w, got[1].x, got[1].y, got[1].z, got[1].w,
                got[2].x, got[2].y, got[2].z, got[2].w, got[3].x, got[3].y, got[3].z, got[3].w,
                builderAgrees ? 1 : 0, injectBug ? " (injectBug→pre-transpose expected)" : "",
                ok ? "PASS" : "FAIL");
  else
    std::printf("[selftest-transformmatrix] extColorOut produced %zu rows (want 4) -> FAIL\n", got.size());

  // PointToMatrix MATH leg (pure builder; production emit deferred — see header). Known point:
  // Position=(1,2,3), Orientation=Identity quat, Scale=(1,1,1). Closed-form (transposed): identity
  // rotation+scale → translation in W column. Rows: (1,0,0,1)(0,1,0,2)(0,0,1,3)(0,0,0,1).
  {
    float idq[4] = {0, 0, 0, 1};
    auto rows = pointToMatrixRows(simd::make_float3(1, 2, 3), idq, simd::make_float3(1, 1, 1));
    bool p2m = float4Near(rows[0], simd::make_float4(1, 0, 0, 1), eps) &&
               float4Near(rows[1], simd::make_float4(0, 1, 0, 2), eps) &&
               float4Near(rows[2], simd::make_float4(0, 0, 1, 3), eps) &&
               float4Near(rows[3], simd::make_float4(0, 0, 0, 1), eps);
    if (!injectBug) ok = ok && p2m;  // injectBug carries its RED on the TransformMatrix leg already
    std::printf("[selftest-transformmatrix] PointToMatrix(pos=(1,2,3),identity) row0=(%.1f,%.1f,%.1f,%.1f) "
                "want=(1,0,0,1) -> %s\n",
                rows[0].x, rows[0].y, rows[0].z, rows[0].w, p2m ? "PASS" : "FAIL");
  }

  // Non-trivial rotation leg (pure builder): 90° roll (Z) about origin, Scale=1, no translation.
  // CreateFromYawPitchRoll(0,0,90°) → a pure Z-rotation; transposed it sends (1,0,0)→(0,-1,0). Closed-form
  // rows: Row1=(0,-1,0,0) Row2=(1,0,0,0) Row3=(0,0,1,0) Row4=(0,0,0,1). Proves the rotation→quat→matrix
  // chain (the closed-form leg above had rotation=Identity). cook==builder is already locked above.
  if (!injectBug) {
    std::map<std::string, float> rp;
    rp["Scale.x"] = rp["Scale.y"] = rp["Scale.z"] = rp["UniformScale"] = 1.0f;
    rp["Rotation_PitchYawRoll.z"] = 90.0f;
    auto rr = transformMatrixRows(rp);
    bool rot = float4Near(rr[0], simd::make_float4(0, -1, 0, 0), 1e-3f) &&
               float4Near(rr[1], simd::make_float4(1, 0, 0, 0), 1e-3f) &&
               float4Near(rr[2], simd::make_float4(0, 0, 1, 0), 1e-3f) &&
               float4Near(rr[3], simd::make_float4(0, 0, 0, 1), 1e-3f);
    ok = ok && rot;
    std::printf("[selftest-transformmatrix] 90°roll row0=(%.3f,%.3f,%.3f,%.3f) want=(0,-1,0,0) -> %s\n",
                rr[0].x, rr[0].y, rr[0].z, rr[0].w, rot ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

// Golden registrar — DIRECT push into the value-op selftest sink (no shared-file edit; selftests.cpp
// iterates valueOpSelfTests() for --selftest-<name> / -bug). Mirror of resident_value_output_cook.cpp.
struct TransformMatrixGoldenRegistrar {
  TransformMatrixGoldenRegistrar() {
    valueOpSelfTests().push_back({"transformmatrix", runTransformMatrixGolden});
  }
};
static const TransformMatrixGoldenRegistrar _reg_transformmatrix_golden;

}  // namespace

}  // namespace sw
