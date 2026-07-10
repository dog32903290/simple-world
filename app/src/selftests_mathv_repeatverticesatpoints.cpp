// selftests_mathv_repeatverticesatpoints.cpp — --selftest-mathv-repeatverticesatpoints (hand-rolled
// TU). MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU
// "repeatverticesatpoints" kernel (app/shaders/repeatverticesatpoints.metal — glslang+spirv-cross
// output of TiXL mesh-RepeatVerticesAtPoints.hlsl, §10.1 recipe, with the wave-2 packed_float3 struct
// fix documented in that .metal's header) against the R-authored CPU oracle
// (app/src/mathv_ref_repeatverticesatpoints.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// TRANSCENDENTAL-class op (quaternion rotation via cross-product/matrix — same class as PointSimulation's
// qSlerp, wave-1 precedent).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ─────────────────────────────────────────────
// mesh-RepeatVerticesAtPoints.hlsl has no .t3ui of its own (internal kernel dispatched by
// RepeatMeshAtPoints.t3's cook loop). Stretch/Size/ApplyScale/ScaleFX/TexCoord2Factor are exercised
// over generous authoring-plausible ranges (Stretch/Size near [0.1,4] typical mesh-instancing scale,
// ApplyScale/enum fields swept 0/1/nonzero).
//
// ── DISPATCH SHAPE: 2D grid (x=source vertex index, y=point index), targetVertexIndex = y*vertexCount
// + x — this TU allocates ResultVertices sized vertexCount*pointCount and reads back at that mapping.
//
// ZONE: shell tier; crosses runtime only for SwPoint/SwVertex (Point/PbrVertex's byte-identical host
// mirrors, runtime/tixl_point.h + runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_repeatverticesatpoints.h"
#include "parity_golden_harness.h"
#include "runtime/repeatverticesatpoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/sw_mesh.h"
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

