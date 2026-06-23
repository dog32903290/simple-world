// field_paramapply_golden — --selftest-field-paramapply. The PF-0c acceptance tooth: proves the
// data-driven field param-apply (fieldConfigurers() table + per-op setter-lambda slot tables) projects a
// NON-DEFAULT graph param all the way to the GPU, via the REAL production path
//   Graph + FieldParamResolver -> buildFieldTree -> configureFieldNodeFromParams (table lookup) -> members.
//
// Before PF-0c, only ToroidalVortexField had a configurer (the one-line if-ladder); SphereSDF's resolved
// {Radius:0.8} never reached the node, so the field rendered the .t3 default R=0.5. This golden FAILS in
// that world (the GPU reads R=0.5) and PASSES once SphereSDF's configurer is registered.
//
// CLOSED-FORM ANCHOR (SphereSDF f.w = length(p - Center) - Radius, Center=0):
//   default  R=0.5 at p=(0.3,0,0) -> f.w = 0.3 - 0.5 = -0.2
//   wired    R=0.8 at p=(0.3,0,0) -> f.w = 0.3 - 0.8 = -0.5
// The golden samples the texel whose field-space p is closest to (0.3,0), reads each texel's EXACT p (the
// half-texel offset is real), and asserts f.w == |p.xy| - 0.8  AND  f.w != |p.xy| - 0.5 (margin > tol).
//
// THREE SUB-CHECKS (all must pass for green):
//   (1) GPU readback — wired R=0.8 reaches the rendered field (closed-form, != default).
//   (2) BUFFER-LEVEL — assembleFieldMSL(tree).floatParams[Radius slot==3] == 0.8 when the graph supplied
//       0.8, and == 0.5 when it did not (the byte-identical no-graph-param baseline).
//   (3) ENUM (CombineSDF) — a non-default CombineMethod switches the assembled MSL TEXT to the matching
//       mode helper (selectors change codegen text, not the float buffer).
//   (4) SLOT-ID == PORT-ID guard — every slot id in each migrated op's apply table exists as a PortSpec.id
//       in that op's NodeSpec (loop fieldSpecSink()). A drift here = a silent default regression.
//
// injectBug: assert the DEFAULT R=0.5 closed form while the graph supplied 0.8 -> the GPU read (R=0.8) no
// longer matches -> RED. Proves the apply actually happened (a no-op apply would read R=0.5 and the -bug
// assertion would wrongly pass).
//
// ZONE: shell tier (app/src/ root, like field_render_golden.cpp). Crosses runtime (buildFieldTree,
// assembleFieldMSL, renderField2d, fieldSpecSink) AND platform (compileLibraryFromSource) — the only tier
// allowed to bind both zones (check_arch: runtime ↛ platform).
#include "runtime/field_render.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"          // assembleFieldMSL, AssembledField, setFieldSourceCompiler
#include "runtime/field_graph_builder.h"  // buildFieldTree, FieldParamResolver
#include "runtime/field_node_registry.h"  // fieldSpecSink, makeFieldNode
#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / resolveNodeParams
#include "runtime/tex_op_cache.h"         // clearTexOpCache
#include "runtime/tixl_point.h"           // EvaluationContext

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource

