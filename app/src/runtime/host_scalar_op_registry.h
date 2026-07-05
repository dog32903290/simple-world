// runtime/host_scalar_op_registry — self-registration seam for HOST-SCALAR CONSUMER ops + the
// FloatList→Float / String→Float BRIDGE (the list-routing seam, SEAM_COMPLETION_PLAN §2 stage 1).
//
// A "host-scalar op" is a node that CONSUMES a host currency (a FloatList and/or a String) and
// PRODUCES a single host FLOAT (e.g. FloatListLength, PickFloatFromList, StringLength). It is NOT a
// FloatList producer (its output is a scalar, not a list) and NOT a pure value op (its INPUT is a
// host list/string that evalFloat — a pure float recursion — cannot see). So it has no
// NodeSpec::evaluate; its real cook is the flat driver's host-scalar branch (point_graph.cpp), which
// gathers the upstream host inputs, runs the op's cook fn to compute the scalar, and stores the
// result BOTH in Impl::floatListBuf (1-element host list — the existing transport, kept so
// debugCookedFloatList readback still works) AND in Node::outCache[outIdx] (the BRIDGE channel that
// evalFloat reads — see below).
//
// THE BRIDGE (the architecture-defining part of this seam):
//   evalFloat (graph.cpp) is a pure float recursion that returns 0 for any node whose evaluate ==
//   nullptr — UNLESS that node is a STATEFUL externally-cooked node, in which case it reads
//   Node::outCache[outputPortIndex] (the AudioReaction escape hatch). This registry GENERALIZES that
//   escape hatch from the hard-coded "type == AudioReaction" to "isHostScalarOp(type)" — so a
//   host-scalar op's cooked scalar, written into outCache, flows downstream into a Float INPUT port
//   exactly like AudioReaction's per-frame Level. fork-evalfloat-stateful-generalized.
//
// fork-floatlist-scalar-via-outcache: the scalar rides Node::outCache (NOT floatListBuf — evalFloat
//   cannot reach the PointGraph-private floatListBuf). floatListBuf transport is RETAINED in parallel.
//
// Pattern cloned from floatlist_op_registry.h / string_op_registry.h: adding a host-scalar op = add
// ONE leaf .cpp ending with a file-scope `HostScalarOp` registrar. The registrar feeds THREE sinks:
//   (1) hostScalarSpecSink()  — its NodeSpec (so it appears in the Add menu / findSpec),
//   (2) hostScalarCookFns()   — its HostScalarCookFn (so the cook driver's host-scalar branch runs it),
//   (3) hostScalarTypes()     — its type NAME (so isHostScalarOp(type) — the eval-side escape-hatch
//                               predicate — recognises it WITHOUT the driver needing to hard-code names).
//
// Init-order safety (identical to the floatlist / string / value-op sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any
// LIVE sink read (node_registry's findSpec/specTypes + evalFloat's isHostScalarOp read live).
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position); findSpec is keyed by
// type name, the cook by type name, isHostScalarOp by a set — none depends on spec position.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

struct SwFloatDict;  // dict_op_registry.h (the Dict<float> host currency the Select*FromDict ops consume)

