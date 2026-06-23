// runtime/field_graph_builder — graph → FieldNode tree builder (PF-0 field-input-projection keystone).
//
// ZONE: runtime (pure host computation; no Metal). This is the production path that turns a WIRED field
// node (ToroidalVortexField.Result → VectorFieldForce's "Field" input) into an assembled FieldNode tree
// the force cook (PF-a, NOT here) consumes. Until PF-0, the ONLY graph→FieldNode walk lived in the field
// render golden's hand-built tree; this TU is the first PRODUCTION builder, mirroring how the SDF/field
// golden built a tree by hand (makeFieldNode + configure + push_back children) but driven by graph edges.
//
// TiXL authority: external/tixl/Core/DataTypes/ShaderGraphNode.cs (CollectEmbeddedShaderCode walks
// InputNodes, synced from _connectedNodeOps in Update). sw has no live slot-sync, so the builder walks
// the graph connections explicitly at cook time and rebuilds the tree fresh (cheap host shared_ptr work).
//
// TWO LEGS (sw's dual-pass cook world-view, same as Gradient/Mesh having flat + resident gathers):
//   buildFieldTree         — FLAT cook leg, walks a Graph + connectionToInput + a node-params resolver.
//   buildResidentFieldTree — RESIDENT/PRODUCTION leg, walks a ResidentEvalGraph (rg.node / input /
//                            nodeParams), mirroring cookResidentGradient. cc.graph==nullptr does NOT
//                            block this: the resident DRIVER holds rg and rebuilds host-value trees the
//                            same way cookResidentGradient already does.
//
// PARAM-APPLY (PF-0a narrow path, blueprint §1.5 choice C-staged): the leaf type is TU-private, so the
// builder does NOT downcast. It calls configureFieldNodeFromParams(node, type, params), which dispatches
// to the op's OWN from-map configure inside the owning TU. PF-0a only wires ToroidalVortexField (the one
// leaf the probe needs); PF-0c generalizes the dispatch to every field op. An unhandled type leaves the
// node at its .t3 defaults (the makeFieldNode ctor already seeds those) — never a crash.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace sw {

struct FieldNode;          // field_graph.h
struct Graph;              // graph.h (flat leg)
struct ResidentEvalGraph;  // resident_eval_graph.h (resident leg)

// Resolve a node's full Float param map (override → binding → wire → stored → default). FLAT callers
// pass the cook driver's `nodeParams` lambda; the builder needs it to apply each field node's params.
using FieldParamResolver = std::function<const std::map<std::string, float>*(int nodeId)>;

// FLAT: build the FieldNode tree rooted at graph node `rootFieldNodeId`. `params` resolves any field
// node's params (recursively, by id). Returns nullptr if the node is not a field op (no factory) or
// `rootFieldNodeId` is absent. ToroidalVortexField is a LEAF (no Field input) so recursion stops at it;
// combiner field ops (BlendSDFWithSDF) with "Field" input ports recurse their children (future).
std::shared_ptr<FieldNode> buildFieldTree(const Graph& g, int rootFieldNodeId,
                                          const FieldParamResolver& params);

// RESIDENT: build the FieldNode tree rooted at resident node `rootPath`. `params` resolves a resident
// node's params by path (the driver's `nodeParams` lambda). Mirror of buildFieldTree on the rg walk.
using FieldParamResolverResident = std::function<const std::map<std::string, float>*(const std::string& path)>;
std::shared_ptr<FieldNode> buildResidentFieldTree(const ResidentEvalGraph& rg, const std::string& rootPath,
                                                  const FieldParamResolverResident& params);

// TWO-HOP gather (PF-0 field-into-force seam, thin call-site for the cook drivers): for the Points op
// `cookingNodeId`/`cookingPath` (ParticleSystem), chase each wired ParticleForce input's "Field" input
// and build the FIRST upstream field tree (v1 single slot). null if no force has a wired Field. Keeps the
// force→field chase + recursion OUT of point_graph.cpp / point_graph_resident.cpp (line-count ratchet).
std::shared_ptr<FieldNode> gatherForceFieldTree(const Graph& g, int cookingNodeId,
                                                const FieldParamResolver& params);
std::shared_ptr<FieldNode> gatherForceResidentFieldTree(const ResidentEvalGraph& rg,
                                                        const std::string& cookingPath,
                                                        const FieldParamResolverResident& params);

}  // namespace sw
