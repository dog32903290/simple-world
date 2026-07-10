// selftests_mathv_meshverticestopoints.cpp — --selftest-mathv-meshverticestopoints (D role, fuzz
// driver). MATH_VERIFY_WORKFLOW.md 量產批: fuzz the GPU "meshverticestopoints" kernel
// (app/shaders/meshverticestopoints.metal) against the R-authored CPU oracle
// (app/src/mathv_ref_meshverticestopoints.h, TRANSCRIBED from external/tixl HLSL) via the shared
// mathv harness (direct-kernel dispatch, §1.3). This is the Tier-H "quat extraction" op: the
// Position channel is EXACT-class (affine multiply-add); the Rotation channel
// (qFromMatrix3Precise + normalize) is TRANSCENDENTAL-class.
// ── ParamDomain provenance (external/tixl SHA 395c4c55, MeshVerticesToPoints.t3/.t3ui) ─────────
//   .t3ui has NO Min/Max on OffsetByTBN or W -> §3.2 Default±4 fallback for all four scalars.
//   OffsetByTBN.{x,y,z} .t3 DefaultValue=(0,0,0) -> [-4,4]. W(=OffsetScale) .t3 DefaultValue=1.0
//   -> [-3,5].
// ── SCOPE split across FOUR teeth (the generic 3-layer harness carries scalar params + a flat
//    per-element input vector, it structurally CANNOT synthesize an orthonormal TBN frame — see
//    mathv_harness.h; every axis it can't carry gets its own EQUAL-STRENGTH direct-dispatch tooth,
//    addnoise pilot #2 discipline) ───────────────────────────────────────────────────────────────
//   PRIMARY (3-layer, EXACT) — Position(3) only, Tangent/Bitangent/Normal PINNED to the identity
//     basis (T=(1,0,0),B=(0,1,0),N=(0,0,1)) so Position_out reduces to a pure affine function of
//     Position_in + the 4 scalar params — the ONLY injectBug tooth.
//   TOOTH GEOMETRY (transcendental) — random Position + a REAL random orthonormal TBN frame
//     (rotation-matrix columns, sub-batch "geometry-orthonormal", N=257 spans 5 threadgroups of 64
//     — probes the MSL `index>=P.Count` guard the ref's HLSL never needed, NOTED-QUIRK #3 below)
//     PLUS a degenerate/non-orthogonal TBN stress layer (sub-batch "geometry-degenerate": zero /
//     colinear / near-singular / fully-independent-random vectors) — both sides must still AGREE
//     on whatever qFromMatrix3Precise computes off an ill-formed matrix (including landing on NaN
//     together via a negative sqrt argument), not that the result is a valid unit rotation.
//     Batch-tags the qFromMatrix3Precise branch distribution (tr>0 / m00- / m11-dominant / else)
//     hit by the random-rotation corpus.
//   TOOTH NAMED-FORK (hand-derived, independent of the ref's own logic) — 4 constructed rotation
//     matrices (identity / Rx180 / Ry180 / Rz180), one per qFromMatrix3Precise branch, dispatched
//     on GPU and compared to hand-computed expected quaternions — deterministic branch coverage
//     the random corpus above can only probabilistically claim.
//   TOOTH PASSTHROUGH (exact) — FX1/FX2 (<-Selection) / Color (<-ColorRgb) / Scale(=1,1,1),
//     equal-strength assertions on every non-arithmetic output field.
// ── THREE QUIRKS carried from the R-authored ref header, each with a dedicated probe above ──────
//   1. TBN transpose convention (NAMED FORK, meshverticestopoints.metal's own header comment):
//      HLSL transpose(float3x3(T,B,N)) ≡ MSL float3x3(T,B,N) with no transpose — TOOTH NAMED-FORK's
//      4 hand cases pin this at every branch.
//   2. Double-normalize (ref NOTED-QUIRK: hlslNormalizeQuat called twice — :25 then :27 — while the
//      MSL kernel normalizes ONCE): both are within TOOTH GEOMETRY/NAMED-FORK's transcendental-class
//      tolerance (an already-unit vector renormalized moves by ~1 ULP), so this is proven a
//      non-regression rather than a silent miscompare.
//   3. No idx guard in the HLSL main() (ref: unconditional once-through math; the MSL port DOES add
//      `index>=P.Count return` for safety) — TOOTH GEOMETRY's N=257 sub-batch exercises that guard
//      as a no-op when Count==n exactly (the only shape the host ever dispatches).
// ZONE: shell tier; crosses runtime only for SwVertex/SwPoint + the params ABI header.
#include "mathv_harness.h"
#include "mathv_ref_meshverticestopoints.h"
#include "runtime/meshverticestopoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/sw_mesh.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::MathvCase;
using mathv::ParamDomain;
using mathv::Rng;

