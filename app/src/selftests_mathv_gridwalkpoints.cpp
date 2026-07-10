// selftests_mathv_gridwalkpoints.cpp — --selftest-mathv-gridwalkpoints (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU "gridwalkpoints"
// kernel (app/shaders/gridwalkpoints.metal — glslang+spirv-cross output of TiXL points/sim/
// grid-walk-points.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_gridwalkpoints.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// TRANSCENDENTAL-class op (qRotateVec3/normalize/hash11 chain feeding a branchy grid-wall test).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (orphaned kernel, no owning .t3 -- see mathv_ref_gridwalkpoints.h header). Position/Seed
// domains deliberately kept NON-NEGATIVE (Position in [0,80], GridOffset in [0,5], Seed in [0,100]):
// the ref's scalar `%1231` op models HLSL-spec TRUNCATED mod while the transpiled kernel's actual
// behavior is FLOORED (see the ref header's AMBIGUITY note) -- non-negative dividends make the two
// forms provably agree, sidestepping the spec-vs-transpiler gap rather than guessing which is
// "right" (same technique as selftests_mathv_updatechunksizes.cpp's int-mod domain restriction).
// GridSize kept in [5,50] (avoids div-by-zero in mod()); TriggerTurn tested at both 0 and 1 to
// exercise the always-wrap branch independent of the boundary geometry.
//
// WRAPPED-branch verification strategy (measured, not speculative -- see
// mathv_ref_gridwalkpoints.h header): a hand-verified sample (Position=(8.072,62.261,62.592), idx=1,
// n64-moving scenario) showed the CPU ref's `hash*6` landing at 1.171875 (comfortably 0.17 inside
// bucket [1,2), independently confirmed via a standalone Python re-derivation of the exact same
// arithmetic) while the GPU's actual table pick corresponds to bucket [0,1) -- a genuine >0.17
// h6-unit divergence, FAR beyond what a boundary-distance pre-filter can safely exclude (hash11's
// two squaring rounds amplify ordinary fast-math ULP drift by several thousand-fold, per the
// sensitivity analysis in mathv_ref_gridwalkpoints.h). Bit-exact WHICH-row agreement between GPU
// fast-math and CPU exact math is therefore NOT achievable via blanket fuzzing for a meaningful
// fraction of inputs -- this is not a bug, it's what "6 buckets fed by a chaotically-amplified hash"
// means under fast-math. This TU instead verifies what IS reliably reproducible:
//   1. Position (never depends on the hash chain) -- asserted on EVERY sample via the Comparator.
//   2. Rotation on NOT-WRAPPED samples -- trivially unchanged on both sides -- asserted via the
//      Comparator.
//   3. Rotation on WRAPPED samples -- NOT asserted for bit-exact table-row agreement; instead
//      verified STRUCTURALLY (matchesAnyTableRow()): the GPU's output must equal
//      qFromAngleAxis(axisAngles[k]) for SOME k in 0..5. This still catches a genuinely broken
//      lookup/PI-constant/table-value/qFromAngleAxis formula (which would produce a quaternion
//      matching NONE of the 6 valid rows) without requiring agreement on WHICH row the chaotic hash
//      selects.
//   4. (S-review batch-3) n4096 row-COVERAGE gate: all 6 valid table-row quaternions must each
//      appear at least once in the GPU's wrapped output -- kills subset-collapse table-read bugs
//      (index mod 3, always-row-0, mis-stride onto a reachable subset) that keep #3 green. A
//      row-CONSISTENCY rate (GPU row == ref row) is printed as a DIAGNOSTIC only -- re-measurement
//      showed baseline agreement is at chance level (~1/6; fast-math reassociation of the hash-input
//      sum decorrelates the picks), so no consistency threshold can discriminate: see rowsAgree().
//
// injectBug: corrupt the REAL GPU-side Rotation.x of point 0 (flows into the qRotateVec3 velocity
// computation, hence Position on every dispatch) while the CPU ref keeps the original. Position is
// asserted unconditionally, so this still trips reliably even though the WRAPPED-branch Rotation
// check is set-membership, not point-equality. Magnitude-aware nudge (see
// selftests_mathv_copyprefixsum.cpp / selftests_mathv_randomizepointslegacy1.cpp for the "+1e-2 gets
// swallowed by rtol*scale" lesson this TU is built to avoid from the start).
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header + tixl_point.h's SwPoint
// struct (data layout, not math).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_gridwalkpoints.h"
#include "parity_golden_harness.h"
#include "runtime/gridwalkpoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

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
using mathv::Rng;

