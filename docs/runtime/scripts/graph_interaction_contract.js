"use strict";

const NODE_SPECS = {
  "tixl.field.generate.sdf.SphereSDF": {
    title: "my_SphereSDF",
    outputs: {
      result: { type: "ShaderGraphNode" },
    },
    inputs: {},
    defaults: {
      center: { x: 0, y: 0, z: 0 },
      radius: 0.5,
    },
    runtimeSemantic: true,
  },
  "tixl.field.render.RaymarchField": {
    title: "my_RaymarchField",
    inputs: {
      sdfField: { type: "ShaderGraphNode" },
      color: { type: "Color" },
    },
    outputs: {
      shaderCode: { type: "ShaderGraph" },
    },
    defaults: {
      writeDepth: true,
      minDistance: 0.002,
      distToColor: 0.15,
      maxSteps: 100,
      uvMapping: 1,
      stepSize: 1,
      textureScale: 1,
      specularAA: 0.5,
      color: { r: 1, g: 1, b: 1, a: 1 },
      maxDistance: 300,
      ambientOcclusion: { x: 0.000001, y: 0.000001, z: 0.000001, w: 1 },
      normalSamplingD: 0.002,
      aoDistance: 1,
    },
    runtimeSemantic: true,
  },
};

function createInitialGraphState({ graphId = "graph.interaction" } = {}) {
  return {
    kind: "GraphState",
    version: "0.1",
    graphId,
    revision: 0,
    nodes: {},
    edges: [],
    selection: { nodeIds: [] },
    interaction: { cableDrag: null, hoveredPort: null },
    runtimeDirty: false,
    commandLog: [],
  };
}

function dispatchGraphCommand(state, command) {
  const diagnostics = [];
  let next = cloneState(state);
  const beforeDocument = serializeGraphDocument(next);

  switch (command.type) {
    case "CreateNode":
      next = createNode(next, command, diagnostics);
      break;
    case "SelectNode":
      next = selectNode(next, command, diagnostics);
      break;
    case "MoveNode":
      next = moveNode(next, command, diagnostics);
      break;
    case "BeginCableDrag":
      next = beginCableDrag(next, command, diagnostics);
      break;
    case "HoverPort":
      next = hoverPort(next, command, diagnostics);
      break;
    case "CommitCableDrag":
      next = commitCableDrag(next, command, diagnostics);
      break;
    case "CancelCableDrag":
      next.interaction.cableDrag = null;
      next.interaction.hoveredPort = null;
      break;
    case "DeleteSelection":
      next = deleteSelection(next, diagnostics);
      break;
    case "SetParameter":
      next = setParameter(next, command, diagnostics);
      break;
    default:
      diagnostics.push(diagnostic("graph.command.unsupported", { commandType: command.type }));
      break;
  }

  const afterDocument = serializeGraphDocument(next);
  if (JSON.stringify(beforeDocument) !== JSON.stringify(afterDocument)) {
    next.revision += 1;
  }
  next.commandLog.push({ index: next.commandLog.length, command: clone(command), diagnostics: clone(diagnostics) });
  return { state: next, diagnostics };
}

function validateGraphState(state) {
  const diagnostics = [];
  for (const edge of state.edges) {
    validateEdge(state, edge.from, edge.to, diagnostics);
  }
  return { ok: diagnostics.length === 0, diagnostics };
}

function buildRuntimeGraph(state) {
  const validation = validateGraphState(state);
  const cookOrder = computeCookOrder(state);
  return {
    kind: "RuntimeGraph",
    graphId: state.graphId,
    revision: state.revision,
    cookOrder,
    nodes: cookOrder.map((nodeId) => ({
      id: nodeId,
      type: state.nodes[nodeId].type,
      title: state.nodes[nodeId].title,
      params: clone(state.nodes[nodeId].params),
      position: clone(state.nodes[nodeId].position),
      domain: "frame",
    })),
    edges: clone(state.edges),
    diagnostics: validation.diagnostics,
  };
}

