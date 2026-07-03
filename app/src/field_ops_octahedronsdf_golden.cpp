// field_ops_octahedronsdf_golden — --selftest-field-octahedronsdf. GPU DISTANCE-VALUE golden for the
// OctahedronSDF field leaf: assemble an OctahedronSDF field, runtime-compile it, render a fullscreen
// pass, read back the R32Float distance texture, and assert each probed texel's RED == the closed-form
// signed distance fsdOctahedron(p, 0, Size, EdgeRadius) at that texel's field-space p (z=0 plane).
//
// ZONE: shell tier (lives at app/src/ root like field_render_golden.cpp / selftests.cpp / main.cpp).
// This file deliberately crosses runtime (renderField2d, makeFieldNode) AND platform
// (compileLibraryFromSource) — a runtime-zone selftest may NOT include platform (check_arch:
// runtime ↛ platform), so this integration golden sits at the shell tier (the only place allowed to
// bind the two zones; selftests.cpp top comment: "may include any zone").
//
// PIXEL -> FIELD-SPACE p (must match field_render_template.metal; identical map to field_render_golden):
//   p.x = (2*px + 1)/W - 1 ;  p.y = 1 - (2*py + 1)/H ;  p.z = 0, p.w = 0.
//   Expected RED = fsdOctahedron(float3(p.x,p.y,0), center=0, Size, EdgeRadius).
//
// ORACLE PROVENANCE (P5 fix): the host mirror octaSdf() below is TRANSCRIBED from the TiXL source
// external/tixl Operators/Lib/field/generate/sdf/OctahedronSDF.cs:38-47 (the fsdOctahedron HLSL global
// the op registers; SHA 395c4c55), NOT from the sw leaf's emitted MSL — so a leaf-side formula drift
// cannot self-certify. On top of the transcription, each GPU probe is ALSO asserted against a
// hand-derived REGIME closed form (independent of the transcription), and the transcription itself is
// pinned to hand-computed constants at the canonical anchor positions:
//
// ANCHORS (Size=0.5, EdgeRadius=0.002, center=0 — hand-derived from OctahedronSDF.cs:41-46, ASSERTED
// below, not just noted):
//   p=(0.5,0,0): m=(0.5-0.5)/3=0 → k=0, clamp no-op → length(p-o)=0, sign(0)=0 → d = -ra      = -0.002
//   p=(0,0,0)  : m=-s/3 → o=(s/3,s/3,s/3), k=0, no clamp → p-o=(m,m,m) → d = -(s/3)√3 - ra
//                                                                          = -(1/6)√3 - 0.002 ≈ -0.290701
//   p=(1,0,0)  : o clamps onto the +x vertex (s,0,0) → d = (1-s) - ra                          =  0.498
// GPU PROBES land near those targets at exact texel centers; each is asserted against its regime
// closed form at that EXACT p (face regime: d = (|px|+|py|-s)/√3 - ra ; +x-vertex regime:
// d = ‖(|px|-s, |py|)‖ - ra — derivations at the probe loop).
// plus a boundary-SIGN probe: along the center row scanning +x, d must flip negative->positive (the
// octahedron has a real inside region at the center). Skipped under injectBug (the +1.0 shift removes
// the inside region entirely).
//
// injectBug: corrupt the template's RED-channel write so every cooked distance is shifted by +1.0
// (same magnitude / technique as field_render_golden) -> all probes go RED. Proves the tooth bites
// (it reads cooked pixels, not a blind pass).
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

#include "runtime/field_graph.h"          // setFieldSourceCompiler, FieldNode
#include "runtime/field_node_registry.h"  // makeFieldNode (OctahedronSDFNode is leaf-private)
#include "runtime/tex_op_cache.h"         // clearTexOpCache (fresh source-PSO cache per run-device)

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the source compiler)

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSize = 0.5f;        // OctahedronSDF.t3 default
constexpr float kEdgeRadius = 0.002f;  // OctahedronSDF.t3 default

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

