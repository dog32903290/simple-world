// selftests_mathv_transformfromclipspace.cpp — --selftest-mathv-transformfromclipspace (D role,
// fuzz driver). MATH_VERIFY_WORKFLOW.md Tier-H op: fuzz the GPU "transformpointsfromclipspace" kernel
// (app/shaders/transformpointsfromclipspace.metal) against the R-authored CPU oracle
// (app/src/mathv_ref_transformfromclipspace.h, TRANSCRIBED from external/tixl HLSL) via the shared
// mathv harness (direct-kernel dispatch, §1.3).
//
// ── ABI CORRECTION (read app/shaders/ + runtime/*_params.h per the D role's mandate) ──────────────
// The production ticket assumed a 10-float4x4 "Transforms" cbuffer array (matching raw TiXL HLSL).
// The REAL sw ABI (transformpointsfromclipspace_params.h) is NOT that: TpfcsParams carries exactly
// ONE float[16] (`CameraToWorld`, row-major m[r*4+c]) + Count + pad — the other 9 TiXL matrices were
// never ported for this op (NAMED FORK "fork-camera-one-matrix-per-op", cited in both the .metal and
// the params header). This TU binds the REAL single-matrix ABI, not a fictitious 10-matrix array.
//
// ── KNOWN FINDING (ref bug, NOT an epsilon issue — §7 "手推 == GPU ≠ ref -> ref 錯") ────────────────
// mathv_ref_transformfromclipspace.h's Rotation path (:196-221) transcribes the raw HLSL text's
// `qFromMatrix3Precise(transpose(orientationDest))` LITERALLY, including the `transpose()` call. But
// transformpointsfromclipspace.metal's own header (:7-28, an already-battle-tested NAMED FORK with a
// real-TiXL numeric anchor at eye=(3,2,4)) documents that TiXL's C# host PRE-TRANSPOSES every matrix
// before upload, so HLSL's own `_mRC` accessor already reads a transposed view — a fact invisible to a
// literal HLSL-only transcription (R's isolation rule explicitly forbids opening the C# host,
// mathv_ref_transformfromclipspace.h:174-175 "out of scope"). sw's ABI does NOT pre-transpose
// (params.h: "NO host transpose"), so the .metal kernel compensates by reading the matrix's COLUMNS
// with NO extra transpose() call — the CORRECT, already-validated behavior. R's ref, performing its
// OWN transpose step on top of that same convention, computes the QUATERNION CONJUGATE (negated x,y,z;
// same w) of the correct answer whenever CameraToWorld's upper-left 3×3 is NOT symmetric (i.e. almost
// always for a real rotation). This is independently CONFIRMED below (not pattern-guessed) via
// `runtime/quat_host.h`'s `qFromMatrix3PreciseHost` — a pre-existing, independently-authored,
// already-in-production host oracle (used by point_ops_transformpointsfromclipspace.cpp's own
// rotExactPass leg) that predates this ticket and was derived without reference to this TU or to R's
// file. See checkRotationConventionDiagnostic() below for the measured per-matrix evidence table.
// CONSEQUENCE: the ROTATION-bearing teeth below are EXPECTED RED against R's ref for any non-symmetric
// matrix — this is the correct, honest signal (a real, now well-evidenced ref bug), not a driver bug.
// The Position-only PRIMARY case and the diagonal-matrix special cases are unaffected and stay GREEN.
// D role is not permitted to edit mathv_ref_transformfromclipspace.h (ticket rule) — this finding
// routes to R for a fix (transpose must be dropped, or equivalently the ref should feed
// qFromMatrix3Precise the RAW (untransposed) upper-left 3×3, matching quat_host.h's convention).
//
// ── quirk probes covered ────────────────────────────────────────────────────────────────────────
//  ① w=0 degenerate matrix: checkWZeroTooth — Inf/NaN division-edge parity (R self-check case 4 style).
//  ② non-orthogonal/shear matrix fed to qFromMatrix3: checkRotationTooth's random-affine corpus already
//     produces generically non-orthogonal upper-left 3×3s (scale ≠ uniform); the exact
//     conjugate-mismatch pattern this triggers against ref is instrumented by
//     checkRotationConventionDiagnostic (isolated, minimal repro, not guessed from aggregate stats).
//
// ZONE: shell tier; crosses runtime only for SwPoint + the params ABI header + quat_host.h (pure host
// math, no Metal/platform — same "runtime pure-math leaf" class as tixl_point.h, already crossed by
// every mathv TU).
#include "mathv_harness.h"
#include "mathv_ref_transformfromclipspace.h"
#include "runtime/quat_host.h"                          // qFromMatrix3PreciseHost — INDEPENDENT oracle
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"
#include "runtime/transformpointsfromclipspace_params.h"  // TpfcsParams + TPFCS_* bindings

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::MathvCase;
using mathv::ParamDomain;
using mathv::Rng;

