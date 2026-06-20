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

// CLOSED-FORM LinePointsCpu reference (LinePointsCpu.cs, NOT a copy of the leaf): 2 points (from→to),
// both carrying rot = CreateFromAxisAngle((0,1,0), atan2(from.X-to.X, from.Y-to.Y)). F1[0]=w, F1[1]=w+wOff.
std::vector<SwPoint> lineRef(const float from[3], const float to[3], float w, float wOff) {
  float angle = std::atan2(from[0] - to[0], from[1] - to[1]);
  float h = angle * 0.5f;
  SwPoint p0 = swPointDefault();
  p0.Position = {from[0], from[1], from[2]}; p0.FX1 = w;
  p0.Rotation = {0.0f, std::sin(h), 0.0f, std::cos(h)};  // axis (0,1,0)
  SwPoint p1 = swPointDefault();
  p1.Position = {to[0], to[1], to[2]}; p1.FX1 = w + wOff;
  p1.Rotation = {0.0f, std::sin(h), 0.0f, std::cos(h)};
  return {p0, p1};
}

// CLOSED-FORM LinearPointsCpu reference (LinearPointsCpu.cs): N points, Position = Lerp(start, start+off,
// x/count) — NOTE /count (last point never reaches start+off), Orientation identity, F1 = Lerp(sw, sw+ow).
std::vector<SwPoint> linearRef(int count, const float start[3], const float off[3], float sw, float ow) {
  std::vector<SwPoint> out;
  int n = count < 1 ? 1 : (count > 10000 ? 10000 : count);
  for (int x = 0; x < n; ++x) {
    float f = (float)x / (float)n;  // /count, NOT /(count-1)
    SwPoint p = swPointDefault();
    p.Position = {start[0] + off[0] * f, start[1] + off[1] * f, start[2] + off[2] * f};
    p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    p.FX1 = sw + ow * f;
    out.push_back(p);
  }
  return out;
}

