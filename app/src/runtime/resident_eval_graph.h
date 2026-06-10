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

// One inlined operator instance. `path` is the path-qualified id (join of the child-id
// chain, e.g. "5/2/1") — unique AND frame-stable, so cache (slice 4) and the per-node
// buffer map (slice 2) key off it. opType = the atomic symbol id = the operator type.
struct ResidentNode {
  std::string path;
  std::string opType;
  std::vector<ResidentInput> inputs;
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

// Headless RED->GREEN proof: builds a nested library (compound w/ reuse) + the equivalent
// flat library, asserts resident eval matches AND matches the hand-computed value, asserts
// reuse isolation, driver resolve, and the two-clock shape. injectBug pollutes a definition
// so reuse leaks -> the equivalence assertion FAILS (teeth).
int runResidentEvalSelfTest(bool injectBug);

}  // namespace sw
