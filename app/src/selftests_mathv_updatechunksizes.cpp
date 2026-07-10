// selftests_mathv_updatechunksizes.cpp — --selftest-mathv-updatechunksizes (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批: fuzz the TRANSPILED GPU "updatechunksizes"
// kernel (app/shaders/updatechunksizes.metal — glslang+spirv-cross output of TiXL MeshChunks-
// UpdateChunkSizes.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_updatechunksizes.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (integer double-lookup + one int->uint reinterpret cast, no floats).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (internal `_` kernel of the DrawMeshChunksAtPoints compound, no user-facing node of its
// own). All three cbuffer ints are counts/sizes computed by the compound's cook loop, always >=1 in
// realistic use. This TU deliberately keeps BOTH modulo operands (thread index, buffer-stored chunk
// index) non-negative — see mathv_ref_updatechunksizes.h header for why: HLSL spec's truncated `%`
// and the transpiler's emitted floored `spvSMod` provably agree whenever both operands are >=0, so
// testing negative operands here would be probing the transpiler's `%`-lowering choice, not this
// kernel's math (out of scope for THIS op's mathv case). ChunkIndicesForPoints values ARE fuzzed
// across [0, 4*ChunkDefCount) to exercise the `% ChunkDefCount` wraparound. ChunkDef.FaceCount IS
// fuzzed across the full int32 range (including negative) to exercise the `uint(int)` reinterpret
// cast on the output write (HLSL implicit int->uint conversion == C++'s well-defined two's-complement
// uint32_t(int32_t) cast — both sides must agree bit-for-bit, e.g. FaceCount=-1 -> ChunkSizes=4294967295).
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header (no SwPoint/SwVertex —
// this op is pure int/struct-of-int buffers).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_updatechunksizes.h"
#include "parity_golden_harness.h"
#include "runtime/selftest_registry.h"
#include "runtime/updatechunksizes_params.h"

#include <cstdint>
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

struct UcsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  UcsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("updatechunksizes", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~UcsDispatch() { if (pso) pso->release(); }
  UcsDispatch(const UcsDispatch&) = delete;

  bool dispatch(const std::vector<int32_t>& chunkIndicesForPoints,
                const std::vector<mathv_ref::UpdateChunkSizesChunkDef>& chunkDefs, int32_t pointCount,
                std::vector<uint32_t>& chunkSizesOut) const {
    if (!ok) return false;
    if (pointCount <= 0 || chunkIndicesForPoints.empty() || chunkDefs.empty()) return false;
    chunkSizesOut.assign((size_t)pointCount, 0xDEADBEEFu);
    MTL::Buffer* bufIdx = dev->newBuffer(chunkIndicesForPoints.data(),
                                         (NS::UInteger)(chunkIndicesForPoints.size() * sizeof(int32_t)),
                                         MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDefs = dev->newBuffer(chunkDefs.data(),
                                          (NS::UInteger)(chunkDefs.size() * sizeof(mathv_ref::UpdateChunkSizesChunkDef)),
                                          MTL::ResourceStorageModeShared);
    MTL::Buffer* bufSizes = dev->newBuffer(chunkSizesOut.data(),
                                           (NS::UInteger)(chunkSizesOut.size() * sizeof(uint32_t)),
                                           MTL::ResourceStorageModeShared);
    UpdateChunkSizesParams prm{pointCount, (int32_t)chunkDefs.size(), (int32_t)chunkIndicesForPoints.size()};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&prm, sizeof(prm), UCS_Params);
    enc->setBuffer(bufIdx, 0, UCS_ChunkIndicesForPoints);
    enc->setBuffer(bufSizes, 0, UCS_ChunkSizes);
    enc->setBuffer(bufDefs, 0, UCS_ChunkDefs);
    const uint32_t tg = 256;  // matches THREADS_PER_GROUP in the HLSL
    const uint32_t n = (uint32_t)pointCount;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(chunkSizesOut.data(), bufSizes->contents(), chunkSizesOut.size() * sizeof(uint32_t));
    bufIdx->release();
    bufDefs->release();
    bufSizes->release();
    return true;
  }
};

}  // namespace

int runMathvUpdateChunkSizesSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-updatechunksizes] FAIL: no metallib\n"); return 1; }
  UcsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-updatechunksizes] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-updatechunksizes", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { int32_t pointCount; int32_t chunkDefCount; int32_t chunkIdxCount; const char* tag; };
  const Scenario scenarios[] = {
      {1, 1, 1, "n1"},
      {63, 5, 17, "n63"},
      {64, 1, 1, "n64-singleton"},   // every mod collapses to index 0 (branch-boundary-ish shape)
      {65, 8, 3, "n65"},
      {256, 13, 40, "n256"},
      {4096, 97, 251, "n4096"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<int32_t> chunkIndices((size_t)s.chunkIdxCount);
    for (auto& v : chunkIndices) v = (int32_t)rng.uniform(0.0f, (float)(4 * s.chunkDefCount));
    std::vector<mathv_ref::UpdateChunkSizesChunkDef> defs((size_t)s.chunkDefCount);
    for (auto& d : defs) {
      d.startFaceIndex = (int32_t)rng.uniform(-1e6f, 1e6f);
      // FaceCount kept inside +-2^20 (comfortably < float32's 2^24 exact-integer ceiling) so the
      // Comparator's float cast below never loses precision -- this still exercises the negative-value
      // uint(int) reinterpret-cast path (a dedicated boundary check below covers INT32_MIN/-1/MAX
      // separately with exact uint32 equality, not float, since |wrapped uint| can exceed 2^24).
      d.faceCount = (int32_t)rng.uniform(-1048576.0f, 1048576.0f);
      d.startVertexIndex = (int32_t)rng.uniform(-1e6f, 1e6f);
      d.vertexCount = (int32_t)rng.uniform(-1e6f, 1e6f);
    }
    // injectBug: perturb the real GPU-side input (first chunk index), ref keeps the original --
    // forces a fork whenever chunkDefCount>1 (the perturbed lookup index selects a different ChunkDef).
    std::vector<int32_t> gpuChunkIndices = chunkIndices;
    if (injectBug && !gpuChunkIndices.empty()) gpuChunkIndices[0] += 1;
    std::vector<uint32_t> gpuOut;
    if (!disp.dispatch(gpuChunkIndices, defs, s.pointCount, gpuOut)) { dispatchOk = false; continue; }
    for (int32_t i = 0; i < s.pointCount; ++i) {
      uint32_t refVal = mathv_ref::updateChunkSizesOne(i, chunkIndices, defs, s.chunkIdxCount);
      float inVec[1] = {(float)i};
      // Compare via the int32 reinterpretation (bit-identical to the uint32 output, but stays inside
      // float32's exact-integer range given the +-2^20 FaceCount domain above).
      cmpMain.add((float)(int32_t)gpuOut[(size_t)i], (float)(int32_t)refVal, inVec, 1, 0, -1.0f, s.tag);
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "updatechunksizes");

  // Dedicated exact-uint32 boundary check (NOT float-based -- exercises the uint(int) reinterpret cast
  // at magnitudes float32 cannot represent exactly): FaceCount in {INT32_MIN, -1, 0, INT32_MAX} must
  // wrap to the corresponding uint32 bit pattern.
  bool boundaryOk = true;
  {
    std::vector<mathv_ref::UpdateChunkSizesChunkDef> defs(4);
    const int32_t faceCounts[4] = {INT32_MIN, -1, 0, INT32_MAX};
    for (int k = 0; k < 4; ++k) defs[(size_t)k] = {0, faceCounts[k], 0, 0};
    for (int k = 0; k < 4; ++k) {
      // chunkIdxCount=1 -> pointIndex(0) % 1 == 0 -> chunkIndex = idx1[0] = k; chunkDefCount=4 ->
      // defIdx = k % 4 == k -> selects defs[k]. Isolates ONE faceCounts[k] per dispatch.
      std::vector<int32_t> idx1{k};
      std::vector<uint32_t> out;
      if (!disp.dispatch(idx1, defs, 1, out)) { boundaryOk = false; continue; }
      uint32_t expect = (uint32_t)faceCounts[k];
      if (out.empty() || out[0] != expect) boundaryOk = false;
    }
  }

  ParityReport rep("selftest-mathv-updatechunksizes");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("intToUintCast(boundary: INT32_MIN/-1/0/MAX)", boundaryOk, boundaryOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1080: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1080, {"mathv-updatechunksizes", runMathvUpdateChunkSizesSelfTest});

}  // namespace sw
