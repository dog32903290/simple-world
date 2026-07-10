// selftests_mathv_transformfromclipspace_probes.cpp — special-matrix/quirk/diagnostic probes for the
// TransformFromClipSpace mathv TU, split out of selftests_mathv_transformfromclipspace.cpp (400-line
// ratchet, same shape as selftests_mathv_snappointstogrid_probes.cpp). Called from
// runMathvTransformFromClipSpaceSelfTest() (the other TU) via the declarations in
// selftests_mathv_transformfromclipspace_shared.h; not independently registered/dispatched.
//
// ── quirk probes covered ────────────────────────────────────────────────────────────────────────
//  ① w=0 degenerate matrix: checkWZeroTooth — Inf/NaN division-edge parity (R self-check case 4 style).
//  ② non-orthogonal/shear matrix fed to qFromMatrix3: checkRotationTooth's random-affine corpus
//     already produces generically non-orthogonal upper-left 3×3s (scale != uniform breaks
//     orthogonality even before the rotation factors are considered); the exact conjugate-mismatch
//     pattern this triggers against ref is instrumented by checkRotationConventionDiagnostic (an
//     isolated, minimal repro, not guessed from aggregate stats — D 儀器化紀律).
//
// See selftests_mathv_transformfromclipspace_shared.h for the PINNED PARITY banner (rotation ==
// Shepperd(transpose(R)) on BOTH sides after S's audit) that checkRotationConventionDiagnostic below
// cross-checks and checkPinnedEyeTooth anchors to an absolute literal (S's eye=(3,2,4) case).
//
// ZONE: shell tier (app/src/ root, mathv support). Same zone as the TU it splits from. Crosses
// runtime for quat_host.h (pure host math, no Metal/platform — same leaf class as tixl_point.h).
#include "selftests_mathv_transformfromclipspace_shared.h"
#include "runtime/quat_host.h"  // qFromMatrix3PreciseHost -- INDEPENDENT oracle, pre-existing/in-prod

#include <cstdio>
#include <vector>

