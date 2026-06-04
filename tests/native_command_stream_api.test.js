const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const apiPath = path.join(repoRoot, "docs/runtime/scripts/native_command_stream_api.py");
const laneScriptPath = path.join(repoRoot, "docs/runtime/scripts/native_runtime_lane.py");
const tracePath = path.join(repoRoot, "docs/runtime/artifacts/native_runtime_lane/native_runtime_lane_trace.json");

function runPython(source) {
  const run = spawnSync("python3", ["-c", source], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(run.status, 0, run.stderr);
  return JSON.parse(run.stdout);
}

test("native_command_stream_api exposes command stream, input assembler, shader binding, rasterizer, output merger, and draw validation", () => {
  const source = fs.readFileSync(apiPath, "utf8");

  assert.match(source, /class RenderState/);
  assert.match(source, /class NativeCommand/);
  assert.match(source, /class CommandStream/);
  assert.match(source, /def make_input_assembler_command/);
  assert.match(source, /def make_shader_stage_command/);
  assert.match(source, /def make_rasterizer_command/);
  assert.match(source, /def make_output_merger_command/);
  assert.match(source, /def make_clear_render_target_command/);
  assert.match(source, /def make_compute_shader_stage_command/);
  assert.match(source, /def make_draw_instanced_indirect_command/);
  assert.match(source, /def make_draw_command/);
  assert.match(source, /prepare -> update -> restore/);
});

test("CommandStream binds shader stage plus RTV and DSV views during update then restores state", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
depth = Texture2DHandle(id="rt.depth", width=320, height=180, format="D32_Float", bindFlags=("DepthStencil", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
dsv = create_texture_view(depth, "dsv")
command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "constantBuffers": [{"id": "pbr:glass"}],
  "shaderResources": [{"id": "brdfLut"}],
  "rasterizerState": {"fillMode": 3, "culling": "Back", "enableZTest": True, "enableZWrite": True},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180, "minDepth": 0, "maxDepth": 1}],
  "outputMergerState": {"blendState": "opaque", "depthStencilState": "defaultDepth", "depthStencilReference": 0, "blendFactor": [1, 1, 1, 1], "blendSampleMask": 4294967295},
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
}
state = RenderState()
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=dsv, outputMergerState=command["outputMergerState"]),
  make_draw_command(command),
])
result = stream.execute(state)
print(json.dumps(result))
`);

  assert.equal(result.ok, true);
  assert.deepEqual(result.trace.map((entry) => entry.op), [
    "prepare:InputAssemblerStage",
    "prepare:ShaderStage",
    "prepare:Rasterizer",
    "prepare:OutputMergerStage",
    "prepare:Draw",
    "update:InputAssemblerStage",
    "bindInputAssembler",
    "update:ShaderStage",
    "bindShaderStage",
    "update:Rasterizer",
    "bindRasterizer",
    "update:OutputMergerStage",
    "bindOutputMerger",
    "update:Draw",
    "draw",
    "restore:InputAssemblerStage",
    "restoreInputAssembler",
    "restore:ShaderStage",
    "restoreShaderStage",
    "restore:Rasterizer",
    "restoreRasterizer",
    "restore:OutputMergerStage",
    "restoreOutputMerger",
    "restore:Draw",
  ]);
  assert.equal(result.finalState.topology, null);
  assert.equal(result.finalState.vertexBuffer, null);
  assert.equal(result.finalState.indexBuffer, null);
  assert.deepEqual(result.finalState.renderTargetViews, []);
  assert.equal(result.finalState.vertexShader, null);
  assert.equal(result.finalState.pixelShader, null);
  assert.deepEqual(result.finalState.constantBuffers, []);
  assert.deepEqual(result.finalState.shaderResources, []);
  assert.equal(result.finalState.rasterizerState, null);
  assert.deepEqual(result.finalState.viewports, []);
  assert.equal(result.finalState.blendState, null);
  assert.equal(result.finalState.depthStencilState, null);
  assert.equal(result.stats.drawCalls, 1);
  assert.deepEqual(result.errors, []);
});

test("InputAssembler command binds TriangleList mesh buffers for Draw to consume", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
command = {
  "meshId": "cube",
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["inputAssembler", "shaderStage", "draw"],
}
stream = CommandStream([make_input_assembler_command(command), make_shader_stage_command(command), make_rasterizer_command(command), make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const bind = result.trace.find((entry) => entry.op === "bindInputAssembler");
  const draw = result.trace.find((entry) => entry.op === "draw");

  assert.equal(bind.topology, "TriangleList");
  assert.deepEqual(bind.vertexBuffer, { buffer: "cube.vb", srv: "cube.vb.srv" });
  assert.deepEqual(bind.indexBuffer, { buffer: "cube.ib", srv: "cube.ib.srv" });
  assert.equal(draw.topology, "TriangleList");
  assert.deepEqual(draw.vertexBuffer, { buffer: "cube.vb", srv: "cube.vb.srv" });
  assert.deepEqual(draw.indexBuffer, { buffer: "cube.ib", srv: "cube.ib.srv" });
  assert.equal(result.finalState.topology, null);
  assert.equal(result.finalState.vertexBuffer, null);
  assert.equal(result.finalState.indexBuffer, null);
});

test("InputAssembler command rejects missing buffers and unsupported topology", () => {
  const missingBuffer = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command

command = {"topology": "TriangleList", "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"}, "indexBuffer": {"buffer": "cube.ib"}}
stream = CommandStream([make_input_assembler_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);
  const badTopology = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command

command = {"topology": "LineList", "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"}, "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"}}
stream = CommandStream([make_input_assembler_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(missingBuffer.ok, false);
  assert.equal(missingBuffer.errors[0].code, "command_stream.missing_index_buffer");
  assert.equal(badTopology.ok, false);
  assert.equal(badTopology.errors[0].code, "command_stream.unsupported_topology");
  assert.equal(badTopology.errors[0].topology, "LineList");
});

test("ShaderStage command publishes bound buffers and resources for Draw to consume", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "constantBuffers": [{"id": "pbr:glass", "slot": 0}],
  "shaderResources": [{"id": "baseColor", "slot": 0}, {"id": "normal", "slot": 1}],
  "samplerStates": [{"id": "linearWrap", "slot": 0}],
  "rasterizerState": {"fillMode": 3, "culling": "Back", "enableZTest": True, "enableZWrite": True},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180, "minDepth": 0, "maxDepth": 1}],
  "commandOps": ["shaderStage", "draw"],
}
stream = CommandStream([make_input_assembler_command(command), make_shader_stage_command(command), make_rasterizer_command(command), make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const bind = result.trace.find((entry) => entry.op === "bindShaderStage");
  const draw = result.trace.find((entry) => entry.op === "draw");

  assert.deepEqual(bind.vertexShader, "vsMain");
  assert.deepEqual(bind.pixelShader, "psMain");
  assert.deepEqual(bind.constantBuffers, [{ id: "pbr:glass", slot: 0 }]);
  assert.equal(bind.shaderResources.length, 2);
  assert.deepEqual(bind.samplerStates, [{ id: "linearWrap", slot: 0 }]);
  assert.deepEqual(draw.constantBuffers, [{ id: "pbr:glass", slot: 0 }]);
  assert.equal(draw.shaderResources.length, 2);
  assert.deepEqual(result.finalState.constantBuffers, []);
  assert.deepEqual(result.finalState.shaderResources, []);
});

test("Rasterizer command binds state and viewports for Draw to consume", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=640, height=360, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
command = {
  "meshId": "cube",
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back", "enableZTest": True, "enableZWrite": True},
  "viewports": [{"x": 0, "y": 0, "width": 640, "height": 360, "minDepth": 0, "maxDepth": 1}],
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "draw"],
}
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None),
  make_draw_command(command),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const bind = result.trace.find((entry) => entry.op === "bindRasterizer");
  const draw = result.trace.find((entry) => entry.op === "draw");

  assert.deepEqual(bind.rasterizerState, { fillMode: 3, culling: "Back", enableZTest: true, enableZWrite: true });
  assert.deepEqual(bind.viewports, [{ x: 0, y: 0, width: 640, height: 360, minDepth: 0, maxDepth: 1 }]);
  assert.deepEqual(draw.rasterizerState, bind.rasterizerState);
  assert.deepEqual(draw.viewports, bind.viewports);
  assert.equal(result.finalState.rasterizerState, null);
  assert.deepEqual(result.finalState.viewports, []);
});

test("Rasterizer command rejects missing or invalid viewports", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_rasterizer_command

bad = {"rasterizerState": {"fillMode": 3, "culling": "Back"}, "viewports": [{"x": 0, "y": 0, "width": 0, "height": 180}]}
stream = CommandStream([make_rasterizer_command(bad)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.invalid_viewport");
  assert.equal(result.errors[0].viewport.width, 0);
});

test("OutputMerger command binds render targets plus blend and depth state for Draw to consume", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
depth = Texture2DHandle(id="rt.depth", width=320, height=180, format="D32_Float", bindFlags=("DepthStencil", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
dsv = create_texture_view(depth, "dsv")
om = {"blendState": "opaque", "depthStencilState": "depthReadWrite", "depthStencilReference": 1, "blendFactor": [1, 1, 1, 1], "blendSampleMask": 255}
command = {
  "meshId": "cube",
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
}
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=dsv, outputMergerState=om),
  make_draw_command(command),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const bind = result.trace.find((entry) => entry.op === "bindOutputMerger");
  const draw = result.trace.find((entry) => entry.op === "draw");

  assert.equal(bind.blendState, "opaque");
  assert.deepEqual(bind.unorderedAccessViews, []);
  assert.equal(bind.depthStencilState, "depthReadWrite");
  assert.equal(bind.depthStencilReference, 1);
  assert.deepEqual(bind.blendFactor, [1, 1, 1, 1]);
  assert.equal(bind.blendSampleMask, 255);
  assert.equal(draw.blendState, "opaque");
  assert.equal(draw.depthStencilState, "depthReadWrite");
  assert.equal(result.finalState.blendState, null);
  assert.equal(result.finalState.depthStencilState, null);
});

test("OutputMerger command binds UAVs separately from compute UAV state", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
target = Texture2DHandle(id="fx.omUav", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
rtv = create_texture_view(color, "rtv")
uav = create_texture_view(target, "uav")
command = {
  "meshId": "cube",
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
}
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None, unorderedAccessViews=[uav]),
  make_draw_command(command),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const bind = result.trace.find((entry) => entry.op === "bindOutputMerger");
  const draw = result.trace.find((entry) => entry.op === "draw");

  assert.equal(bind.unorderedAccessViews[0].textureId, "fx.omUav");
  assert.equal(bind.unorderedAccessViews[0].type, "UAV");
  assert.equal(draw.unorderedAccessViews[0].textureId, "fx.omUav");
  assert.deepEqual(result.finalState.outputMergerUavs, []);
  assert.deepEqual(result.finalState.computeUavs, []);
  assert.equal(result.finalState.resourceAccess["fx.omUav"], "UnorderedAccessWrite");
});

test("OutputMerger command refuses invalid view kinds instead of silently binding them", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_output_merger_command

texture = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
srv = create_texture_view(texture, "srv")
stream = CommandStream([make_output_merger_command(renderTargetViews=[srv], depthStencilView=None)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.invalid_rtv");
  assert.equal(result.errors[0].view.type, "SRV");
  assert.equal(result.stats.drawCalls, 0);
});

test("OutputMerger command refuses invalid UAV view kinds", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_output_merger_command

texture = Texture2DHandle(id="fx.bad", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(texture, "rtv")
srv = create_texture_view(texture, "srv")
stream = CommandStream([make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None, unorderedAccessViews=[srv])])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.invalid_output_merger_uav");
  assert.equal(result.errors[0].view.type, "SRV");
  assert.equal(result.stats.drawCalls, 0);
});

test("ClearRenderTarget command clears RTV and DSV views without binding output merger", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_clear_render_target_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
depth = Texture2DHandle(id="rt.depth", width=320, height=180, format="D32_Float", bindFlags=("DepthStencil", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
dsv = create_texture_view(depth, "dsv")
stream = CommandStream([
  make_clear_render_target_command(renderTargetViews=[rtv], depthStencilView=dsv, clearColor=[0.1, 0.2, 0.3, 1.0]),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  assert.deepEqual(result.trace.map((entry) => entry.op), [
    "prepare:ClearRenderTarget",
    "update:ClearRenderTarget",
    "clearRenderTargetView",
    "clearDepthStencilView",
    "restore:ClearRenderTarget",
  ]);
  const colorClear = result.trace.find((entry) => entry.op === "clearRenderTargetView");
  const depthClear = result.trace.find((entry) => entry.op === "clearDepthStencilView");

  assert.equal(colorClear.textureId, "rt.color");
  assert.deepEqual(colorClear.clearColor, [0.1, 0.2, 0.3, 1.0]);
  assert.equal(depthClear.textureId, "rt.depth");
  assert.equal(depthClear.depth, 1);
  assert.equal(result.stats.clearCalls, 2);
  assert.deepEqual(result.finalState.renderTargetViews, []);
  assert.equal(result.finalState.depthStencilView, null);
  assert.equal(result.finalState.resourceAccess["rt.color"], "RenderTargetWrite");
  assert.equal(result.finalState.resourceAccess["rt.depth"], "DepthStencilWrite");
});

test("ClearRenderTarget command refuses invalid RTV and DSV views", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_clear_render_target_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
depth = Texture2DHandle(id="rt.depth", width=320, height=180, format="D32_Float", bindFlags=("DepthStencil", "ShaderResource"))
srv = create_texture_view(color, "srv")
bad_dsv = create_texture_view(depth, "srv")
stream = CommandStream([
  make_clear_render_target_command(renderTargetViews=[srv], depthStencilView=bad_dsv, clearColor=[0, 0, 0, 1]),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.invalid_clear_rtv");
  assert.equal(result.errors[1].code, "command_stream.invalid_clear_dsv");
  assert.equal(result.stats.clearCalls, 0);
});

test("ComputeShaderStage command binds UAVs, dispatches, and restores compute state", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_compute_shader_stage_command

writable = Texture2DHandle(id="compute.target", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
uav = create_texture_view(writable, "uav")
command = {
  "computeShaderEntry": "csMain",
  "dispatch": {"x": 4, "y": 2, "z": 1},
  "dispatchCallCount": 999,
  "constantBuffers": [{"id": "compute.params", "slot": 0}],
  "shaderResources": [{"id": "source.srv", "slot": 0}],
  "samplerStates": [{"id": "linearClamp", "slot": 0}],
}
stream = CommandStream([make_compute_shader_stage_command(command, uavs=[uav])])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  assert.deepEqual(result.trace.map((entry) => entry.op), [
    "prepare:ComputeShaderStage",
    "update:ComputeShaderStage",
    "bindComputeShaderStage",
    "dispatchCompute",
    "restore:ComputeShaderStage",
    "restoreComputeShaderStage",
  ]);
  const bind = result.trace.find((entry) => entry.op === "bindComputeShaderStage");
  const dispatch = result.trace.find((entry) => entry.op === "dispatchCompute");

  assert.equal(bind.computeShader, "csMain");
  assert.equal(bind.uavs[0].type, "UAV");
  assert.deepEqual(bind.constantBuffers, [{ id: "compute.params", slot: 0 }]);
  assert.equal(dispatch.callCount, 256);
  assert.deepEqual(dispatch.dispatch, { x: 4, y: 2, z: 1 });
  assert.equal(result.stats.computeDispatchCalls, 256);
  assert.equal(result.stats.computeThreadGroups, 2048);
  assert.equal(result.finalState.computeShader, null);
  assert.deepEqual(result.finalState.computeUavs, []);
});

test("ComputeShaderStage command refuses missing compute shader and invalid UAVs", () => {
  const missingShader = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_compute_shader_stage_command

writable = Texture2DHandle(id="compute.target", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
uav = create_texture_view(writable, "uav")
stream = CommandStream([make_compute_shader_stage_command({"dispatch": {"x": 1, "y": 1, "z": 1}}, uavs=[uav])])
print(json.dumps(stream.execute(RenderState())))
`);
  const invalidUav = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_compute_shader_stage_command

