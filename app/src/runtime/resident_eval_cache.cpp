// runtime/resident_eval_cache — batch 1b: version-chasing dirty + per-output-slot cache on the
// resident eval graph (承重決策 6/7; TiXL DirtyFlag.cs version-chasing, NOT content hash). Split
// from resident_eval_graph.cpp so the slice-1 flatten/eval engine stays one job (ARCHITECTURE
// rule 4). runtime leaf: depends only on resident_eval_graph.h + graph.h + Particle.h (ctx).
//
// Model (decision 6, 值圖 = eager 後序一趟): a pull recurses Connection inputs first — it ALWAYS
// walks the cone (cheap: pointer-chase + int compare), so an upstream version change reaches every
// downstream slot the same pass. A DERIVED slot (≥1 Connection input) adopts the SUM of its
// upstream sourceVersions (multi-input combine = TiXL InvalidationOverride; sum, not max, so a
// change on ANY input dirties it). A LEAF slot's sourceVersion is owned externally (LIVE 每幀 bump
// / edit-time push). Recompute happens ONLY when dirty (valueVersion != sourceVersion); otherwise
// the cached value is returned with no evaluate — that skip IS the win (貴的靜態 op 算一次存著).
#include "runtime/resident_eval_graph.h"

#include <queue>
#include <unordered_map>

#include "runtime/graph.h"     // NodeSpec / findSpec / PortSpec
#include "runtime/Particle.h"  // full EvaluationContext definition (for evaluate())