using HostParams = ::MeshVtxToPointsParams;  // host/MSL ABI struct (meshverticestopoints_params.h)

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 / addnoise's AddNoiseDispatch
// shape): fill a SwVertex mesh buffer, setBytes the 32B params, dispatch "meshverticestopoints",
// readback a SwPoint buffer. `prm.Count` is overwritten with `in.size()`.
struct MeshVerticesToPointsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  MeshVerticesToPointsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib)
      : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("meshverticestopoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~MeshVerticesToPointsDispatch() { if (pso) pso->release(); }
  MeshVerticesToPointsDispatch(const MeshVerticesToPointsDispatch&) = delete;

  bool dispatch(HostParams prm, const std::vector<SwVertex>& in, std::vector<SwPoint>& out) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    prm.Count = n;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwVertex)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, MVTP_Vertices);
    enc->setBuffer(dstBuf, 0, MVTP_ResultPoints);
    enc->setBytes(&prm, sizeof(prm), MVTP_Params);
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

// 4-param table (OffsetByTBN.xyz + OffsetScale/W). No .t3ui Min/Max exists for either -> Default±4.
const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"OffsetByTbnX", -4.0f, 4.0f, ParamDomain::Linear,
       "MeshVerticesToPoints.t3 DefaultValue OffsetByTBN.X=0.0, no t3ui Min/Max -> Default±4"},
      {"OffsetByTbnY", -4.0f, 4.0f, ParamDomain::Linear, "ditto .Y=0.0"},
      {"OffsetByTbnZ", -4.0f, 4.0f, ParamDomain::Linear, "ditto .Z=0.0"},
      {"OffsetScale", -3.0f, 5.0f, ParamDomain::Linear,
       "MeshVerticesToPoints.t3 DefaultValue W=1.0, no t3ui Min/Max -> Default±4"},
  };
  return t;
}

// Builds BOTH the host (GPU ABI) and ref (CPU oracle) param structs from one call site (§0: the
// ABI names/offsets are the only legal shared artifact between adapter and ref).
struct DualParams { HostParams h{}; mathv_ref::MeshVerticesToPointsParams r{}; };
DualParams makeParams(float obx, float oby, float obz, float scale) {
  DualParams d;
  d.h.OffsetByTbnX = obx; d.h.OffsetByTbnY = oby; d.h.OffsetByTbnZ = obz; d.h.OffsetScale = scale;
  d.r.offsetByTBNx = obx; d.r.offsetByTBNy = oby; d.r.offsetByTBNz = obz; d.r.offsetScale = scale;
  return d;
}

