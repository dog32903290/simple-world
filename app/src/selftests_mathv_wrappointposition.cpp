// selftests_mathv_wrappointposition.cpp — --selftest-mathv-wrappointposition (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 工單1 pilot: fuzz the GPU "wrappointposition" kernel
// (app/shaders/wrappointposition.metal) against the R-authored CPU oracle
// (app/src/mathv_ref_wrappointposition.h, TRANSCRIBED from external/tixl HLSL) using the shared
// mathv harness (direct-kernel dispatch, §1.3 — NOT buildEvalGraph).
//
// ── ParamDomain table provenance (§3, external/tixl SHA 395c4c55) ──────────────────────────────────
// external/tixl/Operators/Lib/point/transform/WrapPointPosition.t3:4-31 (Inputs) authors DEFAULTS
// only (Size=(1,1,1), UseCameraPosition=false, AddLineBreaks=true, Position/Center=(0,0,0)) — the
// .t3ui (InputUis, same dir) carries NO Min/Max/Scale metadata for any of them. Per §3.2 fallback
// ("無 Min/Max 用 DefaultValue 鄰域 ×[-4,+4]") the swept domain is [Default-4, Default+4] per axis.
//
// ── SCOPE: Center + Size only (the kernel's ACTUAL capability) ─────────────────────────────────────
// The TiXL op also authors UseCameraPosition and AddLineBreaks, but the sw port already documents
// (BEFORE this TU existed) that both are BAKED to 0 with no dispatch path at all:
//   app/shaders/wrappointposition.metal:33-35        "NAMED FORK: UseCamera baked to 0 ... WriteLineBreaks baked to 0"
//   app/src/runtime/wrappointposition_params.h:12-17  cbuffer comment + "Fork: ... baked to 0"
//   app/src/runtime/point_ops_wrappointposition.cpp:11-14  "BAKED=0 -- NAMED FORK" ×2
// There is no kernel param slot to carry a nonzero UseCamera/WriteLineBreaks value into — sweeping
// those axes would only ever measure "the kernel doesn't implement this yet" on every sample, not
// kernel math. That is a PRE-EXISTING, already-cited fork (decided before this pilot), not a fuzz
// discovery, so it is intentionally OUT OF SCOPE here rather than force-fed into the comparator.
// mathv_ref_wrappointposition.h's ref struct still carries useCamera/writeLineBreaks fields (for a
// future tooth once the port grows those); this TU always pins them to 0 to match the current kernel.
//
// ── TWO EXTRA TEETH beyond the generic 3-layer harness ──────────────────────────────────────────
// mathv_harness.h's identity-sentinel layer requires inDim==outDim, so the primary MathvCase below
// carries ONLY Position (inDim=outDim=3); the W/FX1 edge-fade channel and the isnan-quirk edge case
// need their own direct probes:
//   TOOTH FX1      — direct FX1 (edge-fade) parity across the random Position/Size domain.
//   TOOTH NAN-AXIS — S-audit verdict (a) 2026-07-09 (docs/agent/ mathv pilot ledger): TiXL's HLSL
//     guard literally reads isnan(p.x+p.y+p.x) (p.x summed TWICE, p.z NEVER inspected) — an
//     unambiguous hand-slip (sole such idiom in TiXL's points shaders; recovery :58 is
//     axis-symmetric). sw deliberately does NOT reproduce the hand-slip: wrappointposition.metal:57
//     checks all three axes (isnan(p.x+p.y+p.z)), and mathv_ref_wrappointposition.h's oracle now
//     pins that same 3-axis sw semantics (NAMED FORK, not a NOTED-QUIRK). This tooth is therefore a
//     REGULAR PINNED-PARITY tooth, not a discovery probe: it must be GREEN on every axis (x/y/z
//     NaN alone all recover identically); a RED here is a real regression against the pinned fork.
//
// ZONE: shell tier (app/src/ root, mathv support — MATH_VERIFY_WORKFLOW.md §1.1). Crosses runtime
// only for the SwPoint layout + the kernel's params ABI header (data layout, not math).
#include "mathv_harness.h"
#include "mathv_ref_wrappointposition.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"
#include "runtime/wrappointposition_params.h"

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

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape): fill a SwPoint bag,
// setBytes the 48B WrapPointPositionParams, dispatch "wrappointposition", readback. `P` = the 6 fuzz
// scalars {CenterX,Y,Z, SizeX,Y,Z} in that order (matches the ParamDomain table below).
struct WrapDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  WrapDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("wrappointposition", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~WrapDispatch() {
    if (pso) pso->release();
  }
  WrapDispatch(const WrapDispatch&) = delete;

  bool dispatch(const std::vector<float>& P, const std::vector<SwPoint>& in,
                std::vector<SwPoint>& out) const {
    if (!ok || P.size() != 6) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    WrapPointPositionParams params{};
    params.Count = n;
    params.CenterX = P[0]; params.CenterY = P[1]; params.CenterZ = P[2];
    params.SizeX   = P[3]; params.SizeY   = P[4]; params.SizeZ   = P[5];
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, WRAPPOINTPOS_SourcePoints);
    enc->setBuffer(dstBuf, 0, WRAPPOINTPOS_ResultPoints);
    enc->setBytes(&params, sizeof(params), WRAPPOINTPOS_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(n, SwPoint{});
    std::memcpy(out.data(), dstBuf->contents(), (size_t)n * sizeof(SwPoint));
    srcBuf->release();
    dstBuf->release();
    return true;
  }
};