function serializeGraphDocument(state) {
  return {
    kind: "GraphDocument",
    version: state.version,
    graphId: state.graphId,
    nodes: Object.values(state.nodes)
      .sort((a, b) => a.id.localeCompare(b.id))
      .map((node) => ({
        id: node.id,
        type: node.type,
        title: node.title,
        position: clone(node.position),
        params: clone(node.params),
      })),
    edges: state.edges
      .map((edge) => clone(edge))
      .sort((a, b) => a.id.localeCompare(b.id)),
  };
}

function deserializeGraphDocument(document) {
  const state = createInitialGraphState({ graphId: document.graphId });
  const diagnostics = [];
  let next = state;
  for (const node of document.nodes || []) {
    next.nodes[node.id] = {
      id: node.id,
      type: node.type,
      title: node.title,
      position: clone(node.position),
      params: clone(node.params || {}),
    };
  }
  next.edges = clone(document.edges || []);
  next.runtimeDirty = true;
  const validation = validateGraphState(next);
  diagnostics.push(...validation.diagnostics);
  return { state: next, diagnostics };
}

function createNode(state, command, diagnostics) {
  if (state.nodes[command.nodeId]) {
    diagnostics.push(diagnostic("graph.node.duplicate", { nodeId: command.nodeId }));
    return state;
  }
  const spec = NODE_SPECS[command.nodeType];
  if (!spec) {
    diagnostics.push(diagnostic("graph.node.unknown_type", { nodeType: command.nodeType }));
    return state;
  }
  state.nodes[command.nodeId] = {
    id: command.nodeId,
    type: command.nodeType,
    title: spec.title,
    position: clone(command.position || { x: 0, y: 0 }),
    params: clone(spec.defaults),
  };
  state.runtimeDirty = true;
  return state;
}

function selectNode(state, command, diagnostics) {
  if (!state.nodes[command.nodeId]) {
    diagnostics.push(diagnostic("graph.node.missing", { nodeId: command.nodeId }));
    return state;
  }
  if (command.mode === "add") {
    state.selection.nodeIds = Array.from(new Set([...state.selection.nodeIds, command.nodeId])).sort();
  } else {
    state.selection.nodeIds = [command.nodeId];
  }
  return state;
}

function moveNode(state, command, diagnostics) {
  const node = state.nodes[command.nodeId];
  if (!node) {
    diagnostics.push(diagnostic("graph.node.missing", { nodeId: command.nodeId }));
    return state;
  }
  node.position = clone(command.position);
  return state;
}

function beginCableDrag(state, command, diagnostics) {
  const port = resolvePort(state, command.from, "output");
  if (!port) {
    diagnostics.push(diagnostic("graph.port.missing", { port: command.from, direction: "output" }));
    return state;
  }
  state.interaction.cableDrag = { from: clone(command.from) };
  state.interaction.hoveredPort = null;
  return state;
}

function hoverPort(state, command, diagnostics) {
  if (!resolvePort(state, command.port)) {
    diagnostics.push(diagnostic("graph.port.missing", { port: command.port }));
    return state;
  }
  state.interaction.hoveredPort = clone(command.port);
  return state;
}

function commitCableDrag(state, command, diagnostics) {
  const drag = state.interaction.cableDrag;
  if (!drag) {
    diagnostics.push(diagnostic("graph.edge.no_active_drag", {}));
    return state;
  }
  const edgeDiagnostics = [];
  const edge = { id: stableEdgeId(drag.from, command.to), from: clone(drag.from), to: clone(command.to) };
  validateEdge(state, edge.from, edge.to, edgeDiagnostics);
  if (edgeDiagnostics.length > 0) {
    diagnostics.push(...edgeDiagnostics);
    state.interaction.cableDrag = null;
    state.interaction.hoveredPort = null;
    return state;
  }
  const duplicate = state.edges.some((existing) => (
    existing.from.nodeId === edge.from.nodeId &&
    existing.from.port === edge.from.port &&
    existing.to.nodeId === edge.to.nodeId &&
    existing.to.port === edge.to.port
  ));
  if (!duplicate) {
    state.edges.push(edge);
    state.edges.sort((a, b) => a.id.localeCompare(b.id));
    state.runtimeDirty = true;
  }
  state.interaction.cableDrag = null;
  state.interaction.hoveredPort = null;
  return state;
}