struct RvatpDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  RvatpDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("repeatverticesatpoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~RvatpDispatch() { if (pso) pso->release(); }
  RvatpDispatch(const RvatpDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& srcV, const std::vector<SwPoint>& srcP,
                const RepeatVerticesAtPointsParams& prm, std::vector<SwVertex>& result) const {
    if (!ok) return false;
    const uint32_t nv = (uint32_t)srcV.size(), np = (uint32_t)srcP.size();
    if (nv == 0 || np == 0) return false;
    result.assign((size_t)nv * np, SwVertex{});
    MTL::Buffer* bufP =
        dev->newBuffer(srcP.data(), (NS::UInteger)(np * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufV =
        dev->newBuffer(srcV.data(), (NS::UInteger)(nv * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufRes = dev->newBuffer(result.data(), (NS::UInteger)(result.size() * sizeof(SwVertex)),
                                         MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufP, 0, RVATP_Points);
    enc->setBuffer(bufV, 0, RVATP_SourceVertices);
    enc->setBuffer(bufRes, 0, RVATP_ResultVertices);
    enc->setBytes(&prm, sizeof(prm), RVATP_Params);
    const uint32_t tgx = 16, tgy = 16;
    enc->dispatchThreadgroups(MTL::Size::Make((nv + tgx - 1) / tgx, (np + tgy - 1) / tgy, 1),
                              MTL::Size::Make(tgx, tgy, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(result.data(), bufRes->contents(), result.size() * sizeof(SwVertex));
    bufP->release();
    bufV->release();
    bufRes->release();
    return true;
  }
};

SwVertex randomVertex(Rng& rng) {
  SwVertex v{};
  v.Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
  v.Normal = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Tangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Bitangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Texcoord = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Texcoord2 = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

SwPoint randomPoint(Rng& rng) {
  SwPoint p{};
  p.Position = {rng.uniform(-10.0f, 10.0f), rng.uniform(-10.0f, 10.0f), rng.uniform(-10.0f, 10.0f)};
  // random unit quaternion via normalized random 4-vector (avoids the identity/zero degeneracy)
  float x = rng.uniform(-1.0f, 1.0f), y = rng.uniform(-1.0f, 1.0f), z = rng.uniform(-1.0f, 1.0f),
        w = rng.uniform(-1.0f, 1.0f);
  float len = std::sqrt(x * x + y * y + z * z + w * w);
  if (len < 1e-6f) { x = 0; y = 0; z = 0; w = 1; len = 1; }
  p.Rotation = {x / len, y / len, z / len, w / len};
  p.Color = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), 1.0f};
  p.Scale = {rng.uniform(0.1f, 4.0f), rng.uniform(0.1f, 4.0f), rng.uniform(0.1f, 4.0f)};
  p.FX1 = rng.uniform(0.1f, 4.0f);
  p.FX2 = rng.uniform(0.1f, 4.0f);
  return p;
}

}  // namespace

int runMathvRepeatVerticesAtPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-repeatverticesatpoints] FAIL: no metallib\n"); return 1; }
  RvatpDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-repeatverticesatpoints] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-repeatverticesatpoints", EpsSpec::transcendental(), 5);
  bool dispatchOk = true;

  struct Scenario {
    size_t nv, np; float stretch[3]; float size; float applyScale; int32_t scaleFX; int32_t texFactor;
    const char* tag;
  };
  const Scenario scenarios[] = {
      {1, 1, {1, 1, 1}, 1.0f, 0.0f, 0, 0, "nv1-np1"},
      {8, 1, {1.5f, 0.8f, 2.0f}, 1.2f, 1.0f, 1, 1, "nv8-np1"},
      {1, 8, {0.5f, 0.5f, 0.5f}, 0.7f, 1.0f, 2, 2, "nv1-np8"},
      {33, 5, {2.0f, 1.0f, 0.3f}, 3.0f, 1.0f, 0, 1, "nv33-np5"},
      {64, 16, {1.0f, 1.0f, 1.0f}, 1.0f, 0.0f, 1, 0, "nv64-np16"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwVertex> srcV(s.nv);
    for (auto& v : srcV) v = randomVertex(rng);
    std::vector<SwPoint> srcP(s.np);
    for (auto& p : srcP) p = randomPoint(rng);
    RepeatVerticesAtPointsParams prm{};
    prm.StretchX = s.stretch[0]; prm.StretchY = s.stretch[1]; prm.StretchZ = s.stretch[2];
    prm.Size = injectBug ? s.size + 1e-2f : s.size;
    prm.ApplyScale = s.applyScale;
    prm.PointCount = (int32_t)s.np;  // dead field, ABI-mirroring only
    prm.ScaleFX = s.scaleFX;
    prm.TexCoord2Factor = s.texFactor;
    prm.PointsCount = (uint32_t)s.np;
    prm.VerticesCount = (uint32_t)s.nv;
    std::vector<SwVertex> result;
    if (!disp.dispatch(srcV, srcP, prm, result)) { dispatchOk = false; continue; }

    mathv_ref::RepeatVerticesAtPointsParams rp{s.stretch[0], s.stretch[1], s.stretch[2],
                                               s.size, s.applyScale, s.scaleFX, s.texFactor};
    for (size_t py = 0; py < s.np; ++py) {
      for (size_t vx = 0; vx < s.nv; ++vx) {
        const SwVertex& v = srcV[vx];
        const SwPoint& p = srcP[py];
        mathv_ref::RepeatVerticesAtPointsIn in{
            v.Position.x, v.Position.y, v.Position.z, v.Normal.x, v.Normal.y, v.Normal.z,
            v.ColorRgb.x, v.Texcoord2.y, p.Position.x, p.Position.y, p.Position.z,
            p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w, p.Color.x,
            p.Scale.x, p.Scale.y, p.Scale.z, p.FX1, p.FX2};
        mathv_ref::RepeatVerticesAtPointsOut refOut{};
        mathv_ref::repeatVerticesAtPointsOne(in, refOut, rp);
        const SwVertex& g = result[py * s.nv + vx];
        float inVec[20] = {in.vPosX, in.vPosY, in.vPosZ, in.vNormX, in.vNormY, in.vNormZ, in.vColorR,
                           in.vTexCoord2Y, in.pPosX, in.pPosY, in.pPosZ, in.pRotX, in.pRotY, in.pRotZ,
                           in.pRotW, in.pColorR, in.pScaleX, in.pScaleY, in.pScaleZ, in.pFX1};
        float gv[8] = {g.Position.x, g.Position.y, g.Position.z, g.Normal.x, g.Normal.y, g.Normal.z,
                       g.ColorRgb.x, g.Texcoord2.y};
        float rv[8] = {refOut.posX, refOut.posY, refOut.posZ, refOut.normX, refOut.normY, refOut.normZ,
                       refOut.colorR, refOut.texCoord2Y};
        for (int k = 0; k < 8; ++k) cmpMain.add(gv[k], rv[k], inVec, 20, k, -1.0f, s.tag);
      }
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "repeatverticesatpoints");

  ParityReport rep("selftest-mathv-repeatverticesatpoints");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, transcendental)", passMain, passMain ? 1.0 : 0.0);
  return rep.finish();
}

// order 1074: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1074, {"mathv-repeatverticesatpoints", runMathvRepeatVerticesAtPointsSelfTest});

}  // namespace sw
