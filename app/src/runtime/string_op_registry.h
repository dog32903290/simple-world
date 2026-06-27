// runtime/string_op_registry — self-registration seam for STRING ops (the 6th cook flow).
//
// The String channel is a HOST-side value currency — TiXL's Slot<string> (a CPU string that rides
// between ops), NOT a GPU buffer. It is the value-graph parallel of "Points"/"Texture2D"/"Mesh"/
// "FloatList": a producer port (dataType=="String" output) hands a std::string to a consumer port
// (dataType=="String" input). The string lives in host memory the whole way; it never touches the
// 16-byte GPU EvaluationContext. This is the EXACT mirror of the FloatList host-rail (Cut 91),
// substituting std::string for std::vector<float> as the currency.
//
// Pattern cloned VERBATIM from floatlist_op_registry.h (the 5th cook flow): adding a string op =
// add ONE leaf .cpp ending with a file-scope `StringOp` registrar. The registrar feeds two sinks:
//   (1) stringSpecSink()  — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) stringCookFns()   — its StringCookFn (so the cook driver's cookStringNode runs it).
//
// Init-order safety (identical to the floatlist / mesh / value-op sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any
// LIVE sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
//
// WHY NOT evalString: the value-eval engine (graph.cpp evalFloat) returns ONE float per pull — a
// String output cannot ride that path (NodeSpec::evaluate returns float). Rather than thread a
// parallel string-eval through the value-eval CORE (high-risk surgery), the String currency rides
// the SAME flat-cook driver rail as FloatList: a separate cook flow keyed by output dataType.
// fork-string-rail-vs-resident-engine: like FloatList, this seam only walks the flat Graph +
// PointGraph::cook path; it does NOT enter resident_eval_graph (same current scope as FloatList).
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position); findSpec is keyed by
// type name, the cook by type name — neither depends on spec position.
#pragma once

#include <cstdint>
#include <map>
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

struct ContextVarMap;  // stateful_value_ops.h (String ctx-var seam — Set/GetStringVar's stringVars channel)

// Per-node CROSS-FRAME STRING state (the string twin of the colorlist KeepColors accumulator). A
// stateless string op never touches it (StringCookCtx::state stays nullptr → byte-identical for every
// incumbent String op). The FIRST consumer is HasStringChanged (TiXL string/logic/HasStringChanged.cs),
// whose `_lastString` field persists between frames: it compares the current input against `lastString`,
// emits the bool delta, then stores the current string back. ONE struct (not one map per field) holds
// every cross-frame slot a stateful String op might need, so the rail carries a SINGLE
// std::map<path,StringState> store (mirror of ColorListCookCtx's single state slot):
//   lastString — HasStringChanged's `_lastString` (prev-frame string; default "" → frame-0 any non-empty
//                input reads as "changed", the sw init convention).
//   buffer     — scratch accumulator for a future StringBuilder-style stateful op (unused today).
//   index      — cursor for a future cycling/sequence stateful op (unused today).
//   rngState   — per-node RNG seed for a future stateful pick/shuffle op (unused today).
struct StringState {
  bool primed = false;  // false on first frame (no prior value); TiXL's _lastString=null sentinel
  std::string lastString;
  std::string buffer;
  int index = 0;
  uint32_t rngState = 0;
  // lastUpdateTime — BuildRandomString's `_lastUpdateTime` (BuildRandomString.cs:20). The DEBOUNCE
  // anchor: the op skips its whole Update when |LocalFxTime - lastUpdateTime| < 0.001 (so on a paused
  // timeline the buffer stops evolving). Default -1 (not 0) so the FIRST frame at LocalFxTime==0 still
  // runs (|0 - (-1)| = 1 ≥ 0.001) instead of being debounced against a coincidental 0. fork-localfxtime-
  // bars-vs-secs: sw LocalFxTime is BARS, TiXL's is SECS — the 0.001 threshold's meaning differs (named
  // in the leaf). A stateless string op never touches this (byte-identical for every incumbent).
  double lastUpdateTime = -1.0;
};