namespace sw {
namespace {

// Ops that declare an always-dirty output (= TiXL DirtyFlagTrigger.Always/Animated). slice-1b
// scope: Time (Time.cs:9 DirtyFlagTrigger.Animated) + GetFrameSpeedFactor (GetFrameSpeedFactor.cs:6
// DirtyFlagTrigger.Animated). Audio-reactive nodes join here once the value graph drives them;
// automation-driven LIVE arrives with the S3 curve store (now wired below).
bool opDeclaresLiveOutput(const std::string& opType) {
  return opType == "Time" || opType == "GetFrameSpeedFactor";
}

// S3 (slice 1b named-deferred leg, now open): an Automation-driven input makes the node's outputs
// LIVE — the playhead moves every frame so the sampled value must re-cook every pass (=
// DirtyFlagTrigger.Animated on an animated Slot, Animator.cs:189). This is the mechanism behind the
// 拍板 selftest leg: driver Constant<->Automation toggle => isLiveSource STATIC<->LIVE same flip.
bool nodeHasAutomationInput(const ResidentNode& n) {
  for (const ResidentInput& in : n.inputs)
    if (in.driver == ResidentInput::Driver::Automation) return true;
  return false;
}

}  // namespace

void initResidentNodeCache(ResidentNode& n) {
  const NodeSpec* s = findSpec(n.opType);
  if (!s) return;
  // 拍板 1b: isLiveSource = driver(Automation) ∨ op 宣告 ∨ per-output triggerOverride=Always (S2, the
  // third term wired here). The first two are node-wide; the third is PER-OUTPUT (an EditNodeOutput
  // trigger sets one output Always, not the whole node — = TiXL Output.DirtyFlagTrigger). So the
  // node-wide part is OR'd per output with that output's own triggerAlways flag.
  const bool nodeLive = opDeclaresLiveOutput(n.opType) || nodeHasAutomationInput(n);
  for (const PortSpec& p : s->ports)
    if (!p.isInput) {
      ResidentOutputCache c;
      const bool triggerAlways = n.triggerAlwaysOut.count(p.id) > 0;
      c.isLiveSource = nodeLive || triggerAlways;  // bumped every frame (決策 7 / 🪤#1)
      c.isDisabled = n.disabledOut.count(p.id) > 0;  // S2: frozen -> pull returns cachedFloat verbatim
      n.outCache[p.id] = c;
    }
}

void initResidentCache(ResidentEvalGraph& g) {
  for (ResidentNode& n : g.nodes) initResidentNodeCache(n);
}

void transplantDisabledCaches(const ResidentEvalGraph& oldG, ResidentEvalGraph& newG) {
  // A projection REBUILD (libRevision bump) starts every cache cold — but "disabled" means
  // FROZEN AT THE LAST RESULT, and TiXL has no rebuild artifact (its slots persist, Value and
  // all). Without this transplant, toggling disable in the GUI snaps the frozen value to 0 on
  // the very next frame (refuter-S2 P1×P7 combined). Carry the old cache verbatim for outputs
  // that are disabled in the NEW graph; everything else stays cold (normal recompute).
  for (ResidentNode& n : newG.nodes) {
    auto oldIt = oldG.byPath.find(n.path);
    if (oldIt == oldG.byPath.end()) continue;
    const ResidentNode& on = oldG.nodes[oldIt->second];
    for (auto& kv : n.outCache) {
      if (!kv.second.isDisabled) continue;
      auto oc = on.outCache.find(kv.first);
      if (oc != on.outCache.end()) {
        const bool wasDisabled = kv.second.isDisabled;
        const bool wasLive = kv.second.isLiveSource;
        kv.second = oc->second;          // frozen value + versions ride across the rebuild
        kv.second.isDisabled = wasDisabled;   // flags follow the NEW model, not the old
        kv.second.isLiveSource = wasLive;
      }
    }
  }
}

void bumpLiveSources(ResidentEvalGraph& g) {
  for (ResidentNode& n : g.nodes)
    for (auto& kv : n.outCache)
      if (kv.second.isLiveSource) kv.second.baseVersion++;  // Trigger=Always; ++ own version
}

std::unordered_set<std::string> computeLiveDownstreamClosure(const ResidentEvalGraph& g) {
  // Build forward adjacency: srcPath -> vector of dst paths (from Connection input drivers).
  std::unordered_map<std::string, std::vector<std::string>> fwd;
  fwd.reserve(g.nodes.size() * 2);
  for (const ResidentNode& n : g.nodes)
    for (const ResidentInput& ri : n.inputs)
      if (ri.driver == ResidentInput::Driver::Connection)
        fwd[ri.srcNodePath].push_back(n.path);

  // Seed BFS from every node with at least one live-source output.
  std::unordered_set<std::string> closure;
  std::queue<std::string> q;
  for (const ResidentNode& n : g.nodes)
    for (const auto& kv : n.outCache)
      if (kv.second.isLiveSource) {
        if (closure.insert(n.path).second) q.push(n.path);
        break;  // one live output is enough
      }

  // BFS forward along Connection edges.
  while (!q.empty()) {
    const std::string cur = std::move(q.front());
    q.pop();
    auto it = fwd.find(cur);
    if (it == fwd.end()) continue;
    for (const std::string& dst : it->second)
      if (closure.insert(dst).second) q.push(dst);
  }
  return closure;
}

float pullResidentFloat(ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  auto it = g.byPath.find(nodePath);
  if (it == g.byPath.end()) return 0.0f;
  const int idx = it->second;

  // S2 BYPASS (= TiXL Slot.ByPassUpdate, Slot.cs:176-179): a bypassed node's MAIN output returns its
  // MAIN input's value instead of cooking. Read the main input's driver and resolve THROUGH it (a
  // Connection chases upstream; a Constant/Automation reads that value). Done BEFORE the cache lookup:
  // a bypassed output doesn't cook, so it has no meaningful own-cache; its dirtiness is the upstream's.
  // depth+1 keeps the cycle guard honest. Only the MAIN output is bypassed; other outputs cook normally.
  if (g.nodes[idx].bypassed && outSlotId == g.nodes[idx].bypassOutSlot) {
    const ResidentInput* ri = g.nodes[idx].input(g.nodes[idx].bypassInSlot);
    if (!ri) return 0.0f;  // no main input driver -> nothing to pass through
    switch (ri->driver) {
      case ResidentInput::Driver::Constant:   return ri->constant;
      case ResidentInput::Driver::Automation: return sampleAutomation(ctx, *ri);
      case ResidentInput::Driver::Connection:
        return pullResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx, depth + 1);
    }
  }

