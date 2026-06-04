const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/COMMAND_STREAM_CONTRACT.md");
const dangerPath = path.join(repoRoot, "docs/runtime/DANGER_NODE_EXPERIMENTS.md");

function makeCommand(id, phases = {}) {
  return {
    id,
    prepare: phases.prepare ?? (() => {}),
    update: phases.update ?? (() => {}),
    restore: phases.restore ?? (() => {}),
  };
}

function executeCommands(commands, { enabled = true } = {}) {
  const trace = [];

  if (!enabled) {
    return trace;
  }

  for (const command of commands) {
    trace.push(`prepare:${command.id}`);
    command.prepare(trace);
  }
  for (const command of commands) {
    trace.push(`update:${command.id}`);
    command.update(trace);
  }
  for (const command of commands) {
    trace.push(`restore:${command.id}`);
    command.restore(trace);
  }

  return trace;
}

function drawCommand({ hasVertexShader, hasPixelShader, vertexCount = 3, start = 0 }) {
  const diagnostics = [];
  const stats = { drawCalls: 0, triangles: 0 };

  if (!hasVertexShader || !hasPixelShader) {
    diagnostics.push("Trying to issue draw call, but pixel and/or vertex shader are null.");
    return { ok: false, diagnostics, stats };
  }

  stats.drawCalls += 1;
  stats.triangles += Math.floor(vertexCount / 3);
  return { ok: true, op: { kind: "Draw", vertexCount, start }, diagnostics, stats };
}

function computeStage({ computeShader, uavs = [], dispatch = { x: 16, y: 16, z: 1 }, dispatchCallCount = 1 }) {
  if (!computeShader) {
    return { ok: false, dispatches: 0, diagnostics: ["missing compute shader"] };
  }

  if (uavs.length === 0) {
    return { ok: false, dispatches: 0, diagnostics: ["missing UAV"] };
  }

  const callCount = Math.min(Math.max(dispatchCallCount, 1), 256);
  return {
    ok: true,
    dispatches: dispatch.x * dispatch.y * dispatch.z * callCount,
    callCount,
    cleanup: ["unbind UAVs", "unbind samplers", "unbind SRVs", "unbind constant buffers"],
    diagnostics: [],
  };
}

function calcDispatchCount(count, groupSize) {
  if (groupSize.x <= 0) {
    return { x: 0, y: 0, z: 0 };
  }

  return { x: Math.trunc(count / groupSize.x) + 1, y: 1, z: 1 };
}

function calcInt2DispatchCount(size, threadGroups, previous = null) {
  if (threadGroups.x === 0 || threadGroups.y === 0) {
    return previous;
  }

  return {
    x: Math.trunc(size.width / threadGroups.x) + 1,
    y: Math.trunc(size.height / threadGroups.y) + 1,
    z: 1,
  };
}

test("Command stream contract separates ordered render operations from resources and views", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Command := ordered frame operation with optional prepare\/update\/restore phases/);
  assert.match(source, /T3\.Core\.DataTypes\.Command/);
  assert.match(source, /TextureView/);
  assert.match(source, /RenderTarget/);
  assert.match(source, /ShaderGraph/);
  assert.match(source, /Vuo cannot prove TiXL Command\/RestoreAction semantics/);
});

test("Command stream contract records resource hazard visibility without claiming GPU barrier parity", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /resource-access ledger/);
  assert.match(source, /UAVWrite/);
  assert.match(source, /UnorderedAccessWrite/);
  assert.match(source, /ShaderResourceRead/);
  assert.match(source, /RenderTargetWrite/);
  assert.match(source, /non-UAV accesses/);
  assert.match(source, /resourceBarrier/);
  assert.match(source, /not a real GPU resource barrier/);
});

test("Command stream contract records ClearRenderTarget as a clear command, not an output merger bind", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /make_clear_render_target_command/);
  assert.match(source, /my_ClearRenderTarget/);
  assert.match(source, /clearRenderTargetView/);
  assert.match(source, /clearDepthStencilView/);
  assert.match(source, /clearCalls/);
  assert.match(source, /Does not bind OutputMerger state/);
});

test("Command stream contract records OutputMerger UAVs as separate from compute UAV state", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /UnorderedAccessViews/);
  assert.match(source, /outputMergerUavs/);
  assert.match(source, /separate from compute-stage UAV state/);
  assert.match(source, /unorderedAccessViews/);
  assert.match(source, /UnorderedAccessWrite/);
  assert.match(source, /does not prove real GPU\s+UAV slot binding/);
});

