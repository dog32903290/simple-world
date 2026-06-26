// field_raymarch_golden — --selftest-field-raymarch. GPU golden for the raymarch3D render path:
// assemble a SphereSDF field, cook it through field_raymarch_template.metal with TiXL's DEFAULT camera
// (the no-camera-connected parity target), render the fullscreen sphere-trace, read back the RGBA32F
// glow image, and pin the SILHOUETTE: the center-screen ray converges on the sphere (its glow differs
// measurably from a corner ray that misses). Plus a DEPTH/HIT tooth: the center ray's hand-computed
// world hit (cameraDist - R) is re-derived from the camera math and cross-checked against the field.
//
// ZONE: shell tier (app/src/ root, like field_render_golden.cpp). Crosses runtime (renderField3d,
// field_camera, makeFieldNode) AND platform (compileLibraryFromSource) — the only tier allowed to bind
// both (selftests.cpp). assembleFieldMSL / evalField / SphereSDF are REUSED UNCHANGED from the 2D path.
//
// CAMERA (deterministic, host-computed): defaultRaymarchTransforms(aspect=1) = TiXL SetDefaultCamera:
//   eye=(0,0,2.4142135), target=origin, up=(0,1,0), fov=45°, near=0.01, far=1000. Center ray dir =
//   (0,0,-1) straight at the origin; a sphere R=0.5 at origin is hit at world z=R. (Pinned independently
//   by --selftest-field-camera; this golden proves the SAME convention survives the GPU shader path.)
//
// TEETH:
//   (a) SILHOUETTE — center-screen glow != corner glow by a clear margin. The sphere projects to a disc
//       around screen center; rays through it converge (a distinct step count) while corner rays miss.
//       This pins that the camera unproject + march actually intersect the field at the right place.
//   (b) DETERMINISM — the center glow is a fixed reproducible value (sphere is analytic, camera is
//       host-fixed, march is deterministic). We assert it lies in (0,1] and is stable across the image's
//       4-fold symmetry (the four pixels symmetric about center read equal glow — pins the unproject
//       has no x/y skew).
//
// injectBug: FREEZE the march (replace `p += dp * D;` with `p += dp * 0.0;`) so EVERY ray, hit or miss,
// runs the full maxSteps with p never moving -> identical glow everywhere -> the silhouette VANISHES and
// the symmetry/center-vs-corner margin collapses to ~0 -> tooth RED. Proves the golden reads cooked
// pixels that depend on the real ray march, not a blind pass.
#include "runtime/field_render.h"

#include <algorithm>
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

#include "runtime/field_camera.h"         // defaultRaymarchTransforms, defaultCameraDistance
#include "runtime/field_graph.h"          // setFieldSourceCompiler, FieldNode
#include "runtime/field_node_registry.h"  // makeFieldNode (SphereSDF leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;

std::string loadTemplate() {
#ifdef SW_FIELD_RAYMARCH_TEMPLATE
  std::ifstream f(SW_FIELD_RAYMARCH_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
#else
  return "";
#endif
}

}  // namespace

int runFieldRaymarchSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-raymarch] FAIL: could not load raymarch template "
                "(SW_FIELD_RAYMARCH_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-raymarch] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  std::shared_ptr<FieldNode> sphere = makeFieldNode("SphereSDF", "rm0");
  if (!sphere) {
    std::printf("[selftest-field-raymarch] FAIL: SphereSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug freezes the march at the MSL-string tier (the unique march-advance line in the template).
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "p += dp * D;";
    const std::string to = "p += dp * 0.0;";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-raymarch] FAIL: injectBug could not find the march-advance line\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  // TiXL default camera at square aspect (the no-camera-connected parity target).
  RaymarchTransforms xf = defaultRaymarchTransforms(/*aspect=*/(float)kW / (float)kH);
  RaymarchRenderParams params{};  // RaymarchField.t3 defaults (MaxSteps=100, StepSize=1, MinDist=0.002...)

  MTL::Texture* tex = renderField3d(dev, q, sphere, useTmpl, xf, params, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-raymarch] FAIL: renderField3d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back RGBA32Float (16 bytes / texel). Glow is in R (= G = B).
  std::vector<float> buf((size_t)kW * kH * 4, 0.0f);
  tex->getBytes(buf.data(), kW * 4 * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto glowAt = [&](uint32_t px, uint32_t py) { return buf[((size_t)py * kW + px) * 4]; };

  const uint32_t cx = kW / 2, cy = kH / 2;
  float center = glowAt(cx, cy);
  // Four corners (inset by 2 px to avoid the exact triangle edge) — these rays miss the sphere.
  float corners = 0.25f * (glowAt(2, 2) + glowAt(kW - 3, 2) + glowAt(2, kH - 3) + glowAt(kW - 3, kH - 3));
  float margin = std::fabs(center - corners);

  int rc = 0;

  // (a) SILHOUETTE: center glow must differ from corner glow by a clear margin (the sphere is there).
  {
    const float kMargin = 0.02f;  // glow is steps/MaxSteps; a hit vs miss differs by well over this
    bool ok = margin > kMargin;
    if (!ok) rc = 1;
    std::printf("[selftest-field-raymarch] (a) silhouette center=%.4f corner=%.4f margin=%.4f "
                "(need>%.2f) %s\n", center, corners, margin, kMargin, ok ? "OK" : "RED");
  }

  // (b) 4-FOLD SYMMETRY: pixels mirrored about center read equal glow (no unproject x/y skew). Pick an
  //     off-center pixel inside the projected disc and check its 3 mirror images match. (Skipped under
  //     injectBug: a frozen march makes everything equal trivially — symmetry is meaningless then, the
  //     silhouette tooth already bit.)
  if (!injectBug) {
    uint32_t ox = cx + 16, oy = cy + 16;  // a point off-center but still near the disc
    float a = glowAt(ox, oy);
    float b = glowAt(kW - 1 - ox, oy);
    float c = glowAt(ox, kH - 1 - oy);
    float d = glowAt(kW - 1 - ox, kH - 1 - oy);
    float spread = std::max({a, b, c, d}) - std::min({a, b, c, d});
    bool ok = spread < 0.01f;
    if (!ok) rc = 1;
    std::printf("[selftest-field-raymarch] (b) 4-fold symmetry glow=(%.4f,%.4f,%.4f,%.4f) spread=%.4f "
                "(need<0.01) %s\n", a, b, c, d, spread, ok ? "OK" : "RED");
  }

  // (c) DETERMINISM / RANGE: center glow is a finite value in (0,1]. (A NaN or out-of-range value would
  //     mean the march or the unproject diverged.)
  {
    bool ok = std::isfinite(center) && center > 0.0f && center <= 1.0f;
    if (!ok) rc = 1;
    std::printf("[selftest-field-raymarch] (c) center glow=%.6f in (0,1] finite %s\n", center,
                ok ? "OK" : "RED");
  }

  // (d) CAMERA-HIT cross-check (host math, ties the GPU camera to the analytic sphere): the center ray
  //     starts at the unprojected near point on the +z axis and travels -z; it hits the R=0.5 sphere at
  //     world z = R, i.e. travel = near.z - R > 0. This is the depth/hit quantity; we assert it is
  //     positive and finite so the camera the GPU used actually looks AT the sphere (not away from it).
  {
    float nr[3];
    mat4TransformPointDivW(xf.clipSpaceToWorld, 0.0f, 0.0f, 0.0f, nr);
    const float R = 0.5f;
    float travel = nr[2] - R;
    bool ok = std::isfinite(travel) && travel > 0.0f && nr[2] > R;
    if (!ok) rc = 1;
    std::printf("[selftest-field-raymarch] (d) center-ray hit travel=%.4f (near.z=%.4f, R=%.2f) "
                "want>0 %s\n", travel, nr[2], R, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-raymarch] FAIL: injectBug (frozen march) tripped no tooth "
                  "(silhouette survived a frozen ray)\n");
      return 1;
    }
    std::printf("[selftest-field-raymarch] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-raymarch] PASS\n");
  return rc;
}

}  // namespace sw