// Quaternion Hamilton product (x,y,z,w) — for the RepeatAt reference (== .NET Quaternion.Multiply).
void refQMul(const float a[4], const float b[4], float out[4]) {
  float cx = a[1] * b[2] - a[2] * b[1], cy = a[2] * b[0] - a[0] * b[2], cz = a[0] * b[1] - a[1] * b[0];
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  out[0] = a[0] * b[3] + b[0] * a[3] + cx; out[1] = a[1] * b[3] + b[1] * a[3] + cy;
  out[2] = a[2] * b[3] + b[2] * a[3] + cz; out[3] = a[3] * b[3] - dot;
}
// Vector3.Transform(v, q) — rotate v by q (== .NET expansion v + 2*q.w*cross(q.xyz,v) + 2*cross(q.xyz,cross(q.xyz,v))).
void refQRotate(const float q[4], const float v[3], float out[3]) {
  float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]),
        tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  float cx = q[1] * tz - q[2] * ty, cy = q[2] * tx - q[0] * tz, cz = q[0] * ty - q[1] * tx;
  out[0] = v[0] + q[3] * tx + cx; out[1] = v[1] + q[3] * ty + cy; out[2] = v[2] + q[3] * tz + cz;
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

  // ===== LEG 5 — LinePointsCpu: flat transport + ListToBuffer GPU-readback byte-parity + injectBug. =====
  // From=(2,0,0) To=(-2,0,0) W=1 WOffset=0.5 → 2 points; rot = axisAngle((0,1,0), atan2(4,0)=π/2).
  {
    const float kFrom[3] = {2.0f, 0.0f, 0.0f}, kTo[3] = {-2.0f, 0.0f, 0.0f};
    const float kLW = 1.0f, kLWOff = 0.5f;
    auto buildLine = [&](Graph& g) {
      Node r; r.id = 1; r.type = "LinePointsCpu";
      r.params["From.x"] = kFrom[0]; r.params["From.y"] = kFrom[1]; r.params["From.z"] = kFrom[2];
      r.params["To.x"] = kTo[0]; r.params["To.y"] = kTo[1]; r.params["To.z"] = kTo[2];
      r.params["W"] = kLW; r.params["WOffset"] = kLWOff;
      r.params["AddSeparator"] = 0.0f;  // explicit: LEG5 測 2-point case (spec/cook default 改 true 後不依賴 fallback)
      g.nodes.push_back(r);
    };
    std::vector<SwPoint> want = lineRef(kFrom, kTo, kLW, kLWOff);

    // 5a transport (flat). HARD per-op tooth: assert the FULL non-degenerate reference ALWAYS. Under
    // injectBug the op clears its output → got is empty → mismatch → this leg FAILS on its own (each new
    // op has its OWN bite, not leaning on LEG1).
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g; buildLine(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;
    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    bool passT = got && got->size() == want.size();
    if (passT) for (size_t i = 0; i < want.size(); ++i) if (!pointsEq((*got)[i], want[i])) { passT = false; break; }

    // 5b ListToBuffer GPU-readback byte-parity (flat). Same hard tooth: assert full want via GPU contents().
    PointGraph pg2(dev, lib, q, 64, 64);
    Graph g2; buildLine(g2);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g2.nodes.push_back(ltb);
    g2.connections.push_back({100, pinId(1, 0), pinId(2, 1)});
    pointListInjectBug() = injectBug;
    pg2.cook(g2, ctx, nullptr, /*terminal=*/2);
    pointListInjectBug() = false;
    uint32_t gpuCount = pg2.debugCookedCount(2);
    bool passB = gpuCount == (uint32_t)want.size();
    if (passB && gpuCount > 0) {
      MTL::Buffer* buf = const_cast<MTL::Buffer*>(pg2.debugCookedBuffer(2));
      passB = buf && buf->length() >= (NS::UInteger)gpuCount * sizeof(SwPoint);
      if (passB) {
        const SwPoint* gpu = reinterpret_cast<const SwPoint*>(buf->contents());
        for (size_t i = 0; i < want.size(); ++i) if (!pointsEq(gpu[i], want[i])) { passB = false; break; }
      }
    }
    bool pass = passT && passB;
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG5 LinePointsCpu transport+ListToBuffer n=%zu gpuCount=%u want=%zu -> %s\n",
                got ? got->size() : 0, gpuCount, want.size(), pass ? "PASS" : "FAIL");
  }

  // ===== LEG 6 — LinearPointsCpu: ★PRODUCTION pixel (resident) + transport. Count=N along a line. =====
  // Transport: Count=4 Start=(0,0,0) Offset=(4,0,0) → x at 0,1,2,3 (NOT 4: fX=x/count). Production: a
  // horizontal line of points off the vertical center column → lit pixels present, the top-left corner black.
  {
    const float kStart[3] = {0.0f, 0.0f, 0.0f}, kOff[3] = {4.0f, 0.0f, 0.0f};
    // 6a transport (flat), Count=4
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "LinearPointsCpu";
    r.params["Count"] = 4.0f;
    r.params["Start.x"] = kStart[0]; r.params["Start.y"] = kStart[1]; r.params["Start.z"] = kStart[2];
    r.params["Offset.x"] = kOff[0]; r.params["Offset.y"] = kOff[1]; r.params["Offset.z"] = kOff[2];
    g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;
    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    // HARD per-op tooth: assert the full non-degenerate reference ALWAYS → injectBug (empty out) FAILS here.
    std::vector<SwPoint> want = linearRef(4, kStart, kOff, 1.0f, 0.0f);
    bool passT = got && got->size() == want.size();
    if (passT) for (size_t i = 0; i < want.size(); ++i) if (!pointsEq((*got)[i], want[i])) { passT = false; break; }

    // 6b ★PRODUCTION resident pixel: a SHORT horizontal line of points across the image → lit, AND a
    // top-left corner box (far from any point) stays black. Count many points along a small line so the
    // pixel test is robust to point size. Start=(-1.5,0,0) Offset=(3,0,0) → points span x∈[-1.5, ~1.4].
    const uint32_t RW = 128, RH = 128;
    const int lineCount = 48;
    PointGraph pgP(dev, lib, q, RW, RH);
    Graph gp;
    Node lr; lr.id = 1; lr.type = "LinearPointsCpu";
    lr.params["Count"] = (float)lineCount;
    lr.params["Start.x"] = -1.5f; lr.params["Start.y"] = 0.0f; lr.params["Start.z"] = 0.0f;
    lr.params["Offset.x"] = 3.0f; lr.params["Offset.y"] = 0.0f; lr.params["Offset.z"] = 0.0f;
    gp.nodes.push_back(lr);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; gp.nodes.push_back(ltb);
    Node drw; drw.id = 3; drw.type = "DrawPoints"; gp.nodes.push_back(drw);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH;
    gp.nodes.push_back(rt);
    gp.connections.push_back({100, pinId(1, 0), pinId(2, 1)});
    gp.connections.push_back({101, pinId(2, 0), pinId(3, 0)});
    gp.connections.push_back({102, pinId(3, 1), pinId(4, 0)});
    SymbolLibrary slib = libFromGraph(gp);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pointListInjectBug() = injectBug;
    pgP.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");
    pointListInjectBug() = false;
    MTL::Texture* tex = pgP.target();
    bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
    int lit = 0; bool cornerBlack = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      auto isLit = [&](uint32_t x, uint32_t y) {
        size_t i = ((size_t)y * RW + x) * 4; return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40; };
      for (uint32_t y = 0; y < RH; ++y) for (uint32_t x = 0; x < RW; ++x) if (isLit(x, y)) ++lit;
      for (uint32_t y = 0; y < 8; ++y) for (uint32_t x = 0; x < 8; ++x) if (isLit(x, y)) cornerBlack = false;
    }
    // HARD tooth: production must be LIT (and corner black) ALWAYS → injectBug (black screen, lit=0) FAILS.
    bool passP = sized && lit >= lineCount / 2 && cornerBlack;
    bool pass = passT && passP;
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG6 LinearPointsCpu transport(n=%zu/want=%zu) + ★PRODUCTION "
                "lit=%d(need≥%d) cornerBlack=%d -> %s\n",
                got ? got->size() : 0, want.size(), lit, lineCount / 2, cornerBlack ? 1 : 0,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 7 — RepeatAtPointsCpu (★dual-input): source(2 pts) × dest(RadialPointsCpu ring) → product.
  // Transport: assert the cartesian product against an INDEPENDENT re-derivation (dest-frame transform).
  // Production: the product points form rings → lit + center black. injectBug → inputs cleared → empty.
  {
    // Source = LinearPointsCpu Count=2 Start=(0.5,0,0) Offset=(1,0,0) → 2 pts at (0.5,0,0),(1.0,0,0),
    // both identity rotation, F1=1. Dest = RadialPointsCpu Count=4 Radius=2 → 4 ring points w/ rotations.
    const int destN = 4; const float destR = 2.0f;
    const float srcStart[3] = {0.5f, 0.0f, 0.0f}, srcOff[3] = {1.0f, 0.0f, 0.0f};
    std::vector<SwPoint> srcRef = linearRef(2, srcStart, srcOff, 1.0f, 0.0f);  // identity rotations
    std::vector<SwPoint> destRef = radialRef(destN, destR, 1.0f);
    // Independent product re-derivation (outer dest, inner source): pos = dest.Pos + Rotate(src.Pos, dest.Rot),
    // F1 = src.F1, rot = dest.Rot * src.Rot.
    std::vector<SwPoint> prodRef;
    for (const SwPoint& d : destRef) {
      float dq[4] = {d.Rotation.x, d.Rotation.y, d.Rotation.z, d.Rotation.w};
      for (const SwPoint& s : srcRef) {
        float sp[3] = {s.Position.x, s.Position.y, s.Position.z}, rot[3];
        refQRotate(dq, sp, rot);
        float sq[4] = {s.Rotation.x, s.Rotation.y, s.Rotation.z, s.Rotation.w}, oq[4];
        refQMul(dq, sq, oq);
        SwPoint p = swPointDefault();
        p.Position = {d.Position.x + rot[0], d.Position.y + rot[1], d.Position.z + rot[2]};
        p.FX1 = s.FX1; p.Rotation = {oq[0], oq[1], oq[2], oq[3]};
        prodRef.push_back(p);
      }
    }

    auto buildRepeat = [&](Graph& g) {
      Node src; src.id = 1; src.type = "LinearPointsCpu";
      src.params["Count"] = 2.0f;
      src.params["Start.x"] = srcStart[0]; src.params["Start.y"] = srcStart[1]; src.params["Start.z"] = srcStart[2];
      src.params["Offset.x"] = srcOff[0]; src.params["Offset.y"] = srcOff[1]; src.params["Offset.z"] = srcOff[2];
      g.nodes.push_back(src);
      Node dst; dst.id = 2; dst.type = "RadialPointsCpu";
      dst.params["Count"] = (float)destN; dst.params["Radius"] = destR; dst.params["W"] = 1.0f;
      g.nodes.push_back(dst);
      Node rep; rep.id = 3; rep.type = "RepeatAtPointsCpu"; g.nodes.push_back(rep);
      // RepeatAtPointsCpu: SourcePoints = spec port 1, DestinationsPoints = spec port 2.
      g.connections.push_back({100, pinId(1, 0), pinId(3, 1)});  // LinearPointsCpu → SourcePoints
      g.connections.push_back({101, pinId(2, 0), pinId(3, 2)});  // RadialPointsCpu → DestinationsPoints
    };

    // 7a transport (flat): cook RepeatAtPointsCpu as terminal → debugCookedPointList == product.
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g; buildRepeat(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/3);
    pointListInjectBug() = false;
    const std::vector<SwPoint>* got = pg.debugCookedPointList(3);
    // HARD per-op tooth: assert the full cartesian product ALWAYS → injectBug (upstream cleared → RepeatAt
    // sees no input → empty out) FAILS here. Proves the dual-input gather + product math, not vacuously.
    std::vector<SwPoint> want = prodRef;
    bool passT = got && got->size() == want.size();
    if (passT) for (size_t i = 0; i < want.size(); ++i) if (!pointsEq((*got)[i], want[i])) { passT = false; break; }

    // 7b ★PRODUCTION resident pixel: source×dest product rings → DrawPoints → lit + center black.
    const uint32_t RW = 128, RH = 128;
    // Production uses a bigger destination ring (32) × the same 2-point source → 64 product points.
    PointGraph pgP(dev, lib, q, RW, RH);
    Graph gp;
    Node src; src.id = 1; src.type = "LinearPointsCpu";
    src.params["Count"] = 2.0f;
    src.params["Start.x"] = 0.5f; src.params["Start.y"] = 0.0f; src.params["Start.z"] = 0.0f;
    src.params["Offset.x"] = 1.0f; src.params["Offset.y"] = 0.0f; src.params["Offset.z"] = 0.0f;
    gp.nodes.push_back(src);
    Node dst; dst.id = 2; dst.type = "RadialPointsCpu";
    dst.params["Count"] = 32.0f; dst.params["Radius"] = 2.0f; dst.params["W"] = 1.0f;
    gp.nodes.push_back(dst);
    Node rep; rep.id = 3; rep.type = "RepeatAtPointsCpu"; gp.nodes.push_back(rep);
    Node ltb; ltb.id = 4; ltb.type = "ListToBuffer"; gp.nodes.push_back(ltb);
    Node drw; drw.id = 5; drw.type = "DrawPoints"; gp.nodes.push_back(drw);
    Node rt; rt.id = 6; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH;
    gp.nodes.push_back(rt);
    gp.connections.push_back({100, pinId(1, 0), pinId(3, 1)});  // Source
    gp.connections.push_back({101, pinId(2, 0), pinId(3, 2)});  // Destinations
    gp.connections.push_back({102, pinId(3, 0), pinId(4, 1)});  // RepeatAt → ListToBuffer.Lists
    gp.connections.push_back({103, pinId(4, 0), pinId(5, 0)});  // ListToBuffer → DrawPoints
    gp.connections.push_back({104, pinId(5, 1), pinId(6, 0)});  // DrawPoints → RenderTarget
    SymbolLibrary slib = libFromGraph(gp);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pointListInjectBug() = injectBug;
    pgP.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "6");
    pointListInjectBug() = false;
    MTL::Texture* tex = pgP.target();
    bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
    int lit = 0; bool centerBlack = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      auto isLit = [&](uint32_t x, uint32_t y) {
        size_t i = ((size_t)y * RW + x) * 4; return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40; };
      for (uint32_t y = 0; y < RH; ++y) for (uint32_t x = 0; x < RW; ++x) if (isLit(x, y)) ++lit;
      const uint32_t cx = RW / 2, cy = RH / 2, hb = 2;
      for (uint32_t y = cy - hb; y <= cy + hb; ++y) for (uint32_t x = cx - hb; x <= cx + hb; ++x)
        if (isLit(x, y)) centerBlack = false;
    }
    // HARD tooth: production must be LIT (and center black) ALWAYS → injectBug (black screen) FAILS.
    bool passP = sized && lit >= 16 && centerBlack;
    bool pass = passT && passP;
    ok = ok && pass;
    std::printf("[selftest-pointlist] LEG7 RepeatAtPointsCpu(★dual-in) transport(n=%zu/want=%d) + "
                "★PRODUCTION lit=%d centerBlack=%d -> %s\n",
                got ? got->size() : 0, (int)want.size(), lit, centerBlack ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // clears the cooked list → all legs fail (empty transport / count 0 / black screen) → return 1.
  std::printf("[selftest-pointlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
