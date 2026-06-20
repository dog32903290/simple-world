// field_ops_repeatfieldlimit_golden — --selftest-field-repeatfieldlimit. GPU DISTANCE-VALUE golden for
// the RepeatFieldLimit single-input MODIFIER (PRE-wrap: emits preShaderCode BEFORE the child recursion,
// no post — same wrap half as Translate/RepeatAxis, field_graph.cpp:82-86). Builds
// RepeatFieldLimit(GoldenSphere), assembles via the FROZEN base, compiles, renders, reads back R32Float,
// asserts each probe RED == sphereDistance(foldedX(p.x), p.y) at the texel's p (z=0). Mirrors
// field_ops_translate_golden.cpp / field_ops_repeataxis_golden.cpp's harness; exercises the three packed
// scalars (Size/Start/Stop) read under the modifier prefix P.RepeatFieldLimit_<id>_{Size,Start,Stop}.
//
// ZONE: shell tier (app/src/ root) — crosses runtime + platform (see field_ops_translate_golden.cpp).
//
// CLOSED-FORM: child = GoldenSphere(center=0, r=0.4) -> d_child(q) = |q| - 0.4.
//   RepeatFieldLimit pre `p.x = pModLimited2(p.x, Size, Start, Stop);` -> child samples q.x = foldedX(p.x),
//   q.y = p.y. We mirror pModLimited2 (RepeatFieldLimit.cs:40-57) on the HOST so every expected value is
//   DERIVED from the .cs math, never an asserted constant (no tautology).
//
//   Config: Size=1, Start=0, Stop=1 (cells 0..1 kept; outside clamped). On y=0 the probes pick p.x so the
//   FOLD lands on a deterministic plateau in [-1,1]:
//     p.x=0.0  -> c=0 (kept) -> q.x=0.0   -> |0.0|-0.4 = -0.4
//     p.x=0.5  -> c=1 (kept) -> q.x=-0.5  -> |0.5|-0.4 = +0.1
//     p.x=-0.5 -> c=0 (kept) -> q.x=-0.5  -> |0.5|-0.4 = +0.1
//   (Start=Stop=0 would clamp everything to cell 0 — the spec's hand-derived case — but a Start/Stop swap
//   is then a no-op so the param-order tooth would be DEAD. Stop=1 keeps the exact same probe VALUES while
//   leaving the swap observable: see injectBug below.)
//
// PARAM-PREFIX (BLOOD LESSON): the emitted tokens P.RepeatFieldLimit_<id>_{Size,Start,Stop} MUST match
//   sw's frozen prefix convention ("<Type>_"+shortId+"_", accessed P.<prefix><Name>; backward-traced from
//   field_ops_combinesdf.cpp:288 / translate.cpp:46). A wrong prefix reads the wrong/0 struct member -> the
//   probes (which need the full Size/Start/Stop to fold/clamp) go RED. NOT forward-assumed.
//
// injectBug: configureRepeatFieldLimit(node, Size, Start, Stop, axis, injectBug>0) corrupts the OP'S REAL
//   preShaderCode emit:
//     1 = SWAP the Start/Stop args (param-ORDER tooth, mirroring the collectParams depth-first order) ->
//         the kept-cell range inverts (Start=1,Stop=0). p.x=0.0 then clamps to cell 1: q.x=-1.0 ->
//         |1.0|-0.4 = +0.6 != -0.4 -> RED; p.x=-0.5 clamps to q.x=-1.5 -> +1.1 != +0.1 -> RED.
//   The golden runs injectBug=1 (swap) under the --bug entry; the tooth bites the OP's actual emit, not the
//   template (no expected-value tautology — the host pModLimited2 is always evaluated with the CORRECT
//   Start/Stop order, so a swapped emit must diverge).
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

#include "runtime/field_graph.h"
#include "runtime/field_node_registry.h"
#include "runtime/tex_op_cache.h"

#include "platform/metal_compile.h"

namespace sw {

// Param-cook + test seam owned by field_ops_repeatfieldlimit.cpp (leaf type TU-private). Forward-declared.
void configureRepeatFieldLimit(FieldNode& node, float size, float start, float stop, int axis,
                               int injectBug);

namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kSphR = 0.4f;
constexpr float kSize = 1.0f, kStart = 0.0f, kStop = 1.0f;  // cells 0..1 kept.
constexpr int kAxisX = 0;                                   // fold the x axis (deterministic on y=0).

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

struct GoldenSphere : FieldNode {
  float cx, cy, cz, r;
  GoldenSphere(const std::string& id, float x, float y, float z, float radius)
      : cx(x), cy(y), cz(z), r(radius) {
    prefix = "GSphere_" + id + "_";
  }
  void preShaderCode(CodeAssembleCtx& c, int) const override {
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
                 "Radius;");
  }
  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    appendVec3Param(fp, pf, prefix + "Center", cx, cy, cz);
    appendScalarParam(fp, pf, prefix + "Radius", r);
  }
};