test("Command stream contract records the first render and compute donor set", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Operators\/Lib\/flow\/Execute\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/OutputMergerStage\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/fxsetup\/SetPixelAndVertexShaderStage\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/Draw\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/DrawInstancedIndirect\.cs/);
  assert.match(source, /Operators\/TypeOperators\/Gfx\/ComputeShaderStage\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/CalcDispatchCount\.cs/);
  assert.match(source, /Operators\/Lib\/render\/_dx11\/api\/CalcInt2DispatchCount\.cs/);
});

test("Execute fixture preserves TiXL prepare, update, restore ordering", () => {
  const trace = executeCommands([
    makeCommand("om", {
      prepare: (t) => t.push("save-om"),
      update: (t) => t.push("bind-om"),
      restore: (t) => t.push("restore-om"),
    }),
    makeCommand("draw", {
      update: (t) => t.push("draw"),
    }),
  ]);

  assert.deepEqual(trace, [
    "prepare:om",
    "save-om",
    "prepare:draw",
    "update:om",
    "bind-om",
    "update:draw",
    "draw",
    "restore:om",
    "restore-om",
    "restore:draw",
  ]);

  assert.deepEqual(executeCommands([makeCommand("draw")], { enabled: false }), []);
});

test("Draw fixture refuses to fake success when VS or PS is missing", () => {
  assert.deepEqual(drawCommand({ hasVertexShader: false, hasPixelShader: true }), {
    ok: false,
    diagnostics: ["Trying to issue draw call, but pixel and/or vertex shader are null."],
    stats: { drawCalls: 0, triangles: 0 },
  });

  assert.deepEqual(drawCommand({ hasVertexShader: true, hasPixelShader: true, vertexCount: 12, start: 3 }), {
    ok: true,
    op: { kind: "Draw", vertexCount: 12, start: 3 },
    diagnostics: [],
    stats: { drawCalls: 1, triangles: 4 },
  });
});

test("Compute fixture requires compute shader and UAVs and clamps dispatch call count", () => {
  assert.deepEqual(computeStage({ computeShader: null, uavs: ["uav"] }), {
    ok: false,
    dispatches: 0,
    diagnostics: ["missing compute shader"],
  });

  assert.deepEqual(computeStage({ computeShader: "cs", uavs: [] }), {
    ok: false,
    dispatches: 0,
    diagnostics: ["missing UAV"],
  });

  assert.deepEqual(computeStage({
    computeShader: "cs",
    uavs: ["uav"],
    dispatch: { x: 4, y: 2, z: 1 },
    dispatchCallCount: 999,
  }), {
    ok: true,
    dispatches: 2048,
    callCount: 256,
    cleanup: ["unbind UAVs", "unbind samplers", "unbind SRVs", "unbind constant buffers"],
    diagnostics: [],
  });
});

test("Dispatch count fixtures preserve TiXL over-dispatch-by-one semantics", () => {
  assert.deepEqual(calcDispatchCount(64, { x: 16, y: 1, z: 1 }), { x: 5, y: 1, z: 1 });
  assert.deepEqual(calcDispatchCount(63, { x: 16, y: 1, z: 1 }), { x: 4, y: 1, z: 1 });
  assert.deepEqual(calcDispatchCount(64, { x: 0, y: 1, z: 1 }), { x: 0, y: 0, z: 0 });
  assert.deepEqual(calcInt2DispatchCount({ width: 960, height: 540 }, { x: 16, y: 16, z: 1 }), { x: 61, y: 34, z: 1 });
  assert.deepEqual(calcInt2DispatchCount({ width: 960, height: 540 }, { x: 0, y: 16, z: 1 }, { x: 1, y: 1, z: 1 }), { x: 1, y: 1, z: 1 });
});

test("Command contract captures known TiXL restore risks instead of copying them blindly", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Native ports should define explicit per-stage restore\s+coverage/);
  assert.match(source, /does not restore previous UAV bindings/);
  assert.match(source, /Treat scissor support as unproven/);
  assert.match(source, /Restore failure/);
  assert.match(source, /do not keep mutating global GPU state silently/);
});

test("Danger node experiments point E2 at the command stream evidence file", () => {
  const source = fs.readFileSync(dangerPath, "utf8");

  assert.match(source, /E2: Command Stream Shell/);
  assert.match(source, /COMMAND_STREAM_CONTRACT\.md/);
  assert.match(source, /tests\/command_stream_contract\.test\.js/);
});
