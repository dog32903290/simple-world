// SortPoints — the THIRD camera-matrix-into-points seam consumer (PointCookCtx::cameraToWorld) and a
// count-preserving REORDER. Port of external/tixl .../Operators/Lib/point/modify/SortPoints.cs +
// .../Assets/shaders/points/modify/SortPoints.hlsl. Reorders the Points bag by each point's distance
// to the camera WORLD position, so a downstream alpha-blended DrawPoints draws back-to-front (the
// classic painter's-order use). Reads ONLY CameraToWorld (its translation row = the camera world pos);
// no projection, no per-point matrix — rides the camera-matrix-into-points seam C1 already fills.
//
// ★NAMED FORK vs the .cs/.hlsl — fork-sortpoints-converged-not-incremental (v1):
//   TiXL's GPU SortPoints is a FRAME-INCREMENTAL bitonic network: every frame it runs `SortingSpeed`
//   passes of a persistent IndexBuffer (FrameIndex*CallCount+PassIndex drives the stage schedule), so
//   the order only fully converges over many frames. That stateful multi-pass IndexBuffer is a
//   persistent-point-buffer / feedback seam SW does NOT have yet. This v1 collapses it to the CONVERGED
//   result: a single-cook FULL stable sort by the same key. For a still frame (the only thing SW renders
//   today) the converged order is exactly what TiXL reaches after it settles — byte-faithful ENDPOINT,
//   not the per-frame transient. SortingSpeed (how fast it converges) therefore has no observable effect
//   in v1 and is read-but-ignored (a later feedback seam would honour it).
//
// ★SORT KEY (1:1 with the HLSL c2k, .hlsl:73-80):
//   k = length(p.Position - CameraToWorld[3].xyz)            // distance to the camera world position
//   if (isnan(p.Scale.x)) k = -1                              // NaN-scale points sink to the very back
//   if (Ascending)        k = -k                              // flip so the sense matches the .hlsl flag
//   main_sort swaps when k1 < k2 (.hlsl:136) → the result is sorted by k DESCENDING. So:
//     Ascending=false (.t3 default): key = +distance, descending  → FARTHEST point first (back-to-front)
//     Ascending=true               : key = -distance, descending  → NEAREST  point first (front-to-back)
//   NaN-scale (k=-1) is the smallest finite key → always last (Ascending=false) — matches the HLSL.
//
// ★CPU LEAF (no .metal / no _params.h): like ListToBuffer (point_graph.cpp), this op reads the upstream
//   bag's contents() (StorageModeShared, CPU-visible) and writes the reordered bag into c.output's
//   contents() — a pure host reorder. A full bitonic GPU sort is the bulk-N optimisation; the converged
//   ORDER is identical, and SW's golden bags are tiny, so the CPU reorder is the clean v1 (zero shared
//   shader files, fewer merge points). count is preserved (modify op: c.count == upstream count).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"        // SymbolLibrary / atomicOp (resident leg)
#include "runtime/eval_context.h"
#include "runtime/field_camera.h"          // pointCameraMatrices (golden: camera world pos)
#include "runtime/graph.h"                 // Graph/Node/pinId (flat-driver leg)
#include "runtime/point_graph.h"           // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"   // buildEvalGraph (resident leg)
#include "runtime/tixl_point.h"            // SwPoint (64B); .Scale @48 (NaN sink), .Position @0

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"  // CMake passes the built-metallib absolute path; this is the fallback
#endif