function deleteSelection(state) {
  const selected = new Set(state.selection.nodeIds);
  for (const nodeId of selected) {
    delete state.nodes[nodeId];
  }
  state.edges = state.edges.filter((edge) => !selected.has(edge.from.nodeId) && !selected.has(edge.to.nodeId));
  state.selection.nodeIds = [];
  state.interaction.cableDrag = null;
  state.interaction.hoveredPort = null;
  if (selected.size > 0) {
    state.runtimeDirty = true;
  }
  return state;
}

function setParameter(state, command, diagnostics) {
  const node = state.nodes[command.nodeId];
  if (!node) {
    diagnostics.push(diagnostic("graph.node.missing", { nodeId: command.nodeId }));
    return state;
  }
  node.params[command.param] = clone(command.value);
  state.runtimeDirty = true;
  return state;
}

function validateEdge(state, from, to, diagnostics) {
  const fromPort = resolvePort(state, from, "output");
  const toPort = resolvePort(state, to, "input");
  if (!fromPort) {
    diagnostics.push(diagnostic("graph.port.missing", { port: from, direction: "output" }));
    return;
  }
  if (!toPort) {
    diagnostics.push(diagnostic("graph.port.missing", { port: to, direction: "input" }));
    return;
  }
  if (fromPort.type !== toPort.type) {
    diagnostics.push(diagnostic("graph.edge.type_mismatch", {
      from,
      to,
      fromType: fromPort.type,
      toType: toPort.type,
    }));
  }
}

function resolvePort(state, ref, preferredDirection = null) {
  const node = state.nodes[ref.nodeId];
  if (!node) {
    return null;
  }
  const spec = NODE_SPECS[node.type];
  if (!spec) {
    return null;
  }
  if (preferredDirection === "input") {
    return spec.inputs[ref.port] || null;
  }
  if (preferredDirection === "output") {
    return spec.outputs[ref.port] || null;
  }
  return spec.inputs[ref.port] || spec.outputs[ref.port] || null;
}

function computeCookOrder(state) {
  const nodeIds = Object.keys(state.nodes).sort();
  const incoming = new Map(nodeIds.map((nodeId) => [nodeId, 0]));
  const outgoing = new Map(nodeIds.map((nodeId) => [nodeId, []]));
  for (const edge of state.edges) {
    if (!incoming.has(edge.from.nodeId) || !incoming.has(edge.to.nodeId)) {
      continue;
    }
    incoming.set(edge.to.nodeId, incoming.get(edge.to.nodeId) + 1);
    outgoing.get(edge.from.nodeId).push(edge.to.nodeId);
  }

  const ready = nodeIds.filter((nodeId) => incoming.get(nodeId) === 0);
  const order = [];
  while (ready.length > 0) {
    ready.sort();
    const nodeId = ready.shift();
    order.push(nodeId);
    for (const target of outgoing.get(nodeId).sort()) {
      incoming.set(target, incoming.get(target) - 1);
      if (incoming.get(target) === 0) {
        ready.push(target);
      }
    }
  }
  return order.length === nodeIds.length ? order : nodeIds;
}

function stableEdgeId(from, to) {
  return `${from.nodeId}.${from.port}->${to.nodeId}.${to.port}`;
}

function diagnostic(code, fields) {
  return {
    code,
    severity: "error",
    ...clone(fields),
  };
}

function cloneState(value) {
  const state = clone(value);
  state.commandLog = Array.isArray(state.commandLog) ? state.commandLog : [];
  state.selection = state.selection || { nodeIds: [] };
  state.interaction = state.interaction || { cableDrag: null, hoveredPort: null };
  return state;
}

function clone(value) {
  if (value === undefined) {
    return undefined;
  }
  return JSON.parse(JSON.stringify(value));
}

module.exports = {
  NODE_SPECS,
  createInitialGraphState,
  dispatchGraphCommand,
  validateGraphState,
  buildRuntimeGraph,
  serializeGraphDocument,
  deserializeGraphDocument,
};