using HostParams = ::TpfcsParams;  // host/MSL ABI struct (transformpointsfromclipspace_params.h)

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape / AddNoiseDispatch
// precedent): fill a SwPoint bag, setBytes the 80B TpfcsParams, dispatch "transformpointsfromclipspace",
// readback. `prm.Count` is overwritten with `in.size()` (callers need not set it).
struct TfcsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  TfcsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("transformpointsfromclipspace", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~TfcsDispatch() { if (pso) pso->release(); }
  TfcsDispatch(const TfcsDispatch&) = delete;

  bool dispatch(HostParams prm, const std::vector<SwPoint>& in, std::vector<SwPoint>& out) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    prm.Count = n;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, TPFCS_SourcePoints);
    enc->setBuffer(dstBuf, 0, TPFCS_ResultPoints);
    enc->setBytes(&prm, sizeof(prm), TPFCS_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(n, SwPoint{});
    std::memcpy(out.data(), dstBuf->contents(), (size_t)n * sizeof(SwPoint));
    srcBuf->release(); dstBuf->release();
    return true;
  }
};

// 9-param table: Euler-ish rotation angles + per-axis scale + translation, COMPOSED into a realistic
// affine 4×4 (row-vector convention, translation in row 3 — R's self-check case 2 provenance) by
// composeMat16() below. No .t3ui Min/Max exists for this op (Params cbuffer is EMPTY per R's :4-7
// note — CameraToWorld is a runtime camera pose, not an authored .t3 field); domains hand-picked to
// span realistic affine-camera magnitudes: angle = full rotation range, scale away from 0 (degenerate
// scale is separately covered: the special-value grid ALWAYS injects a literal 0.0 regardless of
// domain, mathv_input.h finiteSpecials, so rank-deficient matrices are exercised anyway), translation
// = a representative world-space camera displacement range.
const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"RotX", -3.14159265f, 3.14159265f, ParamDomain::Linear,
       "no .t3ui (Params cbuffer empty, R's :4-7) -- CameraToWorld is a runtime camera pose; full "
       "rotation range"},
      {"RotY", -3.14159265f, 3.14159265f, ParamDomain::Linear, "ditto"},
      {"RotZ", -3.14159265f, 3.14159265f, ParamDomain::Linear, "ditto"},
      {"ScaleX", 0.2f, 2.5f, ParamDomain::Linear,
       "no .t3ui -- realistic camera zoom/dolly scale range away from 0 (0 still hit via the "
       "special-value grid's always-on literal-0 special)"},
      {"ScaleY", 0.2f, 2.5f, ParamDomain::Linear, "ditto"},
      {"ScaleZ", 0.2f, 2.5f, ParamDomain::Linear, "ditto"},
      {"TransX", -15.0f, 15.0f, ParamDomain::Linear,
       "no .t3ui -- representative world-space camera displacement range"},
      {"TransY", -15.0f, 15.0f, ParamDomain::Linear, "ditto"},
      {"TransZ", -15.0f, 15.0f, ParamDomain::Linear, "ditto"},
  };
  return t;
}

