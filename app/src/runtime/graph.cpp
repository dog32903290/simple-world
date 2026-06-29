#include "runtime/graph.h"
#include "runtime/Particle.h"  // full EvaluationContext definition (forward-decl'd in graph.h)
#include "runtime/host_scalar_op_registry.h"  // isHostScalarOp — the FloatList→Float bridge eval-side predicate
#include "runtime/point_ops_setvarcmd.h"  // S3b: liveCtxVars / liveGetVar / isValueRailContextVarReader
#include "runtime/source_registry.h"  // BindingKind/LiveSource/SourceRegistry (L5 resolution)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "crude_json.h"

namespace sw {
Node* Graph::node(int id) {
  for (auto& n : nodes)
    if (n.id == id) return &n;
  return nullptr;
}
const Node* Graph::node(int id) const {
  for (const auto& n : nodes)
    if (n.id == id) return &n;
  return nullptr;
}
const Node* Graph::firstOfType(const std::string& type) const {
  for (const auto& n : nodes)
    if (n.type == type) return &n;
  return nullptr;
}
const Connection* Graph::connectionToInput(int inputPin) const {
  for (const Connection& c : connections)
    if (c.toPin == inputPin) return &c;
  return nullptr;
}
float Graph::param(const std::string& type, const std::string& paramId, float fallback) const {
  const Node* n = firstOfType(type);
  if (!n) return fallback;
  auto it = n->params.find(paramId);
  return it == n->params.end() ? fallback : it->second;
}

namespace {
Node makeNode(int id, const char* type, float x, float y) {
  Node n;
  n.id = id;
  n.type = type;
  n.x = x;
  n.y = y;
  if (const NodeSpec* s = findSpec(type))
    for (const auto& p : s->ports) {
      if (p.isInput && p.dataType == "Float") n.params[p.id] = p.def;  // seed from spec defaults
      else if (p.isInput && p.dataType == "String") n.strParams[p.id] = p.strDef;  // String sub-seam
    }
  return n;
}
}  // namespace

Graph defaultParticleGraph() {
  Graph g;
  g.nodes.push_back(makeNode(1, "RadialPoints", 20, 30));
  g.nodes.push_back(makeNode(2, "ParticleSystem", 260, 90));
  g.nodes.push_back(makeNode(6, "TurbulenceForce", 20, 160));
  // DrawPoints now draws faithful PointSize-sized quad sprites (TiXL DrawPoints.hlsl); the NODE default
  // (DrawPoints.t3) is 0.1 ≈ 1px. This curated SCENE picks a visible PointSize so the default particle
  // preview reads on screen (a scene param, exactly as 柏為 would dial it — the .t3 node default is
  // untouched). [Y] 觀感: the default preview点 is a sized sprite quad now, not the old 4px dead point.
  { Node dp = makeNode(7, "DrawPoints", 560, 50); dp.params["PointSize"] = 1.5f; g.nodes.push_back(dp); }
  // AudioReaction present but UNWIRED: 柏為 drags level -> a knob (e.g. via Multiply) to
  // map sound to it. No hidden auto-drive — the wire is his to make and to see.
  g.nodes.push_back(makeNode(8, "AudioReaction", 20, 300));
  // RadialPoints.points(1,0) -> ParticleSystem.emit(2,0)
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  // TurbulenceForce.force(6,0) -> ParticleSystem.forces(2,1)
  g.connections.push_back({102, pinId(6, 0), pinId(2, 1)});
  // ParticleSystem.result(2,2) -> DrawPoints.points(7,0)
  g.connections.push_back({103, pinId(2, 2), pinId(7, 0)});
  g.nextId = 104;
  return g;
}

std::string toJson(const Graph& g) {
  crude_json::object root;
  root["nextId"] = (crude_json::number)g.nextId;
  crude_json::array nodes;
  for (const auto& n : g.nodes) {
    crude_json::object o;
    o["id"] = (crude_json::number)n.id;
    o["type"] = n.type;
    o["x"] = (crude_json::number)n.x;
    o["y"] = (crude_json::number)n.y;
    crude_json::object params;
    for (const auto& kv : n.params) params[kv.first] = (crude_json::number)kv.second;
    o["params"] = crude_json::value(params);
    // String sub-seam: serialize strParams only when non-empty (zero churn for float-only nodes).
    if (!n.strParams.empty()) {
      crude_json::object strParams;
      for (const auto& kv : n.strParams) strParams[kv.first] = (crude_json::string)kv.second;
      o["strParams"] = crude_json::value(strParams);
    }
    nodes.push_back(crude_json::value(o));
  }
  root["nodes"] = crude_json::value(nodes);
  crude_json::array conns;
  for (const auto& c : g.connections) {
    crude_json::object o;
    o["id"] = (crude_json::number)c.id;
    o["fromPin"] = (crude_json::number)c.fromPin;
    o["toPin"] = (crude_json::number)c.toPin;
    conns.push_back(crude_json::value(o));
  }
  root["connections"] = crude_json::value(conns);
  return crude_json::value(root).dump(2);
}

bool fromJson(const std::string& json, Graph& out) {
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) return false;
  out = Graph{};
  out.nextId = (int)v["nextId"].get<crude_json::number>();
  crude_json::value& nodes = v["nodes"];
  if (!nodes.is_array()) return false;
  for (auto& nv : nodes.get<crude_json::array>()) {
    Node n;
    n.id = (int)nv["id"].get<crude_json::number>();
    n.type = nv["type"].get<crude_json::string>();
    n.x = (float)nv["x"].get<crude_json::number>();
    n.y = (float)nv["y"].get<crude_json::number>();
    crude_json::value& params = nv["params"];
    if (params.is_object())
      for (auto& kv : params.get<crude_json::object>())
        n.params[kv.first] = (float)kv.second.get<crude_json::number>();
    // String sub-seam: load strParams when present (absent for legacy/float-only files).
    crude_json::value& strParams = nv["strParams"];
    if (strParams.is_object())
      for (auto& kv : strParams.get<crude_json::object>())
        if (kv.second.is_string()) n.strParams[kv.first] = kv.second.get<crude_json::string>();
    out.nodes.push_back(n);
  }
  crude_json::value& conns = v["connections"];
  if (conns.is_array())
    for (auto& cv : conns.get<crude_json::array>())
      out.connections.push_back({(int)cv["id"].get<crude_json::number>(),
                                 (int)cv["fromPin"].get<crude_json::number>(),
                                 (int)cv["toPin"].get<crude_json::number>()});
  return true;
}


