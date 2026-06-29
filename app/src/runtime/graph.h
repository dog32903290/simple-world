// Op-type registry (NodeSpec/PortSpec/findSpec) + the legacy flat `Graph` struct.
// STATUS (verified 2026-06-29, prior "remains the editor/save/UI representation until
// the batch-2 production swap" note was STALE — the swap landed incrementally):
//   - LIVE & load-bearing: the NodeSpec/PortSpec/findSpec op registry below. Everything
//     consumes it, INCLUDING the resident engine (resident_eval_graph.* depends on it).
//   - SUPERSEDED for topology: the editor/canvas/save/cook now run on the NESTED model
//     (compound_graph.h Symbol/Child/Connection) projected to a resident eval graph. The
//     live frame loop is frame_cook.cpp `run()` → buildEvalGraph(doc::g_lib()) →
//     pg.cookResident(...); the canvas edits Symbol children directly (editor_ui.cpp:4
//     "no flat Graph, no projection layer").
//   - The flat `Graph` struct here now survives ONLY as per-op golden T1 test legs
//     (hand-built flat graph, the direct-kernel leg) + history; it is NOT the live path.
//     ("R-2 iron rule": a golden that exercises ONLY flat-cook is a production black hole.)
// The compound contract (契約 1, 照 TiXL) also supersedes the old "NOT TiXL's
// Symbol/Instance system" stance. Remaining cleanup (per migration-cost scout): golden
// flat→resident migration, stateful-op state key node-id→path, pin-id tooling.
#pragma once
#include <map>
#include <string>
#include <vector>

// EvaluationContext is defined in runtime/Particle.h (global namespace).
// Forward-declare it here so graph.h does not pull in Particle.h (which would
// conflict with tixl_point.h when both headers end up in the same TU).
struct EvaluationContext;

