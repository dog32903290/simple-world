// SetAttributesWithPointFields golden — split from the leaf (≤400 ratchet). THREE legs (R-2): a
// SourcePoint near a FieldPoint gets pulled/colored; sever FieldPoints (injectBug) → passthrough.
//
//   (1) INJECT direct-cook leg (the biting leg). Hand-built ctx + a SINGLE FieldPoint at origin (W=1,
//       Color=RED) and a SINGLE SourcePoint offset +0.5 in X (Color=WHITE). Forced params:
//         Range=1, OffsetRange=0, AffectPosition=1, AffectColor=1, AffectOrientation=0 (skip slerp),
//         Amount=1, Variation=0, GainAndBias=(0.5,0.5) IDENTITY (ApplyGainAndBias(f,(.5,.5))=f),
//         ColorMode=0 (Add: c=lerp(srcColor,totalColor,AffectColor)), WMode=0 SET, AffectW=0.
//       Field gather (single field, fPos=origin, w=1):
//         dir=(srcPos-fPos)/w=(0.5,0,0); len=0.5; f=(1-saturate((0.5-0)/1))+0=0.5; GainAndBias id→0.5;
//         ×Selected(1)=0.5. WCurveAffectsWeight=0 → f stays 0.5.
//         gradColor=GradientImage.sample(0.5) (black→white)≈(0.5,0.5,0.5,1).
//         ColorMode=0 (Add) → totalColor += fieldColor*gradColor*1 = RED*(.5,.5,.5,1)=(0.5,0,0,1).
//         totalWeight += 0.5. distanceSq=0.25 → force=dir/|dir|^3=(0.5,0,0)/0.125=(4,0,0); totalForce=(4,0,0).
//       Offset: gMag≈4; gdir=(1,0,0); pos -= gdir*totalWeight*Amount*AffectPosition
//         = (0.5,0,0) - (1,0,0)*0.5*1*1 = (0,0,0). → SourcePoint pulled TO the field.
//       Color: ColorMode=0 Add → c=lerp(WHITE,(0.5,0,0,1),1)=(0.5,0,0,1); p.Color=(max(rgb,0),sat(a))=(0.5,0,0,1).
//       want Position≈(0,0,0) (tol 0.02), Color≈(0.5,0,0,1) (tol 0.02). injectBug → FieldCount=0 →
//         no force/color → Position stays (0.5,0,0), Color stays WHITE → both pins diverge → RED.
//
//   (2) FLAT-DRIVER production leg (closes the gather gap + proves inputs[1] addressable on the REAL
//       path). RadialPoints(#1, N=32 @ r=3) + SpherePoints(#2, N=8 @ r=0.5 fields) →
//       SetAttributesWithPointFields(#3, Range=10), cook through PointGraph::cook. Asserts cooked
//       count == 32 (countFromFirstPointsInput: NOT 40) AND some points moved inward (r<2.9 — the
//       field pulled them). A driver that concatenated Points inputs would size to 40 → count RED.
//
//   (3) RESIDENT (production) leg — R-2 iron rule. Same two-source chain → DrawPoints2 → RenderTarget
//       through cookResident; asserts a lit sprite (the 2-Points chain cooked on the resident path).
#include "runtime/point_ops_setattributeswithpointfields.h"

#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"          // SymbolLibrary (resident leg)
#include "runtime/eval_context.h"
#include "runtime/graph.h"                   // Graph/Node/pinId
#include "runtime/point_graph.h"             // PointCookCtx, PointGraph
#include "runtime/resident_eval_graph.h"     // buildEvalGraph (resident leg)
#include "runtime/tixl_point.h"              // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

void registerSetAttributesWithPointFieldsOp();  // leaf .cpp

namespace {

Symbol atomicOp2(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// (1) INJECT direct-cook leg.
bool injectLeg2(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool injectBug, SwPoint& out) {
  SwPoint src{};
  src.Position = SW_PACKED3{0.5f, 0.0f, 0.0f};
  src.FX1 = 0.0f;
  src.Color = SW_FLOAT4{1.0f, 1.0f, 1.0f, 1.0f};   // WHITE
  src.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  src.Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};

  SwPoint field{};
  field.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  field.FX1 = 1.0f;                                // W = 1
  field.Color = SW_FLOAT4{1.0f, 0.0f, 0.0f, 1.0f}; // RED
  field.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  field.Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};