bool saveGraphToFile(const std::string& path, const Graph& g) {
  std::ofstream f(path);
  if (!f) return false;
  f << toJson(g);
  return f.good();
}

bool loadGraphFromFile(const std::string& path, Graph& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return fromJson(json, out);
}


// --- Value evaluation (Task 2) ---

float evalFloat(const Graph& g, int outPin, const EvaluationContext& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  int nodeId = pinNode(outPin);
  const Node* n = g.node(nodeId);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return 0.0f;
  // Stateful / host-scalar nodes carry no pure evaluate; their output pins read the value cooked into
  // Node::outCache (AudioReaction: main cooks the spectrum each frame; host-scalar consumers like
  // FloatListLength/PickFloatFromList/StringLength: the flat cook driver's host-scalar branch writes
  // the scalar — the FloatList→Float BRIDGE, list-routing seam). outIdx = the output port index
  // (inverse of pinId). The predicate was hard-coded "AudioReaction"; GENERALISED to isHostScalarOp
  // (registry set) so the escape hatch is data-driven, NOT a growing type-name chain
  // (fork-evalfloat-stateful-generalized). ZERO REGRESSION: this branch is reachable ONLY when
  // evaluate == nullptr; every existing value op has evaluate != nullptr → never enters → bit-identical.
  if (s->evaluate == nullptr && !n->type.empty() &&
      (n->type == "AudioReaction" || isHostScalarOp(n->type))) {
    const int oi = (outPin - 1) - nodeId * 100;
    return (oi >= 0 && oi < 3) ? n->outCache[oi] : 0.0f;
  }
  // S3b value↔command LIVE-READ (flat mirror of evalResidentFloat — production runs RESIDENT, but the flat leg
  // is the golden's other tooth + must never diverge, S2c blood lesson): a value-rail Get*Var cooked under a
  // Command-rail SetVarCmd SubGraph reads the LIVE ambient ctxVars. Gated on isValueRailContextVarReader + a live
  // scope (liveCtxVars()!=nullptr); OFF-scope this is skipped and the path falls through to the unchanged
  // `return 0.0f` below (the flat value-rail Get*Var was never cooked — it has no flat stateful leg — so 0 was its
  // prior value; this only CHANGES it when a scope is active, never otherwise). VariableName off Node::strParams
  // (the String channel — the Float resolver skips String ports); fallback = the FallbackDefault/FallbackValue
  // Float port resolved through the normal spine (wired/constant/default), the TiXL Get*Var.Update miss value.
  if (!s->evaluate && liveCtxVars() && isValueRailContextVarReader(n->type)) {
    std::string varName;
    auto vit = n->strParams.find("VariableName");
    if (vit != n->strParams.end()) varName = vit->second;
    const char* fbId = (n->type == "GetIntVar") ? "FallbackValue" : "FallbackDefault";
    float fallback = 0.0f;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& p = s->ports[i];
      if (!(p.isInput && p.dataType == "Float" && p.id == fbId)) continue;
      if (const Connection* c = g.connectionToInput(pinId(nodeId, (int)i)))
        fallback = evalFloat(g, c->fromPin, ctx, depth + 1);  // wired fallback
      else {
        auto pit = n->params.find(p.id);
        fallback = (pit != n->params.end()) ? pit->second : p.def;  // stored constant / spec default
      }
      break;
    }
    return liveGetVar(n->type, varName, fallback);
  }
  if (!s->evaluate) return 0.0f;
  // Gather Float input values in port order (only Float inputs contribute to in[]).
  //
  // Cap kMaxFloatIn = 32 (was 16, was 8). Survey of every value-op NodeSpec's Float-input count
  // (node_registry_*.cpp + value_op_*.cpp) gives MAX = 19 (PerlinNoise3: 4 scalars + 5 Vec3 ×3 +
  // BiasAndGain ×2), next PerlinNoise2 = 15, OscillateVec3 = 14, Camera = 13, RemapVec2 = 11.
  // 16 SILENTLY TRUNCATED PerlinNoise3 (19 > 16) → it read garbage tail floats / hit its own
  // n<19 guard and returned 0 on every output with NO error: dead-on-arrival, masked as 0==0.
  // 32 covers the max (19) with comfortable headroom for the next wide op (any all-Vec4 combiner)
  // and is 128 bytes of stack — free. The number is centralised so a future bump touches one line.
  constexpr int kMaxFloatIn = 32;
  // ★LOUD GUARD (the real lesson — silent truncation is self-deception). Count this spec's Float
  // inputs UP FRONT; if it exceeds the cap, do NOT quietly gather a prefix and hand a too-short n[]
  // to evaluate(). Emit an error and return a NaN sentinel: NaN != any finite want, so EVERY golden
  // assertion on this op flips RED (NaN-aware: std::fabs(NaN - want) is NaN, never < eps) instead of
  // a 0 that can collide with a 0-valued expected pin. The fix on tripping this is to raise the cap.
  {
    int floatIn = 0;
    for (const PortSpec& p : s->ports)
      if (p.isInput && p.dataType == "Float") ++floatIn;
    if (floatIn > kMaxFloatIn) {
      std::fprintf(stderr,
                   "[evalFloat] FATAL: node type '%s' has %d Float inputs, exceeds gather cap %d "
                   "— raise kMaxFloatIn in graph.cpp. Returning NaN (was silent truncation).\n",
                   n->type.c_str(), floatIn, kMaxFloatIn);
      return NAN;  // bites every golden; never silently truncate to 0 again
    }
  }
  float in[kMaxFloatIn];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < kMaxFloatIn; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    int inPin = pinId(nodeId, (int)i);
    if (const Connection* c = g.connectionToInput(inPin)) {
      in[ni++] = evalFloat(g, c->fromPin, ctx, depth + 1);  // wired: recurse upstream
    } else {
      auto it = n->params.find(p.id);
      in[ni++] = (it != n->params.end()) ? it->second : p.def;  // stored constant or spec default
    }
  }
  // Which output port of this node is being pulled (inverse of pinId): lets multi-output
  // nodes (AudioReaction level vs hit) return the right value; single-output nodes ignore it.
  const int outIdx = (outPin - 1) - nodeId * 100;
  return s->evaluate(outIdx, in, ni, ctx);
}

