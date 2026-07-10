#pragma once
// selftests_mathv_transformfromclipspace_shared.h — shared dispatch adapter + matrix-compose helpers
// for the TransformFromClipSpace mathv TU (400-line ratchet split, same shape as
// selftests_mathv_snappointstogrid_shared.h): both selftests_mathv_transformfromclipspace.cpp (fuzz
// driver + registration, the tooth CI actually runs) and
// selftests_mathv_transformfromclipspace_probes.cpp (special-matrix/quirk/diagnostic probes, called
// from the former) include this header instead of forking two copies of the dispatch plumbing.
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
// always for a real rotation). This is independently CONFIRMED (not pattern-guessed) via
// `runtime/quat_host.h`'s `qFromMatrix3PreciseHost` — a pre-existing, independently-authored,
// already-in-production host oracle (used by point_ops_transformpointsfromclipspace.cpp's own
// rotExactPass leg) that predates this ticket and was derived without reference to this TU or to R's
// file. See checkRotationConventionDiagnostic() in the probes TU for the measured per-matrix evidence
// table. CONSEQUENCE: the ROTATION-bearing teeth are EXPECTED RED against R's ref for any
// non-symmetric matrix — this is the correct, honest signal (a real, now well-evidenced ref bug), not
// a driver bug. The Position-only PRIMARY case and the diagonal-matrix special cases are unaffected
// and stay GREEN. D role is not permitted to edit mathv_ref_transformfromclipspace.h (ticket rule) —
// this finding routes to R for a fix (transpose must be dropped, or equivalently the ref should feed
// qFromMatrix3Precise the RAW (untransposed) upper-left 3×3, matching quat_host.h's convention).
//
// Ordinary (non-anonymous) namespace on purpose (snaptogrid precedent): this header is meant to be
// textually identical across the two TUs that include it.
#include "mathv_harness.h"
#include "mathv_ref_transformfromclipspace.h"
#include "runtime/tixl_point.h"
#include "runtime/transformpointsfromclipspace_params.h"  // TpfcsParams + TPFCS_* bindings

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace sw {
namespace mathv_tfcs_shared {

using HostParams = ::TpfcsParams;  // host/MSL ABI struct (transformpointsfromclipspace_params.h)
using Mat16 = std::array<float, 16>;

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 / AddNoiseDispatch precedent):
// fill a SwPoint bag, setBytes the 80B TpfcsParams, dispatch "transformpointsfromclipspace", readback.
// `prm.Count` is overwritten with `in.size()` (callers need not set it).
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
inline const std::vector<mathv::ParamDomain>& paramTable() {
  static const std::vector<mathv::ParamDomain> t = {
      {"RotX", -3.14159265f, 3.14159265f, mathv::ParamDomain::Linear,
       "no .t3ui (Params cbuffer empty, R's :4-7) -- CameraToWorld is a runtime camera pose; full "
       "rotation range"},
      {"RotY", -3.14159265f, 3.14159265f, mathv::ParamDomain::Linear, "ditto"},
      {"RotZ", -3.14159265f, 3.14159265f, mathv::ParamDomain::Linear, "ditto"},
      {"ScaleX", 0.2f, 2.5f, mathv::ParamDomain::Linear,
       "no .t3ui -- realistic camera zoom/dolly scale range away from 0 (0 still hit via the "
       "special-value grid's always-on literal-0 special)"},
      {"ScaleY", 0.2f, 2.5f, mathv::ParamDomain::Linear, "ditto"},
      {"ScaleZ", 0.2f, 2.5f, mathv::ParamDomain::Linear, "ditto"},
      {"TransX", -15.0f, 15.0f, mathv::ParamDomain::Linear,
       "no .t3ui -- representative world-space camera displacement range"},
      {"TransY", -15.0f, 15.0f, mathv::ParamDomain::Linear, "ditto"},
      {"TransZ", -15.0f, 15.0f, mathv::ParamDomain::Linear, "ditto"},
  };
  return t;
}

// Compose a row-vector-convention affine 4×4 (row-major m[r*4+c], translation in row 3, m03=m13=m23=0,
// m33=1 -- R's self-check case 2 / transformpointsfromclipspace_params.h provenance) from Euler-XYZ
// rotation + per-axis scale + translation. This is pure TEST-HARNESS "arrange" code (like AddNoise's
// makeParams()) -- NOT part of the math under test; it runs ONCE per sample and its 16 output floats
// are fed byte-identical to both the GPU dispatch and the CPU ref, so nothing about this function's
// own precision can introduce a gpu/ref divergence.
inline Mat16 composeMat16(float rx, float ry, float rz, float sx, float sy, float sz, float tx,
                          float ty, float tz) {
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

inline mathv_ref::Mat4 mat4FromArray(const Mat16& a) {
  return mathv_ref::Mat4{a[0], a[1], a[2],  a[3],  a[4],  a[5],  a[6],  a[7],
                        a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]};
}

// Random UNIT quaternion (axis-angle) -- qMul (ref+kernel) is a valid rotation-composition test only
// for |q|==1 inputs (AddNoise precedent).
struct QuatF { float x, y, z, w; };
inline QuatF randomUnitQuat(mathv::Rng& rng) {
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
inline SW_PACKED3 randomPos3(mathv::Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}

// Defined in selftests_mathv_transformfromclipspace_probes.cpp — declared here so
// selftests_mathv_transformfromclipspace.cpp's registration function can call them across the split.
bool checkRotationTooth(const TfcsDispatch& disp);
bool checkSpecialMatrixTooth(const TfcsDispatch& disp);
bool checkWZeroTooth(const TfcsDispatch& disp);
bool checkRotationConventionDiagnostic(const TfcsDispatch& disp);

}  // namespace mathv_tfcs_shared
}  // namespace sw