// Everything a host-scalar op gets to cook one node this frame. The driver GATHERS the upstream host
// inputs by the op's spec port dataTypes BEFORE calling the op:
//   • each "FloatList" input port → one already-cooked upstream list (cookFloatListNode), in spec port
//     order with MultiInput ports expanded into wire-declaration order (an unwired port → empty list);
//   • each "String" input port → one already-resolved upstream string (WIRE-OR-CONST), in spec port
//     order (same gather as the String rail);
//   • each "Dict" input port → one cooked upstream SwFloatDict (cookFlatDict / cookResidentDict), in spec
//     port order (single-wire; the Select*FromDict family's DictionaryInput). nullptr if unwired.
// The op reads inputLists / inputStrings / inputDicts + its resolved Float/String params, computes ONE
// scalar (or up to 3 components for a Vector2/Vector3 output) and writes them to *output / *outY / *outZ.
// The driver then mirrors the components into Node::outCache[0..2] (the bridge evalFloat reads) AND
// *output into floatListBuf (1-elem, the legacy transport).
//
//   inputLists   : cooked upstream FloatList inputs (borrowed, driver-owned; never retained).
//   inputStrings : resolved upstream String inputs (borrowed, driver-owned; never retained).
//   inputDicts   : cooked upstream Dict inputs (one per "Dict" input port, spec order; nullptr = unwired,
//                  faithful to TiXL's null-dict → TryGetValue miss → 0). Borrowed; never retained.
//   params       : RESOLVED Float params of THIS node (same value spine as FloatListCookCtx::params),
//                  read via hostScalarParam (e.g. PickFloatFromList.Index).
//   strParams    : resolved STRING params of THIS node keyed by port id (the Select*FromDict "Select" key
//                  is a String param the Float value spine cannot carry). Borrowed; may be null.
//   output/outY/outZ : the op writes component 0 to *output (ALWAYS), and — for a Vector2/Vector3 output —
//                  components 1/2 to *outY / *outZ. `components` tells the driver how many outCache slots
//                  to mirror (1 for a plain Float/bool op — the default; 2 for Vec2; 3 for Vec3). A scalar
//                  op leaves outY/outZ untouched and components at 1 → byte-identical to before this seam.
struct HostScalarCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  const std::vector<std::vector<float>>* inputLists = nullptr;  // cooked FloatList inputs (spec order)
  const std::vector<std::string>* inputStrings = nullptr;       // resolved String inputs (spec order)
  const std::vector<const SwFloatDict*>* inputDicts = nullptr;  // cooked Dict inputs (spec order; null=unwired)
  const std::map<std::string, float>* params = nullptr;         // resolved Float params of THIS node
  const std::map<std::string, std::string>* strParams = nullptr;  // resolved String params of THIS node
  float* output = nullptr;                                      // driver-owned component-0 slot; op writes it
  float* outY = nullptr;                                        // component 1 (Vector2/Vector3 output)
  float* outZ = nullptr;                                        // component 2 (Vector3 output)
  int components = 1;                                           // # of outCache components the op wrote (1/2/3)
  // MULTI-OUTPUT sink (mirror of StringCookCtx::scalarOutputs, string_op_registry.h): a host-scalar op with
  // MORE THAN ONE Float output (AnalyzeFloatList = Min port0 / Max port1 / AverageMean port2 / AllValid
  // port3) writes its EXTRA outputs here keyed by OUTPUT-PORT INDEX (port 0 also allowed here, but the
  // single-output incumbents write *output = port 0 → the driver mirrors *output to outCache[0], and
  // scalarOutputs stays empty → byte-identical). The driver distributes scalarOutputs[k] → Node::outCache[k]
  // (flat) / ResidentNode::extOut[k] (resident), the SAME channel evalFloat/evalResidentFloat read. null for
  // a single-output op (the driver may not supply it — e.g. a hand-built golden ctx) → the op falls back to
  // *output only. (Distinct from outY/outZ: those are the Vec2/Vec3 COMPONENT channel — same port, 3
  // components; scalarOutputs is the MULTI-PORT channel — different output ports, one float each.)
  std::map<int, float>* scalarOutputs = nullptr;
};

// A host-scalar op: read inputLists / inputStrings (+ resolved Float params) → write *output (one
// float) AND/OR scalarOutputs[portIdx] (multi-output). ONE fn (a scalar self-sizes); the driver owns
// transport + the outCache bridge mirror.
using HostScalarCookFn = void (*)(HostScalarCookCtx&);

// Read a Float param from a HostScalarCookCtx's RESOLVED map (mirror of floatListParam); `def` when
// the driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float hostScalarParam(const std::map<std::string, float>* params, const char* id, float def);

// Read a STRING param from a HostScalarCookCtx's strParams map (the Select*FromDict "Select" key), keyed
// by port id; `def` when no map / no entry.
std::string hostScalarStrParam(const std::map<std::string, std::string>* strParams, const char* id,
                               const std::string& def);

// --- the three sinks every host-scalar-op leaf registrar feeds ---
std::vector<NodeSpec>& hostScalarSpecSink();                  // NodeSpecs (node_registry reads live)
std::map<std::string, HostScalarCookFn>& hostScalarCookFns();  // type-name -> cook fn
std::set<std::string>& hostScalarTypes();                    // type names (isHostScalarOp predicate set)

// Lookup the cook fn for a type (nullptr if not a host-scalar op). Used by the cook driver's branch.
const HostScalarCookFn* findHostScalarOp(const std::string& type);

// THE EVAL-SIDE PREDICATE (the bridge's other half): is `type` a host-scalar op whose Float output
// rides Node::outCache (not a pure evaluate)? evalFloat / evalResidentFloat call this to generalise
// the AudioReaction escape hatch. A type NOT in the set is untouched (zero regression — see graph.cpp).
// StringLength is ADDED to this set by its own leaf even though its cook lives in a dedicated driver
// branch (its String gather predates this registry) — the set decouples "reads outCache" from "which
// branch cooks it".
bool isHostScalarOp(const std::string& type);

// Register a type name as a host-scalar op for the eval-side predicate ONLY (no cook fn, no spec).
// Used by StringLength's leaf: its NodeSpec + cook stay on the String rail, but its Float output now
// rides outCache, so evalFloat must recognise it. The full HostScalarOp registrar below calls this too.
void registerHostScalarType(const std::string& type);

// Test-only injection seam (goldens): when set, a host-scalar op's cook corrupts its REAL output
// (writes a sentinel / drops the scalar) so the golden's RED case fires on the actual cook path (NOT
// by flipping the expected value). Off in production. A leaf reads it at the end of its cook.
bool& hostScalarInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each host-scalar-op leaf.
//   HostScalarOp(spec, cookFn);  // feeds all three sinks (spec, cook, type set)
struct HostScalarOp {
  HostScalarOp(NodeSpec spec, HostScalarCookFn cook);
};

}  // namespace sw
