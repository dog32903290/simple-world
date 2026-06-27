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

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (cookResidentColorList element type)

struct SwPoint;  // runtime/tixl_point.h (64B host point — PointAccessor element type, below)

namespace sw {

struct ResidentEvalGraph;  // resident_eval_graph.h
struct ResidentEvalCtx;    // resident_eval_graph.h
struct StringState;        // string_op_registry.h (cookStringNodes/cookResidentString cross-frame slot)
struct ContextVarMap;      // stateful_value_ops.h (String ctx-var seam — Set/GetStringVar's stringVars channel)

// value-output-rail Phase 4 — POINT-INTO-FRAME value-emit accessor. The point-buffer twin of
// ResidentEvalCtx (which carries no point buffer to the frame-level emit pass — the WALL named in
// resident_matrix_output_cook.cpp for PointToMatrix). Resolves a resident node's Points-input
// srcNodePath → the cooked Shared point buffer (PointGraph::residentCookedPoints reads p_->outBuf[path],
// ResourceStorageModeShared → contents() is a `const SwPoint*` with ZERO blit), writing the point count
// into `outCount`. Returns nullptr (and leaves outCount 0) when the upstream path produced no buffer this
// frame (off the cooked draw chain, or an empty list) — the emit pass then emits identity, faithful to
// TiXL's NumElements==0 → return (the Slot keeps its prior/default value). The ONE seam between the
// cook-core (PointGraph owns the buffers) and this pure-CPU frame-level emit pass; built once per frame in
// cook_host_values from the live PointGraph (additive — reads finished buffers, NO cook-core touch).
using PointAccessor = std::function<const ::SwPoint*(const std::string& srcNodePath, uint32_t& outCount)>;

// Cook ONE upstream FloatList-producing resident node (FloatsToList, …) into `out`, gathering its
// inputs THROUGH the resident Connection drivers (mirror of the flat cookFloatListNode). Returns false
// when `path` is not a FloatList producer (caller treats it as an empty list). Exposed (defined in
// resident_host_scalar_cook.cpp) so the resident tex-cook own-tex branch (point_graph_resident.cpp) can
// gather the FloatList currency for ValuesToTexture/ValuesToTexture2 — the FloatList twin of the
// in-scope cookResidentGradient/cookResidentCurve gathers (makes the FloatList own-tex family LIVE on
// the production cookResident path, R-2 rule). ALSO the supply side of the FloatList-into-string BRIDGE
// (Sub-seam A): cookResidentString gathers a FloatList input port through this (FloatListToString.Value).
// `state` (optional) = THIS node's per-node CROSS-FRAME state (AmplifyValues's _averagedValues/_lastValues
// /_output, FloatListState). It is the PRODUCTION-path persistence for a stateful FloatList op — but UNLIKE
// colorlist/string there is NO single per-frame cookFloatListNodes pass to thread it (the FloatList rail is
// pull-driven from several sites: ValuesToTexture, host-scalar, cookResidentString). So when `state` is
// nullptr (the default — every existing call site) and the node IS a stateful op, this fn looks up a
// PROCESS-LIFETIME static slot keyed by resident path INTERNALLY (residentFloatListState in the .cpp) +
// a cook-once-per-frame guard keyed by ctx.frameIndex, so the damp advances exactly once per frame from
// whichever consumer pulls first. A stateless op never touches state → byte-identical (no static entry).
// Recursive upstream gathers pass nullptr (upstream producers are stateless; the internal static handles
// any stateful one). A golden may pass an explicit `state` to drive the slot deterministically.
bool cookResidentFloatList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<float>& out, int depth = 0,
                           struct FloatListState* state = nullptr);

// Test-only: clear the PRODUCTION FloatList cross-frame state store (the process-lifetime static keyed by
// resident path). A golden runs several independent trajectories in ONE process; this resets the resident
// accumulator between them (the flat path resets naturally via a fresh PointGraph per case). No production
// caller — frame_cook never resets (state must persist across frames in the running app).
void resetResidentFloatListState();

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

// value-output-rail Phase 3 — MATRIX value cook-emit (resident_matrix_output_cook.cpp). The vec4-LIST
// twin of cookValueOutputNodes: a 4×4 matrix = a 4-element Vector4[] (= TiXL Slot<Vector4[]>, the rows
// _matrix[0..3]), so a matrix VALUE rides the EXISTING extColorOut channel (the Slot<List<Vector4>>
// parallel), keyed by output port index — NOT a new rail. Walks the resident graph; for TransformMatrix
// it resolves the SRT Float inputs (resolveResidentFloatInputs) and writes the 4 transposed rows onto
// extColorOut[outPortIdx]. Mutates g (writes extColorOut, like cookColorListNodes). Pure CPU, no Metal.
// FORK (named) — fork-matrix-as-4-vec4-on-extColorOut: TiXL wires ONE Slot<Vector4[]> (the matrix as a
// 4-element list); sw emits the SAME 4 float4 rows onto the extColorOut vec4-list channel. Faithful in
// VALUE (byte-identical rows), forked only in that the downstream wire-type is the established ColorList
// channel rather than a dedicated Matrix slot. EXTENDS the colorlist channel; cook-core-FREE (additive,
// same frame slot family as cookColorListNodes — NO point_graph recursion/collector touch).
void cookMatrixOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx);