namespace {
// One Float input port's value through the L5 spine. The SSOT for per-port resolution —
// evalParam (first-of-type, single param) and resolveNodeParams (per-node, all params)
// both delegate here so the resolution order can never fork:
//   1. live override (sticky)  2. explicit binding (live-source; Automation -> S4 samples
//   the curve, until then falls through — the graph wire IS the Connection binding and the
//   stored value IS the Constant, both resolved by step 3)  3. wired -> evalFloat upstream,
//   else stored constant, else spec default.
float resolvePortValue(const Graph& g, const Node& n, size_t portIdx, const PortSpec& p,
                       const EvaluationContext& ctx, const SourceRegistry* reg) {
  if (reg) {
    if (const ParamOverride* ov = reg->override_(n.id, p.id); ov && ov->active)
      return ov->value;
    if (const ParamBinding* b = reg->binding(n.id, p.id)) {
      if (b->kind == BindingKind::LiveSource) {
        if (const LiveSource* src = reg->source(b->sourceId); src && src->value)
          return src->value(src->self, ctx);  // audio / hand / MIDI …
      }
    }
  }
  if (const Connection* c = g.connectionToInput(pinId(n.id, (int)portIdx)))
    return evalFloat(g, c->fromPin, ctx, 0);  // driven by connection
  auto it = n.params.find(p.id);
  return (it != n.params.end()) ? it->second : p.def;  // stored constant
}
}  // namespace

