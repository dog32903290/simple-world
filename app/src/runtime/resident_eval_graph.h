// runtime/resident_eval_graph — the RESIDENT (build-once, frame-stable) evaluation
// graph: the flattened form of a nested SymbolLibrary. = TiXL's "resolve boundaries at
// wire-time, transparent at eval-time" + Slot's structural role. A ResidentNode is one
// inlined Child; its inputs each carry a `driver` (= TiXL Slot's update action). Driver
// AUTHORITY for Automation lives in the definition-layer Animator (contract C3/P2) — the
// resident input's driver is a PROJECTION resolved at build/patch time, never the store.
//
// This module is the batch-1 engine. It is pure CPU (no Metal) and a runtime leaf:
// depends only on compound_graph.h (nested model) + graph.h (NodeSpec/findSpec). It is
// proven headless (--selftest-residenteval) and NOT yet wired to production cook — that
// is a named follow-on slice (see docs/superpowers/plans/2026-06-10-resident-eval-graph-batch1.md).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / SymbolConnection

namespace sw {

// Two-clock eval context (bars units, 拍板 P3). localTime = the playhead (scrub/pause
// freezes it) — automation samples THIS (time-lane S3). localFxTime = the wall clock
// (runs while paused) — stateful sims sample THIS. Defined now so automation/sim plug in
// without reshaping (contract 2.5b). This is the CPU eval-time context — NOT the 16-byte
// GPU `EvaluationContext` (eval_context.h); reconciling the GPU constant buffer happens at
// the production-swap slice. evalResidentFloat builds a transient GPU ctx to call evaluate().
struct ResidentEvalCtx {
  float localTime = 0.0f;    // playhead, bars
  float localFxTime = 0.0f;  // wall clock, bars
  uint32_t frameIndex = 0;
};

// How one resident input is driven (= TiXL Slot's UpdateAction). The PROJECTION of the
// definition-layer authority, resolved at build time. Automation carries a ref into the
// def-layer Animator keyed by (symbolId,childId,inputId) — sampled in S3; stub here.
struct ResidentInput {
  enum class Driver { Constant, Connection, Automation };
  std::string slotId;                 // the op's input slot id (= PortSpec.id / SlotDef.id)
  Driver driver = Driver::Constant;
  float constant = 0.0f;              // Driver::Constant: the projected value
  std::string srcNodePath;            // Driver::Connection: upstream resident node path
  std::string srcSlotId;              // Driver::Connection: upstream output slot id
  std::string curveRef;               // Driver::Automation: def-layer animator key (S3 samples it)
};

// --- batch 1b: version-chasing dirty + per-output-slot cache (承重決策 6/7, TiXL DirtyFlag) ---
// One output slot's incremental-eval state, living ON the resident node (= TiXL Slot — cache
// has a frame-stable home, 拍板「節點 = slot」; not a parallel structure). C5: per-output-slot
// granularity. initially dirty: valueVersion(0) != sourceVersion(1) so the first pull computes
// (DirtyFlag.cs:62). dirty == valueVersion != sourceVersion (version-chasing, NOT content hash).
struct ResidentOutputCache {
  uint64_t sourceVersion = 1;  // bumped by: LIVE 每幀 / edit-time push / 上游版本取和(derived)
  uint64_t valueVersion = 0;   // = sourceVersion at last recompute; dirty when they differ
  float cachedFloat = 0.0f;    // last computed value (returned while not dirty — 算一次存著)
  bool isLiveSource = false;   // op 宣告恆髒 (Time…); bumped every frame (決策 7 / 🪤#1)
};

// One inlined operator instance. `path` is the path-qualified id (join of the child-id
// chain, e.g. "5/2/1") — unique AND frame-stable, so cache (slice 4) and the per-node
// buffer map (slice 2) key off it. opType = the atomic symbol id = the operator type.
struct ResidentNode {
  std::string path;
  std::string opType;
  std::vector<ResidentInput> inputs;
  std::map<std::string, ResidentOutputCache> outCache;  // outSlotId -> cache (1b; per-output)
  const ResidentInput* input(const std::string& slotId) const;
};

// The flattened, walkable graph. `outputs` maps the root Symbol's outputDef id -> the
// resident (path, slotId) that produces it (boundary sentinel resolved away).
struct ResidentEvalGraph {
  std::vector<ResidentNode> nodes;
  std::map<std::string, int> byPath;        // path -> index into nodes
  std::map<std::string, std::pair<std::string, std::string>> outputs;  // rootOutputDefId -> (path, slotId)
  const ResidentNode* node(const std::string& path) const;
};

// Flatten a nested SymbolLibrary rooted at rootId into a resident eval graph. Inlines every
// compound child recursively (path prefix grows), resolves boundary-crossing wires via the
// kSymbolBoundary sentinel, and guards self-nesting/cycles (a symbol id repeating on the
// current path, or depth overflow) by skipping the offending child (TiXL Core does NOT guard
// this — we add the guard, contract S14, because the resident era has no per-frame rebuild
// to bail us out). Returns an empty graph if rootId is missing.
ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId);

// Pull the float value produced by (nodePath, outSlotId), resolving each input's driver and
// recursing Connection drivers. Reuses the SAME NodeSpec::evaluate fns as flat evalFloat
// (builds a transient 16-byte EvaluationContext: time = ctx.localFxTime for now). depth>64
// breaks cycles. Automation driver returns 0 (S3 stub).
float evalResidentFloat(const ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth = 0);

// --- batch 1b cache API (resident_eval_cache.cpp) ---
// Populate each node's per-output cache (one entry per NodeSpec output port) and mark live
// sources (ops that declare an always-dirty output, e.g. Time). Call once after buildEvalGraph.
void initResidentCache(ResidentEvalGraph& g);
// Bump every live source's sourceVersion (= TiXL DirtyFlagTrigger.Always 每幀). 🪤#1 (決策 7):
// the per-frame correctness invariant — call at the START of every frame, before pulling.
// Miss one live source -> downstream reads stale cache (卡舊畫面). Wired into the -bug teeth.
void bumpLiveSources(ResidentEvalGraph& g);
// Pull a float through the version-chasing cache (eager post-order, one pass — 決策 6): recurse
// Connection inputs first (always walks the cone — cheap), a DERIVED slot adopts the SUM of its
// upstream sourceVersions (multi-input combine: any input changes -> dirty), recompute+cache only
// when dirty (valueVersion != sourceVersion), else return cachedFloat WITHOUT recomputing (省重算).
// Mutates g — the cache is resident state. depth>64 breaks cycles. Automation driver -> 0 (S3 stub).
float pullResidentFloat(ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth = 0);

// Headless RED->GREEN proof: builds a nested library (compound w/ reuse) + the equivalent
// flat library, asserts resident eval matches AND matches the hand-computed value, asserts
// reuse isolation, driver resolve, and the two-clock shape. injectBug pollutes a definition
// so reuse leaks -> the equivalence assertion FAILS (teeth).
int runResidentEvalSelfTest(bool injectBug);

// Headless RED->GREEN proof of the 1b version-chasing cache: a STATIC subgraph recomputes once
// then returns cache (mutating an upstream constant WITHOUT an edit-time bump stays stale —
// proving the short-circuit), an edit-time bump propagates (sum) and forces recompute, and a
// LIVE source (Time) recomputes every frame. injectBug skips bumpLiveSources -> the LIVE node
// stays frozen on frame 1 (卡舊) -> the per-frame assertion FAILS (teeth, spec 🪤#1).
int runResidentCacheSelfTest(bool injectBug);

}  // namespace sw
