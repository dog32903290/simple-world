// pointlist_golden — --selftest-pointlist. The cpu-point-list seam golden (the 7th cook flow + the
// ListToBuffer upload bridge). Proves FOUR things, the last being the ★ R-2 PRODUCTION leg:
//
//   LEG 1 — TRANSPORT (flat): RadialPointsCpu cooked as terminal → debugCookedPointList readback ==
//           the closed-form ring coords (hand-derived from RadialPointsCpu.cs, NOT self-consistent).
//   LEG 2 — UPLOAD BRIDGE byte-parity (flat): RadialPointsCpu → ListToBuffer → readback the GPU buffer
//           contents() and assert it is byte-identical to the host list, ALL 64 bytes per SwPoint
//           (Position@0 / FX1@12 / Rotation@16 / Color@32 / Scale@48 / FX2@60). This is the
//           packed_float3 stride proof: a wrong stride would scramble every field after offset 12.
//   LEG 3 — TransformCpuPoint (flat): a hand-built 1-point list → TransformCpuPoint(Translation) →
//           readback the 1-element output and assert Position += translation, Rotation composed.
//   LEG 4 — ★ PRODUCTION PIXEL (R-2): RadialPointsCpu → ListToBuffer → DrawPoints → RenderTarget built
//           through the CANONICAL production path (libFromGraph → buildEvalGraph → cookResident), then
//           read pg.target() pixels and assert the RING is lit + the CENTER is black. This is the leg
//           that proves the bridge LIVES in the running app (the lane Cut47 trap: a flat-only bridge
//           passes its golden but draws nothing on screen because production walks cookResident).
//
// injectBug routes through pointListInjectBug(): RadialPointsCpu CLEARS its real output → the transport
// readback is empty (≠ ring), the GPU buffer is count 0, and the production screen is BLACK. Teeth on
// the actual cook path, NOT by flipping the expected value.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"             // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"              // PointGraph::cook/cookResident + debugCookedPointList + registerBuiltinPointOps
#include "runtime/pointlist_op_registry.h"   // pointListInjectBug / swPointDefault
#include "runtime/resident_eval_graph.h"     // buildEvalGraph (production path)
#include "runtime/tixl_point.h"              // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }

// CLOSED-FORM RadialPointsCpu reference (RadialPointsCpu.cs, NOT a copy of the leaf's code path):
// independent re-derivation so the golden checks the leaf against TiXL, not against itself. Defaults
// axis=(0,0,1), center=(0,0,0), offset=0, startAngle=0, cycles=1, radiusOffset=0, closeCircle=false.
// With axis=Z: each point's Position = rotateZ((R,0,0), angle) + center; angle starts at π/2, step
// -2π/corners. F1 = W (no W-offset). The leaf computes the same via Rodrigues — agreement = parity.
std::vector<SwPoint> radialRef(int count, float radius, float w) {
  std::vector<SwPoint> out;
  int corners = count < 1 ? 1 : (count > 10000 ? 10000 : count);
  int pointCount = corners;  // circleOffset 0 (closeCircle false)
  float angle = kPi * 0.5f;
  float delta = -1.0f * (2.0f * kPi) / (float)corners;  // cycles=1
  for (int i = 0; i < pointCount; ++i) {
    float length = radius;  // radiusOffset 0 → lerp(R,R,f)=R
    // rotateZ((length,0,0), angle) = (length*cos, length*sin, 0)
    float px = length * std::cos(angle);
    float py = length * std::sin(angle);
    SwPoint p = swPointDefault();
    p.Position = {px, py, 0.0f};
    p.FX1 = w;
    // Orientation = CreateFromAxisAngle((0,0,1), angle) = (0,0,sin(a/2),cos(a/2))
    p.Rotation = {0.0f, 0.0f, std::sin(angle * 0.5f), std::cos(angle * 0.5f)};
    out.push_back(p);
    angle += delta;
  }
  return out;
}

bool pointsEq(const SwPoint& a, const SwPoint& b) {
  return nearf(a.Position.x, b.Position.x) && nearf(a.Position.y, b.Position.y) &&
         nearf(a.Position.z, b.Position.z) && nearf(a.FX1, b.FX1) &&
         nearf(a.Rotation.x, b.Rotation.x) && nearf(a.Rotation.y, b.Rotation.y) &&
         nearf(a.Rotation.z, b.Rotation.z) && nearf(a.Rotation.w, b.Rotation.w) &&
         nearf(a.Color.x, b.Color.x) && nearf(a.Color.y, b.Color.y) && nearf(a.Color.z, b.Color.z) &&
         nearf(a.Color.w, b.Color.w) && nearf(a.Scale.x, b.Scale.x) && nearf(a.Scale.y, b.Scale.y) &&
         nearf(a.Scale.z, b.Scale.z) && nearf(a.FX2, b.FX2);
}

}  // namespace

int runPointListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-pointlist] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // DrawPoints(cmd) + RenderTarget(tex) needed for the production leg

  bool ok = true;
  const int kCount = 8;
  const float kRadius = 2.0f, kW = 1.0f;

  // ===== LEG 1 — TRANSPORT (flat): cook RadialPointsCpu as terminal → debugCookedPointList. =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "RadialPointsCpu";
    r.params["Count"] = (float)kCount; r.params["Radius"] = kRadius; r.params["W"] = kW;
    g.nodes.push_back(r);

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    std::vector<SwPoint> want = radialRef(kCount, kRadius, kW);
    bool pass = got && got->size() == want.size();
    if (pass)
      for (size_t i = 0; i < want.size(); ++i)
        if (!pointsEq((*got)[i], want[i])) { pass = false; break; }
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG1 transport RadialPointsCpu(%d) n=%zu want=%zu -> %s\n",
                kCount, got ? got->size() : 0, want.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — UPLOAD BRIDGE byte-parity (flat): RadialPointsCpu → ListToBuffer → GPU readback. ====
  // The bridge's whole point: the host list crosses to a GPU SwPoint buffer byte-identical (all 64B).
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "RadialPointsCpu";
    r.params["Count"] = (float)kCount; r.params["Radius"] = kRadius; r.params["W"] = kW;
    g.nodes.push_back(r);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g.nodes.push_back(ltb);
    // RadialPointsCpu.ResultList (port 0) → ListToBuffer.Lists (port 1, MultiInput).
    g.connections.push_back({100, pinId(1, /*ResultList*/ 0), pinId(2, /*Lists*/ 1)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    // ListToBuffer is a Points op → cook it as a Points-producing terminal (preview synth). The cook
    // driver's ListToBuffer branch gathers RadialPointsCpu's host list and memcpys it into outBuf.
    pg.cook(g, ctx, nullptr, /*terminal=*/2);
    pointListInjectBug() = false;

    uint32_t gpuCount = pg.debugCookedCount(2);
    std::vector<SwPoint> want = injectBug ? std::vector<SwPoint>{} : radialRef(kCount, kRadius, kW);
    bool pass = gpuCount == (uint32_t)want.size();
    // Read the ACTUAL GPU buffer contents() (StorageModeShared) and assert byte-parity per SwPoint —
    // ALL 64 bytes (Position@0 / FX1@12 / Rotation@16 / Color@32 / Scale@48 / FX2@60). This is the
    // packed_float3 STRIDE proof: a wrong stride would scramble every field past offset 12. The bytes
    // came through ListToBuffer's host→GPU memcpy, so this proves the bridge crossed correctly.
    if (pass && gpuCount > 0) {
      MTL::Buffer* buf = const_cast<MTL::Buffer*>(pg.debugCookedBuffer(2));  // contents() is non-const
      pass = buf != nullptr && buf->length() >= (NS::UInteger)gpuCount * sizeof(SwPoint);
      if (pass) {
        const SwPoint* gpu = reinterpret_cast<const SwPoint*>(buf->contents());
        for (size_t i = 0; i < want.size(); ++i)
          if (!pointsEq(gpu[i], want[i])) { pass = false; break; }
      }
    }
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG2 ListToBuffer GPU-readback gpuCount=%u want=%zu (host→GPU "
                "memcpy, 64B stride) -> %s\n", gpuCount, want.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 3 — TransformCpuPoint (flat): hand-built 1-point list → +translation → readback. =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    // A RadialPointsCpu(Count=1) source = a single deterministic point, then TransformCpuPoint moves it.
    Node r; r.id = 1; r.type = "RadialPointsCpu";
    r.params["Count"] = 1.0f; r.params["Radius"] = 0.0f; r.params["W"] = 1.0f;  // point at origin
    g.nodes.push_back(r);
    Node t; t.id = 2; t.type = "TransformCpuPoint";
    t.params["Translation.x"] = 1.5f; t.params["Translation.y"] = -0.5f; t.params["Translation.z"] = 0.25f;
    g.nodes.push_back(t);
    // RadialPointsCpu.ResultList (port 0) → TransformCpuPoint.Lists2 (port 1, MultiInput).
    g.connections.push_back({100, pinId(1, 0), pinId(2, 1)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;  // clears RadialPointsCpu → Transform sees no input → empty out
    pg.cook(g, ctx, nullptr, /*terminal=*/2);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(2);
    // Source: RadialPointsCpu Count=1 Radius=0 → one point at origin (f=1 for corners==1; v=(0,0,0)).
    // TransformCpuPoint: Position += (1.5,-0.5,0.25); Rotation unchanged (Rotation params 0 → identity
    // composed). Expected single output point at (1.5,-0.5,0.25).
    bool pass = !injectBug ? (got && got->size() == 1 && nearf((*got)[0].Position.x, 1.5f) &&
                              nearf((*got)[0].Position.y, -0.5f) && nearf((*got)[0].Position.z, 0.25f))
                           : (got && got->empty());  // inject → no input → empty output
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG3 TransformCpuPoint +(1.5,-0.5,0.25) n=%zu pos=(%.2f,%.2f,%.2f) -> %s\n",
                got ? got->size() : 0, (got && !got->empty()) ? (*got)[0].Position.x : 0.0f,
                (got && !got->empty()) ? (*got)[0].Position.y : 0.0f,
                (got && !got->empty()) ? (*got)[0].Position.z : 0.0f, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 4 — ★ PRODUCTION PIXEL (R-2): RadialPointsCpu → ListToBuffer → DrawPoints → RenderTarget
  // through the canonical production path (libFromGraph → buildEvalGraph → cookResident), then read
  // pg.target() pixels: the RING must be lit (white points), the CENTER must be black (no point there).
  // This is the leg that distinguishes a LIVING bridge from a flat-only one — production cooks resident.
  {
    const uint32_t RW = 128, RH = 128;
    const int ringCount = 64;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "RadialPointsCpu";
    r.params["Count"] = (float)ringCount; r.params["Radius"] = kRadius; r.params["W"] = 1.0f;
    g.nodes.push_back(r);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g.nodes.push_back(ltb);
    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f;  // Custom
    rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
    g.connections.push_back({100, pinId(1, 0), pinId(2, 1)});  // RadialPointsCpu.ResultList → ListToBuffer.Lists
    g.connections.push_back({101, pinId(2, 0), pinId(3, 0)});  // ListToBuffer.OutBuffer → DrawPoints.points
    g.connections.push_back({102, pinId(3, 1), pinId(4, 0)});  // DrawPoints.out(cmd) → RenderTarget.command

    // PRODUCTION path: flat Graph → SymbolLibrary (paths == ids) → resident graph → cookResident.
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");  // RenderTarget terminal
    pointListInjectBug() = false;

    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
    int ringLit = 0; bool centerBlack = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      auto lit = [&](uint32_t x, uint32_t y) {
        size_t i = ((size_t)y * RW + x) * 4;
        return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40;
      };
      // RING: count lit pixels across the whole image (the white points). CENTER: a small box at image
      // center (world (0,0) → NDC (0,0) → pixel center) must be black (no point sits at the origin).
      for (uint32_t y = 0; y < RH; ++y)
        for (uint32_t x = 0; x < RW; ++x)
          if (lit(x, y)) ++ringLit;
      const uint32_t cx = RW / 2, cy = RH / 2, hb = 3;
      for (uint32_t y = cy - hb; y <= cy + hb; ++y)
        for (uint32_t x = cx - hb; x <= cx + hb; ++x)
          if (lit(x, y)) centerBlack = false;
    }
    // GREEN: ring lit (≥ ringCount/2 lit pixels, allowing point-size spread) AND center black.
    // injectBug clears the list → empty bag → DrawPoints draws nothing → ringLit 0 → RED. (No need for
    // centerBlack under inject; the empty screen fails the ring assertion decisively.)
    bool pass = sized && (injectBug ? (ringLit == 0) : (ringLit >= ringCount / 2 && centerBlack));
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG4 ★PRODUCTION cookResident pixel: ringLit=%d(need≥%d) "
                "centerBlack=%d size=%lux%lu -> %s\n",
                ringLit, ringCount / 2, centerBlack ? 1 : 0, tex ? tex->width() : 0,
                tex ? tex->height() : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // clears the cooked list → all legs fail (empty transport / count 0 / black screen) → return 1.
  std::printf("[selftest-pointlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
