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

#include "runtime/graph.h"     // NodeSpec / findSpec / PortSpec
#include "runtime/Particle.h"  // full EvaluationContext definition (for evaluate())

namespace sw {
namespace {

// Ops that declare an always-dirty output (= TiXL DirtyFlagTrigger.Always/Animated). slice-1b
// scope: Time (RunTime.cs:8 declares Animated). Audio-reactive nodes join here once the value
// graph drives them; automation-driven LIVE arrives with the S3 curve store (both named-deferred).
bool opDeclaresLiveOutput(const std::string& opType) {
  return opType == "Time";
}

}  // namespace

void initResidentNodeCache(ResidentNode& n) {
  const NodeSpec* s = findSpec(n.opType);
  if (!s) return;
  const bool live = opDeclaresLiveOutput(n.opType);
  for (const PortSpec& p : s->ports)
    if (!p.isInput) {
      ResidentOutputCache c;
      c.isLiveSource = live;  // leaf live source: bumped every frame (決策 7 / 🪤#1)
      n.outCache[p.id] = c;
    }
}

void initResidentCache(ResidentEvalGraph& g) {
  for (ResidentNode& n : g.nodes) initResidentNodeCache(n);
}

void bumpLiveSources(ResidentEvalGraph& g) {
  for (ResidentNode& n : g.nodes)
    for (auto& kv : n.outCache)
      if (kv.second.isLiveSource) kv.second.baseVersion++;  // Trigger=Always; ++ own version
}

float pullResidentFloat(ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  auto it = g.byPath.find(nodePath);
  if (it == g.byPath.end()) return 0.0f;
  const int idx = it->second;
  const NodeSpec* s = findSpec(g.nodes[idx].opType);
  if (!s || !s->evaluate) return 0.0f;

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
          v = 0.0f;  // S3 stub
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
  return v;
}

}  // namespace sw