SwPoint randomPoint(Rng& rng) {
  SwPoint p{};
  p.Position = {rng.uniform(0.0f, 80.0f), rng.uniform(0.0f, 80.0f), rng.uniform(0.0f, 80.0f)};
  p.FX1 = rng.uniform(-10.0f, 10.0f);  // LegacyPoint.W, untouched by the main branch, only zeroed
                                        // on the tail-guard path
  float ax = rng.uniform(-1.0f, 1.0f), ay = rng.uniform(-1.0f, 1.0f), az = rng.uniform(-1.0f, 1.0f);
  float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len < 1e-6f) { ax = 1.0f; ay = 0.0f; az = 0.0f; len = 1.0f; }
  float half = rng.uniform(-3.14159f, 3.14159f) * 0.5f;
  float sn = std::sin(half), cs = std::cos(half);
  p.Rotation = {ax / len * sn, ay / len * sn, az / len * sn, cs};
  p.Color = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
             rng.uniform(0.0f, 1.0f)};
  p.Scale = {1.0f, 1.0f, 1.0f};
  p.FX2 = 0.0f;
  return p;
}

struct GwpDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  GwpDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("gridwalkpoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~GwpDispatch() { if (pso) pso->release(); }
  GwpDispatch(const GwpDispatch&) = delete;

  // `count` = the logical point count (P.Count / GetDimensions); `in.size()` = the padded dispatch
  // buffer size (may exceed count, to exercise the tail-guard branch).
  bool dispatch(const std::vector<SwPoint>& in, int32_t count, const GridWalkPointsParams& prm,
                std::vector<SwPoint>& out) const {
    if (!ok) return false;
    if (in.empty()) return false;
    out = in;
    MTL::Buffer* buf = dev->newBuffer(out.data(), (NS::UInteger)(out.size() * sizeof(SwPoint)),
                                       MTL::ResourceStorageModeShared);
    GridWalkPointsParams p2 = prm;
    p2.Count = count;
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(buf, 0, GRIDWALKPOINTS_ResultPoints);
    enc->setBytes(&p2, sizeof(p2), GRIDWALKPOINTS_Params);
    const uint32_t tg = 64;  // matches numthreads(64,1,1) in the HLSL
    const uint32_t n = (uint32_t)in.size();
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(out.data(), buf->contents(), out.size() * sizeof(SwPoint));
    buf->release();
    return true;
  }
};

void addPositionFields(Comparator& cmp, const SwPoint& gpu, const SwPoint& ref, const SwPoint& in,
                        const char* tag) {
  float inVec[8] = {in.Position.x, in.Position.y, in.Position.z, in.Rotation.x, in.Rotation.y,
                     in.Rotation.z, in.Rotation.w, in.FX1};
  float gv[3] = {gpu.Position.x, gpu.Position.y, gpu.Position.z};
  float rv[3] = {ref.Position.x, ref.Position.y, ref.Position.z};
  for (int k = 0; k < 3; ++k) cmp.add(gv[k], rv[k], inVec, 8, k, -1.0f, tag);
}

void addRotationExact(Comparator& cmp, const SwPoint& gpu, const SwPoint& ref, const SwPoint& in,
                       const char* tag) {
  float inVec[8] = {in.Position.x, in.Position.y, in.Position.z, in.Rotation.x, in.Rotation.y,
                     in.Rotation.z, in.Rotation.w, in.FX1};
  float gv[4] = {gpu.Rotation.x, gpu.Rotation.y, gpu.Rotation.z, gpu.Rotation.w};
  float rv[4] = {ref.Rotation.x, ref.Rotation.y, ref.Rotation.z, ref.Rotation.w};
  for (int k = 0; k < 4; ++k) cmp.add(gv[k], rv[k], inVec, 8, 3 + k, -1.0f, tag);
}

// matchingTableRowIndex — which of the 6 valid table rows (if any) `gpuRot` equals; -1 if none.
// matchesAnyTableRow (the pre-existing structural check for the WRAPPED branch, see file header) is
// the boolean form. S-review batch-3 adds two more consumers of the SAME per-row distance test:
//   1. the COVERAGE gate (n4096 scenario): which of the 6 rows the GPU's fast-math hash actually
//      picked at least once (a stride/permutation bug in the table lookup would silently collapse
//      the reachable set below 6 -- e.g. a mis-strided read that only ever lands on 3 of the 6 rows).
//   2. rowsAgree() below: whether the GPU and the CPU ref picked the SAME row (not just SOME row).
int matchingTableRowIndex(const SW_FLOAT4& gpuRot) {
  for (int k = 0; k < 6; ++k) {
    const float* aa = mathv_ref::gwp::AXIS_ANGLES[k];
    mathv_ref::quat::Quat expect = mathv_ref::gwp::qFromAngleAxis(
        aa[3] * mathv_ref::gwp::PI / 1.5f, mathv_ref::quat::Vec3{aa[0], aa[1], aa[2]});
    float d = std::fabs(gpuRot.x - expect.x) + std::fabs(gpuRot.y - expect.y) +
              std::fabs(gpuRot.z - expect.z) + std::fabs(gpuRot.w - expect.w);
    if (d < 1e-4f) return k;
  }
  return -1;
}
bool matchesAnyTableRow(const SW_FLOAT4& gpuRot) { return matchingTableRowIndex(gpuRot) >= 0; }