// value-output-rail Phase 4 — POINT-INTO-FRAME value-emit (resident_point_value_output_cook.cpp). The
// pass that lifts PointToMatrix off its deferral (resident_matrix_output_cook.cpp header named
// defer-pointtomatrix-needs-point-into-frame-pass) and wires GetPointDataFromList: both read a single
// POINT from a cooked Shared point buffer host-side (= TiXL pointList.TypedElements[i], PointToMatrix.cs:
// 27 / GetPointDataFromList.cs:40) via `acc` (the PointAccessor, resolving the node's Points-input
// srcNodePath → buffer). For PointToMatrix it builds the SRT matrix of point[0] (pointToMatrixRows) and
// writes the 4 rows onto extColorOut (IDENTICAL channel + math as cookMatrixOutputNodes's TransformMatrix
// path); for GetPointDataFromList it indexes point[ItemIndex.Mod(N)] (Euclidean) and writes Position→
// extOut[0..2], F1(W)→extOut[3], Orientation→extOut[4..7] (the scalar-pack fork, mirror of
// RequestedResolution). `ctx` resolves a WIRED ItemIndex through the frame clocks (a Constant ItemIndex
// ignores it). Runs AFTER pg.cookResident (the point buffers exist), unlike cookMatrixOutputNodes (which
// runs in cook_host_values, BEFORE the point cook — the ordering reason this is a SEPARATE pass). Mutates g
// (writes extOut/extColorOut). Pure CPU, no Metal. Additive — reads finished buffers through `acc`; NO
// point_graph recursion/cook-core touch. When `acc` yields no buffer (off the cooked chain / empty list)
// the op emits identity (PointToMatrix) / leaves extOut 0 (GetPointDataFromList), faithful to TiXL's
// NumElements==0 early-return (the Slot keeps its prior/default value).
void cookPointValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                               const PointAccessor& acc);

// PointToMatrix's 4 output rows from one point (Position/Orientation/Scale) — VERBATIM PointToMatrix.cs:
// 55-95 (pivot=0, rotation=the point's Orientation quat, no shear/invert; Transpose; Row1..4). Defined in
// resident_matrix_output_cook.cpp (the matrix MATH home, golden-verified there); declared here so the
// point-into-frame emit pass reuses the IDENTICAL SRT/transpose/row path on a cooked point.
std::array<simd::float4, 4> pointToMatrixRows(const simd::float3& position, const float orientQ[4],
                                              const simd::float3& scale);

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
// `vars` (String ctx-var seam, sub-seam C) = the host per-frame ContextVarMap, passed ONLY for the top
// producer the cookStringNodes loop cooks (Set/GetStringVar touch vars->stringVars). nullptr for the
// recursive upstream gather (an upstream String producer never reads/writes a var) — byte-identical.
bool cookResidentString(const ResidentEvalGraph& g, const std::string& path,
                        const ResidentEvalCtx& ctx, std::string& out, int depth = 0,
                        std::map<int, std::string>* extraStrOut = nullptr,
                        std::map<int, float>* scalarOut = nullptr, StringState* state = nullptr,
                        ContextVarMap* vars = nullptr);

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
//
// `vars` (String ctx-var seam, sub-seam C) = the host per-frame ContextVarMap whose stringVars channel the
// String-channel ctx-var ops touch. SetStringVar (a String WRITER) writes vars->stringVars[name]=value;
// GetStringVar (a String reader) reads it, else FallbackDefault. cookStringNodes runs ALL String writers
// BEFORE any reader (the writer-first 2-pass — the structural delta vs the old single build-order loop), so
// a within-frame Set→Get rendezvous deterministically regardless of declaration order. nullptr (the default
// — every existing String golden + the recursive gather) → the ctx-var ops see no map (GetStringVar →
// FallbackDefault, SetStringVar → echo-only). A non-ctx-var String op ignores it (byte-identical).
void cookStringNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                     std::map<std::string, StringState>* state = nullptr,
                     ContextVarMap* vars = nullptr);

}  // namespace sw