// Compose a row-vector-convention affine 4×4 (row-major m[r*4+c], translation in row 3, m03=m13=m23=0,
// m33=1 -- R's self-check case 2 / TransformFromClipSpace_params.h provenance) from Euler-XYZ rotation
// + per-axis scale + translation. This is pure TEST-HARNESS "arrange" code (like AddNoise's
// makeParams()) -- NOT part of the math under test; it runs ONCE per sample and its 16 output floats
// are fed byte-identical to both the GPU dispatch and the CPU ref, so nothing about this function's
// own precision can introduce a gpu/ref divergence.
using Mat16 = std::array<float, 16>;
Mat16 composeMat16(float rx, float ry, float rz, float sx, float sy, float sz, float tx, float ty,
                  float tz) {
  auto mul3 = [](const float A[3][3], const float B[3][3], float C[3][3]) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) {
        float s = 0.0f;
        for (int k = 0; k < 3; ++k) s += A[r][k] * B[k][c];
        C[r][c] = s;
      }
  };
  const float cx = std::cos(rx), sxr = std::sin(rx);
  const float cy = std::cos(ry), syr = std::sin(ry);
  const float cz = std::cos(rz), szr = std::sin(rz);
  float Rx[3][3] = {{1, 0, 0}, {0, cx, -sxr}, {0, sxr, cx}};
  float Ry[3][3] = {{cy, 0, syr}, {0, 1, 0}, {-syr, 0, cy}};
  float Rz[3][3] = {{cz, -szr, 0}, {szr, cz, 0}, {0, 0, 1}};
  float S[3][3] = {{sx, 0, 0}, {0, sy, 0}, {0, 0, sz}};
  float RyRx[3][3], RzRyRx[3][3], RS[3][3];
  mul3(Ry, Rx, RyRx);
  mul3(Rz, RyRx, RzRyRx);
  mul3(RzRyRx, S, RS);
  Mat16 M{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) M[(size_t)(r * 4 + c)] = RS[r][c];
  M[3] = 0.0f; M[7] = 0.0f; M[11] = 0.0f;
  M[12] = tx; M[13] = ty; M[14] = tz; M[15] = 1.0f;
  return M;
}

mathv_ref::Mat4 mat4FromArray(const Mat16& a) {
  return mathv_ref::Mat4{a[0], a[1], a[2],  a[3],  a[4],  a[5],  a[6],  a[7],
                        a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]};
}

// Random UNIT quaternion (axis-angle) -- qMul (ref+kernel) is a valid rotation-composition test only
// for |q|==1 inputs (AddNoise precedent).
struct QuatF { float x, y, z, w; };
QuatF randomUnitQuat(Rng& rng) {
  float ax, ay, az, len2;
  do {
    ax = rng.uniform(-1.0f, 1.0f); ay = rng.uniform(-1.0f, 1.0f); az = rng.uniform(-1.0f, 1.0f);
    len2 = ax * ax + ay * ay + az * az;
  } while (len2 < 1e-4f);
  float invLen = 1.0f / std::sqrt(len2);
  ax *= invLen; ay *= invLen; az *= invLen;
  float theta = rng.uniform(0.0f, 6.2831853f);
  float s = std::sin(theta * 0.5f), c = std::cos(theta * 0.5f);
  return {ax * s, ay * s, az * s, c};
}
SW_PACKED3 randomPos3(Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}

// ── TOOTH ROTATION: random affine matrix (composeMat16, generically NON-symmetric upper-left 3x3 --
// quirk probe ② non-orthogonal-matrix coverage, since scale != uniform breaks orthogonality even
// before the rotation factors are considered) + random UNIT-quaternion Rotation, compared against
// R's ref over Position(3)+Rotation(4)=7 lanes. EXPECTED RED for generic (asymmetric) matrices per
// this file's KNOWN FINDING banner above -- that is the correct, evidenced signal, not a driver bug.
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