// rowsAgree — GPU and CPU-ref picked the BIT-APPROXIMATE SAME table row (not just membership in the
// set of 6). DIAGNOSTIC ONLY, deliberately NOT an asserting gate (S-review batch-3, premise
// re-measured and DISPROVEN): the S review hypothesized a ~93% baseline agreement rate that a
// stride-level table-read bug would crater to ~17% — the actual measured baseline on this hardware/
// toolchain is 17.5-28.1% per scenario (overall 18.6%), i.e. AT CHANCE LEVEL (1/6 ≈ 16.7%). Metal
// fast-math reassociates the :50 hash-input sum (Seed+idx+Pos.x+y+z); a single-ULP rounding
// difference is amplified thousands-fold by hash11's two squaring rounds, so GPU-vs-CPU WHICH-row
// picks decorrelate almost completely (consistent with the file-header's hand-verified sample: ref
// mid-bucket at 1.17, GPU picked bucket 0 — never a boundary-only effect). At chance-level baseline
// a consistency threshold cannot separate healthy from stride-broken (both ≈ 1/6), so asserting on
// it would be a fake tooth. The stride-error kill is carried by the two gates that DO discriminate:
// table-MEMBERSHIP (a mis-strided read yields garbage quaternions matching NO row → RED on ~100% of
// wrapped samples) and n4096 row-COVERAGE (a subset-collapse bug — e.g. index mod 3, or always-row-0
// — leaves <6 rows reachable → RED). The per-scenario agreement rates are still printed as a drift
// diagnostic (a future toolchain that stops fast-math reassociation would push them toward ~100%).
bool rowsAgree(const SW_FLOAT4& gpuRot, const SW_FLOAT4& refRot) {
  float d = std::fabs(gpuRot.x - refRot.x) + std::fabs(gpuRot.y - refRot.y) +
            std::fabs(gpuRot.z - refRot.z) + std::fabs(gpuRot.w - refRot.w);
  return d < 1e-4f;
}

}  // namespace

int runMathvGridWalkPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-gridwalkpoints] FAIL: no metallib\n"); return 1; }
  GwpDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-gridwalkpoints] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-gridwalkpoints", EpsSpec::transcendental(), 5);
  bool dispatchOk = true;
  uint64_t wrappedTotal = 0, wrappedTableMatch = 0, wrappedRowAgree = 0;
  bool n4096RowSeen[6] = {false, false, false, false, false, false};  // coverage gate (n4096 only)

  struct Scenario {
    size_t dispatched;
    int32_t count;
    float gridSize, speed, gridOffset, triggerTurn, seed;
    const char* tag;
  };
  const Scenario scenarios[] = {
      {64, 64, 20.0f, 0.0f, 0.0f, 0.0f, 10.0f, "n64-speed0"},       // Speed=0 -> velocity=0, no wrap
      {64, 64, 20.0f, 5.0f, 2.0f, 0.0f, 20.0f, "n64-moving"},
      {128, 128, 10.0f, -3.0f, 1.0f, 0.0f, 50.0f, "n128"},
      {128, 128, 15.0f, 2.0f, 0.0f, 1.0f, 5.0f, "n128-alwaysturn"},  // TriggerTurn>0.5 forces wrap every thread
      {4096, 4096, 30.0f, 8.0f, 3.0f, 0.0f, 100.0f, "n4096"},
      {128, 65, 20.0f, 4.0f, 0.0f, 0.0f, 1.0f, "n128-tail-guard"},  // count<dispatched -> exercise :36-39
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwPoint> in(s.dispatched);
    for (auto& p : in) p = randomPoint(rng);

    GridWalkPointsParams prm{};
    prm.GridSizeX = prm.GridSizeY = prm.GridSizeZ = s.gridSize;
    prm.Speed = s.speed;
    prm.GridOffsetX = prm.GridOffsetY = prm.GridOffsetZ = s.gridOffset;
    prm.SpeedVariation = 0.0f;  // unread, present for byte-fidelity
    prm.TriggerTurn = s.triggerTurn;
    prm.Seed = s.seed;

    // injectBug: corrupt the real dispatch's source data (Rotation.x of point 0) while the CPU ref
    // keeps the original -- real "corrupt input" lever; Position is asserted unconditionally below,
    // so this trips regardless of the WRAPPED-branch structural (not point-equality) check.
    std::vector<SwPoint> inGpu = in;
    if (injectBug && !inGpu.empty()) inGpu[0].Rotation.x += 0.5f;

    std::vector<SwPoint> gpuOut;
    if (!disp.dispatch(inGpu, s.count, prm, gpuOut)) { dispatchOk = false; continue; }
    std::vector<SwPoint> refOut(s.dispatched);
    mathv_ref::GridWalkPointsParams refP{s.gridSize, s.gridSize, s.gridSize, s.speed,
                                          s.gridOffset, s.gridOffset, s.gridOffset, s.triggerTurn,
                                          s.seed};
    mathv_ref::mathvRefGridWalkPoints(in.data(), refOut.data(), s.dispatched, s.count, refP);  // UNPERTURBED
    uint64_t scenWrapped = 0, scenAgree = 0;
    for (size_t i = 0; i < s.dispatched; ++i) {
      addPositionFields(cmpMain, gpuOut[i], refOut[i], in[i], s.tag);
      bool wrapped = (int32_t)i < s.count && mathv_ref::gridWalkPointsWrapped(in[i], refP);
      if (!wrapped) {
        addRotationExact(cmpMain, gpuOut[i], refOut[i], in[i], s.tag);
      } else {
        ++wrappedTotal;
        ++scenWrapped;
        int row = matchingTableRowIndex(gpuOut[i].Rotation);
        if (row >= 0) {
          ++wrappedTableMatch;
          if (std::strcmp(s.tag, "n4096") == 0) n4096RowSeen[row] = true;
        }
        if (rowsAgree(gpuOut[i].Rotation, refOut[i].Rotation)) { ++wrappedRowAgree; ++scenAgree; }
      }
    }
    if (scenWrapped > 0)
      printf("[mathv-gridwalkpoints] %s per-scenario row-consistency: %llu/%llu (%.1f%%)\n", s.tag,
             (unsigned long long)scenAgree, (unsigned long long)scenWrapped,
             100.0 * (double)scenAgree / (double)scenWrapped);
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  bool wrappedOk = wrappedTotal > 0 && wrappedTableMatch == wrappedTotal;
  printf("[mathv-gridwalkpoints] wrapped-branch table-membership: %llu/%llu -> %s\n",
         (unsigned long long)wrappedTableMatch, (unsigned long long)wrappedTotal,
         wrappedOk ? "ok" : "RED");

  // Row-consistency DIAGNOSTIC (S-review batch-3 — demoted from asserting gate to print after the
  // baseline re-measure disproved the premise; see rowsAgree()'s comment for the full derivation:
  // measured baseline 17.5-28.1% per scenario ≈ chance 1/6, so no threshold can separate healthy
  // from stride-broken — the stride kill lives in the MEMBERSHIP + COVERAGE gates instead).
  double consistencyRate = wrappedTotal > 0 ? (double)wrappedRowAgree / (double)wrappedTotal : 0.0;
  printf("[mathv-gridwalkpoints] wrapped-branch row-consistency (diagnostic, chance≈16.7%%): %llu/%llu (%.1f%%)\n",
         (unsigned long long)wrappedRowAgree, (unsigned long long)wrappedTotal, consistencyRate * 100.0);

  // COVERAGE gate (S-review batch-3, n4096 scenario only -- the one large enough to make a 6/6 miss
  // meaningful): every one of the 6 valid table-row quaternions must appear at least once in the
  // GPU's wrapped output. A stride/permutation bug that only ever reaches a subset of rows (e.g. a
  // mis-strided read landing on 3 of 6) trips this even if matchesAnyTableRow/consistency still look
  // plausible on the reachable subset.
  int n4096Covered = 0;
  for (bool seen : n4096RowSeen) if (seen) ++n4096Covered;
  bool coverageOk = n4096Covered == 6;
  printf("[mathv-gridwalkpoints] n4096 wrapped-branch row-coverage: %d/6 -> %s\n", n4096Covered,
         coverageOk ? "ok" : "RED");

  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "gridwalkpoints");

  ParityReport rep("selftest-mathv-gridwalkpoints");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(Position+not-wrapped-Rotation, transcendental)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("wrappedTableMembership(structural, chaotic-hash-safe)", wrappedOk, wrappedOk ? 1.0 : 0.0);
  rep.expectTrue("n4096WrappedCoverage(6/6 valid quaternions seen)", coverageOk, (double)n4096Covered / 6.0);
  return rep.finish();
}

// order 1091: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1091, {"mathv-gridwalkpoints", runMathvGridWalkPointsSelfTest});

}  // namespace sw