// Random rotation matrix via axis-angle/Rodrigues (INPUT GENERATOR, not oracle math — independent
// of mathv_ref's own qFromMatrix3Precise transcription). Columns are orthonormal by construction;
// which column feeds Tangent/Bitangent/Normal doesn't matter for orthonormality, only for exercising
// the extraction formula against a KNOWN-valid rotation.
struct Mat3F { float m[3][3]; };
Mat3F randomRotationMatrix(Rng& rng) {
  float ax = rng.uniform(-1.0f, 1.0f), ay = rng.uniform(-1.0f, 1.0f), az = rng.uniform(-1.0f, 1.0f);
  float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len < 1e-4f) { ax = 1.0f; ay = 0.0f; az = 0.0f; len = 1.0f; }
  ax /= len; ay /= len; az /= len;
  float theta = rng.uniform(0.0f, 6.2831853f);
  float c = std::cos(theta), s = std::sin(theta), t = 1.0f - c;
  Mat3F r;
  r.m[0][0] = t*ax*ax + c;      r.m[0][1] = t*ax*ay - s*az;  r.m[0][2] = t*ax*az + s*ay;
  r.m[1][0] = t*ax*ay + s*az;   r.m[1][1] = t*ay*ay + c;     r.m[1][2] = t*ay*az - s*ax;
  r.m[2][0] = t*ax*az - s*ay;   r.m[2][1] = t*ay*az + s*ax;  r.m[2][2] = t*az*az + c;
  return r;
}

// Degenerate/non-orthogonal TBN stress patterns (cycled by index): zero Tangent / all-zero TBN /
// colinear T==B / near-singular tiny-scale / fully-independent random (not a rotation at all).
void buildDegenerateVertex(Rng& rng, int pattern, SwVertex& v) {
  v.Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
  v.Selection = 0.0f; v.ColorRgb = {0.0f, 0.0f, 0.0f};
  switch (pattern % 5) {
    case 0: v.Tangent = {0,0,0}; v.Bitangent = {0,1,0}; v.Normal = {0,0,1}; break;
    case 1: v.Tangent = {0,0,0}; v.Bitangent = {0,0,0}; v.Normal = {0,0,0}; break;
    case 2: { float k = rng.uniform(-2.0f, 2.0f);
              v.Tangent = {k,0,0}; v.Bitangent = {k,0,0}; v.Normal = {0,0,1}; break; }
    case 3: { float e = 1e-6f; v.Tangent = {e,0,0}; v.Bitangent = {0,e,0}; v.Normal = {0,0,e}; break; }
    default:
      v.Tangent = {rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f)};
      v.Bitangent = {rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f)};
      v.Normal = {rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f)};
  }
}

// Shared direct-dispatch compare core (addnoise's compareBatch shape): dispatch `src` once, compare
// Position(3)+Rotation(4)=7 lanes/element against mathv_ref::meshVerticesToPointsOne.
bool compareBatch(const MeshVerticesToPointsDispatch& disp, Comparator& cmp, const HostParams& hprm,
                   const mathv_ref::MeshVerticesToPointsParams& rprm,
                   const std::vector<SwVertex>& src, const char* batchTag) {
  std::vector<SwPoint> dst;
  const uint32_t n = (uint32_t)src.size();
  if (!disp.dispatch(hprm, src, dst) || dst.size() != n) return false;
  for (uint32_t i = 0; i < n; ++i) {
    SwPoint refOut{};
    mathv_ref::meshVerticesToPointsOne(src[i], refOut, rprm);
    float in12[12] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                      src[i].Tangent.x, src[i].Tangent.y, src[i].Tangent.z,
                      src[i].Bitangent.x, src[i].Bitangent.y, src[i].Bitangent.z,
                      src[i].Normal.x, src[i].Normal.y, src[i].Normal.z};
    float g[7] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z,
                  dst[i].Rotation.x, dst[i].Rotation.y, dst[i].Rotation.z, dst[i].Rotation.w};
    float r[7] = {refOut.Position.x, refOut.Position.y, refOut.Position.z,
                  refOut.Rotation.x, refOut.Rotation.y, refOut.Rotation.z, refOut.Rotation.w};
    for (int k = 0; k < 7; ++k) cmp.add(g[k], r[k], in12, 12, k, -1.0f, batchTag);
  }
  return true;
}

