// runtime/curve_animator_selftest_vec — leg ⑤vec of --selftest-animator (批次8 Vec multi-channel),
// mechanical TU split out of curve_animator_selftest.cpp (ARCHITECTURE rule 4). Proves, on a REAL
// spec (RadialPoints.Center, Vec3):
//   a) animateFloatVector builds N curves under the group HEAD's id, first keys = each channel's
//      value (= TiXL Animator.AddCurvesForFloatVector, Animator.cs:97-126)
//   b) the resident projection flips EVERY component slot to Automation with a channel-indexed
//      curveRef (#0..#N-1) and each component samples ITS OWN curve (the 承重關節 golden)
//   c) savev2 roundtrips the index column bit-stable; reload samples identically
//   d) remove() drops the whole group; rebuild flips every component back to Constant
// injectBug asserts the OLD single-channel bug's value (every channel reads #0) -> FAILS (teeth).
#include "runtime/curve_animator.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/compound_save.h"
#include "runtime/graph.h"         // findSpec / animGroupForSlot (the 同源 grouping helper)
#include "runtime/graph_bridge.h"  // libFromGraph (seed from the default graph)
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [animator] FAIL %s\n", what); }
  else printf("  [animator] ok   %s\n", what);
}
void expectNear(const char* what, float got, float want) {
  bool ok = std::abs(got - want) <= 1e-4f;
  if (!ok) { ++g_fail; printf("  [animator] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
  else printf("  [animator] ok   %s = %.5f\n", what, got);
}

}  // namespace

int runCurveAnimatorVecLeg(bool injectBug) {
  g_fail = 0;
  SymbolLibrary sl = libFromGraph(defaultParticleGraph());
  Symbol* root = sl.find(sl.rootId);
  // Find a Vec3 group through the SAME helper the projection uses (同源 by construction).
  int vchild = -1, arity = 0;
  std::string headId;
  std::vector<std::string> comp;  // component slot ids in channel order
  for (const SymbolChild& c : root->children) {
    const NodeSpec* spec = findSpec(c.symbolId);
    if (!spec) continue;
    for (size_t i = 0; i < spec->ports.size() && vchild == -1; ++i) {
      const PortSpec& p = spec->ports[i];
      if (!(p.isInput && p.widget == Widget::Vec && p.vecArity == 3)) continue;
      vchild = c.id; headId = p.id; arity = p.vecArity;
      for (int k = 0; k < arity; ++k) {
        comp.push_back(spec->ports[i + (size_t)k].id);
        const AnimGroup ag = animGroupForSlot(*spec, comp.back());
        expect("vec: component resolves to (head, channel) via animGroupForSlot",
               ag.headId == headId && ag.channel == k && ag.arity == 3);
      }
    }
    if (vchild != -1) break;
  }
  expect("vec: found a Vec3 group on the default graph", vchild != -1 && arity == 3);

  // Animate the group: first keys @0 = {10, 0, -5} (= AddCurvesForFloatVector), then turn
  // channel 1 into a ramp (key @4 = 8) so the three curves are DISTINCT.
  const float vals[3] = {10.0f, 0.0f, -5.0f};
  root->animator.animateFloatVector(vchild, headId, 0.0, vals, 3);
  Animator::CurveArray* arr = root->animator.curvesFor(vchild, headId);
  expect("vec: 3 curves live under the HEAD id", arr && arr->size() == 3);
  if (arr && arr->size() == 3) {
    bool firstKeys = true;
    for (int k = 0; k < 3; ++k)
      firstKeys = firstKeys && (*arr)[k].count() == 1 && (float)(*arr)[k].sample(0.0) == vals[k];
    expect("vec: each channel seeded with its own first key", firstKeys);
    VDefinition k8; k8.value = 8.0; k8.u = 4.0;
    k8.inInterpolation = KeyInterpolation::Linear; k8.outInterpolation = KeyInterpolation::Linear;
    (*arr)[1].addOrUpdate(4.0, k8);
  }
  // makeRef/parseRef carry the channel index.
  int pc = 0, pidx = -1; std::string pin;
  expect("vec: makeRef('#2') parses back to channel 2",
         Animator::parseRef(Animator::makeRef(vchild, headId, 2), pc, pin, pidx) &&
             pc == vchild && pin == headId && pidx == 2);

  // Projection: every component slot flips to Automation with ITS channel's curveRef.
  ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
  const ResidentNode* n = g.node(std::to_string(vchild));
  bool refsOk = n != nullptr;
  for (int k = 0; n && k < 3; ++k) {
    const ResidentInput* ri = n->input(comp[(size_t)k]);
    refsOk = refsOk && ri && ri->driver == ResidentInput::Driver::Automation &&
             ri->curveRef == Animator::makeRef(vchild, headId, k);
  }
  expect("vec: 3 component slots project Automation w/ channel-indexed refs", refsOk);

  // Sampling golden: each component follows ITS OWN curve (@2 = ramp midpoint, @4 = ramp top).
  ResidentEvalCtx ctx; ctx.lib = &sl; ctx.localTime = 2.0f;
  std::map<std::string, float> at2 = resolveResidentFloatInputs(g, *n, ctx);
  expectNear("vec: ch0 (flat 10) @2", at2[comp[0]], 10.0f);
  expectNear("vec: ch1 (ramp 0->8) @2", at2[comp[1]], 4.0f);
  expectNear("vec: ch2 (flat -5) @2", at2[comp[2]], -5.0f);
  ctx.localTime = 4.0f;
  std::map<std::string, float> at4 = resolveResidentFloatInputs(g, *n, ctx);
  expectNear("vec: ch1 (ramp 0->8) @4", at4[comp[1]], 8.0f);

  // savev2: the index column roundtrips bit-stable; the reload samples identically.
  std::string j1 = libToJsonV2(sl);
  expect("vec: savev2 wrote channel index entries", j1.find("\"index\"") != std::string::npos);
  SymbolLibrary rl;
  expect("vec: savev2 reload ok", libFromJsonAny(j1, rl));
  const Animator::CurveArray* rarr = rl.symbols.count(rl.rootId)
      ? rl.symbols[rl.rootId].animator.curvesFor(vchild, headId) : nullptr;
  expect("vec: reload kept all 3 channels", rarr && rarr->size() == 3);
  expect("vec: savev2 multi-channel roundtrip bit-stable", libToJsonV2(rl) == j1);
  ResidentEvalGraph rg = buildEvalGraph(rl, rl.rootId);
  const ResidentNode* rn = rg.node(std::to_string(vchild));
  ResidentEvalCtx rctx; rctx.lib = &rl; rctx.localTime = 2.0f;
  std::map<std::string, float> rat2 = rn ? resolveResidentFloatInputs(rg, *rn, rctx)
                                         : std::map<std::string, float>{};
  expectNear("vec: reload ch1 @2 identical", rat2[comp[1]], 4.0f);

  // Remove drops the WHOLE group; rebuild flips every component back to Constant.
  root->animator.remove(vchild, headId);
  expect("vec: remove drops every channel", !root->animator.isAnimated(vchild, headId));
  ResidentEvalGraph g2 = buildEvalGraph(sl, sl.rootId);
  const ResidentNode* n2 = g2.node(std::to_string(vchild));
  bool backConst = n2 != nullptr;
  for (int k = 0; n2 && k < 3; ++k) {
    const ResidentInput* ri = n2->input(comp[(size_t)k]);
    backConst = backConst && ri && ri->driver == ResidentInput::Driver::Constant;
  }
  expect("vec: after remove, all 3 components back to Constant", backConst);

  // teeth: under -bug, assert the OLD single-channel bug's value (every channel reads #0).
  if (injectBug) {
    root->animator.animateFloatVector(vchild, headId, 0.0, vals, 3);
    Animator::CurveArray* barr = root->animator.curvesFor(vchild, headId);
    VDefinition k8; k8.value = 8.0; k8.u = 4.0;
    k8.inInterpolation = KeyInterpolation::Linear; k8.outInterpolation = KeyInterpolation::Linear;
    if (barr && barr->size() == 3) (*barr)[1].addOrUpdate(4.0, k8);
    ResidentEvalGraph bg = buildEvalGraph(sl, sl.rootId);
    const ResidentNode* bn = bg.node(std::to_string(vchild));
    ResidentEvalCtx bctx; bctx.lib = &sl; bctx.localTime = 2.0f;
    std::map<std::string, float> bat2 = bn ? resolveResidentFloatInputs(bg, *bn, bctx)
                                           : std::map<std::string, float>{};
    expectNear("BUG: vec ch1 @2 asserting ch0's 10 (real=4, must FAIL)", bat2[comp[1]], 10.0f);
  }
  return g_fail;
}

}  // namespace sw