namespace sw {

// --- Node type definitions (NodeSpec registry, faithful to TiXL ports/params) ---
// How a Float input param is edited in the Inspector. Enum/Bool still store a float in
// Node::params (the enum index / 0|1) — the widget only changes presentation + edit.
// Vec = the head of a vector param: a run of `vecArity` consecutive Float ports
// (ids "<base>.x"/".y"/".z"/".w") drawn as ONE DragFloatN row. Each component is a plain
// Float port (own constant, own evalParam path) so the buffer/save/value-spine model is
// unchanged — a vector is just N scalars wearing one widget. See evalVecN.
enum class Widget { Slider, Enum, Bool, Vec };
struct PortSpec {
  std::string id, name, dataType;  // dataType: "Points" | "ParticleForce" | "Float"
  bool isInput;
  float def = 0.0f, minV = 0.0f, maxV = 1.0f;  // Float input only
  Widget widget = Widget::Slider;              // Inspector affordance (Float input only)
  std::vector<std::string> labels;             // Widget::Enum option labels (index = value)
  bool pinless = false;                        // param-only: editable in Inspector, no canvas pin
  int vecArity = 1;                            // Widget::Vec head: # of components (2/3/4); 1 = scalar
  // MultiInput (批次25 seam, = TiXL MultiInputSlot): this ONE input port accepts N wires; eval
  // expands all connected sources into in[] consecutively (reducer ops like Sum consume in[0..n-1]).
  // Single-input ports leave this false → exactly the prior single-cardinality behaviour.
  bool multiInput = false;
  // String-param sub-seam (context-var YELLOW): a String input port (dataType=="String") carries
  // its default text HERE — the value rail is float-only (def/minV/maxV above), so a String port's
  // default cannot ride on `def`. The Float-only resolution loops all filter dataType=="Float" so a
  // String port is skipped by every value path (zero regression); only the String resolver
  // (resolveResidentStringInputs) reads strDef. LAST member + defaulted → existing positional
  // {...,multiInput,vecArity} aggregate inits at ~hundreds of port rows stay valid unchanged.
  std::string strDef;
  // Required/relevancy (experience-parity, = TiXL InputSlot relevancy / IsRequired): when true, an
  // unconnected input is "missing" — the UI paints the necessity indicator (L-G) and the node is
  // incomplete. Default false = optional (the universal case), so existing positional
  // {...,multiInput,vecArity} inits are unaffected. MUST stay AFTER strDef as the new LAST member —
  // inserting it earlier would shift hundreds of positional aggregate inits across the registry.
  bool required = false;
};
struct NodeSpec {
  std::string type, title;
  std::vector<PortSpec> ports;
  // params retired: original params are now dataType=="Float" && isInput ports.
  // Constants live in Node::params[port.id]. evaluate is used by Task 2+ value nodes.
  // outIdx = which output port (the port index within this spec) is being pulled — lets a
  // node expose several outputs (e.g. AudioReaction's level vs hit); single-output nodes ignore it.
  float (*evaluate)(int outIdx, const float* in, int n, const EvaluationContext& ctx) = nullptr;
  // Namespace/category (experience-parity, = TiXL Symbol.Namespace): groups ops for the search tree
  // (L-B). Additive, default empty → MUST stay AFTER evaluate as the new LAST member: NodeSpec rows
  // are built with positional aggregate-init {type, title, {ports}, evaluate} across the registry,
  // so a mid-struct insert would shift hundreds of them. Default empty = unchanged for non-setters.
  std::string category;
};
const NodeSpec* findSpec(const std::string& type);
std::vector<std::string> specTypes();  // all registered node types (for the Add menu)

// Animation-group resolution (S3 Vec multi-channel). = TiXL Animator: a vector input owns ONE
// inputId with Curve[N] (Animator.AddCurvesForFloatVector, Animator.cs:97-126). Our Vec param is
// N consecutive Float ports (head Widget::Vec, vecArity>=2; components follow positionally), so
// the WHOLE group's curves live under the HEAD port's id and channel k = curve index k. A scalar
// Float is a group of 1 (head = itself, channel 0) — identical to TiXL's float = Curve[1].
// SINGLE SOURCE for the Inspector gesture AND the resident projection (批次7 同源拍板: both
// consult the same grouping, so the isAnimated/driver flip cannot drift).
struct AnimGroup {
  std::string headId;  // the Animator inputId this slot's curves live under
  int channel = 0;     // this slot's curve index within the group
  int arity = 1;       // total channels in the group
};
AnimGroup animGroupForSlot(const NodeSpec& spec, const std::string& slotId);
// Swap the DYNAMIC spec table (compound symbols as operators, 批次 3). Owned by the registry;
// in the LIVE app the single producer is graph_bridge::refreshCompoundSpecs — product code must
// not call this directly (single producer keeps stale-entry reasoning trivial). HEADLESS
// --selftest-* processes may inject isolated test-op specs through it (they never run
// refreshCompoundSpecs, so the invariant holds per process). Built-ins win on clash.
void setDynamicSpecs(std::map<std::string, NodeSpec> specs);

// --- Graph instances (editorGraph) ---
struct Node {
  int id = 0;
  std::string type;
  float x = 0.0f, y = 0.0f;
  std::map<std::string, float> params;  // param id -> value
  // String params (context-var YELLOW string sub-seam): String input ports (dataType=="String")
  // store their text here, parallel to `params` (the float rail). Serialized in toJson/fromJson
  // alongside params. EMPTY map for every node with no String port (the universal case → zero file
  // churn). The ONLY non-float param channel in the value graph; carries e.g. SetFloatVar's
  // VariableName. Absent key = the spec's PortSpec.strDef default.
  std::map<std::string, std::string> strParams;  // param id -> text value
  // Transient (not serialized): outputs for stateful nodes whose value can't come from the
  // pure evaluate() — e.g. AudioReaction, cooked in main from the live spectrum each frame.
  // evalFloat returns outCache[outPortIndex] for such nodes. Index by the node's output port.
  float outCache[3] = {0.0f, 0.0f, 0.0f};
};
struct Connection {
  int id = 0;
  int fromPin = 0, toPin = 0;  // absolute pin ids (see pinId())
};
struct Graph {
  std::vector<Node> nodes;
  std::vector<Connection> connections;
  int nextId = 1;

  Node* node(int id);
  const Node* node(int id) const;
  const Node* firstOfType(const std::string& type) const;
  float param(const std::string& type, const std::string& paramId, float fallback) const;