// Field-space p at pixel (px,py) (see header derivation; identical to field_render_golden).
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// Host mirror of fsdOctahedron, TRANSCRIBED from TiXL OctahedronSDF.cs:38-47 (the HLSL global the op
// registers; SHA 395c4c55), NOT from the sw leaf's emitted MSL. Line map (center=0 fixed, .cs:39 p-=center
// is a no-op here):
//   .cs:40  p = abs(p)                          -> ax/ay/az
//   .cs:41  m = (p.x+p.y+p.z - s) / 3.0         -> m
//   .cs:42  o = p - m                           -> ox/oy/oz
//   .cs:43  k = min(o, 0.0)                     -> kx/ky/kz
//   .cs:44  o = o + (k.x+k.y+k.z)*0.5 - k*1.5   -> the ksum line
//   .cs:45  o = clamp(o, 0.0, s)                -> the fmin/fmax pair
//   .cs:46  return length(p-o)*sign(m) - ra     -> len * sgn - ra
float octaSdf(float px, float py, float pz, float s, float ra) {
  float ax = std::fabs(px), ay = std::fabs(py), az = std::fabs(pz);
  float m = (ax + ay + az - s) / 3.0f;
  float ox = ax - m, oy = ay - m, oz = az - m;
  float kx = std::fmin(ox, 0.0f), ky = std::fmin(oy, 0.0f), kz = std::fmin(oz, 0.0f);
  float ksum = kx + ky + kz;
  ox = ox + ksum * 0.5f - kx * 1.5f;
  oy = oy + ksum * 0.5f - ky * 1.5f;
  oz = oz + ksum * 0.5f - kz * 1.5f;
  ox = std::fmin(std::fmax(ox, 0.0f), s);
  oy = std::fmin(std::fmax(oy, 0.0f), s);
  oz = std::fmin(std::fmax(oz, 0.0f), s);
  float dx = ax - ox, dy = ay - oy, dz = az - oz;
  float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  float sgn = (m > 0.0f) ? 1.0f : (m < 0.0f ? -1.0f : 0.0f);
  return len * sgn - ra;
}

}  // namespace

int runFieldOctahedronSdfGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-octahedronsdf] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-octahedronsdf] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Wire the field source compiler (runtime->platform leaf seam) for this process — same lambda the
  // live app registers in main.cpp, so the source-PSO cache can compile the assembled MSL.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();  // a stale PSO built on a released device from a prior run must not be reused

  // OctahedronSDF leaf via the FieldOp factory (OctahedronSDFNode is leaf-private). The factory builds
  // it with the .t3 defaults Center=(0,0,0), Size=0.5, EdgeRadius=0.002 — which the probes assume.
  std::shared_ptr<FieldNode> octa = makeFieldNode("OctahedronSDF", "golden0");
  if (!octa) {
    std::printf("[selftest-field-octahedronsdf] FAIL: OctahedronSDF factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // injectBug at the MSL-string tier (the node is leaf-private): corrupt the template's RED-channel
  // write so every cooked distance is shifted by +1.0 -> all probes go RED. Same site/technique as
  // field_render_golden.cpp.
  std::string useTmpl = tmpl;
  if (injectBug) {
    const std::string from = "float4(f.w, 0.0, 0.0, 1.0)";
    const std::string to = "float4(f.w + 1.0, 0.0, 0.0, 1.0)";
    size_t pos = useTmpl.find(from);
    if (pos == std::string::npos) {
      std::printf("[selftest-field-octahedronsdf] FAIL: injectBug could not find the distance-write "
                  "site in the template (tooth cannot bite)\n");
      q->release(); dev->release(); pool->release();
      return 1;
    }
    useTmpl.replace(pos, from.size(), to);
  }

  MTL::Texture* tex = renderField2d(dev, q, octa, useTmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-octahedronsdf] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Read back the R32Float distance texture (4 bytes / texel = one float).
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  // GPU float distance tolerance (same as field_render_golden: single-precision length()/sqrt parity).
  const float kTol = 1e-5f;      // GPU vs the transcription (identical op sequence, float both sides)
  const float kTolClosed = 5e-5f;  // GPU vs the regime closed form (different op order → looser)
  int rc = 0;

  // ── HOST ANCHOR asserts (the old header-comment-only values, upgraded to real asserts): pin the
  // transcription itself to hand-derived constants at the canonical reference positions. Derivations in
  // the file header (from OctahedronSDF.cs:41-46). Pure host math — no GPU involved, so these hold in
  // both legs (the injectBug leg's bite comes from the GPU probes below).
  {
    const double kSq3 = std::sqrt(3.0);
    struct Anchor { const char* name; float px, py, pz; double expected; };
    const Anchor anchors[] = {
        {"anchor-boundary", 0.5f, 0.0f, 0.0f, -0.002},                       // sign(0)=0 → d=-ra
        {"anchor-inside",   0.0f, 0.0f, 0.0f, -(1.0 / 6.0) * kSq3 - 0.002},  // -(s/3)√3 - ra
        {"anchor-outside",  1.0f, 0.0f, 0.0f, 0.5 - 0.002},                  // (1-s) - ra
    };
    for (const Anchor& a : anchors) {
      float got = octaSdf(a.px, a.py, a.pz, kSize, kEdgeRadius);
      double diff = std::fabs((double)got - a.expected);
      bool ok = diff <= 2e-6;
      if (!ok) rc = 1;
      std::printf("[selftest-field-octahedronsdf] %-15s p=(%.1f,%.1f,%.1f) got=% .6f expected=% .6f %s\n",
                  a.name, a.px, a.py, a.pz, got, a.expected, ok ? "OK" : "RED");
    }
  }

  // DISTANCE GOLDEN at the three reference field positions (boundary / inside / outside). Probe pixels
  // chosen so each probed p lands near the anchor targets (0.5,0)/(0,0)/(1,0). Each probe is asserted
  // TWO independent ways at its EXACT texel p:
  //   (C) a hand-derived REGIME closed form (independent of the transcription — a transcription typo
  //       cannot self-certify):
  //       face regime  (inside probe: p_abs components small → m<0, k=0, clamp no-op, p_abs-o=(m,m,m)):
  //           d = length((m,m,m))*sign(m) - ra = m√3 - ra = (|px|+|py|-s)/√3 - ra
  //       +x-vertex regime (boundary/outside probes: hand-run of .cs:41-45 lands o EXACTLY on the +x
  //           vertex (s,0,0) — for boundary p=(0.5078125,0.0078125): m=0.005208¯3, o'=(0.5,0,0) before
  //           clamp already; for outside p=(0.9921875,0.0078125): clamp caps (0.6627..,-0.0833..,
  //           -0.0794..) to (0.5,0,0)):
  //           d = ‖p_abs - (s,0,0)‖ - ra = ‖(|px|-s, |py|)‖ - ra
  //   (T) the transcription octaSdf(p) (OctahedronSDF.cs:38-47) — cross-checked against (C) so the two
  //       oracles cannot drift apart silently.
  const uint32_t cy = (kH - 1) / 2;     // 63 -> p.y = 0.0078125 (≈ center row)
  const uint32_t cx = (kW - 1) / 2;     // 63 -> p.x = -0.0078125 (≈ center)
  // p.x ≈ 0.5 -> px = ((0.5+1)*W - 1)/2 = 95.5 -> px=96 gives p.x=0.5078125
  const uint32_t boundaryPx = 96;
  const uint32_t rightPx = kW - 1;      // 127 -> p.x = 0.9921875 (≈ 1.0, outside)
  struct Probe { const char* name; uint32_t px, py; bool vertexRegime; };
  Probe probes[] = {
      {"boundary", boundaryPx, cy, true},
      {"inside", cx, cy, false},
      {"outside", rightPx, cy, true},
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    double ax = std::fabs((double)px), ay = std::fabs((double)py);
    double expectedC = pr.vertexRegime
        ? std::sqrt((ax - (double)kSize) * (ax - (double)kSize) + ay * ay) - (double)kEdgeRadius
        : (ax + ay - (double)kSize) / std::sqrt(3.0) - (double)kEdgeRadius;
    float expectedT = octaSdf(px, py, 0.0f, kSize, kEdgeRadius);
    // Transcription vs closed form (host-only): the two oracles must agree.
    double diffTC = std::fabs((double)expectedT - expectedC);
    bool okTC = diffTC <= 2e-6;
    if (!okTC) rc = 1;
    // GPU vs the closed form (THE anchor assert) and vs the transcription (tightest).
    float got = sampleAt(pr.px, pr.py);
    double diffC = std::fabs((double)got - expectedC);
    float diffT = std::fabs(got - expectedT);
    bool okC = diffC <= kTolClosed;
    bool okT = diffT <= kTol;
    if (!okC || !okT) rc = 1;
    std::printf("[selftest-field-octahedronsdf] probe %-8s p=(% .4f,% .4f) got=% .6f closed=% .6f "
                "transcribed=% .6f dC=%.2e dT=%.2e dTC=%.2e %s\n",
                pr.name, px, py, got, expectedC, expectedT, diffC, (double)diffT, diffTC,
                (okC && okT && okTC) ? "OK" : "RED");
  }

  // BOUNDARY-SIGN tooth: along the center row, scanning +x, the distance must flip from negative
  //   (inside the octahedron) to positive (outside). Pins the texCoord->p map (a wrong scale/offset
  //   shifts the crossing). Skipped under injectBug (the +1.0 shift removes the inside region).
  if (!injectBug) {
    bool sawInside = false, sawOutside = false;
    for (uint32_t px = cx; px < kW; ++px) {
      float v = sampleAt(px, cy);
      if (v < 0.0f) sawInside = true;
      if (sawInside && v >= 0.0f) { sawOutside = true; break; }
    }
    bool ok = sawInside && sawOutside;
    if (!ok) rc = 1;
    std::printf("[selftest-field-octahedronsdf] boundary-sign center row: inside=%s outside=%s %s\n",
                sawInside ? "yes" : "no", sawOutside ? "yes" : "no", ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-octahedronsdf] FAIL: injectBug did not trip any probe (tooth has "
                  "no bite)\n");
      return 0;  // dead tooth -> exit 0 so --bite NO-BITE list catches it
    }
    std::printf("[selftest-field-octahedronsdf] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-octahedronsdf] PASS\n");
  return rc;
}

}  // namespace sw
