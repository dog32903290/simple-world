const assert = require("node:assert/strict");
const test = require("node:test");

const {
  createInitialGraphState,
  dispatchGraphCommand,
  validateGraphState,
  buildRuntimeGraph,
  serializeGraphDocument,
  deserializeGraphDocument,
} = require("../docs/runtime/scripts/graph_interaction_contract.js");

function runCommands(state, commands) {
  let current = state;
  const diagnostics = [];
  for (const command of commands) {
    const result = dispatchGraphCommand(current, command);
    current = result.state;
    diagnostics.push(...result.diagnostics);
  }
  return { state: current, diagnostics };
}

test("pure interaction commands create connect edit validate and dirty a SphereSDF RaymarchField graph", () => {
  const initial = createInitialGraphState({ graphId: "test.interaction.sphere_raymarch" });
  const { state, diagnostics } = runCommands(initial, [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF", position: { x: 10, y: 20 } },
    { type: "CreateNode", nodeId: "raymarch_field_1", nodeType: "tixl.field.render.RaymarchField", position: { x: 360, y: 20 } },
    { type: "BeginCableDrag", from: { nodeId: "sphere_sdf_1", port: "result" } },
    { type: "HoverPort", port: { nodeId: "raymarch_field_1", port: "sdfField" } },
    { type: "CommitCableDrag", to: { nodeId: "raymarch_field_1", port: "sdfField" } },
    { type: "SetParameter", nodeId: "sphere_sdf_1", param: "radius", value: 0.75 },
  ]);

  const validation = validateGraphState(state);
  const runtimeGraph = buildRuntimeGraph(state);

  assert.deepEqual(diagnostics, []);
  assert.deepEqual(validation.diagnostics, []);
  assert.equal(state.edges.length, 1);
  assert.deepEqual(state.edges[0].from, { nodeId: "sphere_sdf_1", port: "result" });
  assert.deepEqual(state.edges[0].to, { nodeId: "raymarch_field_1", port: "sdfField" });
  assert.equal(state.nodes.sphere_sdf_1.params.radius, 0.75);
  assert.equal(state.runtimeDirty, true);
  assert.deepEqual(runtimeGraph.cookOrder, ["sphere_sdf_1", "raymarch_field_1"]);
});

test("invalid cable type mismatch returns diagnostics and does not create an edge", () => {
  const { state, diagnostics } = runCommands(createInitialGraphState(), [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF" },
    { type: "CreateNode", nodeId: "raymarch_field_1", nodeType: "tixl.field.render.RaymarchField" },
    { type: "BeginCableDrag", from: { nodeId: "sphere_sdf_1", port: "result" } },
    { type: "CommitCableDrag", to: { nodeId: "raymarch_field_1", port: "color" } },
  ]);

  assert.equal(state.edges.length, 0);
  assert.equal(state.runtimeDirty, true);
  assert.equal(diagnostics.length, 1);
  assert.equal(diagnostics[0].code, "graph.edge.type_mismatch");
});

test("deleting a selected node removes attached edges", () => {
  const connected = runCommands(createInitialGraphState(), [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF" },
    { type: "CreateNode", nodeId: "raymarch_field_1", nodeType: "tixl.field.render.RaymarchField" },
    { type: "BeginCableDrag", from: { nodeId: "sphere_sdf_1", port: "result" } },
    { type: "CommitCableDrag", to: { nodeId: "raymarch_field_1", port: "sdfField" } },
    { type: "SelectNode", nodeId: "sphere_sdf_1", mode: "replace" },
  ]);

  const deleted = dispatchGraphCommand(connected.state, { type: "DeleteSelection" });

  assert.deepEqual(deleted.diagnostics, []);
  assert.equal(Object.hasOwn(deleted.state.nodes, "sphere_sdf_1"), false);
  assert.equal(Object.hasOwn(deleted.state.nodes, "raymarch_field_1"), true);
  assert.deepEqual(deleted.state.edges, []);
  assert.equal(deleted.state.runtimeDirty, true);
});

test("save reload preserves graph data and drops unsafe interaction state", () => {
  const built = runCommands(createInitialGraphState(), [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF", position: { x: 10, y: 20 } },
    { type: "CreateNode", nodeId: "raymarch_field_1", nodeType: "tixl.field.render.RaymarchField", position: { x: 360, y: 20 } },
    { type: "SelectNode", nodeId: "sphere_sdf_1", mode: "replace" },
    { type: "BeginCableDrag", from: { nodeId: "sphere_sdf_1", port: "result" } },
    { type: "CommitCableDrag", to: { nodeId: "raymarch_field_1", port: "sdfField" } },
    { type: "SetParameter", nodeId: "sphere_sdf_1", param: "radius", value: 0.33 },
  ]);

  const document = serializeGraphDocument(built.state);
  const reloaded = deserializeGraphDocument(document);

  assert.deepEqual(reloaded.diagnostics, []);
  assert.deepEqual(reloaded.state.nodes.sphere_sdf_1.position, { x: 10, y: 20 });
  assert.equal(reloaded.state.nodes.sphere_sdf_1.params.radius, 0.33);
  assert.equal(reloaded.state.edges.length, 1);
  assert.deepEqual(reloaded.state.selection.nodeIds, []);
  assert.equal(reloaded.state.interaction.cableDrag, null);
  assert.equal(reloaded.state.runtimeDirty, true);
});

test("CancelCableDrag leaves graph data unchanged", () => {
  const created = runCommands(createInitialGraphState(), [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF" },
    { type: "CreateNode", nodeId: "raymarch_field_1", nodeType: "tixl.field.render.RaymarchField" },
  ]);
  const before = serializeGraphDocument(created.state);
  const dragging = dispatchGraphCommand(created.state, { type: "BeginCableDrag", from: { nodeId: "sphere_sdf_1", port: "result" } });
  const canceled = dispatchGraphCommand(dragging.state, { type: "CancelCableDrag" });

  assert.deepEqual(canceled.diagnostics, []);
  assert.deepEqual(serializeGraphDocument(canceled.state), before);
  assert.equal(canceled.state.interaction.cableDrag, null);
});

test("MoveNode changes only persisted layout and does not dirty runtime semantics", () => {
  const created = runCommands(createInitialGraphState(), [
    { type: "CreateNode", nodeId: "sphere_sdf_1", nodeType: "tixl.field.generate.sdf.SphereSDF", position: { x: 10, y: 20 } },
  ]);
  const clean = { ...created.state, runtimeDirty: false };
  const moved = dispatchGraphCommand(clean, { type: "MoveNode", nodeId: "sphere_sdf_1", position: { x: 80, y: 120 } });

  assert.deepEqual(moved.diagnostics, []);
  assert.deepEqual(moved.state.nodes.sphere_sdf_1.position, { x: 80, y: 120 });
  assert.equal(moved.state.runtimeDirty, false);
  assert.equal(moved.state.revision, clean.revision + 1);
});