  // The connection feeding this input pin, or nullptr. Inputs are single-
  // cardinality, so there is at most one. Single source of truth for the
  // "is this input already wired?" question (reconnect / cardinality).
  const Connection* connectionToInput(int inputPin) const;
};

// Stable pin id from a node id + the port index in its spec.
inline int pinId(int nodeId, int portIndex) { return nodeId * 100 + portIndex + 1; }
// Inverse of pinId: recover the owning node id from a pin id. Single source of
// truth for the pin->node mapping (callers must not re-derive (pin-1)/100).
inline int pinNode(int pin) { return (pin - 1) / 100; }

// Default particle graph: RadialPoints -> ParticleSystem <- TurbulenceForce, ParticleSystem -> DrawPoints.
Graph defaultParticleGraph();

// Save/load (crude_json). fromJson returns false on malformed input.
std::string toJson(const Graph& g);
bool fromJson(const std::string& json, Graph& out);

// Disk I/O for project files (.swproj). saveGraphToFile writes toJson(g) to path;
// loadGraphFromFile reads a file and fromJson()s it. Both return false on failure
// (unwritable path / missing file / malformed json) — callers must not mutate
// their live graph until loadGraphFromFile returns true.
bool saveGraphToFile(const std::string& path, const Graph& g);
bool loadGraphFromFile(const std::string& path, Graph& out);

// L-save proof: default graph -> file -> graph, assert identical; AND a malformed
// file must load to false. injectBug perturbs the reloaded graph so the roundtrip
// comparison must FAIL.
int runSaveLoadSelfTest(bool injectBug);

// L6 proof: default graph -> json -> graph, assert identical. injectBug perturbs
// a param after the roundtrip so the comparison must FAIL.
int runGraphRoundtripSelfTest(bool injectBug);

// SourceRegistry (runtime/source_registry.h) holds per-param binding/override state
// + the live-source table; evalParam consults it. Forward-declared so graph.h stays
// light — callers that don't resolve live sources never pull the registry in.
class SourceRegistry;

// --- Value evaluation (Task 2+) ---
// Pull-evaluate the float value produced by outPin, recursing through connections.
// depth > 64 breaks cycles (returns 0). EvaluationContext supplies time/frame.
float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth = 0);
// Resolve a named Float input port on the first node of the given type. Resolution
// order (L5 model): override → binding → constant. When reg != nullptr it is asked
// first — a live override wins, then an explicit LiveSource/Automation binding;
// otherwise (and always when reg == nullptr) the value-spine behavior applies: if the
// input is wired evalFloat the upstream (binding=Connection), else the stored constant
// (Node::params[paramId]) or fallback. reg defaults to nullptr so value-spine callers
// are unchanged.
// Takes the full per-frame EvaluationContext (time/frame/deltaTime) — the
// caller builds it once per frame, like TiXL's Update(EvaluationContext). main can hold
// EvaluationContext now (it arrives via tixl_point.h -> eval_context.h, the S0 split).
float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                const EvaluationContext& ctx, float fallback, const SourceRegistry* reg = nullptr);
// Resolve EVERY Float input port of a SPECIFIC node through the same value spine as evalParam
// (override → binding → wire → stored → spec default), keyed by port id. The cook drivers hand
// this map to ops (PointCookCtx::params, the slice-2b seam) so op bodies never walk the graph —
// and so a Connection into ANY Float param drives it (per-node, reuse-safe; evalParam stays the
// legacy first-of-type read for value-spine callers).
std::map<std::string, float> resolveNodeParams(const Graph& g, const Node& n,
                                               const EvaluationContext& ctx,
                                               const SourceRegistry* reg = nullptr);
// Read a vector param off a SPECIFIC node: its `n` components are Float ports stored as
// params["<base>.x"/".y"/".z"/".w"]. Cook fns read the node they are cooking (c.nodeId),
// NOT first-of-type — generators get instanced many times, so first-of-type would feed the
// 2nd instance the 1st's vector (silent corruption). v1 reads the stored constant (vector
// components are pinless = unwired); when component wiring lands this grows an evalParam
// resolution path and every cook upgrades together. Writes `n` floats into out[]; fallback[]
// is the per-component default. SSOT for "read a vector param" — cooks must not re-derive
// the ".x"/".y" suffixing.
void readVecN(const Node& node, const std::string& base, const float* fallback, int n, float* out);
// Headless RED->GREEN proof for the value-cook engine. injectBug flips the result.
int runValueCookSelfTest(bool injectBug);
// L5 resolution proof: constant / connection / live-source(read via self) / override-
// beats-binding / re-enable-falls-back / explicit-binding-beats-wire / one-binding-
// replaces. injectBug flips one assertion so the test must FAIL.
int runResolveSelfTest(bool injectBug);
// Proof that wiring AudioReaction.level -> ParticleSystem.Speed resolves (no hang) and
// that a raw level wired to Speed reads 0 when silent (the "particles freeze" gotcha).
int runAudioNodeSelfTest(bool injectBug);
// Proof for math value ops: 8 original (批次12 lane F: Add/Sub/Div/Clamp/Remap/Abs/Floor/Lerp)
// + 7 overnight batch (Sqrt/Pow/Modulo/Ceil/SmoothStep/Log/Cos).
// Each op: ≥1 typical value + ≥1 boundary; injectBug swaps Div inputs → suite fails.
int runMathOpsSelfTest(bool injectBug);

}  // namespace sw