// Everything a string op gets to cook one node this frame. Mirrors FloatListCookCtx
// (floatlist_op_registry.h) but the currency is a HOST std::string, not a host float list — so
// there is still NO pre-sizing (a string self-sizes) and NO Metal allocation. The dev/lib/queue
// refs ride along for symmetry with the sibling ctxs; a pure value op (StringLength/FloatToString/
// CombineStrings) ignores them.
//
//   inputStrings : the already-cooked upstream String inputs, in spec port order with MultiInput
//                  ports expanded into wire-declaration order. CRUCIAL DUAL IDENTITY (the new
//                  fork): a String input port that is WIRED contributes its upstream cooked string;
//                  a String input port that is UNWIRED contributes its strDef CONST (the spec's
//                  PortSpec.strDef, or Node::strParams[id] if the node carries a stored override).
//                  So EVERY String input port yields exactly one entry (a MultiInput String port
//                  yields one entry PER wire, and contributes NOTHING when unwired — faithful to
//                  GetCollectedTypedInputs, same as the FloatList scalar MultiInput gather).
//   output       : THIS node's host string. The cook driver owns it (in Impl::stringBuf, keyed by
//                  flatKey(id)); the op WRITES into *output (assign) — it does not allocate it.
//   params       : RESOLVED Float params of THIS node (same value spine as FloatListCookCtx::params)
//                  — the cook driver resolves every Float input port and hands the result here
//                  (FloatToString.Value reads this).
struct StringCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  // Cooked upstream String inputs (one entry per String input port — or per WIRE for a MultiInput
  // String port — in spec port order with MultiInput expanded into wire-declaration order; an
  // unwired single String port contributes its strDef const). Borrowed (driver-owned); never retained.
  const std::vector<std::string>* inputStrings = nullptr;
  // --- Sub-seam A list inputs (additive; nullptr for every incumbent string op). ---
  // Cooked upstream FloatList inputs (one entry per WIRED FloatList input port, spec port order). The
  // FloatList-into-string BRIDGE: FloatListToString reads inputFloatLists[0] (its Value). The driver
  // gathers these via the EXISTING FloatList currency (flat cookFloatListNode / resident
  // cookResidentFloatList) — no new channel, just the existing FloatList gather wired into this ctx.
  // An UNWIRED FloatList port contributes no entry (→ empty list → "" per the .cs null/empty guard).
  const std::vector<std::vector<float>>* inputFloatLists = nullptr;
  // Cooked upstream StringList inputs (one entry per WIRED StringList input port — or per WIRE for a
  // MultiInput StringList port — in spec port order, wire-declaration order). The NEW StringList
  // currency (mirror of inputStrings widened to a list-of-lists, the string twin of ColorList's
  // inputLists): JoinStringList reads inputStringLists[0] (its Input list) and joins it. An UNWIRED
  // StringList port contributes no entry (→ empty list → "" per the .cs empty-list guard).
  const std::vector<std::vector<std::string>>* inputStringLists = nullptr;
  // Driver-owned output string. The op writes via *output = ...; never allocates/frees it.
  // This is ALWAYS the MAIN String output = port 0 (the channel a downstream String consumer reads,
  // and the one the recursive gather follows). A single-output op writes ONLY this.
  std::string* output = nullptr;
  // Sub-seam A: OPTIONAL driver-owned StringList output, for a (future) String op whose MAIN output is a
  // host string LIST rather than a single string — the contract slot mirroring `output` for the StringList
  // currency. nullptr today (FloatListToString / JoinStringList both produce a single String → write
  // *output; the dedicated StringList PRODUCER, SplitString, rides its own StringListCookCtx, not this).
  // Present so the ctx is complete the day a String-rail op also emits a list (mirror of extraStrOutputs).
  std::vector<std::string>* listOutput = nullptr;
  // RESOLVED Float params of THIS node (mirror of FloatListCookCtx::params); read via stringFloatParam.
  const std::map<std::string, float>* params = nullptr;

  // --- MULTI-OUTPUT sinks (Sub-seam B; additive). A single-output op IGNORES both (they stay nullptr
  // when the driver invokes a 1-output op, and a multi-output op only fills them when present). A
  // multi-output op writes its MAIN String to *output (port 0) and its EXTRA outputs here, KEYED BY THE
  // OP'S OWN SPEC OUTPUT-PORT INDEX (so the producer loop can fan each output onto the right channel:
  // extra strings → ResidentNode::extStrOut[portIdx] / flat stringBuf[":"portIdx], scalars →
  // ResidentNode::extOut[portIdx] / flat outCache[portIdx]). The op only writes the keys it owns.
  //   extraStrOutputs : ADDITIONAL String outputs (port>0 String ports), keyed by spec output-port idx.
  //   scalarOutputs   : Int/bool outputs DISSOLVED to float (fork-int-bool-dissolve-to-float), keyed by
  //                     spec output-port idx. (FilePathParts.FileExists bool → 0.0/1.0;
  //                     PickStringPart.TotalCount int → (float)count.)
  std::map<int, std::string>* extraStrOutputs = nullptr;
  std::map<int, float>* scalarOutputs = nullptr;

  // Per-node CROSS-FRAME state slot (HasStringChanged's `_lastString`): the driver owns it and threads
  // the same per-path slot every frame so a stateful String op accumulates across frames — the EXACT
  // mirror of ColorListCookCtx::state. nullptr for a STATELESS op (every incumbent String op ignores it,
  // so this is byte-identical for them) AND for a hand-built ctx with no driver-owned state (the op then
  // sees no persistence — faithful to a single frame-0 cook). Flat: &Impl::stringState[flatKey(id)];
  // resident (production): &s_stringState[path] (frame_cook's cookHostValueNodes), threaded ONLY into the
  // top producer the loop cooks; recursive upstream gathers pass nullptr (upstream producers are stateless).
  StringState* state = nullptr;

  // String ctx-var seam (sub-seam C): the host per-frame ContextVarMap whose stringVars channel the String-
  // channel ctx-var ops touch (= TiXL context.StringVariables, a typed Dictionary<string,string>). SetStringVar
  // writes vars->stringVars[name]=value; GetStringVar reads it, else FallbackDefault. Threaded ONLY by the
  // cookStringNodes producer loop's writer-first 2-pass (the recursive upstream gather passes nullptr — an
  // upstream String producer never reads/writes a var). nullptr for every NON-ctx-var String op (byte-identical)
  // and for a hand-built ctx with no map (GetStringVar → FallbackDefault, SetStringVar → echo-only).
  ContextVarMap* ctxVars = nullptr;
};