texture = Texture2DHandle(id="read.only", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource",))
bad = create_texture_view(texture, "uav")
stream = CommandStream([make_compute_shader_stage_command({"computeShaderEntry": "csMain", "dispatch": {"x": 1, "y": 1, "z": 1}}, uavs=[bad])])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(missingShader.ok, false);
  assert.equal(missingShader.errors[0].code, "command_stream.missing_compute_shader");
  assert.equal(missingShader.stats.computeDispatchCalls, 0);
  assert.equal(invalidUav.ok, false);
  assert.equal(invalidUav.errors[0].code, "command_stream.invalid_uav");
  assert.equal(invalidUav.errors[0].view.type, "UAV");
});

test("DrawInstancedIndirect unbinds compute state before drawing with indirect args", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_compute_shader_stage_command, make_draw_instanced_indirect_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
writable = Texture2DHandle(id="compute.target", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
uav = create_texture_view(writable, "uav")
command = {
  "meshId": "cube",
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "drawInstancedIndirect"],
}
compute = {"computeShaderEntry": "csMain", "dispatch": {"x": 1, "y": 1, "z": 1}}
indirect = {"argsBuffer": {"buffer": "draw.args", "srv": "draw.args.srv"}, "alignedByteOffsetForArgs": 16}
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None),
  make_compute_shader_stage_command(compute, uavs=[uav]),
  make_draw_instanced_indirect_command(indirect),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  assert.deepEqual(result.trace.map((entry) => entry.op), [
    "prepare:InputAssemblerStage",
    "prepare:ShaderStage",
    "prepare:Rasterizer",
    "prepare:OutputMergerStage",
    "prepare:ComputeShaderStage",
    "prepare:DrawInstancedIndirect",
    "update:InputAssemblerStage",
    "bindInputAssembler",
    "update:ShaderStage",
    "bindShaderStage",
    "update:Rasterizer",
    "bindRasterizer",
    "update:OutputMergerStage",
    "bindOutputMerger",
    "update:ComputeShaderStage",
    "bindComputeShaderStage",
    "dispatchCompute",
    "update:DrawInstancedIndirect",
    "unbindComputeBeforeIndirectDraw",
    "drawInstancedIndirect",
    "restore:InputAssemblerStage",
    "restoreInputAssembler",
    "restore:ShaderStage",
    "restoreShaderStage",
    "restore:Rasterizer",
    "restoreRasterizer",
    "restore:OutputMergerStage",
    "restoreOutputMerger",
    "restore:ComputeShaderStage",
    "restoreComputeShaderStage",
    "restore:DrawInstancedIndirect",
  ]);
  const draw = result.trace.find((entry) => entry.op === "drawInstancedIndirect");

  assert.deepEqual(draw.argsBuffer, { buffer: "draw.args", srv: "draw.args.srv" });
  assert.equal(draw.alignedByteOffsetForArgs, 16);
  assert.equal(result.stats.drawCalls, 1);
  assert.equal(result.stats.indirectDrawCalls, 1);
  assert.deepEqual(result.finalState.computeUavs, []);
});