// Host mirror of pModLimited2 (RepeatFieldLimit.cs:40-57). The CORRECT Start/Stop order is ALWAYS used
// here, so a swapped OP emit (injectBug=1) must diverge from this — no tautology. `mod` matches the Common
// macro (x - y*floor(x/y)); for these positive inputs it equals fmod's positive branch.
float hostMod(float x, float y) { return x - y * std::floor(x / y); }
float pModLimited2(float p, float size, float start, float stop) {
  const float halfsize = size * 0.5f;
  float c = std::floor((p + halfsize) / size);
  p = hostMod(p + halfsize, size) - halfsize;
  if (c > stop) {
    p += size * (c - stop);
    c = stop;
  }
  if (c < start) {
    p += size * (c - start);
    c = start;
  }
  return p;  // .cs returns the cell index c; sw's fork returns the folded point (the consumed output).
}

// Host closed-form: child sphere (origin, r) sampled at the FOLDED point q = (foldedX(p.x), p.y, 0).
float repeatLimitField(float px, float py) {
  const float qx = pModLimited2(px, kSize, kStart, kStop);
  const float qy = py;  // qz = 0
  return std::sqrt(qx * qx + qy * qy) - kSphR;
}

std::shared_ptr<FieldNode> buildTree(int injectBug) {
  std::shared_ptr<FieldNode> mod = makeFieldNode("RepeatFieldLimit", "golden0");
  if (!mod) return nullptr;
  configureRepeatFieldLimit(*mod, kSize, kStart, kStop, kAxisX, injectBug);
  mod->inputs.push_back(std::make_shared<GoldenSphere>("a", 0.f, 0.f, 0.f, kSphR));
  return mod;
}

struct Probe { const char* name; uint32_t px, py; };

}  // namespace

int runFieldRepeatFieldLimitGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf(
        "[selftest-field-repeatfieldlimit] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-repeatfieldlimit] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();

  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  // injectBug=1 (swap Start/Stop) lives in the OP's REAL preShaderCode emit; production passes 0.
  const int bugMode = injectBug ? 1 : 0;
  std::shared_ptr<FieldNode> tree = buildTree(bugMode);
  if (!tree) {
    std::printf("[selftest-field-repeatfieldlimit] FAIL: RepeatFieldLimit factory not registered\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  clearTexOpCache();
  MTL::Texture* tex = renderField2d(dev, q, tree, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-repeatfieldlimit] FAIL: renderField2d null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);
  auto sampleAt = [&](uint32_t px, uint32_t py) { return buf[(size_t)py * kW + px]; };

  const float kTol = 1e-5f;
  int rc = 0;

  const uint32_t cy = (kH - 1) / 2;
  auto pxFor = [](float target) -> uint32_t {
    float f = ((target + 1.0f) * kW - 1.0f) * 0.5f;
    int px = (int)std::lround(f);
    if (px < 0) px = 0;
    if (px >= (int)kW) px = kW - 1;
    return (uint32_t)px;
  };

  // Probes: p.x chosen so foldedX(p.x) lands on a deterministic plateau (Size=1,Start=0,Stop=1):
  //   center p.x≈0.0  -> q.x=0.0  -> -0.4 (the param-read discriminator; swap reads q.x=-1.0 -> +0.6)
  //   right  p.x≈0.5  -> q.x=-0.5 -> +0.1 (cell 1, kept; swap also gives +0.1 -> NOT a swap discriminator)
  //   left   p.x≈-0.5 -> q.x=-0.5 -> +0.1 (cell 0, kept; swap clamps to q.x=-1.5 -> +1.1 -> RED)
  Probe probes[] = {
      {"center", pxFor(0.0f), cy},   // q.x=0.0  -> -0.4 (full Start/Stop read; swap reads +0.6)
      {"right", pxFor(0.5f), cy},    // q.x=-0.5 -> +0.1
      {"left", pxFor(-0.5f), cy},    // q.x=-0.5 -> +0.1 (swap -> +1.1)
  };
  for (const Probe& pr : probes) {
    float px = pX(pr.px), py = pY(pr.py);
    float expected = repeatLimitField(px, py);  // CORRECT fold (correct Start/Stop order — never swapped)
    float got = sampleAt(pr.px, pr.py);
    float diff = std::fabs(got - expected);
    bool ok = diff <= kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-repeatfieldlimit] probe %-8s p=(% .4f,% .4f) got=% .6f expected=% .6f "
                "diff=%.2e %s\n",
                pr.name, px, py, got, expected, diff, ok ? "OK" : "RED");
  }

  tex->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-repeatfieldlimit] FAIL: injectBug did not trip any probe (tooth has no "
                  "bite)\n");
      return 1;
    }
    std::printf("[selftest-field-repeatfieldlimit] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-repeatfieldlimit] PASS\n");
  return rc;
}

}  // namespace sw
