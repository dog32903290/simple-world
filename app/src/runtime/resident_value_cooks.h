// runtime/resident_value_cooks — the RESIDENT (production-path) cook PROTOTYPES for the host-side
// value-currency families that ride between value ports: FloatList, ColorList, String, and (Sub-seam A)
// StringList. Extracted out of resident_eval_graph.h to keep that header at-or-below its line-count cap
// (ARCHITECTURE.md rule 4 ratchet) — the declarations are a cohesive group (each is "cook ONE upstream
// list/string producer through the resident drivers" + "per-frame production pass"), and the impls live
// in the matching leaf .cpp files (resident_host_scalar_cook / resident_colorlist_cook /
// resident_string_cook / resident_stringlist_cook).
//
// resident_eval_graph.h includes THIS header at its tail (after defining ResidentEvalGraph /
// ResidentEvalCtx), so every existing caller that includes resident_eval_graph.h still sees these
// prototypes unchanged. We only forward-declare the two ctx types here (the prototypes take them by
// reference/pointer), so this header does NOT include resident_eval_graph.h (no circular include).
#pragma once

#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (cookResidentColorList element type)

namespace sw {

struct ResidentEvalGraph;  // resident_eval_graph.h
struct ResidentEvalCtx;    // resident_eval_graph.h
struct StringState;        // string_op_registry.h (cookStringNodes/cookResidentString cross-frame slot)

// Cook ONE upstream FloatList-producing resident node (FloatsToList, …) into `out`, gathering its
// inputs THROUGH the resident Connection drivers (mirror of the flat cookFloatListNode). Returns false
// when `path` is not a FloatList producer (caller treats it as an empty list). Exposed (defined in
// resident_host_scalar_cook.cpp) so the resident tex-cook own-tex branch (point_graph_resident.cpp) can
// gather the FloatList currency for ValuesToTexture/ValuesToTexture2 — the FloatList twin of the
// in-scope cookResidentGradient/cookResidentCurve gathers (makes the FloatList own-tex family LIVE on
// the production cookResident path, R-2 rule). ALSO the supply side of the FloatList-into-string BRIDGE
// (Sub-seam A): cookResidentString gathers a FloatList input port through this (FloatListToString.Value).
bool cookResidentFloatList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<float>& out, int depth = 0);

// Per-frame PRODUCTION cook for the FloatList→Float BRIDGE (list-routing seam). Walks the resident
// graph, cooks every FloatList host-scalar op (FloatListLength / PickFloatFromList) by gathering its
// upstream FloatList inputs THROUGH the resident Connection drivers, and writes the scalar onto the
// node's extOut[0] — the channel evalResidentFloat reads for !evaluate nodes. The resident twin of the
// flat cookHostScalar branch (which only runs as a flat-cook TERMINAL = golden-only); THIS is the leg
// that lives in the running app (frame_cook.cpp calls it once per frame, same slot as
// cookAudioReactionNodes). StringLength is SKIPPED (its String wire is dropped by the resident flatten;
// resident-bridging it needs the resident string-wire rail first — see resident_host_scalar_cook.cpp).
// Mutates g (writes extOut, like cookAudioReactionNodes). Pure CPU, no Metal.
void cookHostScalarNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx);

// Per-frame PRODUCTION cook for the VALUE-OUTPUT rail (value-output-rail Phase 1): cook-emit ops that
// read the COOK CONTEXT (not their inputs) and emit a multi-component value onto extOut[]. These ops
// have no pure evaluate() (the value comes from ctx, which evaluate cannot see), so they ride the same
// extOut readback channel as DetectBpm/AudioReaction — but cooked HERE, frame-level, from ctx fields.
// Phase 1 handles RequestedResolution (Width/Height/AspectRatio ← ctx.requestedWidth/Height). Walks the
// resident graph; for each matched op writes extOut[0..N-1] in spec output-port order (so a downstream
// Float wire to Result.Width resolves to extOut[0] via evalResidentFloat). Mutates g (writes extOut,
// like cookHostScalarNodes). Pure CPU, no Metal. The "N scalar Float output ports" fork
// (fork-vec-output-as-n-scalar-ports) lives in the NodeSpec; this pass fills the slots.
void cookValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx);

