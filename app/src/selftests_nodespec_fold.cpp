// selftests_nodespec_fold — the param-completion FOLD golden (--selftest-nodespecfold). Pins the
// folded-logical-count walk that the integrity gate (tools/nodespec_integrity.sh via --dump-nodespec)
// depends on, so a future refactor of the Vec-fold rule trips here LOUD instead of silently re-breaking
// the gate. Mirrors dumpNodeSpec's POSITIONAL consume-the-run walk (selftests.cpp), which is itself the
// same walk the Inspector (ui/inspector.cpp:83-86) and animGroupForSlot (node_registry.cpp:196-213) use
// — one grouping rule, now four consumers.
//
// WHY THIS EXISTS: field/mesh ops hand-write Vec component ports (Center.y/.z) with NO widget set
// (default Slider, vecArity 1). The PRIOR fold rule keyed components on `widget==Vec && vecArity==1`,
// which did not recognise them → each counted 1 → the gate read false EXTRA (SphereSDF dumped 4, true 2).
// The positional walk folds by POSITION regardless of the component's own widget. This golden anchors the
// known-true counts so the fix can't regress.
//
// TEETH (each = a folded-logical count anchored to the node's TiXL .cs [Input] count):
//   SphereSDF == 2   (Center vec3 folds to 1 + Radius) — the canonical fold-bug victim
//   BoxSDF    == 4   (Center vec3 + Size vec3 each fold to 1 + UniformScale + EdgeRadius)
//   RaymarchField == 11 — a multi-Vec op with correctly widget-tagged components; both walks fold it to
//                        11 (stability sentinel: the positional fix must not over-fold a correct op).
//                        11 is the sw-side fold, NOT its TiXL .cs count (14) — RaymarchField still has 3
//                        unported inputs, a real gap this golden does not pretend to close.
//   RadialPoints == 17 — a NO-Vec-head generator (generator-safe: the walk must be identical to the
//                        old per-port walk → the 13/13 generator gate cannot regress)
//   Blend     == 8   — the CHAINED-Vec-head victim (image island): two Vector4 params ColorA/ColorB
//                        whose components are THEMSELVES tagged widget==Vec with descending arity
//                        (4→3→2→1). The prior guard broke on a Vec-tagged component → folded each
//                        Vector4 into 3 → false EXTRA (symbol flip). The blind positional consume
//                        folds each to 1. (8 != its TiXL .cs 10 — Blend has 2 real unported gaps.)
// -bug: reverts to the DIVERGENT old rule (component = widget==Vec && vecArity==1 only). SphereSDF/BoxSDF
//   over-count (hand-written un-tagged components leak) AND Blend over-folds (each Vector4 → 3, total 12)
//   → their assertions go RED, proving the positional walk is load-bearing for BOTH fold-bug shapes.
//
// Pure CPU runtime leaf (findSpec only — no Metal, no UI, no upward deps).
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/graph.h"               // findSpec / NodeSpec / PortSpec / Widget
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace sw {
namespace {

// Fold the spec to its logical-input count. `positional` = the production rule (a Vec head at i owns the
// next min(arity,4)-1 ports BY POSITION); !positional = the old divergent rule (component keyed on its own
// widget==Vec&&vecArity==1), reproduced for the -bug tooth. Excludes output ports, the grid-capacity
// "Count", and the image RenderTarget trio — same exclusions as dumpNodeSpec.
int foldCount(const NodeSpec& spec, bool positional) {
  bool hasCountX = false;
  for (const auto& p : spec.ports)
    if (p.id == "CountX") { hasCountX = true; break; }

  auto excluded = [&](const PortSpec& p) {
    if (!p.isInput) return true;
    if (hasCountX && p.id == "Count") return true;
    if (p.id == "Resolution" || p.id == "CustomW" || p.id == "CustomH") return true;
    return false;
  };

  const auto& ports = spec.ports;
  int folded = 0;
  for (size_t i = 0; i < ports.size(); ++i) {
    const PortSpec& p = ports[i];
    if (excluded(p)) continue;
    if (positional) {
      if (p.isInput && p.widget == Widget::Vec && p.vecArity >= 2) {
        int N = p.vecArity > 4 ? 4 : p.vecArity;
        ++folded;
        // consume the next N-1 ports positionally (stop ONLY at output / end — NOT at a component
        // that is itself tagged Vec; the authority walk consumes by position regardless of widget,
        // and the chained-Vec image ops (Blend etc.) tag Color.x/.y/.z/.w with descending arity).
        int consumed = 0;
        for (int k = 1; k < N; ++k) {
          size_t j = i + (size_t)k;
          if (j >= ports.size() || !ports[j].isInput)
            break;
          ++consumed;
        }
        i += (size_t)consumed;
        continue;
      }
      ++folded;
    } else {
      // OLD divergent rule: a component is ONLY a port with widget==Vec && vecArity==1.
      if (p.widget == Widget::Vec && p.vecArity == 1) continue;  // skip "component"
      ++folded;
    }
  }
  return folded;
}

struct Check { const char* label; int got; int want; bool ok; };

}  // namespace (anonymous)

int runNodeSpecFoldSelfTest(bool injectBug) {
  const bool positional = !injectBug;  // -bug = old divergent rule

  std::vector<Check> checks;
  auto pin = [&](const char* type, int want) {
    const NodeSpec* s = findSpec(type);
    if (!s) { checks.push_back({type, -1, want, false}); return; }
    int got = foldCount(*s, positional);
    checks.push_back({type, got, want, got == want});
  };

  // Anchored to each node's TiXL .cs [Input] count (field/generate/sdf/*.cs, field/render/RaymarchField.cs,
  // point/generate/RadialPoints.cs).
  pin("SphereSDF", 2);
  pin("BoxSDF", 4);
  pin("RadialPoints", 17);  // NO-Vec-head generator-safe sentinel

  // RaymarchField: a multi-Vec op whose component ports ARE correctly widget-tagged (the already-folded
  // batch). Both the new positional walk AND the old widget-keyed rule fold it to 11 → a STABILITY
  // sentinel: the positional fix must not OVER-fold a correctly-authored op. (11 != its TiXL .cs count
  // of 14 — RaymarchField has 3 genuinely-unported inputs, a real MISSING gap this batch does NOT close;
  // this golden anchors the sw-side fold, not parity completeness.)
  pin("RaymarchField", 11);

  // Blend (image island): the CHAINED-Vec-head fold-bug victim. This op spells TWO Vector4 params
  // (ColorA/ColorB) as Color?.x/.y/.z/.w where EACH component is itself tagged widget==Vec with
  // DESCENDING arity (4→3→2→1). The prior positional guard broke on a component being a Vec head →
  // folded each Vector4 into 3 → false EXTRA (symbol flip: really MISSING). The blind positional
  // consume folds each to 1. Folded = ImageA + ImageB + ColorA + ColorB + BlendMode + AlphaMode +
  // NormalForUpperHalf + ScaleMode = 8 (out + Resolution/CustomW/CustomH excluded). 8 != its TiXL .cs
  // count of 10 (Blend has 2 genuinely-unported [Input]s — a real MISSING gap this golden anchors the
  // sw-side fold for, not parity). Under -bug each Vector4 over-folds to 3 → 12 → RED.
  pin("Blend", 8);

  bool all = true;
  for (const Check& c : checks) {
    std::printf("[selftest-nodespecfold]   %-16s folded=%d want=%d -> %s\n", c.label, c.got, c.want,
                c.ok ? "ok" : "RED");
    all = all && c.ok;
  }

  if (injectBug) {
    if (all) {
      std::printf("[selftest-nodespecfold] FAIL: injectBug still passed — the positional Vec fold is not "
                  "load-bearing (the old widget-keyed rule produced the same counts)\n");
      return 1;
    }
    std::printf("[selftest-nodespecfold] injectBug correctly RED — old widget-keyed rule un-folds "
                "hand-written Vec components (SphereSDF/BoxSDF over-count)\n");
    return 1;
  }
  std::printf("[selftest-nodespecfold] %s\n", all ? "PASS" : "FAIL");
  return all ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/337, {"nodespecfold", runNodeSpecFoldSelfTest});

}  // namespace sw