// ── ROTATION-CONVENTION DIAGNOSTIC (instrumented finding, D 儀器化紀律 — measured, not guessed) ──
// Isolates qCamNormalized (Rotation_in pinned to identity so qMul(q,identity)==q) and cross-checks
// GPU / ref against `runtime/quat_host.h`'s qFromMatrix3PreciseHost -- a pre-existing, INDEPENDENT,
// already-in-production host oracle (point_ops_transformpointsfromclipspace.cpp's rotExactPass leg)
// that was authored without reference to this ticket or to R's file. This tooth's PASS/FAIL gate is
// ONLY "gpu matches the independent oracle" (confirms the KERNEL is right); ref's result is printed
// as measured evidence, not silently re-gated -- checkRotationTooth above already carries that RED
// honestly. See this file's KNOWN FINDING banner for the full derivation.
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
    float a33[3][3];
    for (int R = 0; R < 3; ++R)
      for (int C = 0; C < 3; ++C) a33[R][C] = cs.M[(size_t)(R * 4 + C)];
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

}  // namespace

int runMathvTransformFromClipSpaceSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-transformfromclipspace] FAIL: no metallib\n");
    return 1;
  }
  TfcsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-transformfromclipspace] FAIL: no transformpointsfromclipspace kernel\n");
    return 1;
  }

  MathvCase c;
  c.opName = "transformfromclipspace";
  c.params = paramTable();
  c.inDim = c.outDim = 3;              // Position only (Rotation gets its own teeth, AddNoise shape)
  c.eps = EpsSpec::exact();            // §2 "仿射" class -- multiply/add/divide, no transcendental
  c.inputLo = -4.0f; c.inputHi = 4.0f;
  // identity sentinel: RotXYZ=0, Scale=(1,1,1), Trans=(0,0,0) -> composeMat16 == identity exactly ->
  // Position_out == Position_in exactly (w always 1 in this composed family, m03=m13=m23=0/m33=1).
  c.identityParams = {{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f}};
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    Mat16 M = composeMat16(P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7], P[8]);
    std::vector<SwPoint> src(in.size() / 3), dst;
    for (size_t i = 0; i < src.size(); ++i) {
      src[i].Position = SW_PACKED3{in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
      src[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // pinned -- rotation teeth cover this
    }
    HostParams hp{};
    for (int k = 0; k < 16; ++k) hp.CameraToWorld[k] = M[(size_t)k];
    if (!disp.dispatch(hp, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i * 3 + 0] = dst[i].Position.x; out[i * 3 + 1] = dst[i].Position.y;
      out[i * 3 + 2] = dst[i].Position.z;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    Mat16 M = composeMat16(P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7], P[8]);
    mathv_ref::TransformFromClipSpaceParams prm{mat4FromArray(M)};
    SwPoint pin{}, pout{};
    pin.Position = SW_PACKED3{in[0], in[1], in[2]};
    pin.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    mathv_ref::transformFromClipSpaceOne(pin, pout, /*idx=*/0, /*numStructs=*/1, prm);
    out[0] = pout.Position.x; out[1] = pout.Position.y; out[2] = pout.Position.z;
  };

  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "transformfromclipspace");

  bool passRotation = checkRotationTooth(disp);
  bool passSpecial = checkSpecialMatrixTooth(disp);
  bool passWZero = checkWZeroTooth(disp);
  bool passDiagnostic = checkRotationConventionDiagnostic(disp);

  ParityReport rep("selftest-mathv-transformfromclipspace");
  rep.expectTrue("position(3-layer fuzz, exact/affine)", passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("rotation(random-affine + random-quat vs ref -- KNOWN FINDING, see file header)",
                passRotation, passRotation ? 1.0 : 0.0);
  rep.expectTrue("specialMatrix(identity/translation/180rot/degenerate/perspective, symmetric-safe)",
                passSpecial, passSpecial ? 1.0 : 0.0);
  rep.expectTrue("wZeroEdge(quirk probe (1): division-by-zero Inf/NaN parity)", passWZero,
                passWZero ? 1.0 : 0.0);
  rep.expectTrue("rotationConventionDiagnostic(gpu vs INDEPENDENT quat_host oracle)", passDiagnostic,
                passDiagnostic ? 1.0 : 0.0);
  return rep.finish();
}

// order 1005: appends after mathv-blendpoints/snaptopoints/clearsomepoints (1004).
REGISTER_SELFTESTS(/*orderBase=*/1005,
                  {"mathv-transformfromclipspace", runMathvTransformFromClipSpaceSelfTest});

}  // namespace sw
