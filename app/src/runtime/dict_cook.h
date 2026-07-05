// runtime/dict_cook — the FLAT + RESIDENT cook entrypoints for the Dict<float> PRODUCER rail
// (dict-currency seam). A Dict producer (BuildFloatDict, and later the device-io OscInput family) is
// cooked here into a SwFloatDict, gathering its String key input (wire-OR-const) + Float value MultiInput
// (evalFloat / evalResidentFloat over the wires). These are FREE functions (not woven into every
// point_graph.cpp cook slot) because the ONLY consumer of Dict currency is the host-scalar rail (the
// Select*FromDict family) — the host-scalar Dict gather calls straight into these, mirroring how the
// list-currency bridge (commit 66a1829) calls cookResidentFloatList as a free function.
//
// Depth-cap + null-return contract mirror cookResidentFloatList: a `path`/`id` that is NOT a Dict
// producer returns false (caller treats it as an empty dict → the consumer's TryGetValue misses → 0,
// faithful to TiXL's null-dict path).
#pragma once

#include <string>

#include "runtime/dict_op_registry.h"  // SwFloatDict

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

struct Graph;              // runtime/graph.h
struct ResidentEvalGraph;  // runtime/resident_eval_graph.h
struct ResidentEvalCtx;    // runtime/resident_eval_graph.h

// FLAT leg: cook the Dict producer at node `id` into `out`. Gathers the "Keys" String input (wire-OR-
// const via the node's strParams / spec strDef) + the "Values" Float MultiInput (each wire evalFloat'd,
// wire-declaration order). Returns false if `id` is not a Dict producer (out left empty).
bool cookFlatDict(const Graph& g, const EvaluationContext& ctx, int id, SwFloatDict& out);

// RESIDENT leg (production): cook the Dict producer at `path` into `out`. Gathers "Keys" from the
// resident node's strInputs (resolved constant — String wires do not survive flatten) + "Values" from
// the Float MultiInput Connection drivers (primary + extraConns) via evalResidentFloat. Returns false
// if `path` is not a Dict producer (out left empty).
bool cookResidentDict(const ResidentEvalGraph& g, const std::string& path,
                      const ResidentEvalCtx& ctx, SwFloatDict& out);

}  // namespace sw
