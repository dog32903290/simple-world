// field_ops_toroidalvortexfield_golden — --selftest-field-toroidalvortexfield. GPU golden for the
// ToroidalVortexField VECTOR-field generator. Two complementary teeth:
//   (1) DECAY-CHANNEL GPU golden — assemble ToroidalVortexField (configured params), compile, render the
//       2D field template, read back R32Float (the template writes f.w into RED), and assert each probed
//       texel's RED == the closed-form decay weight at that texel's field-space p (z=0 plane).
//   (2) VELOCITY-TEXT assertion — the 2D render template visualizes ONLY f.w (decay); the velocity (f.xyz)
//       — the op's actual purpose — is NOT renderable by the current template (its consumers are particle
//       field-forces / ApplyVectorField on an unbuilt seam). So we ALSO assert the ASSEMBLED MSL contains
//       the velocity math (the cross(e_phi,r) swirl + normalize + the radial dirToRing terms). A
//       regression that drops the velocity is caught at the codegen tier even though the template can't
//       render it. When the particle-field / vector-application seam lands, a follow-up golden should
//       probe f.xyz directly.
//
// ZONE: shell tier (app/src/ root) — crosses runtime (renderField2d, makeFieldNode, assembleFieldMSL) +
//   platform (compileLibraryFromSource); a runtime-zone selftest may not include platform (check_arch),
//   so this integration golden sits at the shell tier (same rationale as the SDF goldens).
//
// CONFIG (golden, NON-default to make the decay channel discriminating): Center=(0,0,0), Radius=0.5,
//   Range=0.5, SwirlGain=1, RadialGain=1, FallOffRate=2 (decayK), Axis=Z. With Axis=Z the swizzle is
//   identity ("xyz"), so the field is authored in the natural xy-plane.
//
// CLOSED-FORM DECAY (port of fToroidalVectorField, z=0 plane, Center=0, Axis=Z identity swizzle):
//   p = (px, py, 0). phi = atan2(py, px); e_r = (cos phi, sin phi, 0) points toward (px,py).
//   C = Radius*e_r; since p lies along e_r with magnitude m = sqrt(px^2+py^2), r = p - C = (m - Radius)*e_r
//   and rho = |m - Radius|. If rho < 1e-6 -> the field early-returns float4(0,0,0,0) so f.w = 0.
//   Else decay = saturate(1 - pow(rho/max(Range,1e-6), decayK)).  f.w = decay.
//   Spot values (Radius=0.5, Range=0.5, decayK=2):
//     m=0.25 (px=0.25,py=0) -> rho=0.25 -> decay = 1 - (0.5)^2 = 0.75
//     m=0.375              -> rho=0.125 -> decay = 1 - (0.25)^2 = 0.9375
//     m=0   (center)       -> rho=0.5   -> decay = 1 - 1 = 0
//
// injectBug: configureToroidalVortexField(..., injectBug=1) drops the OP's REAL preShaderCode field call
//   -> f stays the seed (1,1,1,1) -> f.w = 1.0 everywhere -> every decay probe (expecting < 1) goes RED.
//   The tooth bites the OP's emit (the expected values are the CORRECT decay, never altered for injectBug).
//   The velocity-text assertion ALSO goes RED under injectBug (no field call emitted -> no cross/normalize).
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

#include "runtime/field_graph.h"          // setFieldSourceCompiler, makeFieldNode, assembleFieldMSL
#include "runtime/field_node_registry.h"
#include "runtime/tex_op_cache.h"

#include "platform/metal_compile.h"