namespace sw {
namespace {

// The .hlsl c2k key for one point given the camera world position (CameraToWorld[3].xyz). isnan(Scale.x)
// → -1 (sinks to the back); Ascending flips the sign so the DESCENDING main_sort gives near-first.
float sortKey(const SwPoint& p, const float camWorldPos[3], bool ascending) {
  if (std::isnan(p.Scale.x)) return -1.0f;  // .hlsl:75 — NaN-scale points to the very back
  float dx = p.Position.x - camWorldPos[0];
  float dy = p.Position.y - camWorldPos[1];
  float dz = p.Position.z - camWorldPos[2];
  float k = std::sqrt(dx * dx + dy * dy + dz * dz);  // .hlsl:76 length(pos - CameraToWorld[3].xyz)
  return ascending ? -k : k;                          // .hlsl:78 if(Ascending>0) k=-k
}

// SortPoints cook: read the upstream bag, stable-sort indices by the camera-distance key DESCENDING
// (the converged result of the .hlsl main_sort's k1<k2 swap), write the reordered points to c.output.
// No Points input / no camera → nothing to do (the seam guard; a hand-built ctx with hasCamera=false
// reorders with an identity CameraToWorld → camera at origin, the injectBug observation).
void cookSortPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input → nothing to do

  const SwPoint* src = reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(srcBag)->contents());
  SwPoint* dst = reinterpret_cast<SwPoint*>(c.output->contents());
  const uint32_t n = c.count;

  // Camera WORLD position = CameraToWorld translation row (row-major m[12..14]). hasCamera=false (the
  // injectBug leg / no Camera scope) leaves it identity → camera at the origin (distances become raw
  // |pos|), the RED tooth's divergence from the real default-camera key.
  float camWorldPos[3] = {0.0f, 0.0f, 0.0f};
  if (c.hasCamera) {
    camWorldPos[0] = c.cameraToWorld[12];
    camWorldPos[1] = c.cameraToWorld[13];
    camWorldPos[2] = c.cameraToWorld[14];
  }
  const bool ascending = cookParam(c, "Ascending", 0.0f) > 0.5f;  // .t3 default false

  // STABLE sort of indices by key DESCENDING (== the converged main_sort order). Stable keeps the
  // original relative order for equal keys (the bitonic network is deterministic but stability is the
  // clean, observable contract for a single-cook reorder).
  std::vector<uint32_t> order(n);
  for (uint32_t i = 0; i < n; ++i) order[i] = i;
  std::vector<float> keys(n);
  for (uint32_t i = 0; i < n; ++i) keys[i] = sortKey(src[i], camWorldPos, ascending);
  std::stable_sort(order.begin(), order.end(),
                   [&](uint32_t a, uint32_t b) { return keys[a] > keys[b]; });  // descending

  for (uint32_t i = 0; i < n; ++i) dst[i] = src[order[i]];
}

}  // namespace

void registerSortPointsOp() {
  registerPointOp("SortPoints", cookSortPoints);
}

// ============================================================================================
// Golden — THREE legs (R-2: flat-only is self-deception; the camera key must reach BOTH cook drivers and
// the resulting ORDER must match the host closed-form camera-distance sort).
//
//  (1) DIRECT-COOK closed-form order: hand-built ctx with hasCamera + the default-camera cameraToWorld.
//      Three points at known world z (0, +1, -1). The default camera sits at world (0,0,+2.4142) looking
//      down -z. Distances: z=+1 nearest (1.41), z=0 mid (2.41), z=-1 farthest (3.41). Ascending=false →
//      DESCENDING distance → output order = [z=-1, z=0, z=+1] (farthest first). The host computes the
//      same key from pointCameraMatrices' cameraToWorld[3] and asserts the output Position.z sequence
//      matches. injectBug (hasCamera=false → camera at origin) → distances |z| = [0,1,1] → the z=0 point
//      becomes NEAREST not MIDDLE → the order changes → RED.
//
//  (2) DIRECT-COOK Ascending flip + NaN-scale sink: the SAME three points with Ascending=true → key
//      negated → NEAREST first → order = [z=+1, z=0, z=-1] (the reverse of leg 1). A FOURTH point with
//      Scale.x = NaN must land LAST regardless of its distance (k=-1, the smallest key). Asserts the
//      ascending order AND the NaN point's final slot — bites a sign or a NaN-handling regression.
//
//  (3) RESIDENT (production) leg: RadialPoints → SortPoints → DrawPoints2 → RenderTarget via cookResident;
//      read the rendered pixels → assert lit sprites exist. Proves the camera key gather reaches
//      cookResident (the production driver) and the reordered bag still renders (count preserved, no bag
//      corruption). The exact-order teeth live in the direct legs; this leg proves the seam + count.
// ============================================================================================