namespace sw {
namespace {

constexpr uint32_t kW = 128, kH = 128;
constexpr float kWiredRadius = 0.8f;    // != SphereSDF .t3 default 0.5 -> bites the apply.
constexpr float kDefaultRadius = 0.5f;  // SphereSDF.t3.
constexpr int kRadiusSlot = 3;          // collectParams lays Center[0..2] then Radius[3].

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

// Field-space p at pixel (px,py) — identical mapping to field_render_template.metal / field_render_golden.
float pX(uint32_t px) { return (2.0f * px + 1.0f) / kW - 1.0f; }
float pY(uint32_t py) { return 1.0f - (2.0f * py + 1.0f) / kH; }

// REAL production path: a 1-node Graph holding SphereSDF with a stored Radius override, built via
// buildFieldTree (which calls configureFieldNodeFromParams through the fieldConfigurers() table). The
// resolver IS the cook driver's nodeParams shape (resolveNodeParams -> the value spine). injectRadius
// lets the caller choose what the resolver returns (0.8 = wired, omitted to test the byte-identical
// no-graph-param baseline).
std::shared_ptr<FieldNode> buildSphere(float radius, bool supplyRadius) {
  static Graph g;  // outlives the resolver's returned map references for the duration of the call.
  g = Graph{};
  Node fld; fld.id = 7; fld.type = "SphereSDF";
  if (supplyRadius) fld.params["Radius"] = radius;
  g.nodes = {fld};
  static EvaluationContext ctx;
  ctx = EvaluationContext{};
  static std::map<int, std::map<std::string, float>> scratch;
  scratch.clear();
  FieldParamResolver params = [&](int id) -> const std::map<std::string, float>* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    return &(scratch[id] = resolveNodeParams(g, *n, ctx, nullptr));
  };
  return buildFieldTree(g, 7, params);
}

// Build a CombineSDF tree (two SphereSDF children + a CombineMethod override) via the real graph path, so
// the enum apply is exercised through configureFieldNodeFromParams. Returns the assembled MSL text.
std::string assembleCombineWithMethod(const std::string& tmpl, int combineMethod) {
  static Graph g;
  g = Graph{};
  Node comb; comb.id = 1; comb.type = "CombineSDF"; comb.params["CombineMethod"] = (float)combineMethod;
  Node a; a.id = 2; a.type = "SphereSDF";
  Node b; b.id = 3; b.type = "SphereSDF";
  g.nodes = {comb, a, b};
  // Wire the two spheres into CombineSDF's InputA/InputB (port indices 0,1 in combineSdfSpec).
  g.connections = {{pinId(2, 4), pinId(1, 0)}, {pinId(3, 4), pinId(1, 1)}};  // Sphere.Result(port4)->InA/InB
  static EvaluationContext ctx;
  ctx = EvaluationContext{};
  static std::map<int, std::map<std::string, float>> scratch;
  scratch.clear();
  FieldParamResolver params = [&](int id) -> const std::map<std::string, float>* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    return &(scratch[id] = resolveNodeParams(g, *n, ctx, nullptr));
  };
  auto tree = buildFieldTree(g, 1, params);
  if (!tree) return "";
  return assembleFieldMSL(tree, tmpl).msl;
}

// SLOT-ID == PORT-ID guard: for each migrated op, the apply table's slot ids MUST be a subset of the op's
// NodeSpec PortSpec ids (the resolver keys by port id; a drift = silent default). We assert it from the
// outside: the literal slot ids each configureXxx uses (kept in sync with the .cpp slot tables).
struct OpSlots { const char* type; std::vector<const char*> slots; };
const std::vector<OpSlots>& migratedOpSlots() {
  static const std::vector<OpSlots> s = {
      {"SphereSDF", {"Center.x", "Center.y", "Center.z", "Radius"}},
      {"BoxSDF", {"Center.x", "Center.y", "Center.z", "Size.x", "Size.y", "Size.z", "UniformScale",
                  "EdgeRadius"}},
      {"TorusSDF", {"Center.x", "Center.y", "Center.z", "Radius", "Thickness", "Axis"}},
      {"CombineSDF", {"K", "CombineMethod"}},
      {"ToroidalVortexField", {"Center.x", "Center.y", "Center.z", "Radius", "Range", "SwirlGain",
                               "RadialGain", "FallOffRate", "Axis"}},
  };
  return s;
}

bool specHasPort(const NodeSpec& spec, const char* id) {
  for (const PortSpec& p : spec.ports)
    if (p.id == id) return true;
  return false;
}

}  // namespace

int runFieldParamApplySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  int rc = 0;

  const std::string tmpl = loadTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-field-paramapply] FAIL: could not load field template (SW_FIELD_TEMPLATE)\n");
    pool->release();
    return 1;
  }

  // ---- (4) SLOT-ID == PORT-ID guard (pure host; runs first, no GPU needed) ----
  {
    bool allOk = true;
    for (const OpSlots& op : migratedOpSlots()) {
      const NodeSpec* spec = nullptr;
      for (const NodeSpec& s : fieldSpecSink())
        if (s.type == op.type) { spec = &s; break; }
      if (!spec) {
        std::printf("[selftest-field-paramapply] slot-id guard: %s NOT in fieldSpecSink -> FAIL\n", op.type);
        allOk = false;
        continue;
      }
      for (const char* slot : op.slots) {
        if (!specHasPort(*spec, slot)) {
          std::printf("[selftest-field-paramapply] slot-id guard: %s slot '%s' has NO matching PortSpec.id "
                      "-> FAIL (silent-default drift)\n", op.type, slot);
          allOk = false;
        }
      }
    }
    std::printf("[selftest-field-paramapply] slot-id==port-id guard over %zu migrated ops -> %s\n",
                migratedOpSlots().size(), allOk ? "OK" : "FAIL");
    if (!allOk) rc = 1;
  }

  // ---- (2) BUFFER-LEVEL: assembled floatParams[Radius slot] tracks the graph-supplied Radius ----
  {
    auto wired = buildSphere(kWiredRadius, /*supplyRadius=*/true);
    auto deflt = buildSphere(kDefaultRadius, /*supplyRadius=*/false);  // no graph param -> ctor .t3 default.
    float wiredSlot = -999.f, defltSlot = -999.f;
    if (wired) { AssembledField af = assembleFieldMSL(wired, tmpl); if (af.floatParams.size() > kRadiusSlot) wiredSlot = af.floatParams[kRadiusSlot]; }
    if (deflt) { AssembledField af = assembleFieldMSL(deflt, tmpl); if (af.floatParams.size() > kRadiusSlot) defltSlot = af.floatParams[kRadiusSlot]; }
    bool wok = std::fabs(wiredSlot - kWiredRadius) < 1e-5f;
    bool dok = std::fabs(defltSlot - kDefaultRadius) < 1e-5f;
    std::printf("[selftest-field-paramapply] buffer: Radius slot[%d] wired=%.4f (want %.3f) default=%.4f "
                "(want %.3f) -> %s\n", kRadiusSlot, wiredSlot, kWiredRadius, defltSlot, kDefaultRadius,
                (wok && dok) ? "OK" : "FAIL");
    if (!(wok && dok)) rc = 1;
  }

  // ---- (3) ENUM (CombineSDF): non-default CombineMethod switches the assembled MSL mode helper ----
  {
    // Default CombineMethod = 2 (UnionRound) -> helper fOpUnionRound. Non-default 4 (UnionSmooth) ->
    // fOpSmoothUnion. The TEXT must switch (selectors change codegen, not the buffer).
    std::string mslDefault = assembleCombineWithMethod(tmpl, 2);
    std::string mslSmooth = assembleCombineWithMethod(tmpl, 4);
    bool defaultHasRound = mslDefault.find("fOpUnionRound") != std::string::npos;
    bool smoothHasSmooth = mslSmooth.find("fOpSmoothUnion") != std::string::npos;
    bool smoothLacksRound = mslSmooth.find("fOpUnionRound") == std::string::npos;
    bool ok = defaultHasRound && smoothHasSmooth && smoothLacksRound;
    std::printf("[selftest-field-paramapply] enum: CombineMethod 2->fOpUnionRound=%d  4->fOpSmoothUnion=%d "
                "(no Round=%d) -> %s\n", defaultHasRound, smoothHasSmooth, smoothLacksRound,
                ok ? "OK" : "FAIL");
    if (!ok) rc = 1;
  }

  // ---- (1) GPU readback: wired R=0.8 reaches the rendered field (closed-form, != default) ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-field-paramapply] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }
  MTL::CommandQueue* q = dev->newCommandQueue();
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();

  auto sphere = buildSphere(kWiredRadius, /*supplyRadius=*/true);
  if (!sphere) {
    std::printf("[selftest-field-paramapply] FAIL: buildFieldTree(SphereSDF) returned null\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Texture* tex = renderField2d(dev, q, sphere, tmpl, kW, kH);
  if (!tex) {
    std::printf("[selftest-field-paramapply] FAIL: renderField2d returned null (compile/PSO failure)\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  std::vector<float> buf((size_t)kW * kH, 0.0f);
  tex->getBytes(buf.data(), kW * sizeof(float), MTL::Region::Make2D(0, 0, kW, kH), 0);

  // Texel whose field-space p is closest to (0.3, 0): p.x = 0.3 -> px ≈ ((0.3+1)*W-1)/2 = (1.3*128-1)/2 =
  // 82.7 -> px=83 gives p.x = (2*83+1)/128 - 1 = 0.3046875; center row py = (kH-1)/2 = 63.
  const uint32_t probePx = 83, probePy = (kH - 1) / 2;
  float px = pX(probePx), py = pY(probePy);
  float got = buf[(size_t)probePy * kW + probePx];
  float expectedWired = std::sqrt(px * px + py * py) - kWiredRadius;    // R=0.8 -> ~ -0.495
  float expectedDefault = std::sqrt(px * px + py * py) - kDefaultRadius;// R=0.5 -> ~ -0.195
  const float kTol = 1e-4f;
  float diffWired = std::fabs(got - expectedWired);
  float diffDefault = std::fabs(got - expectedDefault);

  // Production assertion: the GPU read the WIRED radius (matches R=0.8 closed form, clearly NOT R=0.5).
  bool matchesWired = diffWired <= kTol;
  bool differsFromDefault = diffDefault > 0.05f;  // 2*(0.8-0.5) margin = 0.6; any apply-failure shows here.
  std::printf("[selftest-field-paramapply] gpu: p=(% .4f,% .4f) f.w=% .6f  wired(R=0.8)=% .6f diff=%.2e  "
              "default(R=0.5)=% .6f diff=%.2e\n", px, py, got, expectedWired, diffWired, expectedDefault,
              diffDefault);

  if (injectBug) {
    // -bug: assert the DEFAULT R=0.5 closed form while the graph supplied 0.8. The GPU actually read 0.8,
    // so this assertion must FAIL -> the tooth bites. (A no-op apply would render R=0.5 and this would
    // wrongly pass.)
    bool bugCaught = std::fabs(got - expectedDefault) > kTol;  // got (R=0.8) does NOT match the R=0.5 claim
    std::printf("[selftest-field-paramapply] -bug (assert default R=0.5 while graph supplied 0.8) -> %s\n",
                bugCaught ? "RED (GPU read R=0.8, the R=0.5 claim is false — apply happened, tooth bit)"
                          : "NO-BITE (GPU read R=0.5 — the apply did NOT happen, default leaked)");
    tex->release(); q->release(); dev->release(); pool->release();
    return bugCaught ? 1 : 0;
  }

  if (!(matchesWired && differsFromDefault)) {
    std::printf("[selftest-field-paramapply] gpu FAIL: wired Radius did not reach the field\n");
    rc = 1;
  } else {
    std::printf("[selftest-field-paramapply] gpu OK: wired R=0.8 reached the rendered field\n");
  }

  tex->release(); q->release(); dev->release(); pool->release();

  if (rc == 0)
    std::printf("[selftest-field-paramapply] PASS (graph param-apply: buffer + GPU + enum + slot-id guard)\n");
  return rc;
}

}  // namespace sw