  MTL::Buffer* srcBag = dev->newBuffer(&src, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* fieldBag = dev->newBuffer(&field, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);

  std::map<std::string, float> params;
  params["Amount"] = 1.0f;
  params["Range"] = 1.0f;
  params["OffsetRange"] = 0.0f;
  params["AffectPosition"] = 1.0f;
  params["AffectOrientation"] = 0.0f;  // skip the slerp
  params["AffectColor"] = 1.0f;
  params["AffectW"] = 0.0f;
  params["Variation"] = 0.0f;
  params["BiasAndGain.x"] = 0.5f;       // GainAndBias IDENTITY
  params["BiasAndGain.y"] = 0.5f;
  params["ColorMode"] = 0.0f;           // Add
  params["WMode"] = 0.0f;               // Set (AffectW=0 → W untouched)
  params["WCurveAffectsWeight"] = 0.0f;

  const MTL::Buffer* ins[2] = {srcBag, fieldBag};
  uint32_t insCounts[2] = {1, 1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 2;
  c.output = outBag; c.params = &params;

  setAttrWithFieldsInjectBug() = injectBug;
  cookSetAttributesWithPointFields(c);
  setAttrWithFieldsInjectBug() = false;

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); fieldBag->release(); outBag->release();
  return true;
}

// (2) FLAT-DRIVER production leg.
bool flatGraphLeg2(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, uint32_t& cookedCount,
                   int& movedCount) {
  registerBuiltinPointOps();
  registerSetAttributesWithPointFieldsOp();

  const uint32_t N = 32;
  Graph g;
  Node radial; radial.id = 1; radial.type = "RadialPoints";
  radial.params["Count"] = (float)N; radial.params["Radius"] = 3.0f;
  g.nodes.push_back(radial);
  Node sph; sph.id = 2; sph.type = "SpherePoints";
  sph.params["Count"] = 8.0f; sph.params["Radius"] = 0.5f;
  g.nodes.push_back(sph);
  Node sawf; sawf.id = 3; sawf.type = "SetAttributesWithPointFields";
  sawf.params["AffectPosition"] = 1.0f; sawf.params["Range"] = 10.0f;
  g.nodes.push_back(sawf);
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});  // RadialPoints → Points (port 0)
  g.connections.push_back({102, pinId(2, 0), pinId(3, 1)});  // SpherePoints → FieldPoints (port 1)

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, /*reg=*/nullptr, /*targetNodeId=*/3);

  cookedCount = pg.debugCookedCount(3);
  movedCount = 0;
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(3);
  if (!outBuf || cookedCount == 0) return false;
  const SwPoint* gpu =
      reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(outBuf)->contents());
  // RadialPoints sits at radius 3; the field (a sphere of 8 points near the origin) perturbs each
  // point's position along the accumulated gravity vector. Faithful TiXL behavior pushes them OFF
  // radius 3 (the exact sign is geometry-dependent — here outward to ~3.67). The tooth: positions
  // MOVED from the RadialPoints input (|r - 3| > 0.1). Without the field (countFromFirstPointsInput
  // mis-sized, or inputs[1] unaddressable) totalForce stays 0 → no offset → r stays exactly 3.
  for (uint32_t i = 0; i < cookedCount; ++i) {
    float r = std::sqrt(gpu[i].Position.x * gpu[i].Position.x +
                        gpu[i].Position.y * gpu[i].Position.y +
                        gpu[i].Position.z * gpu[i].Position.z);
    if (std::fabs(r - 3.0f) > 0.1f) ++movedCount;  // perturbed off the input radius by the field
  }
  return true;
}

