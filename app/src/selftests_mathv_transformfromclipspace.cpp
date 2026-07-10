// selftests_mathv_transformfromclipspace.cpp — --selftest-mathv-transformfromclipspace (D role,
// fuzz driver). MATH_VERIFY_WORKFLOW.md Tier-H op: fuzz the GPU "transformpointsfromclipspace" kernel
// (app/shaders/transformpointsfromclipspace.metal) against the R-authored CPU oracle
// (app/src/mathv_ref_transformfromclipspace.h, TRANSCRIBED from external/tixl HLSL) via the shared
// mathv harness (direct-kernel dispatch, §1.3). Dispatch adapter / matrix-compose helpers shared with
// selftests_mathv_transformfromclipspace_probes.cpp via selftests_mathv_transformfromclipspace_shared.h
// (400-line ratchet split, selftests_mathv_snappointstogrid precedent).
//
// See the shared header for: the ABI CORRECTION (real single-matrix TpfcsParams vs the ticket's
// assumed 10-matrix array) and the PINNED PARITY note (rotation = Shepperd(transpose(R)) on BOTH
// sides after S's audit reversed the a3068d8 transpose-drop; both GREEN).
//
// This TU owns: the PRIMARY Position-only 3-layer generic fuzz (exact/affine class, GREEN) + the
// injectBug leg (perturbs P[0]=RotX, a continuous dependency) + registration. The probes TU owns:
// TOOTH ROTATION (quirk ②, GREEN — ref+kernel both Shepperd(transpose(R))) / TOOTH SPECIAL MATRICES
// (symmetric-safe, GREEN) / quirk probe ① w=0 edge / the rotation-convention diagnostic / the pinned
// eye=(3,2,4) anti-conjugate tooth.
//
// ZONE: shell tier; crosses runtime only for SwPoint + the params ABI header (via the shared header).
#include "selftests_mathv_transformfromclipspace_shared.h"
#include "runtime/selftest_registry.h"

#include <cstdio>
#include <vector>

namespace sw {
namespace {

using mathv::EpsSpec;
using mathv::MathvCase;
using mathv_tfcs_shared::composeMat16;
using mathv_tfcs_shared::HostParams;
using mathv_tfcs_shared::Mat16;
using mathv_tfcs_shared::mat4FromArray;
using mathv_tfcs_shared::paramTable;
using mathv_tfcs_shared::TfcsDispatch;

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
  c.inDim = c.outDim = 3;  // Position only (Rotation gets its own teeth, AddNoise shape)
  // §2 class: the ticket allows "Position=exact OR transcendental (矩陣乘+除法)". measured: under
  // EpsSpec::exact() the 3-layer corpus showed exactly 3/188928 misses, ALL batch=grid-param, relErr
  // 1.3e-5..2.23e-5 (evidence rows: gpu=1677.91162 vs ref=1677.9491 absErr=0.0375; gpu=-11545.1631
  // vs ref=-11545.3135 absErr=0.15) -- the ±1e6 param specials drive O(1e6) intermediate terms
  // through mul4row's 4-term fma chain; fast-math contraction/reassociation drift is O(intermediate
  // × ulp) ≈ 0.06-0.15 abs, and cancellation down to O(1e3-1e4) outputs inflates relErr just past
  // exact's rtol=1e-5. Zero misses across the random + identity layers. Transcendental's tight gate
  // (rtolTight=1e-3, >=99% fraction) passes those 3 with 40x headroom while still catching a wrong
  // formula (which decorrelates at O(1) relErr on ~every sample). The identity sentinel stays
  // EXACT-gated inside the harness regardless of this class choice.
  c.eps = EpsSpec::transcendental();
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

  bool passRotation = mathv_tfcs_shared::checkRotationTooth(disp);
  bool passSpecial = mathv_tfcs_shared::checkSpecialMatrixTooth(disp);
  bool passWZero = mathv_tfcs_shared::checkWZeroTooth(disp);
  bool passDiagnostic = mathv_tfcs_shared::checkRotationConventionDiagnostic(disp);
  bool passPinned = mathv_tfcs_shared::checkPinnedEyeTooth(disp);

  ParityReport rep("selftest-mathv-transformfromclipspace");
  rep.expectTrue("position(3-layer fuzz, transcendental -- measured note at c.eps)", passPos,
                passPos ? 1.0 : 0.0);
  rep.expectTrue("rotation(random-affine + random-quat vs ref -- Shepperd(transpose(R)), both GREEN)",
                passRotation, passRotation ? 1.0 : 0.0);
  rep.expectTrue("specialMatrix(identity/translation/180rot/degenerate/perspective, symmetric-safe)",
                passSpecial, passSpecial ? 1.0 : 0.0);
  rep.expectTrue("wZeroEdge(quirk probe (1): division-by-zero Inf/NaN parity)", passWZero,
                passWZero ? 1.0 : 0.0);
  rep.expectTrue("rotationConventionDiagnostic(gpu vs quat_host oracle fed transpose(R))", passDiagnostic,
                passDiagnostic ? 1.0 : 0.0);
  rep.expectTrue("pinnedEye324(anti-conjugate anchor: Rotation==(-0.179,0.311,0.060,0.932))", passPinned,
                passPinned ? 1.0 : 0.0);
  return rep.finish();
}

// order 1005: appends after mathv-blendpoints/snaptopoints/clearsomepoints (1004).
REGISTER_SELFTESTS(/*orderBase=*/1005,
                  {"mathv-transformfromclipspace", runMathvTransformFromClipSpaceSelfTest});

}  // namespace sw
