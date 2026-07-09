#pragma once
// selftests_mathv_snappointstogrid_shared.h — shared dispatch adapter + CPU-ref bridge for the
// SnapPointsToGrid mathv TU (Part D split, XS verdict 2026-07-10 / §4.3 rule 4: the single-TU file
// hit 399 lines with zero headroom before this work order's additions). Both
// selftests_mathv_snappointstogrid.cpp (fuzz driver + registration, the tooth CI actually runs) and
// selftests_mathv_snappointstogrid_probes.cpp (diagnostic/quirk/isolation probes, called from the
// former) include this header instead of forking two copies of the dispatch plumbing.
//
// Ordinary (non-anonymous) namespace on purpose: this header is meant to be textually identical
// across the two TUs that include it, exactly like any other shared header — the anonymous
// namespace the original single-file TU used for "local to this file" symbols doesn't apply once
// the symbols are genuinely shared across TUs.
#include "mathv_harness.h"
#include "mathv_ref_snappointstogrid.h"
#include "runtime/snaptogrid_params.h"
#include "runtime/tixl_point.h"

#include <cstring>
#include <vector>

namespace sw {
namespace mathv_snap_shared {

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 / WrapDispatch precedent):
// fill a SwPoint bag, setBytes the 64B SnapToGridParams, dispatch "snaptogrid", readback. `P` = the
// 11 fuzz scalars {GridStretchX,Y,Z, Amount, GridOffsetX,Y,Z, GridScale, Mode, GainAndBiasX(gain),
// GainAndBiasY(bias)} in that order (matches paramTable() below).
struct SnapDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  SnapDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("snaptogrid", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SnapDispatch() {
    if (pso) pso->release();
  }
  SnapDispatch(const SnapDispatch&) = delete;

  bool dispatch(const std::vector<float>& P, const std::vector<SwPoint>& in,
                std::vector<SwPoint>& out) const {
    if (!ok || P.size() != 11) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    SnapToGridParams params{};
    params.Count = n;
    params.Amount = P[3]; params.GridScale = P[7]; params.Mode = P[8];
    params.GridStretchX = P[0]; params.GridStretchY = P[1]; params.GridStretchZ = P[2];
    params.GridOffsetX = P[4]; params.GridOffsetY = P[5]; params.GridOffsetZ = P[6];
    params.GainAndBiasX = P[9]; params.GainAndBiasY = P[10];
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, SNAPTOGRID_SourcePoints);
    enc->setBuffer(dstBuf, 0, SNAPTOGRID_ResultPoints);
    enc->setBytes(&params, sizeof(params), SNAPTOGRID_Params);
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

// CPU ref params matching the kernel's ACTUAL scope (see selftests_mathv_snappointstogrid.cpp
// header SCOPE note): Scatter/StrengthFactor pinned to 0 -- the kernel has no slot for either.
inline mathv_ref::SnapPointsToGridParams refParamsFrom(const std::vector<float>& P) {
  mathv_ref::SnapPointsToGridParams p{};
  p.gridStretchX = P[0]; p.gridStretchY = P[1]; p.gridStretchZ = P[2];
  p.amount = P[3];
  p.gridOffsetX = P[4]; p.gridOffsetY = P[5]; p.gridOffsetZ = P[6];
  p.gridScale = P[7];
  p.scatter = 0.0f;  // NAMED FORK pin (snaptogrid.metal:29 "Scatter baked to 0")
  p.mode = P[8];
  p.gainAndBiasX = P[9]; p.gainAndBiasY = P[10];
  p.strengthFactor = 0;  // NAMED FORK pin (snaptogrid.metal:30 "StrengthFactor=None baked")
  return p;
}

inline const std::vector<mathv::ParamDomain>& paramTable() {
  static const std::vector<mathv::ParamDomain> t = {
      {"GridStretchX", -3.0f, 5.0f, mathv::ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:45-50 GridStretch default(1,1,1), no Min/Max -> §3.2 default±4"},
      {"GridStretchY", -3.0f, 5.0f, mathv::ParamDomain::Linear, "ditto"},
      {"GridStretchZ", -3.0f, 5.0f, mathv::ParamDomain::Linear, "ditto"},
      {"Amount", 0.0f, 1.0f, mathv::ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3ui:14-21 Min=0 Max=1"},
      {"GridOffsetX", -4.0f, 4.0f, mathv::ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:5-11 Offset default(0,0,0), no Min/Max -> default±4"},
      {"GridOffsetY", -4.0f, 4.0f, mathv::ParamDomain::Linear, "ditto"},
      {"GridOffsetZ", -4.0f, 4.0f, mathv::ParamDomain::Linear, "ditto"},
      {"GridScale", -3.5f, 4.5f, mathv::ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:21-23 GridScale default 0.5, no Min/Max -> default±4"},
      {"Mode", 0.0f, 3.0f, mathv::ParamDomain::Enum,
       "tixl SnapPointsToGrid.cs:44-49 enum SnapModes{Center=0,Corners=1,AxisCenter=2,AxisEdge=3} "
       "-- domain fixed by the enum, .t3ui carries no Min/Max for Mode"},
      {"GainAndBiasX(gain)", 0.0f, 1.0f, mathv::ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3ui:86-98 BiasAndGain Min=0 Max=1 ClampMin/Max true; HLSL "
       "GainAndBias.x read as gain (ref AMBIGUITY note)"},
      {"GainAndBiasY(bias)", 0.0f, 1.0f, mathv::ParamDomain::Linear, "ditto, .y read as bias"},
  };
  return t;
}

// Defined in selftests_mathv_snappointstogrid_probes.cpp — declared here so
// selftests_mathv_snappointstogrid.cpp's registration function can call them across the TU split.
bool runZeroGridSizeDiagnostic(const SnapDispatch& disp);
bool checkApplyGainAndBiasQuirk(const SnapDispatch& disp);
bool checkSaturateNanQuirk(const SnapDispatch& disp);
bool checkBiasGainIsolationProbe(const SnapDispatch& disp);

}  // namespace mathv_snap_shared
}  // namespace sw