  const NodeSpec* s = findSpec(g.nodes[idx].opType);
  if (!s || !s->evaluate) return 0.0f;

  // S2 DISABLED (= TiXL Slot.SetDisabled, Slot.cs:43-67): a disabled output FREEZES at its last
  // result. In this version-chasing cache that means stop chasing — return cachedFloat verbatim,
  // WITHOUT touching valueVersion/sourceVersion, so a later upstream change can never thaw it. The
  // frozen value is whatever was last computed before disable (or the cache default 0 if never cooked,
  // which is the faithful "last result" for a never-evaluated slot). Clearing isDisabled resumes the
  // normal version compare below, which finds it stale (versions diverged while frozen) and recomputes.
  {
    auto dc = g.nodes[idx].outCache.find(outSlotId);
    if (dc != g.nodes[idx].outCache.end() && dc->second.isDisabled) return dc->second.cachedFloat;
  }

  // Gather Float inputs in spec port order (mirrors evalResidentFloat) AND sum upstream versions.
  // Recursion can grow g.nodes? No — pull never appends nodes, so `idx` stays valid across calls.
  float in[8];
  int ni = 0;
  uint64_t upstreamSum = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 8; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    float v = p.def;
    if (const ResidentInput* ri = g.nodes[idx].input(p.id)) {
      switch (ri->driver) {
        case ResidentInput::Driver::Constant:
          v = ri->constant;
          break;
        case ResidentInput::Driver::Connection: {
          v = pullResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx, depth + 1);
          // 取和. An UNRESOLVABLE upstream (dangling path / missing out slot) still contributes a
          // fixed version 1, never 0 — so a fully-dangling derived slot stays initially-dirty
          // (sourceVersion >= 1 != valueVersion 0), computes once with the upstream treated as 0
          // (matching the no-cache evalResidentFloat), and doesn't freeze on its uninitialized
          // cache (refuter D1: sum==0 colliding with valueVersion==0 = permanent false-clean 卡舊).
          uint64_t upSv = 1;
          if (const ResidentNode* up = g.node(ri->srcNodePath)) {
            auto uc = up->outCache.find(ri->srcSlotId);
            if (uc != up->outCache.end()) upSv = uc->second.sourceVersion;
          }
          upstreamSum += upSv;
          break;
        }
        case ResidentInput::Driver::Automation:
          v = sampleAutomation(ctx, *ri);  // S3 sample @ localTime (the playhead)
          break;
      }
    }
    in[ni++] = v;
  }

  ResidentOutputCache& cache = g.nodes[idx].outCache[outSlotId];
  // Effective version = this slot's own accumulated baseVersion (LIVE bump / edit-time push) PLUS
  // the summed upstream versions (a leaf has upstreamSum 0 -> sourceVersion = baseVersion). baseVersion
  // is monotonic and never overwritten, so an edit-time bump on a DERIVED node survives to dirty it
  // (refuter A4: the old `sourceVersion = upstreamSum` overwrite erased the bump -> stale value edit).
  cache.sourceVersion = cache.baseVersion + upstreamSum;
  if (cache.valueVersion == cache.sourceVersion) return cache.cachedFloat;  // not dirty -> cache

  int outIdx = 0;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].id == outSlotId) { outIdx = (int)i; break; }

  EvaluationContext ec{};
  ec.frameIndex = ctx.frameIndex;
  ec.time = ctx.localFxTime;  // wall clock (automation sampling localTime arrives in S3)
  ec.deltaTime = 0.0f;
  float v = s->evaluate(outIdx, in, ni, ec);
  cache.cachedFloat = v;
  cache.valueVersion = cache.sourceVersion;  // Clear(): valueVersion = sourceVersion
  cache.lastUpdatePass = ctx.frameIndex;     // editor-only: record when this output last recomputed
  return v;
}

}  // namespace sw