// Cook ONE upstream COLORLIST-producing resident node (ColorsToList, …) into `out` (host color list),
// gathering its inputs THROUGH the resident Connection drivers (mirror of the flat cookColorListNode).
// The vec4 twin of cookResidentFloatList: a "ColorList" input port follows each Connection driver (a
// future combiner); the 4 PARALLEL scalar Float MultiInput component ports (Colors.x/.y/.z/.w) gather
// their wired scalars via evalResidentFloat per channel, then the leaf zips index i across the 4 into
// one float4. Returns false when `path` is not a ColorList producer (caller treats it as empty).
// `state` (optional) = THIS node's per-node CROSS-FRAME accumulator (KeepColors's `_list`). Supplied by
// cookColorListNodes (keyed by resident path, the s_colorListState static — mirror of s_svState) ONLY for
// the top node it cooks; recursive upstream gathers pass null (upstream producers are stateless). A
// stateless op ignores it → byte-identical.
bool cookResidentColorList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<simd::float4>& out, int depth = 0,
                           std::vector<simd::float4>* state = nullptr);

// Per-frame PRODUCTION cook for the COLORLIST currency (vec4-list cook flow). Walks the resident graph,
// cooks every colorlist op (ColorsToList) by gathering its inputs THROUGH the resident Connection
// drivers, and writes the host color list onto the node's extColorOut[outputPortIndex] — the channel a
// downstream resident colorlist consumer (+ the golden) reads. The resident twin of the flat colorlist
// terminal (which only runs as a flat-cook TERMINAL = golden-only); THIS is the leg that lives in the
// running app (frame_cook.cpp calls it once per frame, same slot as cookHostScalarNodes). Mutates g
// (writes extColorOut, like cookHostScalarNodes writes extOut). Pure CPU, no Metal. R-2 rule: the
// colorlist currency is GENUINELY on the production resident path, not flat-only.
//
// `state` = the PER-NODE CROSS-FRAME accumulator store (KeepColors's `_list`), keyed by resident path,
// owned by the caller as a function-local static (frame_cook.cpp's s_colorListState) — the EXACT mirror
// of cookStatefulValueNodes's s_svState (frame_cook.cpp:337). It SURVIVES between frames so KeepColors
// accumulates on the PRODUCTION resident path (R-2: not flat-only). Stateless colorlist ops never touch
// their slot. Pass an empty map for a single-frame caller (a stateless golden) — the slot default-creates.
void cookColorListNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                        std::map<std::string, std::vector<simd::float4>>& state);

// Cook ONE upstream STRINGLIST-producing resident node (SplitString, …) into `out` (host string list),
// gathering its inputs THROUGH the resident Connection drivers (Sub-seam A — the string-rail twin of
// cookResidentColorList over std::string). A "StringList" input port follows each Connection driver
// (primary + extraConns, wire order) and recurses this same fn (a future StringList combiner; today only
// SplitString, which has a String input — gathered via cookResidentString — not a StringList input).
// Returns false when `path` is not a StringList producer (caller treats it as an empty list). StringList
// ops are STATELESS (no KeepColors-style accumulator), so no `state` param. Pure CPU, no Metal.
// USED inline by cookResidentString (JoinStringList gathers its StringList input THROUGH this) — there is
// NO per-frame cookStringListNodes pass: a StringList is only ever consumed by a String producer, which
// the existing cookStringNodes pass already drives, so the StringList is gathered on-demand (mirror of how
// cookResidentColorList's future combiner recursion works), keeping frame_cook.cpp untouched (at cap).
bool cookResidentStringList(const ResidentEvalGraph& g, const std::string& path,
                            const ResidentEvalCtx& ctx, std::vector<std::string>& out, int depth = 0);

