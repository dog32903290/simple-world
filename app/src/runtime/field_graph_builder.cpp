// runtime/field_graph_builder — see field_graph_builder.h. graph → FieldNode tree (PF-0).
#include "runtime/field_graph_builder.h"

#include <vector>

#include "runtime/field_graph.h"          // FieldNode (recurse children via inputs)
#include "runtime/field_node_registry.h"  // makeFieldNode + configureFieldNodeFromParams + findSpec via spec
#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / findSpec / pinId / pinNode
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / ResidentInput

namespace sw {

namespace {

// Depth cap (same spirit as the cook drivers' kCookDepthCap): a malformed/cyclic field wire never
// recurses unbounded. A field tree is shallow in practice (a few combiners); 64 is plenty.
constexpr int kFieldDepthCap = 64;

// shortId seed = the node id (flat) / path (resident) → a collision-free BuildNodeId prefix per node.
// Identical-type field nodes never collide (TiXL ShaderGraphNode.BuildNodeId uses the instance guid).
std::string flatShortId(int nodeId) { return "n" + std::to_string(nodeId); }

std::shared_ptr<FieldNode> buildFlat(const Graph& g, int id, const FieldParamResolver& params, int depth) {
  if (depth > kFieldDepthCap) return nullptr;
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  // Only a field-producing op (a "Field" output port) participates. makeFieldNode returns nullptr for a
  // non-field type (no registered factory) → the tree stops / never starts.
  auto node = makeFieldNode(n->type, flatShortId(id));
  if (!node) return nullptr;

  // Apply THIS node's resolved params (PF-0a: ToroidalVortexField; others → .t3 defaults from ctor).
  if (const std::map<std::string, float>* p = params ? params(id) : nullptr)
    configureFieldNodeFromParams(*node, n->type, *p);

  // Recurse children: every "Field" INPUT port → its wired upstream field op. Single-cardinality
  // (connectionToInput), in spec port order. ToroidalVortexField has no Field input → loop is inert.
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!(port.isInput && port.dataType == "Field")) continue;
    const Connection* c = g.connectionToInput(pinId(id, (int)i));
    if (!c) continue;
    auto child = buildFlat(g, pinNode(c->fromPin), params, depth + 1);
    if (child) node->inputs.push_back(child);
  }
  return node;
}

std::shared_ptr<FieldNode> buildResident(const ResidentEvalGraph& rg, const std::string& path,
                                         const FieldParamResolverResident& params, int depth) {
  if (depth > kFieldDepthCap) return nullptr;
  const ResidentNode* n = rg.node(path);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return nullptr;
  // shortId seed = the resident path (collision-free; mirrors flatShortId on the rg key space).
  auto node = makeFieldNode(n->opType, "p" + path);
  if (!node) return nullptr;

  if (const std::map<std::string, float>* p = params ? params(path) : nullptr)
    configureFieldNodeFromParams(*node, n->opType, *p);

  // Recurse children through the resident Connection drivers (mirror of cookResidentGradient), in spec
  // port order. Field input → upstream field node by srcNodePath.
  for (const PortSpec& port : s->ports) {
    if (!(port.isInput && port.dataType == "Field")) continue;
    const ResidentInput* ri = n->input(port.id);
    if (!ri || ri->driver != ResidentInput::Driver::Connection) continue;
    auto child = buildResident(rg, ri->srcNodePath, params, depth + 1);
    if (child) node->inputs.push_back(child);
  }
  return node;
}

}  // namespace

std::shared_ptr<FieldNode> buildFieldTree(const Graph& g, int rootFieldNodeId,
                                          const FieldParamResolver& params) {
  return buildFlat(g, rootFieldNodeId, params, 0);
}

std::shared_ptr<FieldNode> buildResidentFieldTree(const ResidentEvalGraph& rg, const std::string& rootPath,
                                                  const FieldParamResolverResident& params) {
  return buildResident(rg, rootPath, params, 0);
}