// CPU ref params matching the kernel's ACTUAL scope (see header SCOPE note): UseCamera/WriteLineBreaks
// pinned to 0, camToWorld unused (only read when useCamera>0.5).
mathv_ref::WrapPointPositionParams refParamsFrom(const std::vector<float>& P) {
  mathv_ref::WrapPointPositionParams p{};
  p.centerX = P[0]; p.centerY = P[1]; p.centerZ = P[2];
  p.useCamera = 0.0f;
  p.sizeX = P[3]; p.sizeY = P[4]; p.sizeZ = P[5];
  p.writeLineBreaks = 0.0f;
  p.camToWorldTx = p.camToWorldTy = p.camToWorldTz = 0.0f;
  return p;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"CenterX", -4.0f, 4.0f, ParamDomain::Linear,
       "external/tixl WrapPointPosition.t3:25-31 Position DefaultValue(0,0,0), no Min/Max authored "
       "-> MATH_VERIFY_WORKFLOW.md §3.2 DefaultValue±4 fallback"},
      {"CenterY", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
      {"CenterZ", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
      {"SizeX", -3.0f, 5.0f, ParamDomain::Linear,
       "external/tixl WrapPointPosition.t3:5-10 Size DefaultValue(1,1,1), no Min/Max authored -> "
       "DefaultValue±4 fallback"},
      {"SizeY", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"SizeZ", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
  };
  return t;
}

// ── TOOTH FX1: direct random-domain parity of the W edge-fade channel (exact class — pure
// affine/saturate math, no transcendentals) ─────────────────────────────────────────────────────
bool checkFx1Tooth(const WrapDispatch& disp, float inLo, float inHi) {
  Comparator fx1("mathv-wrappointposition-fx1", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("wrappointposition-fx1"));
  const auto& dom = paramTable();
  for (int v = 0; v < 8; ++v) {
    std::vector<float> P(6);
    for (int k = 0; k < 6; ++k) P[k] = mathv::sampleUniform(rng, dom[k]);
    const size_t N = 512;
    std::vector<SwPoint> src(N), dst;
    std::vector<float> pos(N * 3);
    for (size_t i = 0; i < N; ++i) {
      float px = rng.uniform(inLo, inHi), py = rng.uniform(inLo, inHi), pz = rng.uniform(inLo, inHi);
      src[i].Position = {px, py, pz};
      pos[i * 3 + 0] = px; pos[i * 3 + 1] = py; pos[i * 3 + 2] = pz;
    }
    if (!disp.dispatch(P, src, dst) || dst.size() != N) continue;
    mathv_ref::WrapPointPositionParams prm = refParamsFrom(P);
    for (size_t i = 0; i < N; ++i) {
      SwPoint pout{};
      mathv_ref::wrapPointPositionOne(src[i], pout, 0, 1, prm);
      fx1.add(dst[i].FX1, pout.FX1, &pos[i * 3], 3, /*lane=*/0, -1.0f, "fx1-random");
    }
  }
  fx1.print();
  return fx1.verdict();
}

// ── TOOTH NAN-AXIS: pinned parity for the NaN-recovery branch, all three axes (see header —
// S-audit verdict (a) 2026-07-09). One case per axis (NaN placed alone in x / y / z,
// Center=(0,0,0) Size=(10,10,10) — matches mathv_ref_wrappointposition.h's own hand-derived
// self-check convention). Expected: all three axes recover identically (GPU == ref exactly);
// RED = regression. ──────────────────────────────────────────────────────────────────────────
bool checkNanAxisTooth(const WrapDispatch& disp) {
  Comparator cmp("mathv-wrappointposition-nan-axis", EpsSpec::exact(), 5);
  const std::vector<float> P = {0.0f, 0.0f, 0.0f, 10.0f, 10.0f, 10.0f};
  mathv_ref::WrapPointPositionParams prm = refParamsFrom(P);
  const char* axisName[3] = {"x-only-NaN", "y-only-NaN", "z-only-NaN"};
  const float kNaN = std::nanf("");
  for (int axis = 0; axis < 3; ++axis) {
    float pos[3] = {1.0f, 1.0f, 1.0f};
    pos[axis] = kNaN;
    std::vector<SwPoint> src(1), dst;
    src[0].Position = {pos[0], pos[1], pos[2]};
    SwPoint refOut{};
    mathv_ref::wrapPointPositionOne(src[0], refOut, 0, 1, prm);
    if (!disp.dispatch(P, src, dst) || dst.size() != 1) {
      printf("[mathv-wrappointposition-nan-axis] %s: dispatch FAILED\n", axisName[axis]);
      continue;
    }
    printf("[mathv-wrappointposition-nan-axis] %s: gpu Position=(%.6g,%.6g,%.6g) FX1=%.6g | "
           "ref Position=(%.6g,%.6g,%.6g) FX1=%.6g\n",
           axisName[axis], dst[0].Position.x, dst[0].Position.y, dst[0].Position.z, dst[0].FX1,
           refOut.Position.x, refOut.Position.y, refOut.Position.z, refOut.FX1);
    cmp.add(dst[0].Position.x, refOut.Position.x, pos, 3, 0, -1.0f, axisName[axis]);
    cmp.add(dst[0].Position.y, refOut.Position.y, pos, 3, 1, -1.0f, axisName[axis]);
    cmp.add(dst[0].Position.z, refOut.Position.z, pos, 3, 2, -1.0f, axisName[axis]);
    cmp.add(dst[0].FX1, refOut.FX1, pos, 3, 3, -1.0f, axisName[axis]);
  }
  cmp.print();
  return cmp.verdict();
}

}  // namespace

int runMathvWrapPointPositionSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-wrappointposition] FAIL: no metallib\n");
    return 1;
  }
  WrapDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-wrappointposition] FAIL: no wrappointposition kernel\n");
    return 1;
  }

  MathvCase c;
  c.opName = "wrappointposition";
  c.params = paramTable();
  c.inDim = c.outDim = 3;                 // Position only (FX1 gets its own tooth — identity needs inDim==outDim)
  c.eps = EpsSpec::exact();               // wrap/select/affine math, no transcendentals — §2.1
  c.inputLo = -4.0f; c.inputHi = 4.0f;    // Position element domain (Center's own neighborhood)
  // identity sentinel: Size >> input domain -> no axis ever exceeds `padded` -> offsetFactor==0 ->
  // Position_out == Position_in exactly (p - center + size*0 + center == p when center==0).
  c.identityParams = {{0.0f, 0.0f, 0.0f, 1000.0f, 1000.0f, 1000.0f}};
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in,
                  std::vector<float>& out) {
    std::vector<SwPoint> src(in.size() / 3), dst;
    for (size_t i = 0; i < src.size(); ++i)
      src[i].Position = {in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
    if (!disp.dispatch(P, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i * 3 + 0] = dst[i].Position.x;
      out[i * 3 + 1] = dst[i].Position.y;
      out[i * 3 + 2] = dst[i].Position.z;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    mathv_ref::WrapPointPositionParams prm = refParamsFrom(P);
    SwPoint pin{}, pout{};
    pin.Position = {in[0], in[1], in[2]};
    mathv_ref::wrapPointPositionOne(pin, pout, 0, 1, prm);
    out[0] = pout.Position.x; out[1] = pout.Position.y; out[2] = pout.Position.z;
  };

  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "wrappointposition");

  bool passFx1 = checkFx1Tooth(disp, c.inputLo, c.inputHi);
  bool passNanAxis = checkNanAxisTooth(disp);

  ParityReport rep("selftest-mathv-wrappointposition");
  rep.expectTrue("position(3-layer fuzz, exact)", passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("fx1(direct random parity, exact)", passFx1, passFx1 ? 1.0 : 0.0);
  rep.expectTrue("nanAxis(pinned parity, all 3 axes, S-audit (a) 2026-07-09 — RED=regression)",
                 passNanAxis, passNanAxis ? 1.0 : 0.0);
  return rep.finish();
}

// order 1001: appends right after mathv-core (1000).
REGISTER_SELFTESTS(/*orderBase=*/1001, {"mathv-wrappointposition", runMathvWrapPointPositionSelfTest});

}  // namespace sw