// Cook ONE upstream STRING-producing resident node (FloatToString / IntToString / Vec3ToString /
// CombineStrings, …) into `out` (host string), gathering its inputs THROUGH the resident Connection
// drivers (mirror of the flat cookStringNode, point_graph.cpp). The string twin of cookResidentColorList:
//   • each String input port follows the Connection driver (primary + extraConns in WIRE order) and
//     recurses this same fn (the upstream string); an UNWIRED port falls back to n->strInputs[port.id]
//     (the wire-OR-const dual identity — byte-identical to the flat gather);
//   • the op's Float params (FloatToString.Value, …) are resolved via resolveResidentFloatInputs.
//   • (Sub-seam A) a FloatList input port is gathered via cookResidentFloatList (FloatListToString.Value),
//     and a StringList input port via cookResidentStringList (JoinStringList.Input).
// Runs the op's StringCookFn (findStringOp). Returns false when `path` is not a String producer
// (caller treats it as an empty string). String ops are STATELESS — no cross-frame accumulator (simpler
// than colorlist's KeepColors), so there is no `state` parameter. Pure CPU, no Metal.
// MULTI-OUTPUT (Sub-seam B): extraStrOut / scalarOut are the op's EXTRA String / scalar outputs keyed
// by the op's own spec output-port index. They are nullptr for the recursive gather (a downstream
// String input only wants port-0); cookStringNodes passes its per-node sinks so a multi-output op
// (PickStringPart, FilePathParts) fans all outputs in ONE cook. Single-output ops never touch them.
// `state` (optional) = THIS node's per-node CROSS-FRAME slot (HasStringChanged's `_lastString`), supplied
// by cookStringNodes ONLY for the top node it cooks (keyed by resident path in the s_stringState store);
// the RECURSIVE upstream gathers pass nullptr (upstream String producers are stateless). A stateless op
// ignores it → byte-identical. The string twin of cookResidentColorList's `state`.
bool cookResidentString(const ResidentEvalGraph& g, const std::string& path,
                        const ResidentEvalCtx& ctx, std::string& out, int depth = 0,
                        std::map<int, std::string>* extraStrOut = nullptr,
                        std::map<int, float>* scalarOut = nullptr, StringState* state = nullptr);

// Per-frame PRODUCTION cook for the STRING currency (host std::string cook flow = TiXL Slot<string>).
// Walks the resident graph, cooks every String-producer op (findStringOp != null) by gathering its
// inputs THROUGH the resident Connection drivers, and writes the host string onto the node's
// extStrOut[outputPortIndex] — the channel a downstream resident String consumer (StringLength's
// resident leg + the golden) reads. The resident twin of the flat cookStringNode (which only runs as a
// flat-cook TERMINAL = golden-only); THIS is the leg that lives in the running app (frame_cook.cpp calls
// it once per frame, BEFORE cookHostScalarNodes). Mutates g (writes extStrOut, like cookColorListNodes
// writes extColorOut). Pure CPU, no Metal. R-2 rule: the String currency is GENUINELY on the production
// resident path, not flat-only (task_32b5b6e5 — production cooks via resident; flat-only goldens lied).
//
// `state` = the PER-NODE CROSS-FRAME STRING store (HasStringChanged's `_lastString`), keyed by resident
// path, owned by the caller as a function-local static (cook_host_values.cpp's s_stringState) — the EXACT
// mirror of cookColorListNodes's s_colorListState. It SURVIVES between frames so HasStringChanged compares
// against the previous frame on the PRODUCTION resident path (R-2: not flat-only). nullptr (the default)
// for a single-frame / stateless caller (every existing String golden) → no per-node slot is threaded,
// byte-identical to before. A stateful String op cooked with a null store sees no persistence (frame 0).
void cookStringNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                     std::map<std::string, StringState>* state = nullptr);

}  // namespace sw
