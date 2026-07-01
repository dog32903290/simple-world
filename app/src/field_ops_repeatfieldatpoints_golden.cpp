// field_ops_repeatfieldatpoints_golden — --selftest-field-repeatfieldatpoints. GPU DISTANCE-VALUE
// golden for the RepeatFieldAtPoints field leaf — the FIRST field op that BINDS A STRUCTURED BUFFER
// (the point-buffer→field "SRV seam": FieldNode::collectBuffers + the template's /*{STRUCT_DEFS}*/ /
// /*{BUFFERS}*/ / /*{BUFFER_PARAMS}*/ / /*{BUFFER_ARGS}*/ hooks + field_render's setFragmentBuffer
// loop). This is the ATOM_SEAM_MAP "唯一真硬 resource 接縫" — the point-buffer→field resource KIND
// that Image2dSDF's texture seam did NOT cover. It host-authors a small StructuredBuffer<PointTransform>
// (two points, each a pure world→object TRANSLATION), binds it through configureRepeatFieldAtPoints
// (standing in for the deferred Points port + the ComputePointTransformMatrix compute pass — the proven
// compute-stage keystone), assembles the field MSL via the FROZEN base, runtime-compiles it, renders a
// fullscreen pass, reads back the R32Float distance texture, and asserts each probed texel's RED == the
// closed-form union-of-repeated-spheres value. Mirrors field_ops_image2dsdf_golden.cpp's harness.
//
// ZONE: shell tier (app/src/ root like field_ops_image2dsdf_golden.cpp). It crosses runtime
// (renderField2d, makeFieldNode, configureRepeatFieldAtPoints) AND platform (compileLibraryFromSource)
// — a runtime-zone selftest may NOT include platform (check_arch), so this integration golden sits at
// the shell tier. It also names MTL directly to author the input point buffer.
//
// CLOSED FORM (deterministic, no reference frame needed — the seam is a resource-BIND + a codegen loop,
// both source-derivable):
//   The input field is a unit-ish sphere at origin: distSphere(q) = length(q) - r.
//   Each point i contributes WorldToPointObject_i = a pure translation-by(-T_i) matrix, so the loop
//   evaluates the sphere at (p - T_i) -> a sphere CENTERED AT T_i: length(p - T_i) - r.
//   CombineMethod = Union -> fLoop.w = min over i. Point colors are white (color multiply is a no-op;
//   the golden asserts .w only, so K/color do not enter the value).
//   => expected f.w(p) = min_i( length(p.xy - T_i.xy) - r )   (p.z = T_i.z = 0)
//
// PIXEL -> FIELD-SPACE p (identical to field_ops_image2dsdf_golden.cpp / the template):
//   p.x = (2*px+1)/W - 1 ; p.y = 1 - (2*py+1)/H ; p.z = 0 ; p.w = 0.
//
// MATRIX BYTE LAYOUT (the load-bearing SRV + orientation fork): the leaf emits MSL
//   `WorldToPointObject * float4(pStart,1)` (matrix × column-vector) to reproduce TiXL's HLSL
//   `mul(v, WorldToPointObject)` (see field_ops_transformfield.cpp fork (2)). An MSL `float4x4` STRUCT
//   MEMBER read from the device buffer is COLUMN-major, so a translation-by(-T) matrix is authored as
//   the 16 floats: col0=(1,0,0,0) col1=(0,1,0,0) col2=(0,0,1,0) col3=(-Tx,-Ty,-Tz,1). Then
//   M * float4(p,1) = (p.x-Tx, p.y-Ty, p.z-Tz, 1) -> the sphere lands at T. If the SRV failed to bind
//   or the matrix byte order were wrong, the spheres would land elsewhere and every probe would RED.
//
// injectBug: corrupt the template's RED-channel distance write so every cooked distance is shifted by
// +1.0 -> all VALUE probes RED (same technique/tier/magnitude as field_ops_image2dsdf_golden.cpp). This
// proves the value teeth bite cooked pixels. A SECOND failure mode — the buffer failing to bind / the
// buffer hook breaking -> the source fails to compile or the loop reads garbage -> renderField2d null or
// wrong values -> FAIL — is exercised implicitly (the fixed buffer is known-good, so a regression in the
// SRV bind/hook path turns the render null or moves the spheres).
#include "runtime/field_render.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"          // setFieldSourceCompiler, FieldNode, CodeAssembleCtx, appendVec3/Scalar
#include "runtime/field_node_registry.h"  // makeFieldNode (RepeatFieldAtPointsNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {

// Param-cook seam owned by field_ops_repeatfieldatpoints.cpp (the leaf type is TU-private). Forward-
// declared here (no header) exactly as selftests forward-declare the golden entry points. `pointBuffer`
// is an opaque MTL::Buffer* (passed as void* across the seam — runtime stays pure-compute).
void configureRepeatFieldAtPoints(FieldNode& node, const void* pointBuffer, uint32_t pointCount, float k,
                                  int combineMethod);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.2f;   // input-sphere radius (centered at origin before the per-point translate)
constexpr float kK = 1.0f;      // K (blend width) — does not affect the .w Union value the golden reads

// Two point translations (the WorldToPointObject moves the sphere to each T). Chosen symmetric so the
// per-probe closest point is unambiguous.
constexpr float kT0x = -0.5f, kT0y = 0.0f;
constexpr float kT1x = 0.5f, kT1y = 0.0f;