// TOOTH GEOMETRY: random Position + orthonormal-rotation TBN (sub-batch A) + degenerate/non-
// orthogonal TBN stress (sub-batch B). eps=transcendental (Rotation dominates the shared gate;
// Position's exact affine correctness is separately proven tight by PRIMARY above).
bool checkGeometryTooth(const MeshVerticesToPointsDispatch& disp) {
  Comparator cmp("mathv-meshverticestopoints-geometry", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("meshverticestopoints-geometry"));
  const auto& dom = paramTable();
  int branchTr = 0, branchM00 = 0, branchM11 = 0, branchElse = 0;
  auto tally = [&](float m00, float m11, float m22) {
    float tr = m00 + m11 + m22;
    if (tr > 0.0f) ++branchTr;
    else if (m00 > m11 && m00 > m22) ++branchM00;
    else if (m11 > m22) ++branchM11;
    else ++branchElse;
  };
  bool dispatchOk = true;
  const size_t NA = 257;  // spans 5 threadgroups of 64 -> exercises the MSL idx>=Count guard.
  for (int v = 0; v < 6; ++v) {
    DualParams dp = makeParams(mathv::sampleUniform(rng, dom[0]), mathv::sampleUniform(rng, dom[1]),
                                mathv::sampleUniform(rng, dom[2]), mathv::sampleUniform(rng, dom[3]));
    std::vector<SwVertex> src(NA);
    for (size_t i = 0; i < NA; ++i) {
      Mat3F rm = randomRotationMatrix(rng);
      src[i].Position = {rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f)};
      src[i].Tangent = {rm.m[0][0], rm.m[1][0], rm.m[2][0]};
      src[i].Bitangent = {rm.m[0][1], rm.m[1][1], rm.m[2][1]};
      src[i].Normal = {rm.m[0][2], rm.m[1][2], rm.m[2][2]};
      src[i].Selection = 0.0f; src[i].ColorRgb = {0.0f, 0.0f, 0.0f};
      tally(src[i].Tangent.x, src[i].Bitangent.y, src[i].Normal.z);  // m00,m11,m22 (ref's Mat3 diag)
    }
    if (!compareBatch(disp, cmp, dp.h, dp.r, src, "geometry-orthonormal")) dispatchOk = false;
  }
  const size_t NB = 64;
  for (int v = 0; v < 4; ++v) {
    DualParams dp = makeParams(mathv::sampleUniform(rng, dom[0]), mathv::sampleUniform(rng, dom[1]),
                                mathv::sampleUniform(rng, dom[2]), mathv::sampleUniform(rng, dom[3]));
    std::vector<SwVertex> src(NB);
    for (size_t i = 0; i < NB; ++i) buildDegenerateVertex(rng, (int)i, src[i]);
    if (!compareBatch(disp, cmp, dp.h, dp.r, src, "geometry-degenerate")) dispatchOk = false;
  }
  printf("[mathv-meshverticestopoints-geometry] branch tally (batch-tag): tr>0=%d m00-dominant=%d "
         "m11-dominant=%d else=%d\n", branchTr, branchM00, branchM11, branchElse);
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// TOOTH NAMED-FORK: 4 hand-derived rotation matrices, one per qFromMatrix3Precise branch, dispatched
// on GPU and checked against independently hand-computed expected quaternions (re-derived from the
// HLSL text, NOT read back from mathv_ref's own logic) — deterministic branch coverage.
struct BranchCase { const char* name; float t[3], b[3], n[3], q[4]; };
bool checkNamedForkBranchesTooth(const MeshVerticesToPointsDispatch& disp) {
  static const BranchCase cases[] = {
      {"identity(tr>0)",      {1,0,0}, {0,1,0}, {0,0,1},   {0,0,0,1}},
      {"Rx180(m00-dominant)", {1,0,0}, {0,-1,0}, {0,0,-1}, {1,0,0,0}},
      {"Ry180(m11-dominant)", {-1,0,0}, {0,1,0}, {0,0,-1}, {0,1,0,0}},
      {"Rz180(else)",         {-1,0,0}, {0,-1,0}, {0,0,1}, {0,0,1,0}},
  };
  bool allOk = true;
  DualParams dp = makeParams(0.0f, 0.0f, 0.0f, 0.0f);  // OffsetScale=0 isolates Rotation.
  for (const BranchCase& bc : cases) {
    SwVertex v{};
    v.Position = {2.0f, -3.0f, 1.0f};
    v.Tangent = {bc.t[0], bc.t[1], bc.t[2]};
    v.Bitangent = {bc.b[0], bc.b[1], bc.b[2]};
    v.Normal = {bc.n[0], bc.n[1], bc.n[2]};
    v.Selection = 0.0f; v.ColorRgb = {0.0f, 0.0f, 0.0f};
    std::vector<SwVertex> src(1, v);
    std::vector<SwPoint> dst;
    bool dispatched = disp.dispatch(dp.h, src, dst) && dst.size() == 1;
    bool ok = dispatched && std::fabs(dst[0].Rotation.x - bc.q[0]) < 1e-4f &&
              std::fabs(dst[0].Rotation.y - bc.q[1]) < 1e-4f &&
              std::fabs(dst[0].Rotation.z - bc.q[2]) < 1e-4f &&
              std::fabs(dst[0].Rotation.w - bc.q[3]) < 1e-4f;
    printf("[mathv-meshverticestopoints-namedfork] %s gpu=(%.6f,%.6f,%.6f,%.6f) expected=(%.6f,%.6f,"
           "%.6f,%.6f) -> %s\n", bc.name, dispatched ? dst[0].Rotation.x : 0.0f,
           dispatched ? dst[0].Rotation.y : 0.0f, dispatched ? dst[0].Rotation.z : 0.0f,
           dispatched ? dst[0].Rotation.w : 0.0f, bc.q[0], bc.q[1], bc.q[2], bc.q[3],
           ok ? "ok" : "RED");
    allOk = allOk && ok;
  }
  return allOk;
}