namespace {

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// A grid of points — the resident-leg source gen (mirrors the camera-op goldens). Spread on an XY grid
// at z=0 so several project on-screen at the default camera; SortPoints reorders them by camera distance,
// the cluster still renders (count preserved).
void gridPointsGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    float fx = (float)(i % 8) / 7.0f - 0.5f, fy = (float)((i / 8) % 8) / 7.0f - 0.5f;  // [-0.5,0.5]
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1}; dst[i].Scale = {1, 1, 1};
    dst[i].Position = {0.6f * fx, 0.6f * fy, 0.0f};
  }
}

// DIRECT-COOK leg: dispatch over a hand-built bag, read back the reordered output. hasCamera = withCamera;
// optional Ascending param. Returns out[0..N-1] + the default-camera cameraToWorld (for the host key).
void directLeg(MTL::Device* dev, MTL::CommandQueue* q, bool withCamera, bool ascending, const SwPoint* in,
               uint32_t N, SwPoint* out, float cameraToWorld[16]) {
  float o2cUnused[16];
  pointCameraMatrices(1.0f, o2cUnused, cameraToWorld);

  MTL::Buffer* srcBag = dev->newBuffer(in, (size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  std::map<std::string, float> params;
  params["Ascending"] = ascending ? 1.0f : 0.0f;
  PointCookCtx c;
  c.dev = dev; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (withCamera) { std::memcpy(c.cameraToWorld, cameraToWorld, 16 * sizeof(float)); c.hasCamera = true; }
  cookSortPoints(c);
  std::memcpy(out, outBag->contents(), (size_t)N * sizeof(SwPoint));
  srcBag->release(); outBag->release();
}

// RESIDENT (production) leg: RadialPoints → SortPoints → DrawPoints2 → RenderTarget via cookResident; read
// the rendered pixels. Asserts lit sprites (the camera key gather LIVES + the reordered bag renders).
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();
  registerPointOp("RadialPoints", gridPointsGen);  // override with the grid gen (visible cluster)

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["SortPoints"] = atomicOp(
      "SortPoints",
      {{"Points", "Points", "Points", 0.0f}, {"Camera", "Camera", "Camera", 0.0f},
       {"SortingSpeed", "SortingSpeed", "Float", 1.0f}, {"Ascending", "Ascending", "Float", 0.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.05f}, {"UseWForSize", "UseWForSize", "Float", 0.0f}},
      {{"out", "out", "Command", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 256.0f}, {"CustomH", "CustomH", "Float", 256.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "SortPoints";
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints2";
  c3.overrides["Radius"] = 0.04f; c3.overrides["UseWForSize"] = 0.0f;
  SymbolChild c4; c4.id = 4; c4.symbolId = "RenderTarget";
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {
      {1, "points", 2, "Points"},
      {2, "out", 3, "points"},
      {3, "out", 4, "command"},
      {4, "out", kSymbolBoundary, "out"},
  };
  slib.symbols["Root"] = root; slib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"4");
  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

// Build the four-point probe set: z = {0, +1, -1} plus a NaN-scale point at z=+0.5.
void makeProbes(SwPoint in[4]) {
  for (int i = 0; i < 4; ++i) {
    in[i] = SwPoint{};
    in[i].Rotation = SW_FLOAT4{0, 0, 0, 1}; in[i].Color = SW_FLOAT4{1, 1, 1, 1};
    in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  in[0].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};   // distance 2.4142 (mid)
  in[1].Position = SW_PACKED3{0.0f, 0.0f, 1.0f};   // distance 1.4142 (near)
  in[2].Position = SW_PACKED3{0.0f, 0.0f, -1.0f};  // distance 3.4142 (far)
  in[3].Position = SW_PACKED3{0.0f, 0.0f, 0.5f};   // NaN-scale → sinks last
  in[3].Scale = SW_PACKED3{NAN, 1.0f, 1.0f};
}

}  // namespace

int runSortPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-sortpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  SwPoint in[4];
  makeProbes(in);

  // ── (1) DESCENDING-by-distance (Ascending=false) + NaN sink: four points (z=0,+1,-1 + NaN@z=0.5) →
  //        finite descending [far, mid, near] = z[-1,0,+1], the NaN-scale point LAST (key=-1, smallest). ──
  SwPoint out1[4];
  float c2w[16];
  directLeg(dev, q, /*withCamera=*/!injectBug, /*ascending=*/false, in, 4, out1, c2w);
  // Host closed-form: camera world pos = cameraToWorld[12..14]; key = distance; descending order.
  float camPos[3] = {c2w[12], c2w[13], c2w[14]};
  auto dist = [&](const SwPoint& p) {
    float dx = p.Position.x - camPos[0], dy = p.Position.y - camPos[1], dz = p.Position.z - camPos[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  // Expected descending-distance order of the finite points: far(z=-1) → mid(z=0) → near(z=+1); NaN last.
  bool descPass = (out1[0].Position.z == in[2].Position.z) &&  // far first
                  (out1[1].Position.z == in[0].Position.z) &&  // mid
                  (out1[2].Position.z == in[1].Position.z);    // near (last finite, before NaN)
  // Confirm the finite ordering is monotonic non-increasing in distance (the real key drove it).
  bool descMono = dist(out1[0]) >= dist(out1[1]) && dist(out1[1]) >= dist(out1[2]);
  bool nanLast = std::isnan(out1[3].Scale.x);  // NaN-scale point (.hlsl c2k → -1) sank to the very back

  // ── (2) ASCENDING flip: three FINITE points, Ascending=true → key negated → nearest first ──
  // (NaN deliberately excluded here: the .hlsl c2k returns -1 for NaN BEFORE the Ascending flip, so in
  //  ascending mode the NaN key (-1) is NOT the smallest vs the negated finite keys — it lands mid-pack,
  //  not last. The NaN-last contract is an Ascending=false property, pinned in leg 1.)
  SwPoint out2[3];
  float c2wA[16];
  directLeg(dev, q, /*withCamera=*/!injectBug, /*ascending=*/true, in, 3, out2, c2wA);
  // Ascending → nearest first: near(z=+1) → mid(z=0) → far(z=-1) (the reverse of leg 1's finite order).
  bool ascPass = (out2[0].Position.z == in[1].Position.z) &&  // near first
                 (out2[1].Position.z == in[0].Position.z) &&  // mid
                 (out2[2].Position.z == in[2].Position.z);    // far last

  // ── (3) RESIDENT (production) leg ──
  std::vector<uint8_t> px;
  uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, px, ow, oh);
  int litCount = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (px[i * 4 + 0] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++litCount;
  bool resPass = gotRes && litCount > 20;

  // injectBug: hasCamera=false → camera at origin → distances become |z| = [0,1,1] for z=[0,+1,-1]. The
  // z=0 point becomes the NEAREST (dist 0) not the MIDDLE → descending order = [z=±1 (tie), z=±1, z=0] →
  // out1[2] is z=0 not z=+1 → descPass FAILS. The NaN/ascending legs ride the same camera divergence.
  bool pass = descPass && descMono && ascPass && nanLast && resPass;
  std::printf("[selftest-sortpoints] DESC+NaN: order.z=(%.1f,%.1f,%.1f) want(-1,0,1) pass=%d mono=%d "
              "nanLast=%d | ASC: order.z=(%.1f,%.1f,%.1f) want(1,0,-1) pass=%d | RESIDENT: %ux%u lit=%d "
              "pass=%d | camPos=(%.3f,%.3f,%.3f) | injectBug=%d -> %s\n",
              out1[0].Position.z, out1[1].Position.z, out1[2].Position.z, descPass ? 1 : 0,
              descMono ? 1 : 0, nanLast ? 1 : 0, out2[0].Position.z, out2[1].Position.z,
              out2[2].Position.z, ascPass ? 1 : 0, ow, oh, litCount, resPass ? 1 : 0,
              camPos[0], camPos[1], camPos[2], injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