// TWO-HOP gather (flat): the field is wired to the FORCE node, not to the cooking ParticleSystem. For
// each wired ParticleForce input of the cooking node, chase that force node's "Field" input and build
// the first upstream tree. Lives here (not point_graph.cpp) to keep that file's call-site one line.
std::shared_ptr<FieldNode> gatherForceFieldTree(const Graph& g, int cookingNodeId,
                                                const FieldParamResolver& params) {
  const Node* cooking = g.node(cookingNodeId);
  const NodeSpec* cs = cooking ? findSpec(cooking->type) : nullptr;
  if (!cs) return nullptr;
  for (size_t i = 0; i < cs->ports.size(); ++i) {
    const PortSpec& fport = cs->ports[i];
    if (!(fport.isInput && fport.dataType == "ParticleForce")) continue;
    const Connection* fc = g.connectionToInput(pinId(cookingNodeId, (int)i));
    if (!fc) continue;
    const int forceNodeId = pinNode(fc->fromPin);
    const Node* forceNode = g.node(forceNodeId);
    const NodeSpec* fs = forceNode ? findSpec(forceNode->type) : nullptr;
    if (!fs) continue;
    for (size_t j = 0; j < fs->ports.size(); ++j) {
      const PortSpec& ffp = fs->ports[j];
      if (!(ffp.isInput && ffp.dataType == "Field")) continue;
      const Connection* wc = g.connectionToInput(pinId(forceNodeId, (int)j));
      if (wc) {
        if (auto t = buildFieldTree(g, pinNode(wc->fromPin), params)) return t;  // first wins (v1)
      }
    }
  }
  return nullptr;
}

// ONE-HOP gather (flat): the field is wired DIRECTLY to the tex op's "Field" input (RaymarchField).
// Find the cooking node's FIRST wired "Field" input port and build that upstream tree. null = no
// wired Field. Lives here (not point_graph_tex_cook.cpp) so the tex leaf's call-site is one line.
std::shared_ptr<FieldNode> gatherTexFieldTree(const Graph& g, int cookingNodeId,
                                              const FieldParamResolver& params) {
  const Node* cooking = g.node(cookingNodeId);
  const NodeSpec* cs = cooking ? findSpec(cooking->type) : nullptr;
  if (!cs) return nullptr;
  for (size_t i = 0; i < cs->ports.size(); ++i) {
    const PortSpec& port = cs->ports[i];
    if (!(port.isInput && port.dataType == "Field")) continue;
    const Connection* c = g.connectionToInput(pinId(cookingNodeId, (int)i));
    if (c) {
      if (auto t = buildFieldTree(g, pinNode(c->fromPin), params)) return t;  // first wired Field wins
    }
  }
  return nullptr;
}

// ONE-HOP gather (resident): mirror of gatherTexFieldTree via the rg Connection drivers.
std::shared_ptr<FieldNode> gatherTexResidentFieldTree(const ResidentEvalGraph& rg,
                                                      const std::string& cookingPath,
                                                      const FieldParamResolverResident& params) {
  const ResidentNode* cooking = rg.node(cookingPath);
  const NodeSpec* cs = cooking ? findSpec(cooking->opType) : nullptr;
  if (!cs) return nullptr;
  for (const PortSpec& port : cs->ports) {
    if (!(port.isInput && port.dataType == "Field")) continue;
    const ResidentInput* ri = cooking->input(port.id);
    if (ri && ri->driver == ResidentInput::Driver::Connection) {
      if (auto t = buildResidentFieldTree(rg, ri->srcNodePath, params)) return t;  // first wins
    }
  }
  return nullptr;
}

// TWO-HOP gather (resident): mirror of gatherForceFieldTree via the rg Connection drivers.
std::shared_ptr<FieldNode> gatherForceResidentFieldTree(const ResidentEvalGraph& rg,
                                                        const std::string& cookingPath,
                                                        const FieldParamResolverResident& params) {
  const ResidentNode* cooking = rg.node(cookingPath);
  const NodeSpec* cs = cooking ? findSpec(cooking->opType) : nullptr;
  if (!cs) return nullptr;
  for (const PortSpec& fport : cs->ports) {
    if (!(fport.isInput && fport.dataType == "ParticleForce")) continue;
    const ResidentInput* fri = cooking->input(fport.id);
    if (!fri || fri->driver != ResidentInput::Driver::Connection) continue;
    const ResidentNode* forceNode = rg.node(fri->srcNodePath);
    const NodeSpec* fs = forceNode ? findSpec(forceNode->opType) : nullptr;
    if (!fs) continue;
    for (const PortSpec& ffp : fs->ports) {
      if (!(ffp.isInput && ffp.dataType == "Field")) continue;
      const ResidentInput* fieldRi = forceNode->input(ffp.id);
      if (fieldRi && fieldRi->driver == ResidentInput::Driver::Connection) {
        if (auto t = buildResidentFieldTree(rg, fieldRi->srcNodePath, params)) return t;  // first wins
      }
    }
  }
  return nullptr;
}

}  // namespace sw