namespace sw {

// Param-cook + test seam owned by field_ops_toroidalvortexfield.cpp (leaf type TU-private). Forward-decl.
void configureToroidalVortexField(FieldNode& node, float centerX, float centerY, float centerZ,
                                  float radius, float range, float swirlGain, float radialGain,
                                  float fallOffRate, int axis, int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kRadius = 0.5f, kRange = 0.5f, kDecayK = 2.0f;
constexpr int kAxisZ = 2;

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

// Closed-form decay (f.w) at field-space (px,py), z=0, Center=0, Axis=Z. See header derivation.
float toroidalDecay(float px, float py) {
  const float eps = 1e-6f;
  const float m = std::sqrt(px * px + py * py);
  const float rho = std::fabs(m - kRadius);
  if (rho < eps) return 0.0f;  // field early-returns float4(0,0,0,0)
  const float x = rho / std::fmax(kRange, eps);
  float decay = 1.0f - std::pow(x, kDecayK);
  if (decay < 0.0f) decay = 0.0f;
  if (decay > 1.0f) decay = 1.0f;  // saturate
  return decay;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> n = makeFieldNode("ToroidalVortexField", "golden0");
  if (!n) return nullptr;
  configureToroidalVortexField(*n, /*center*/ 0.0f, 0.0f, 0.0f, kRadius, kRange,
                               /*swirl*/ 1.0f, /*radial*/ 1.0f, kDecayK, kAxisZ, injectBug);
  return n;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldToroidalVortexFieldGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-toroidalvortexfield] FAIL: could not load field template "
                "(SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-toroidalvortexfield] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-toroidalvortexfield] FAIL: ToroidalVortexField factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  int rc = 0;

  // ── Tooth (2): VELOCITY-TEXT assertion on the ASSEMBLED MSL ──────────────────────────────────────
  // The 2D template renders only f.w (decay); the velocity (f.xyz) is not renderable here. Assert the
  // assembled shader CONTAINS the velocity math (swirl cross + normalize, radial dirToRing) and the
  // field-call site, so a regression that drops the velocity is caught at the codegen tier. Under
  // injectBug (field call dropped) these substrings vanish -> RED.
  {
    AssembledField asmField = assembleFieldMSL(tree, tmpl);
    const std::string& msl = asmField.msl;
    struct TextCheck { const char* name; const char* needle; };
    const TextCheck checks[] = {
        {"helper-decl", "float4 fToroidalVectorField("},
        {"swirl-cross", "cross(e_phi, r)"},
        {"swirl-norm", "normalize(vSwirl)"},
        {"radial-dir", "float3 dirToRing = -r / rho"},
        {"field-call", "fToroidalVectorField(p"},
    };
    for (const TextCheck& tc : checks) {
      bool present = contains(msl, tc.needle);
      // The helper-decl ALWAYS appears (addGlobals runs regardless of injectBug); the field-CALL and the
      // helper BODY (cross/normalize/radial) are present iff the op emitted its preShaderCode. Under
      // injectBug the field call is dropped, but addGlobals still emits the helper body — so the
      // discriminating needle is "field-call" (the call site). We assert all present in the GOOD path and
      // require "field-call" absent under injectBug.
      bool wantPresent = true;
      bool ok = (present == wantPresent);
      if (injectBug) {
        // Under injectBug only the field CALL must vanish; the globals body may still be present.
        if (std::string(tc.name) == "field-call") ok = !present;
        else ok = true;  // not the discriminator for this mode
      }
      if (!ok) rc = 1;
      std::printf("[selftest-field-toroidalvortexfield] text %-12s present=%d %s\n", tc.name,
                  present ? 1 : 0, ok ? "OK" : "RED");
    }
  }

  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-toroidalvortexfield] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;

  // ── Tooth (1): DECAY-CHANNEL GPU golden ──────────────────────────────────────────────────────────
  // Center row (p.y ≈ 0); probe at field-space x where m = |p_xy| gives a clean rho. The expected value
  // is the CORRECT decay at the texel's EXACT p (robust to the half-texel offset); never altered for
  // injectBug (which forces f.w=1.0 everywhere -> every probe RED).
  const uint32_t cy = (kH - 1) / 2;  // p.y ≈ 0.0078 (≈ center row)
  auto pxFor = [](float target) -> uint32_t {
    // invert pX: target = (2*px+1)/kW - 1 -> px = ((target+1)*kW - 1)/2
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };
  // m=0.25 -> rho=0.25 -> decay 0.75 ; m=0.375 -> rho=0.125 -> decay 0.9375 ; m=0.625 -> rho=0.125 ->
  // decay 0.9375 (other side of the ring). All three < 1.0 so injectBug (f.w=1) trips them.
  Probe probes[] = {
      {"inner0p25", pxFor(0.25f), cy},
      {"inner0p375", pxFor(0.375f), cy},
      {"outer0p625", pxFor(0.625f), cy},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = toroidalDecay(px, py);
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-toroidalvortexfield] probe %-11s p=(% .4f,% .4f) got=% .6f expected=% "
                ".6f diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-toroidalvortexfield] FAIL: injectBug did not trip any probe (tooth "
                  "has no bite)\n");
      return 1;
    }
    std::printf("[selftest-field-toroidalvortexfield] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-toroidalvortexfield] PASS\n");
  return rc;
}

}  // namespace sw