// TOOTH PASSTHROUGH: FX1/FX2 (<-Selection), Color (<-ColorRgb), Scale(=1,1,1) — equal-strength
// exact-class assertions on every non-arithmetic output field (random Selection/ColorRgb/TBN).
bool checkPassthroughTooth(const MeshVerticesToPointsDispatch& disp) {
  Comparator cmp("mathv-meshverticestopoints-passthrough", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("meshverticestopoints-passthrough"));
  const size_t N = 256;
  DualParams dp = makeParams(0.3f, -0.7f, 1.1f, 0.6f);  // arbitrary; irrelevant to these channels.
  std::vector<SwVertex> src(N);
  for (size_t i = 0; i < N; ++i) {
    Mat3F rm = randomRotationMatrix(rng);
    src[i].Position = {rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f), rng.uniform(-4.0f,4.0f)};
    src[i].Tangent = {rm.m[0][0], rm.m[1][0], rm.m[2][0]};
    src[i].Bitangent = {rm.m[0][1], rm.m[1][1], rm.m[2][1]};
    src[i].Normal = {rm.m[0][2], rm.m[1][2], rm.m[2][2]};
    src[i].Selection = rng.uniform(-2.0f, 2.0f);
    src[i].ColorRgb = {rng.uniform(-2.0f,2.0f), rng.uniform(-2.0f,2.0f), rng.uniform(-2.0f,2.0f)};
  }
  std::vector<SwPoint> dst;
  bool dispatched = disp.dispatch(dp.h, src, dst) && dst.size() == N;
  if (dispatched) {
    for (size_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::meshVerticesToPointsOne(src[i], refOut, dp.r);
      float sel[1] = {src[i].Selection};
      cmp.add(dst[i].FX1, refOut.FX1, sel, 1, 0, -1.0f, "fx1");
      cmp.add(dst[i].FX2, refOut.FX2, sel, 1, 0, -1.0f, "fx2");
      float col[3] = {src[i].ColorRgb.x, src[i].ColorRgb.y, src[i].ColorRgb.z};
      cmp.add(dst[i].Color.x, refOut.Color.x, col, 3, 0, -1.0f, "color");
      cmp.add(dst[i].Color.y, refOut.Color.y, col, 3, 1, -1.0f, "color");
      cmp.add(dst[i].Color.z, refOut.Color.z, col, 3, 2, -1.0f, "color");
      cmp.add(dst[i].Color.w, refOut.Color.w, col, 3, 3, -1.0f, "color-alpha");
      cmp.add(dst[i].Scale.x, refOut.Scale.x, col, 3, 4, -1.0f, "scale");
      cmp.add(dst[i].Scale.y, refOut.Scale.y, col, 3, 5, -1.0f, "scale");
      cmp.add(dst[i].Scale.z, refOut.Scale.z, col, 3, 6, -1.0f, "scale");
    }
  }
  cmp.print();
  return dispatched && cmp.verdict();
}

}  // namespace

int runMathvMeshVerticesToPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-meshverticestopoints] FAIL: no metallib\n");
    return 1;
  }
  MeshVerticesToPointsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-meshverticestopoints] FAIL: no meshverticestopoints kernel\n");
    return 1;
  }

  MathvCase c;
  c.opName = "meshverticestopoints";
  c.params = paramTable();
  c.inDim = c.outDim = 3;      // Position only; TBN pinned identity (Rotation/geometry own teeth)
  c.eps = EpsSpec::exact();    // affine multiply-add with identity TBN pinned — no transcendental
  c.inputLo = -4.0f; c.inputHi = 4.0f;
  // identity sentinel: OffsetScale=0 -> Position_out == Position_in exactly, whatever OffsetByTBN is
  // (multiplied by zero) — proves the params actually reach the kernel.
  c.identityParams = {{1.0f, -2.0f, 3.0f, 0.0f}};
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    DualParams dp = makeParams(P[0], P[1], P[2], P[3]);
    std::vector<SwVertex> src(in.size() / 3);
    for (size_t i = 0; i < src.size(); ++i) {
      src[i].Position = {in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
      src[i].Tangent = {1.0f, 0.0f, 0.0f};    // pinned identity TBN — TOOTH GEOMETRY covers the rest
      src[i].Bitangent = {0.0f, 1.0f, 0.0f};
      src[i].Normal = {0.0f, 0.0f, 1.0f};
      src[i].Selection = 0.0f; src[i].ColorRgb = {0.0f, 0.0f, 0.0f};
    }
    std::vector<SwPoint> dst;
    if (!disp.dispatch(dp.h, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i*3+0] = dst[i].Position.x; out[i*3+1] = dst[i].Position.y; out[i*3+2] = dst[i].Position.z;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    DualParams dp = makeParams(P[0], P[1], P[2], P[3]);
    SwVertex v{};
    v.Position = {in[0], in[1], in[2]};
    v.Tangent = {1.0f, 0.0f, 0.0f}; v.Bitangent = {0.0f, 1.0f, 0.0f}; v.Normal = {0.0f, 0.0f, 1.0f};
    v.Selection = 0.0f; v.ColorRgb = {0.0f, 0.0f, 0.0f};
    SwPoint p{};
    mathv_ref::meshVerticesToPointsOne(v, p, dp.r);
    out[0] = p.Position.x; out[1] = p.Position.y; out[2] = p.Position.z;
  };

  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "meshverticestopoints");

  bool passGeometry = checkGeometryTooth(disp);
  bool passNamedFork = checkNamedForkBranchesTooth(disp);
  bool passPassthrough = checkPassthroughTooth(disp);

  ParityReport rep("selftest-mathv-meshverticestopoints");
  rep.expectTrue("position(3-layer fuzz, pinned identity TBN)", passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("geometry(orthonormal+degenerate TBN, position+rotation)", passGeometry,
                 passGeometry ? 1.0 : 0.0);
  rep.expectTrue("namedFork(4 quat-branch hand cases)", passNamedFork, passNamedFork ? 1.0 : 0.0);
  rep.expectTrue("passthrough(FX1/FX2/Color/Scale)", passPassthrough, passPassthrough ? 1.0 : 0.0);
  return rep.finish();
}

// order 1005: appends after mathv-blendpoints/clearsomepoints/snaptopoints (1004).
REGISTER_SELFTESTS(/*orderBase=*/1005,
                   {"mathv-meshverticestopoints", runMathvMeshVerticesToPointsSelfTest});

}  // namespace sw