// Seam-facing API. Takes `time` (not EvaluationContext&) so callers in translation
// units that already pull tixl_point.h — e.g. main.cpp — need not include Particle.h
// (both define `struct Particle`; they can't coexist). ctx is built here. Value
// nodes currently use only ctx.time; thread more fields here when a node needs them.
float evalParam(const Graph& g, const std::string& type, const std::string& paramId,
                const EvaluationContext& ctx, float fallback, const SourceRegistry* reg) {
  const Node* n = g.firstOfType(type);
  if (!n) return fallback;
  const NodeSpec* s = findSpec(type);
  if (!s) return fallback;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float" && p.id == paramId)) continue;
    return resolvePortValue(g, *n, i, p, ctx, reg);
  }
  return fallback;
}

std::map<std::string, float> resolveNodeParams(const Graph& g, const Node& n,
                                               const EvaluationContext& ctx,
                                               const SourceRegistry* reg) {
  std::map<std::string, float> out;
  const NodeSpec* s = findSpec(n.type);
  if (!s) return out;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    out[p.id] = resolvePortValue(g, n, i, p, ctx, reg);
  }
  return out;
}

void readVecN(const Node& node, const std::string& base, const float* fallback, int n, float* out) {
  static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
  for (int i = 0; i < n && i < 4; ++i) {
    auto it = node.params.find(base + kSuffix[i]);
    out[i] = (it != node.params.end()) ? it->second : fallback[i];
  }
}

}  // namespace sw