// Host mirror of the MSL float4x4 STRUCT MEMBER (column-major) for a translation-by(-Tx,-Ty,0):
//   col0=(1,0,0,0) col1=(0,1,0,0) col2=(0,0,1,0) col3=(-Tx,-Ty,0,1). See header MATRIX BYTE LAYOUT.
// PointTransform = { float4x4 WorldToPointObject; float4 PointColor; } — 16 + 4 = 20 floats, 80 bytes.
struct PointTransformHost {
  float m[16];
  float color[4];
};

void fillTranslate(PointTransformHost& pt, float tx, float ty) {
  // column-major identity
  for (int i = 0; i < 16; ++i) pt.m[i] = 0.0f;
  pt.m[0] = 1.0f; pt.m[5] = 1.0f; pt.m[10] = 1.0f; pt.m[15] = 1.0f;
  // translation in the LAST COLUMN (col3 = floats 12..15): (-tx,-ty,0,1)
  pt.m[12] = -tx; pt.m[13] = -ty; pt.m[14] = 0.0f;
  pt.color[0] = 1.0f; pt.color[1] = 1.0f; pt.color[2] = 1.0f; pt.color[3] = 1.0f;  // white (no-op)
}

std::string loadTemplate() {
#ifdef SW_FIELD_TEMPLATE
  std::ifstream f(SW_FIELD_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
#else
  return "";
#endif
}

float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Local SphereSDF child at origin (golden-only), radius r. The per-point translate moves it to T.
struct GoldenSphere : FieldNode {
  float r;
  explicit GoldenSphere(const std::string& id, float radius) : r(radius) { prefix = "GSphere_" + id + "_"; }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
                 "Radius;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendVec3Param(fp, pf, prefix + "Center", 0.f, 0.f, 0.f);
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

// Host closed form: Union (min) of the two repeated spheres.
float expectedField(float px, float py) {
  float d0 = std::sqrt((px - kT0x) * (px - kT0x) + (py - kT0y) * (py - kT0y)) - kSphR;
  float d1 = std::sqrt((px - kT1x) * (px - kT1x) + (py - kT1y) * (py - kT1y)) - kSphR;
  return std::fmin(d0, d1);
}

struct Probe { const char* name; uint32_t px, py; };

// Build a two-point PointTransform buffer (OWNED MTL::Buffer*, caller release()s).
MTL::Buffer* makePointBuffer(MTL::Device* dev) {
  PointTransformHost pts[2];
  fillTranslate(pts[0], kT0x, kT0y);
  fillTranslate(pts[1], kT1x, kT1y);
  return dev->newBuffer(pts, sizeof(pts), MTL::ResourceStorageModeShared);
}

}  // namespace

int runFieldRepeatFieldAtPointsGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-repeatfieldatpoints] FAIL: could not load field template "
                "(SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-repeatfieldatpoints] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-repeatfieldatpoints] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Buffer* pointBuf = makePointBuffer(dev);
  if (!pointBuf) {
    std::printf("[selftest-field-repeatfieldatpoints] FAIL: could not author point buffer\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build RepeatFieldAtPoints(GoldenSphere). The op is the REGISTERED leaf (custom-code loop path).
  std::shared_ptr<FieldNode> tree = makeFieldNode("RepeatFieldAtPoints", "golden0");
  if (!tree) {
    std::printf("[selftest-field-repeatfieldatpoints] FAIL: RepeatFieldAtPoints factory not registered\n");
    pointBuf->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  configureRepeatFieldAtPoints(*tree, pointBuf, /*pointCount=*/2, kK, /*combineMethod=*/0 /*Union*/);
  tree->inputs.push_back(std::make_shared<GoldenSphere>("a", kSphR));

  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, useTmpl, kW, kH);
  if (!tex) {
    // null = the SRV bind/hook path broke (source failed to compile / PSO failed) OR a real regression.
    // The fixed buffer is known-good, so a null here is a genuine FAIL (this IS the RED signal if the
    // point-buffer→field seam has a hole).
    std::printf("[selftest-field-repeatfieldatpoints] FAIL: renderField2d null (buffer seam/compile "
                "broke — point→field SRV hole)\n");
    pointBuf->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-4f;  // slightly looser than image2dsdf: float4x4 buffer read + sqrt accumulation
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;  // center row, p.y ≈ 0
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // Probes across the row: center of point0's sphere (inside -> ~-r), midpoint (outside both, symmetric),
  // center of point1's sphere (inside -> ~-r), far-left (outside, closest to point0), far-right (point1).
  Probe probes[] = {
      {"at_pt0", pxFor(kT0x), cy},   // p≈(-0.5,0): inside sphere0 -> -r
      {"midgap", pxFor(0.0f), cy},   // p≈(0,0): outside both, equidistant -> 0.5-r
      {"at_pt1", pxFor(kT1x), cy},   // p≈(0.5,0): inside sphere1 -> -r
      {"far_l", pxFor(-0.9f), cy},   // p≈(-0.9,0): closest to pt0 -> 0.4-r
      {"far_r", pxFor(0.9f), cy},    // p≈(0.9,0): closest to pt1 -> 0.4-r
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = expectedField(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-repeatfieldatpoints] probe %-8s p=(% .4f,% .4f) got=% .6f "
                "expected=% .6f diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  pointBuf->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-repeatfieldatpoints] FAIL: injectBug did not trip any probe (tooth "
                  "has no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-repeatfieldatpoints] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-repeatfieldatpoints] PASS\n");
  return rc;
}

}  // namespace sw