test("DrawInstancedIndirect refuses missing args buffer", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_output_merger_command, make_draw_instanced_indirect_command

color = Texture2DHandle(id="rt.color", width=320, height=180, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
rtv = create_texture_view(color, "rtv")
command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
}
stream = CommandStream([
  make_input_assembler_command(command),
  make_shader_stage_command(command),
  make_rasterizer_command(command),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None),
  make_draw_instanced_indirect_command({"argsBuffer": None}),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_indirect_args_buffer");
  assert.equal(result.stats.indirectDrawCalls, 0);
});

test("CommandStream records UAV write to shader resource read barriers", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_compute_shader_stage_command, make_shader_stage_command

shared = Texture2DHandle(id="fx.ping", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
uav = create_texture_view(shared, "uav")
srv = create_texture_view(shared, "srv")
compute = {"computeShaderEntry": "csMain", "dispatch": {"x": 1, "y": 1, "z": 1}}
shade = {
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "shaderResources": [srv.to_json()],
}
stream = CommandStream([
  make_compute_shader_stage_command(compute, uavs=[uav]),
  make_shader_stage_command(shade),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  assert.deepEqual(result.trace.map((entry) => entry.op), [
    "prepare:ComputeShaderStage",
    "prepare:ShaderStage",
    "update:ComputeShaderStage",
    "bindComputeShaderStage",
    "dispatchCompute",
    "update:ShaderStage",
    "resourceBarrier",
    "bindShaderStage",
    "restore:ComputeShaderStage",
    "restoreComputeShaderStage",
    "restore:ShaderStage",
    "restoreShaderStage",
  ]);
  const barrier = result.trace.find((entry) => entry.op === "resourceBarrier");

  assert.equal(barrier.textureId, "fx.ping");
  assert.equal(barrier.before, "UAVWrite");
  assert.equal(barrier.after, "ShaderResourceRead");
  assert.equal(barrier.reason, "uav-write-to-srv-read");
  assert.equal(result.stats.resourceBarriers, 1);
  assert.equal(result.finalState.resourceAccess["fx.ping"], "ShaderResourceRead");
});

test("CommandStream records UAV write to render-target barriers", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_compute_shader_stage_command, make_output_merger_command

shared = Texture2DHandle(id="fx.pong", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess", "RenderTarget"))
uav = create_texture_view(shared, "uav")
rtv = create_texture_view(shared, "rtv")
compute = {"computeShaderEntry": "csMain", "dispatch": {"x": 1, "y": 1, "z": 1}}
stream = CommandStream([
  make_compute_shader_stage_command(compute, uavs=[uav]),
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const barrier = result.trace.find((entry) => entry.op === "resourceBarrier");

  assert.equal(barrier.textureId, "fx.pong");
  assert.equal(barrier.before, "UAVWrite");
  assert.equal(barrier.after, "RenderTargetWrite");
  assert.equal(barrier.reason, "uav-write-to-rtv-write");
  assert.equal(result.stats.resourceBarriers, 1);
  assert.equal(result.finalState.resourceAccess["fx.pong"], "RenderTargetWrite");
});

test("CommandStream records output-merger UAV write to shader resource read barriers", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_resource_api import Texture2DHandle, create_texture_view
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_output_merger_command, make_shader_stage_command

color = Texture2DHandle(id="rt.color", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("RenderTarget", "ShaderResource"))
shared = Texture2DHandle(id="fx.omPing", width=128, height=64, format="R16G16B16A16_Float", bindFlags=("ShaderResource", "UnorderedAccess"))
rtv = create_texture_view(color, "rtv")
uav = create_texture_view(shared, "uav")
srv = create_texture_view(shared, "srv")
shade = {
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "shaderResources": [srv.to_json()],
}
stream = CommandStream([
  make_output_merger_command(renderTargetViews=[rtv], depthStencilView=None, unorderedAccessViews=[uav]),
  make_shader_stage_command(shade),
])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, true);
  const barrier = result.trace.find((entry) => entry.op === "resourceBarrier");

  assert.equal(barrier.textureId, "fx.omPing");
  assert.equal(barrier.before, "UnorderedAccessWrite");
  assert.equal(barrier.after, "ShaderResourceRead");
  assert.equal(barrier.reason, "uav-write-to-srv-read");
  assert.equal(result.stats.resourceBarriers, 1);
  assert.equal(result.finalState.resourceAccess["fx.omPing"], "ShaderResourceRead");
});

test("Draw command refuses to run when OutputMerger was not bound first", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_draw_command

command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "draw"],
}
stream = CommandStream([make_input_assembler_command(command), make_shader_stage_command(command), make_rasterizer_command(command), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_output_merger");
  assert.equal(result.stats.drawCalls, 0);
});

test("ShaderStage command refuses missing shader entries and preserves TiXL failure meaning", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_rasterizer_command, make_draw_command

bad = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "commandOps": ["inputAssembler", "shaderStage", "rasterizer", "outputMerger", "draw"],
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
}
stream = CommandStream([make_input_assembler_command(bad), make_shader_stage_command(bad), make_rasterizer_command(bad), make_draw_command(bad)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_shader_stage");
  assert.equal(result.stats.drawCalls, 0);
});

test("Draw command refuses to run when ShaderStage was not bound first", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_rasterizer_command, make_draw_command

command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "commandOps": ["inputAssembler", "draw"],
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
}
stream = CommandStream([make_input_assembler_command(command), make_rasterizer_command(command), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_shader_stage");
  assert.equal(result.stats.drawCalls, 0);
});

test("Draw command refuses to run when InputAssembler was not bound first", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_shader_stage_command, make_rasterizer_command, make_draw_command

command = {
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "rasterizerState": {"fillMode": 3, "culling": "Back"},
  "viewports": [{"x": 0, "y": 0, "width": 320, "height": 180}],
  "commandOps": ["shaderStage", "rasterizer", "draw"],
}
stream = CommandStream([make_shader_stage_command(command), make_rasterizer_command(command), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_input_assembler");
  assert.equal(result.stats.drawCalls, 0);
});

test("Draw command refuses to run when Rasterizer was not bound first", () => {
  const result = runPython(`
import json
from docs.runtime.scripts.native_command_stream_api import CommandStream, RenderState, make_input_assembler_command, make_shader_stage_command, make_draw_command

command = {
  "topology": "TriangleList",
  "vertexBuffer": {"buffer": "cube.vb", "srv": "cube.vb.srv"},
  "indexBuffer": {"buffer": "cube.ib", "srv": "cube.ib.srv"},
  "vertexShaderEntry": "vsMain",
  "pixelShaderEntry": "psMain",
  "commandOps": ["inputAssembler", "shaderStage", "draw"],
}
stream = CommandStream([make_input_assembler_command(command), make_shader_stage_command(command), make_draw_command(command)])
print(json.dumps(stream.execute(RenderState())))
`);

  assert.equal(result.ok, false);
  assert.equal(result.errors[0].code, "command_stream.missing_rasterizer");
  assert.equal(result.stats.drawCalls, 0);
});

test("native runtime lane uses native_command_stream_api and publishes command stream trace", () => {
  const source = fs.readFileSync(laneScriptPath, "utf8");

  assert.match(source, /from native_command_stream_api import/);
  const trace = JSON.parse(fs.readFileSync(tracePath, "utf8"));
  const commandEntry = trace.find((entry) => entry.op === "commandStream.execute");

  assert.ok(commandEntry);
  assert.equal(commandEntry.result.ok, true);
  assert.equal(commandEntry.result.stats.drawCalls, 1);
  assert.ok(commandEntry.result.trace.find((entry) => entry.op === "bindInputAssembler"));
  assert.ok(commandEntry.result.trace.find((entry) => entry.op === "bindShaderStage"));
  assert.ok(commandEntry.result.trace.find((entry) => entry.op === "bindRasterizer"));
  assert.equal(commandEntry.result.finalState.topology, null);
  assert.equal(commandEntry.result.finalState.rasterizerState, null);
  assert.deepEqual(commandEntry.result.finalState.renderTargetViews, []);
  assert.equal(commandEntry.result.finalState.vertexShader, null);
  assert.equal(commandEntry.result.finalState.pixelShader, null);
});
