// runtime/stateful_value_op_registry — self-registration seam for STATEFUL value ops (Damp/Spring/
// Ease/...). The data-driven sink that REPLACES the old hardcoded kStatefulValueOps[] manifest.
//
// These ops have NO pure evaluate() — their output depends on PRIOR frames (per-instance memory
// across cooks), so frame_cook (cookStatefulValueNodes) drives them once per frame via the public
// API (isStatefulValueOp / cookStatefulValueOp, declared in stateful_value_ops.h). That public API
// now reads THIS sink instead of a central array.
//
// Pattern cloned VERBATIM from string_op_registry.h / value_op_registry.h: adding a stateful op =
// drop ONE leaf .cpp ending with a file-scope `static const StatefulOp _reg_<name>{type, stepFn}`.
// The registrar appends to the single sink (statefulOpSink) read live by findStatefulOp.
//
// Init-order safety (identical to the string/value/floatlist sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any
// LIVE sink read (the public cook/predicate API reads the sink live, never snapshots).
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Behavior-irrelevant: findStatefulOp / isStatefulValueOp
// / cookStatefulValueOp are all keyed by TYPE NAME — none depends on sink position. (The old
// kStatefulValueOps[] array order was likewise never read positionally.)
#pragma once

#include <map>
#include <string>
#include <vector>

namespace sw {

struct StatefulValueState;  // stateful_value_ops.h
struct TransportSnapshot;   // stateful_value_ops.h
struct ContextVarMap;       // stateful_value_ops.h

// A stateful value op = a TYPE NAME + its per-frame step fn. The step fn signature is UNCHANGED from
// the old kStatefulValueOps[] manifest: the ~30 non-var ops ignore the trailing (TransportSnapshot,
// ContextVarMap*, varName); Set*/Get*Var read them; StopWatch reads the TransportSnapshot. ONE
// uniform signature keeps ONE dispatch table.
struct StatefulOp {
  const char* type;
  void (*step)(const std::map<std::string, float>&, float, float, StatefulValueState&, float[8],
               const TransportSnapshot&, ContextVarMap*, const std::string&);
};

// The single self-registration sink (Meyers singleton, init-order safe). Each leaf's file-scope
// StatefulOp registrar appends here; the public cook/predicate API reads it live.
std::vector<StatefulOp>& statefulOpSink();

// Lookup the StatefulOp for a type (nullptr if not a stateful value op). Used by isStatefulValueOp /
// cookStatefulValueOp (stateful_value_ops.cpp) — the same lookup the old findStatefulOp did over the
// array, now over the sink.
const StatefulOp* findStatefulOp(const std::string& type);

// RAII registrar: declare one file-scope static of this type at the end of each stateful-op leaf.
//   StatefulOp _reg_<name>{"TypeName", stepFn};  // pushes into statefulOpSink()
struct StatefulOpReg {
  StatefulOpReg(const char* type,
                void (*step)(const std::map<std::string, float>&, float, float, StatefulValueState&,
                             float[8], const TransportSnapshot&, ContextVarMap*, const std::string&));
};

}  // namespace sw