namespace sw {
namespace mathv_tfcs_shared {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::Rng;

// ── TOOTH ROTATION: random affine matrix (composeMat16, generically NON-symmetric upper-left 3x3 --
// quirk probe ② non-orthogonal-matrix coverage) + random UNIT-quaternion Rotation, compared against
// R's ref over Position(3)+Rotation(4)=7 lanes. GREEN: ref restores Shepperd(transpose(R)) and the
// kernel is restored to 抽row (columns = R's rows ≡ transpose), so both sides agree on asymmetric
// matrices too (the a3068d8 conjugate divergence is retired -- shared header PINNED PARITY banner).
bool checkRotationTooth(const TfcsDispatch& disp) {
  Comparator cmp("mathv-transformfromclipspace-rotation", EpsSpec::transcendental(), 6);
  Rng rng(mathv::mathvSeed("transformfromclipspace-rotation"));
  const auto& dom = paramTable();
  const size_t N = 256;
  bool dispatchOk = true;
  for (int v = 0; v < 8; ++v) {
    float P[9];
    for (int k = 0; k < 9; ++k) P[k] = mathv::sampleUniform(rng, dom[(size_t)k]);
    Mat16 M = composeMat16(P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7], P[8]);
    HostParams hp{};
    for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = M[(size_t)k];
    mathv_ref::TransformFromClipSpaceParams rprm{mat4FromArray(M)};
    std::vector<SwPoint> src(N), dst;
    for (size_t i = 0; i < N; ++i) {
      src[i].Position = randomPos3(rng);
      QuatF q = randomUnitQuat(rng);
      src[i].Rotation = SW_FLOAT4{q.x, q.y, q.z, q.w};
    }
    if (!disp.dispatch(hp, src, dst) || dst.size() != N) { dispatchOk = false; continue; }
    for (size_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::transformFromClipSpaceOne(src[i], refOut, (uint32_t)i, (uint32_t)N, rprm);
      float in7[7] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                      src[i].Rotation.x, src[i].Rotation.y, src[i].Rotation.z, src[i].Rotation.w};
      float g[7] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z,
                    dst[i].Rotation.x, dst[i].Rotation.y, dst[i].Rotation.z, dst[i].Rotation.w};
      float r[7] = {refOut.Position.x, refOut.Position.y, refOut.Position.z,
                    refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
      for (int k = 0; k < 7; ++k) cmp.add(g[k], r[k], in7, 7, k, -1.0f, "rotation-random-affine");
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── TOOTH SPECIAL MATRICES: identity / pure-translation / pure-rotation(180-about-X) /
// degenerate-det0 / w-row-nontrivial(true clip-projection form). Every case's upper-left 3x3 is
// DIAGONAL (rotation angles pinned 0, only scale+translation+w-row vary) -> trivially SYMMETRIC ->
// sidesteps the KNOWN FINDING's transpose divergence, so this tooth stays a clean, meaningful
// Position+Rotation gate (both channels expected GREEN).
bool checkSpecialMatrixTooth(const TfcsDispatch& disp) {
  Comparator cmp("mathv-transformfromclipspace-specialmatrix", EpsSpec::transcendental(), 8);
  struct Case { const char* tag; Mat16 M; };
  std::vector<Case> cases = {
      {"identity", composeMat16(0, 0, 0, 1, 1, 1, 0, 0, 0)},
      {"pure-translation", composeMat16(0, 0, 0, 1, 1, 1, 7.5f, -3.25f, 12.0f)},
      // diag(1,-1,-1) = a genuine 180-deg-about-X rotation (det=+1), DIAGONAL hence symmetric.
      {"pure-rotation-180x", composeMat16(0, 0, 0, 1, -1, -1, 0, 0, 0)},
      // diag(1,1,0): singular (det=0), still symmetric.
      {"degenerate-det0", composeMat16(0, 0, 0, 1, 1, 0, 0, 0, 0)},
  };
  {  // w-row nontrivial: identity 3x3 (symmetric) + translation + a real perspective row.
    Mat16 M = composeMat16(0, 0, 0, 1, 1, 1, 2.0f, -1.0f, 0.5f);
    M[3] = 0.02f; M[7] = -0.015f; M[11] = 0.01f; M[15] = 1.3f;  // m03,m13,m23,m33
    cases.push_back({"perspective-w-row", M});
  }
  Rng rng(mathv::mathvSeed("transformfromclipspace-specialmatrix"));
  bool dispatchOk = true;
  for (const Case& cs : cases) {
    HostParams hp{};
    for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = cs.M[(size_t)k];
    mathv_ref::TransformFromClipSpaceParams rprm{mat4FromArray(cs.M)};
    const size_t N = 32;
    std::vector<SwPoint> src(N), dst;
    for (size_t i = 0; i < N; ++i) {
      src[i].Position = randomPos3(rng);
      QuatF q = randomUnitQuat(rng);
      src[i].Rotation = SW_FLOAT4{q.x, q.y, q.z, q.w};
    }
    if (!disp.dispatch(hp, src, dst) || dst.size() != N) { dispatchOk = false; continue; }
    for (size_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::transformFromClipSpaceOne(src[i], refOut, (uint32_t)i, (uint32_t)N, rprm);
      float in7[7] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                      src[i].Rotation.x, src[i].Rotation.y, src[i].Rotation.z, src[i].Rotation.w};
      float g[7] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z,
                    dst[i].Rotation.x, dst[i].Rotation.y, dst[i].Rotation.z, dst[i].Rotation.w};
      float r[7] = {refOut.Position.x, refOut.Position.y, refOut.Position.z,
                    refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
      for (int k = 0; k < 7; ++k) cmp.add(g[k], r[k], in7, 7, k, -1.0f, cs.tag);
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── QUIRK PROBE ①: w=0 division edge (R self-check case 4 style — identity matrix except m33=0, so
// w = v.w*m33 = 0 exactly since m03=m13=m23=0 for identity). Position-only; both sides run the SAME
// simple divide, so IEEE-754 Inf/NaN classification (mathv_compare.h's classify()) must match exactly
// on every probe -- no transcendental function involved, EpsSpec::exact() throughout.
bool checkWZeroTooth(const TfcsDispatch& disp) {
  Comparator cmp("mathv-transformfromclipspace-wzero", EpsSpec::exact(), 6);
  Mat16 M = composeMat16(0, 0, 0, 1, 1, 1, 0, 0, 0);
  M[15] = 0.0f;  // m33 = 0 -- the w==0 edge
  HostParams hp{};
  for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = M[(size_t)k];
  mathv_ref::TransformFromClipSpaceParams rprm{mat4FromArray(M)};
  // probe0 = R's self-check case 4 exactly (+Inf,NaN,NaN expected); probe1 = (0,0,0) -> 0/0=NaN on
  // every lane; probe2 = a generic nonzero point -> signed Inf on every lane.
  std::vector<SwPoint> src(3), dst;
  src[0].Position = SW_PACKED3{1.0f, 0.0f, 0.0f}; src[0].Rotation = SW_FLOAT4{0, 0, 0, 1};
  src[1].Position = SW_PACKED3{0.0f, 0.0f, 0.0f}; src[1].Rotation = SW_FLOAT4{0, 0, 0, 1};
  src[2].Position = SW_PACKED3{2.0f, 3.0f, -1.0f}; src[2].Rotation = SW_FLOAT4{0, 0, 0, 1};
  bool dispatchOk = disp.dispatch(hp, src, dst) && dst.size() == src.size();
  if (dispatchOk) {
    for (size_t i = 0; i < src.size(); ++i) {
      SwPoint refOut{};
      mathv_ref::transformFromClipSpaceOne(src[i], refOut, (uint32_t)i, (uint32_t)src.size(), rprm);
      float in3[3] = {src[i].Position.x, src[i].Position.y, src[i].Position.z};
      float g[3] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z};
      float r[3] = {refOut.Position.x, refOut.Position.y, refOut.Position.z};
      char tag[32];
      std::snprintf(tag, sizeof tag, "wzero-probe-%zu", i);
      for (int k = 0; k < 3; ++k) cmp.add(g[k], r[k], in3, 3, k, -1.0f, tag);
      printf("[mathv-transformfromclipspace-wzero] probe%zu in=(%.3f,%.3f,%.3f) gpu=(%.3f,%.3f,%.3f) "
            "ref=(%.3f,%.3f,%.3f)\n",
            i, in3[0], in3[1], in3[2], g[0], g[1], g[2], r[0], r[1], r[2]);
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── ROTATION-CONVENTION DIAGNOSTIC (instrumented cross-check, D 儀器化紀律 — measured, not guessed) ──
// Isolates qCamNormalized (Rotation_in pinned to identity so qMul(q,identity)==q) and cross-checks
// GPU / ref against `runtime/quat_host.h`'s qFromMatrix3PreciseHost. NOTE (S's audit): quat_host's
// formula is Shepperd(whatever matrix it is FED) and carries ZERO information about the transpose
// decision on its own -- fed the RAW rows it computes Shepperd(R) (the CONJUGATE), which is exactly
// what made the a3068d8 mistake look self-consistent (buggy kernel == buggy oracle). So this diagnostic
// now feeds the oracle transpose(R) (a33[R][C] = M[C*4+R]) -- the CORRECT TiXL net convention -- so the
// oracle computes Shepperd(transpose(R)) matching the fixed kernel+ref. The ABSOLUTE anchor that pins
// the convention to a literal (not another Shepperd call) is checkPinnedEyeTooth. This tooth's gate is
// "gpu matches the (correctly-fed) oracle"; ref's result is printed as evidence (now also matching).
bool checkRotationConventionDiagnostic(const TfcsDispatch& disp) {
  struct Case { const char* tag; Mat16 M; };
  std::vector<Case> cases = {
      // symmetric control: diag(1,-1,-1) -- ref's transpose() is a no-op on a diagonal matrix, so
      // ref MUST agree with gpu/oracle here (confirms the divergence below is specifically about
      // asymmetry, not a blanket ref bug).
      {"symmetric-diag-control", composeMat16(0, 0, 0, 1.0f, -1.0f, -1.0f, 0, 0, 0)},
      // asymmetric: generic multi-axis rotations -> non-symmetric upper-left 3x3.
      {"asymmetric-rot-a", composeMat16(0.0f, 0.6458f, 0.9076f, 1, 1, 1, 0, 0, 0)},
      {"asymmetric-rot-b", composeMat16(0.35f, -0.62f, 0.81f, 1, 1, 1, 3, 2, 4)},
  };
  bool allOk = true;
  for (const Case& cs : cases) {
    HostParams hp{};
    for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = cs.M[(size_t)k];
    mathv_ref::TransformFromClipSpaceParams rprm{mat4FromArray(cs.M)};
    SwPoint pin{}, refOut{};
    pin.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
    pin.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity input isolates qCamNormalized
    mathv_ref::transformFromClipSpaceOne(pin, refOut, 0, 1, rprm);
    std::vector<SwPoint> src(1, pin), dst;
    bool dispatched = disp.dispatch(hp, src, dst) && dst.size() == 1;
    // Feed the oracle transpose(R): a33[R][C] = M[C*4+R] -> qFromMatrix3PreciseHost computes
    // Shepperd(transpose(R)), the correct TiXL net convention (see this tooth's header note).
    float a33[3][3];
    for (int R = 0; R < 3; ++R)
      for (int C = 0; C < 3; ++C) a33[R][C] = cs.M[(size_t)(C * 4 + R)];
    float qe[4];
    qFromMatrix3PreciseHost(a33, qe);
    float gq[4] = {dispatched ? dst[0].Rotation.x : 0.0f, dispatched ? dst[0].Rotation.y : 0.0f,
                  dispatched ? dst[0].Rotation.z : 0.0f, dispatched ? dst[0].Rotation.w : 0.0f};
    float rq[4] = {refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
    float dotGpuOracle = gq[0] * qe[0] + gq[1] * qe[1] + gq[2] * qe[2] + gq[3] * qe[3];
    float dotRefOracle = rq[0] * qe[0] + rq[1] * qe[1] + rq[2] * qe[2] + rq[3] * qe[3];
    bool gpuOk = dispatched && std::fabs(std::fabs(dotGpuOracle) - 1.0f) < 1e-3f;
    bool refMatchesOracle = std::fabs(std::fabs(dotRefOracle) - 1.0f) < 1e-3f;
    printf("[mathv-transformfromclipspace-rotdiag] %-24s gpu=(%.4f,%.4f,%.4f,%.4f) "
          "ref=(%.4f,%.4f,%.4f,%.4f) oracle=(%.4f,%.4f,%.4f,%.4f) |dot(gpu,oracle)|=%.4f "
          "|dot(ref,oracle)|=%.4f gpuOk=%s refMatchesOracle=%s\n",
          cs.tag, gq[0], gq[1], gq[2], gq[3], rq[0], rq[1], rq[2], rq[3], qe[0], qe[1], qe[2], qe[3],
          std::fabs(dotGpuOracle), std::fabs(dotRefOracle), gpuOk ? "yes" : "NO",
          refMatchesOracle ? "yes" : "NO(conjugate/mismatch)");
    allOk = allOk && gpuOk;
  }
  return allOk;
}

// ── PINNED EYE=(3,2,4) TOOTH (S's numeric judgment, the anti-conjugate anchor) ────────────────────
// The ONLY tooth here that pins the Rotation to an ABSOLUTE LITERAL rather than to another Shepperd
// call -- so it cannot be fooled by a circular oracle. Both the CameraToWorld matrix and the expected
// quaternion are hard-coded 32-bit constants:
//   • Matrix = raymarchTransforms(eye=(3,2,4), target=origin, up=+Y).cameraToWorld (LookAt inverse,
//     GraphicsMath.cs LookAtRH -> field_camera.cpp). Its upper-left 3x3 is a proper rotation R, so
//     it is ASYMMETRIC -> transpose(R) != R -> Shepperd(transpose(R)) != its own conjugate. This is
//     the REASON a symmetric case cannot pin the transpose (conjugate==self hides the sign).
//   • Expected Rotation = Shepperd(transpose(R)) = (-0.179403, 0.310522, 0.059801, 0.931566)
//     (rotates +X onto the camera's right axis). DERIVED from TiXL's HLSL net semantics, NOT from a
//     TiXL GPU capture: WrapPointPosition.hlsl:45 + DX11ShaderCompiler.cs:38 pin M≡N (see the shared
//     header PINNED PARITY banner), and the position path proves that convention closed-form; the raw
//     HLSL rotation code then forces Shepperd(transpose(R)). Re-derived independently by the
//     orchestrator via field_camera.cpp + a bare Shepperd on transpose(R).
// A future re-drop of the transpose (ref or kernel) yields the conjugate (0.179,-0.311,-0.060,0.932)
// -> x/y/z signs flip -> abs diff ~0.36/0.62/0.12 >> 2e-3 -> RED. Guards both ref and gpu.
bool checkPinnedEyeTooth(const TfcsDispatch& disp) {
  // eye=(3,2,4) CameraToWorld, row-major m[r*4+c] (see header note for provenance).
  const Mat16 M = {{
      0.80000001f, -0.00000001f, -0.60000002f, 0.0f,
     -0.22283441f,  0.92847675f, -0.29711255f, 0.0f,
      0.55708605f,  0.37139070f,  0.74278140f, 0.0f,
      3.00000048f,  2.00000024f,  4.00000048f, 1.00000012f,
  }};
  const float qExpected[4] = {-0.179403f, 0.310522f, 0.059801f, 0.931566f};
  const float eps = 2e-3f;  // float32 gpu vs literal; conjugate diverges by ~0.36 so this is tight.

  HostParams hp{};
  for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = M[(size_t)k];
  mathv_ref::TransformFromClipSpaceParams rprm{mat4FromArray(M)};

  // identity input Rotation isolates qCamNormalized (qMul(qCam, identity) == qCam).
  SwPoint pin{}; pin.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  pin.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  std::vector<SwPoint> src(1, pin), dst;
  bool dispatched = disp.dispatch(hp, src, dst) && dst.size() == 1;

  SwPoint refOut{};
  mathv_ref::transformFromClipSpaceOne(pin, refOut, 0, 1, rprm);

  float g[4] = {dispatched ? dst[0].Rotation.x : 0.0f, dispatched ? dst[0].Rotation.y : 0.0f,
                dispatched ? dst[0].Rotation.z : 0.0f, dispatched ? dst[0].Rotation.w : 0.0f};
  float r[4] = {refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
  bool gpuOk = dispatched, refOk = true;
  for (int k = 0; k < 4; ++k) {
    gpuOk = gpuOk && std::fabs(g[k] - qExpected[k]) < eps;
    refOk = refOk && std::fabs(r[k] - qExpected[k]) < eps;
  }
  printf("[mathv-transformfromclipspace-pinned-eye324] gpu=(%.4f,%.4f,%.4f,%.4f) "
         "ref=(%.4f,%.4f,%.4f,%.4f) expected=(%.4f,%.4f,%.4f,%.4f) gpuOk=%s refOk=%s\n",
         g[0], g[1], g[2], g[3], r[0], r[1], r[2], r[3], qExpected[0], qExpected[1], qExpected[2],
         qExpected[3], gpuOk ? "yes" : "NO(conjugate?)", refOk ? "yes" : "NO(conjugate?)");
  return gpuOk && refOk;
}

}  // namespace mathv_tfcs_shared
}  // namespace sw