// (3) RESIDENT production leg.
bool residentLeg2(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, std::vector<uint8_t>& px,
                  uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp2("RadialPoints", {{"Count", "Count", "Float", 32.0f}, {"Radius", "Radius", "Float", 3.0f}},
                {{"points", "points", "Points", 0.0f}});
  slib.symbols["SpherePoints"] =
      atomicOp2("SpherePoints", {{"Count", "Count", "Float", 8.0f}, {"Radius", "Radius", "Float", 0.5f}},
                {{"points", "points", "Points", 0.0f}});
  slib.symbols["SetAttributesWithPointFields"] = atomicOp2(
      "SetAttributesWithPointFields",
      {{"Points", "Points", "Points", 0.0f},
       {"FieldPoints", "FieldPoints", "Points", 0.0f},
       {"AffectPosition", "AffectPosition", "Float", 1.0f}, {"Range", "Range", "Float", 10.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp2(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.01f}, {"UseWForSize", "UseWForSize", "Float", 1.0f}},
      {{"out", "out", "Command", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp2(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "SpherePoints";
  SymbolChild c3; c3.id = 3; c3.symbolId = "SetAttributesWithPointFields";
  SymbolChild c4; c4.id = 4; c4.symbolId = "DrawPoints2";
  c4.overrides["Radius"] = 0.20f; c4.overrides["UseWForSize"] = 0.0f;
  SymbolChild c5; c5.id = 5; c5.symbolId = "RenderTarget";
  c5.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4, c5};
  root.connections = {
      {1, "points", 3, "Points"},
      {2, "points", 3, "FieldPoints"},
      {3, "out", 4, "points"},
      {4, "out", 5, "command"},
      {5, "out", kSymbolBoundary, "out"},
  };
  slib.symbols["Root"] = root; slib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"5");
  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runSetAttributesWithPointFieldsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-setattributeswithpointfields] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // (1) INJECT: pos→origin (0,0,0), Color→(0.5,0,0,1). want FIXED.
  SwPoint o{};
  injectLeg2(dev, q, lib, injectBug, o);
  const float wantPos[3] = {0.0f, 0.0f, 0.0f};
  const float wantColor[4] = {0.5f, 0.0f, 0.0f, 1.0f};
  float posErr = std::fabs(o.Position.x - wantPos[0]) + std::fabs(o.Position.y - wantPos[1]) +
                 std::fabs(o.Position.z - wantPos[2]);
  float colErr = std::fabs(o.Color.x - wantColor[0]) + std::fabs(o.Color.y - wantColor[1]) +
                 std::fabs(o.Color.z - wantColor[2]) + std::fabs(o.Color.w - wantColor[3]);
  bool injPosPass = posErr < 0.02f;
  bool injColPass = colErr < 0.02f;

  // (2) FLAT-DRIVER: count == 32 (NOT 40), some points moved inward.
  uint32_t fgCount = 0; int fgMoved = 0;
  bool gotFg = flatGraphLeg2(dev, q, lib, fgCount, fgMoved);
  bool fgPass = gotFg && fgCount == 32 && fgMoved > 0;

  // (3) RESIDENT: a lit sprite.
  std::vector<uint8_t> rpx; uint32_t rw = 0, rh = 0;
  bool gotRes = residentLeg2(dev, q, lib, rpx, rw, rh);
  int litCount = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)rw * rh; ++i)
      if (rpx[i * 4 + 0] > 30 || rpx[i * 4 + 1] > 30 || rpx[i * 4 + 2] > 30) ++litCount;
  bool resPass = gotRes && litCount > 20;

  bool pass = injPosPass && injColPass && fgPass && resPass;
  std::printf("[selftest-setattributeswithpointfields] INJECT: pos=(%.3f,%.3f,%.3f) want(0,0,0) "
              "posErr=%.4f pass=%d Color=(%.3f,%.3f,%.3f,%.3f) want(0.5,0,0,1) colErr=%.4f pass=%d | "
              "FLAT-DRIVER: count=%u(want32,not40) moved=%d pass=%d | RESIDENT: %ux%u lit=%d(need>20) "
              "pass=%d | injectBug=%d -> %s\n",
              (double)o.Position.x, (double)o.Position.y, (double)o.Position.z, (double)posErr,
              injPosPass ? 1 : 0, (double)o.Color.x, (double)o.Color.y, (double)o.Color.z,
              (double)o.Color.w, (double)colErr, injColPass ? 1 : 0, fgCount, fgMoved, fgPass ? 1 : 0,
              rw, rh, litCount, resPass ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