// A string op: read inputStrings (+ resolved Float params) → write *output. ONE fn (like a floatlist
// op, unlike a mesh op's count+cook pair) because a host string self-sizes — driver never pre-allocs.
using StringCookFn = void (*)(StringCookCtx&);

// Read a Float param from a StringCookCtx's RESOLVED map (mirror of floatListParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float stringFloatParam(const std::map<std::string, float>* params, const char* id, float def);

// --- the two sinks every string-op leaf registrar feeds ---
std::vector<NodeSpec>& stringSpecSink();             // NodeSpecs (node_registry reads live)
std::map<std::string, StringCookFn>& stringCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a string op). Used by the cook driver's dispatch.
const StringCookFn* findStringOp(const std::string& type);

// Test-only injection seam (goldens): when set, a string op's cook corrupts its REAL output (e.g.
// drops the last character / last gathered input) so the golden's RED case fires on the actual cook
// path (NOT by flipping the expected value). Off in production. A leaf reads it at the end of its cook.
bool& stringInjectBug();

// String ctx-var seam (sub-seam C) ORDERING TEETH hook (the --selftest-stringctxvar -bug leg). When set,
// cookStringNodes COLLAPSES its writer-first 2-pass into a single build-order loop → a GetStringVar declared
// BEFORE its SetStringVar writer reads the empty-map fallback instead of the set value (proving the 2-pass is
// load-bearing). Off in production. NOT a per-frame flag — a sticky module switch the golden clears back.
bool& stringCtxVarOrderBug();

// RAII registrar: declare one file-scope static of this type at the end of each string-op leaf.
//   StringOp(spec, cookFn);  // pushes spec into stringSpecSink() and cook into stringCookFns()
struct StringOp {
  StringOp(NodeSpec spec, StringCookFn cook);
};

}  // namespace sw
