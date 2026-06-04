#!/usr/bin/env python3
"""
Native command stream API for render-state commands.

The command stream preserves TiXL's prepare -> update -> restore force without
pretending this data-backed shell is a real GPU backend.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable

try:
    from native_resource_api import TextureViewHandle
except ModuleNotFoundError:
    from docs.runtime.scripts.native_resource_api import TextureViewHandle


@dataclass
class RenderState:
    topology: str | None = None
    vertexBuffer: dict[str, Any] | None = None
    indexBuffer: dict[str, Any] | None = None
    vertexShader: str | None = None
    pixelShader: str | None = None
    constantBuffers: list[Any] = field(default_factory=list)
    shaderResources: list[Any] = field(default_factory=list)
    samplerStates: list[Any] = field(default_factory=list)
    rasterizerState: dict[str, Any] | None = None
    viewports: list[dict[str, Any]] = field(default_factory=list)
    renderTargetViews: list[dict[str, Any]] = field(default_factory=list)
    depthStencilView: dict[str, Any] | None = None
    outputMergerUavs: list[dict[str, Any]] = field(default_factory=list)
    blendState: Any | None = None
    depthStencilState: Any | None = None
    depthStencilReference: int = 0
    blendFactor: list[float] = field(default_factory=list)
    blendSampleMask: int | None = None
    computeShader: str | None = None
    computeConstantBuffers: list[Any] = field(default_factory=list)
    computeShaderResources: list[Any] = field(default_factory=list)
    computeUavs: list[dict[str, Any]] = field(default_factory=list)
    computeSamplerStates: list[Any] = field(default_factory=list)
    drawCalls: int = 0
    triangles: int = 0
    indirectDrawCalls: int = 0
    computeDispatchCalls: int = 0
    computeThreadGroups: int = 0
    resourceAccess: dict[str, str] = field(default_factory=dict)
    resourceBarriers: int = 0
    clearCalls: int = 0

    def snapshot(self) -> dict[str, Any]:
        return {
            "topology": self.topology,
            "vertexBuffer": self.vertexBuffer,
            "indexBuffer": self.indexBuffer,
            "vertexShader": self.vertexShader,
            "pixelShader": self.pixelShader,
            "constantBuffers": list(self.constantBuffers),
            "shaderResources": list(self.shaderResources),
            "samplerStates": list(self.samplerStates),
            "rasterizerState": self.rasterizerState,
            "viewports": list(self.viewports),
            "renderTargetViews": list(self.renderTargetViews),
            "depthStencilView": self.depthStencilView,
            "outputMergerUavs": list(self.outputMergerUavs),
            "blendState": self.blendState,
            "depthStencilState": self.depthStencilState,
            "depthStencilReference": self.depthStencilReference,
            "blendFactor": list(self.blendFactor),
            "blendSampleMask": self.blendSampleMask,
            "computeShader": self.computeShader,
            "computeConstantBuffers": list(self.computeConstantBuffers),
            "computeShaderResources": list(self.computeShaderResources),
            "computeUavs": list(self.computeUavs),
            "computeSamplerStates": list(self.computeSamplerStates),
            "drawCalls": self.drawCalls,
            "triangles": self.triangles,
            "indirectDrawCalls": self.indirectDrawCalls,
            "computeDispatchCalls": self.computeDispatchCalls,
            "computeThreadGroups": self.computeThreadGroups,
            "resourceAccess": dict(self.resourceAccess),
            "resourceBarriers": self.resourceBarriers,
            "clearCalls": self.clearCalls,
        }

    def restore_input_assembler(self, snapshot: dict[str, Any]) -> None:
        self.topology = snapshot.get("topology")
        self.vertexBuffer = snapshot.get("vertexBuffer")
        self.indexBuffer = snapshot.get("indexBuffer")

    def restore_shader_stage(self, snapshot: dict[str, Any]) -> None:
        self.vertexShader = snapshot.get("vertexShader")
        self.pixelShader = snapshot.get("pixelShader")
        self.constantBuffers = list(snapshot.get("constantBuffers", []))
        self.shaderResources = list(snapshot.get("shaderResources", []))
        self.samplerStates = list(snapshot.get("samplerStates", []))

    def restore_rasterizer(self, snapshot: dict[str, Any]) -> None:
        self.rasterizerState = snapshot.get("rasterizerState")
        self.viewports = list(snapshot.get("viewports", []))

    def restore_targets(self, snapshot: dict[str, Any]) -> None:
        self.renderTargetViews = list(snapshot.get("renderTargetViews", []))
        self.depthStencilView = snapshot.get("depthStencilView")
        self.outputMergerUavs = list(snapshot.get("outputMergerUavs", []))
        self.blendState = snapshot.get("blendState")
        self.depthStencilState = snapshot.get("depthStencilState")
        self.depthStencilReference = snapshot.get("depthStencilReference", 0)
        self.blendFactor = list(snapshot.get("blendFactor", []))
        self.blendSampleMask = snapshot.get("blendSampleMask")

    def restore_compute_stage(self, snapshot: dict[str, Any]) -> None:
        self.computeShader = snapshot.get("computeShader")
        self.computeConstantBuffers = list(snapshot.get("computeConstantBuffers", []))
        self.computeShaderResources = list(snapshot.get("computeShaderResources", []))
        self.computeUavs = list(snapshot.get("computeUavs", []))
        self.computeSamplerStates = list(snapshot.get("computeSamplerStates", []))


@dataclass
class NativeCommand:
    id: str
    prepare: Callable[[RenderState, list[dict[str, Any]], list[dict[str, Any]]], None]
    update: Callable[[RenderState, list[dict[str, Any]], list[dict[str, Any]]], None]
    restore: Callable[[RenderState, list[dict[str, Any]], list[dict[str, Any]]], None]


class CommandStream:
    def __init__(self, commands: list[NativeCommand], enabled: bool = True) -> None:
        self.commands = commands
        self.enabled = enabled

    def execute(self, state: RenderState) -> dict[str, Any]:
        trace: list[dict[str, Any]] = []
        errors: list[dict[str, Any]] = []

        if not self.enabled:
            return {
                "ok": True,
                "trace": trace,
                "errors": errors,
                "stats": stats_for(state),
                "finalState": state.snapshot(),
            }

        for command in self.commands:
            trace.append({"op": f"prepare:{command.id}"})
            command.prepare(state, trace, errors)
        for command in self.commands:
            trace.append({"op": f"update:{command.id}"})
            command.update(state, trace, errors)
        for command in self.commands:
            trace.append({"op": f"restore:{command.id}"})
            command.restore(state, trace, errors)

        return {
            "ok": not errors,
            "trace": trace,
            "errors": errors,
            "stats": stats_for(state),
            "finalState": state.snapshot(),
        }


def stats_for(state: RenderState) -> dict[str, int]:
    return {
        "drawCalls": state.drawCalls,
        "triangles": state.triangles,
        "indirectDrawCalls": state.indirectDrawCalls,
        "computeDispatchCalls": state.computeDispatchCalls,
        "computeThreadGroups": state.computeThreadGroups,
        "resourceBarriers": state.resourceBarriers,
        "clearCalls": state.clearCalls,
    }


def make_input_assembler_command(command: dict[str, Any]) -> NativeCommand:
    saved_state: dict[str, Any] = {}

    def prepare(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        nonlocal saved_state
        saved_state = state.snapshot()

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        topology = command.get("topology", "TriangleList")
        if topology != "TriangleList":
            errors.append({
                "code": "command_stream.unsupported_topology",
                "message": f"unsupported topology: {topology}",
                "topology": topology,
                "supported": ["TriangleList"],
            })
            return

        vertex_buffer = command.get("vertexBuffer")
        if not vertex_buffer or not vertex_buffer.get("buffer") or not vertex_buffer.get("srv"):
            errors.append({
                "code": "command_stream.missing_vertex_buffer",
                "message": "Vertex buffer undefined",
                "vertexBuffer": vertex_buffer,
            })
            return

        index_buffer = command.get("indexBuffer")
        if not index_buffer or not index_buffer.get("buffer") or not index_buffer.get("srv"):
            errors.append({
                "code": "command_stream.missing_index_buffer",
                "message": "Indices buffer undefined",
                "indexBuffer": index_buffer,
            })
            return

        state.topology = topology
        state.vertexBuffer = dict(vertex_buffer)
        state.indexBuffer = dict(index_buffer)
        trace.append({
            "op": "bindInputAssembler",
            "topology": state.topology,
            "vertexBuffer": state.vertexBuffer,
            "indexBuffer": state.indexBuffer,
        })

    def restore(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        state.restore_input_assembler(saved_state)
        trace.append({"op": "restoreInputAssembler"})

    return NativeCommand("InputAssemblerStage", prepare, update, restore)


def make_shader_stage_command(command: dict[str, Any]) -> NativeCommand:
    saved_state: dict[str, Any] = {}

    def prepare(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        nonlocal saved_state
        saved_state = state.snapshot()

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        vertex_shader = command.get("vertexShaderEntry")
        pixel_shader = command.get("pixelShaderEntry")
        if vertex_shader != "vsMain" or pixel_shader != "psMain":
            errors.append({
                "code": "command_stream.missing_shader_stage",
                "message": "Trying to issue draw call, but pixel and/or vertex shader are null.",
                "vertexShader": vertex_shader,
                "pixelShader": pixel_shader,
            })
            return

        state.vertexShader = vertex_shader
        state.pixelShader = pixel_shader
        state.constantBuffers = list(command.get("constantBuffers", []))
        state.shaderResources = list(command.get("shaderResources", []))
        state.samplerStates = list(command.get("samplerStates", []))
        for resource in state.shaderResources:
            record_resource_access(
                state,
                trace,
                resource,
                "ShaderResourceRead",
                "uav-write-to-srv-read",
            )
        trace.append({
            "op": "bindShaderStage",
            "vertexShader": state.vertexShader,
            "pixelShader": state.pixelShader,
            "constantBuffers": list(state.constantBuffers),
            "shaderResources": list(state.shaderResources),
            "samplerStates": list(state.samplerStates),
        })

    def restore(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        state.restore_shader_stage(saved_state)
        trace.append({"op": "restoreShaderStage"})

    return NativeCommand("ShaderStage", prepare, update, restore)


def make_rasterizer_command(command: dict[str, Any]) -> NativeCommand:
    saved_state: dict[str, Any] = {}

    def prepare(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        nonlocal saved_state
        saved_state = state.snapshot()

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        rasterizer_state = dict(command.get("rasterizerState", {}))
        culling = rasterizer_state.get("culling", "Back")
        if culling not in ["None", "Front", "Back"]:
            errors.append({
                "code": "command_stream.invalid_rasterizer_state",
                "message": f"unsupported culling: {culling}",
                "rasterizerState": rasterizer_state,
            })
            return

        viewports = [dict(viewport) for viewport in command.get("viewports", [])]
        if not viewports:
            errors.append({
                "code": "command_stream.missing_viewport",
                "message": "Rasterizer requires at least one viewport.",
            })
            return

        for viewport in viewports:
            if viewport.get("width", 0) <= 0 or viewport.get("height", 0) <= 0:
                errors.append({
                    "code": "command_stream.invalid_viewport",
                    "message": "Viewport width and height must be positive.",
                    "viewport": viewport,
                })
                return

        state.rasterizerState = rasterizer_state
        state.viewports = viewports
        trace.append({
            "op": "bindRasterizer",
            "rasterizerState": state.rasterizerState,
            "viewports": list(state.viewports),
        })

    def restore(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        state.restore_rasterizer(saved_state)
        trace.append({"op": "restoreRasterizer"})

    return NativeCommand("Rasterizer", prepare, update, restore)


def make_output_merger_command(
    *,
    renderTargetViews: list[TextureViewHandle],
    depthStencilView: TextureViewHandle | None,
    unorderedAccessViews: list[TextureViewHandle] | None = None,
    outputMergerState: dict[str, Any] | None = None,
) -> NativeCommand:
    saved_state: dict[str, Any] = {}

    def prepare(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        nonlocal saved_state
        saved_state = state.snapshot()

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        rtv_payloads = []
        for view in renderTargetViews:
            if not view.ok or view.type != "RTV":
                errors.append({
                    "code": "command_stream.invalid_rtv",
                    "view": view.to_json(),
                })
                continue
            rtv_payloads.append(view.to_json())

        dsv_payload = None
        if depthStencilView is not None:
            if not depthStencilView.ok or depthStencilView.type != "DSV":
                errors.append({
                    "code": "command_stream.invalid_dsv",
                    "view": depthStencilView.to_json(),
                })
            else:
                dsv_payload = depthStencilView.to_json()

        uav_payloads = []
        for view in unorderedAccessViews or []:
            if not view.ok or view.type != "UAV":
                errors.append({
                    "code": "command_stream.invalid_output_merger_uav",
                    "view": view.to_json(),
                })
                continue
            uav_payloads.append(view.to_json())

        if errors:
            return

        merger_state = outputMergerState or {}
        state.renderTargetViews = rtv_payloads
        state.depthStencilView = dsv_payload
        state.outputMergerUavs = uav_payloads
        for view in state.renderTargetViews:
            record_resource_access(
                state,
                trace,
                view,
                "RenderTargetWrite",
                "uav-write-to-rtv-write",
            )
        for view in state.outputMergerUavs:
            record_resource_access(
                state,
                trace,
                view,
                "UnorderedAccessWrite",
                "uav-write-to-output-merger-uav-write",
            )
        state.blendState = merger_state.get("blendState", "opaque")
        state.depthStencilState = merger_state.get("depthStencilState", "defaultDepth")
        state.depthStencilReference = merger_state.get("depthStencilReference", 0)
        state.blendFactor = list(merger_state.get("blendFactor", [1, 1, 1, 1]))
        state.blendSampleMask = merger_state.get("blendSampleMask", 0xFFFFFFFF)
        trace.append({
            "op": "bindOutputMerger",
            "renderTargetViews": rtv_payloads,
            "depthStencilView": dsv_payload,
            "unorderedAccessViews": uav_payloads,
            "blendState": state.blendState,
            "depthStencilState": state.depthStencilState,
            "depthStencilReference": state.depthStencilReference,
            "blendFactor": list(state.blendFactor),
            "blendSampleMask": state.blendSampleMask,
        })

    def restore(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        state.restore_targets(saved_state)
        trace.append({"op": "restoreOutputMerger"})

    return NativeCommand("OutputMergerStage", prepare, update, restore)


def make_clear_render_target_command(
    *,
    renderTargetViews: list[TextureViewHandle],
    depthStencilView: TextureViewHandle | None,
    clearColor: list[float] | None = None,
) -> NativeCommand:
    def noop(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        return

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        rtv_payloads = []
        for view in renderTargetViews:
            if not view.ok or view.type != "RTV":
                errors.append({
                    "code": "command_stream.invalid_clear_rtv",
                    "view": view.to_json(),
                })
                continue
            rtv_payloads.append(view.to_json())

        dsv_payload = None
        if depthStencilView is not None:
            if not depthStencilView.ok or depthStencilView.type != "DSV":
                errors.append({
                    "code": "command_stream.invalid_clear_dsv",
                    "view": depthStencilView.to_json(),
                })
            else:
                dsv_payload = depthStencilView.to_json()

        if errors:
            return

        color = list(clearColor if clearColor is not None else [0, 0, 0, 0])
        for view in rtv_payloads:
            record_resource_access(
                state,
                trace,
                view,
                "RenderTargetWrite",
                "uav-write-to-rtv-write",
            )
            state.clearCalls += 1
            trace.append({
                "op": "clearRenderTargetView",
                "textureId": view.get("textureId"),
                "view": view,
                "clearColor": color,
            })

        if dsv_payload is not None:
            record_resource_access(
                state,
                trace,
                dsv_payload,
                "DepthStencilWrite",
                "uav-write-to-dsv-write",
            )
            state.clearCalls += 1
            trace.append({
                "op": "clearDepthStencilView",
                "textureId": dsv_payload.get("textureId"),
                "view": dsv_payload,
                "depth": 1,
            })

    return NativeCommand("ClearRenderTarget", noop, update, noop)


def make_compute_shader_stage_command(
    command: dict[str, Any],
    *,
    uavs: list[TextureViewHandle],
) -> NativeCommand:
    saved_state: dict[str, Any] = {}

    def prepare(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        nonlocal saved_state
        saved_state = state.snapshot()

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        compute_shader = command.get("computeShaderEntry")
        if compute_shader is None:
            errors.append({
                "code": "command_stream.missing_compute_shader",
                "message": "Trying to dispatch compute, but compute shader is null.",
            })
            return

        uav_payloads = []
        for view in uavs:
            if not view.ok or view.type != "UAV":
                errors.append({
                    "code": "command_stream.invalid_uav",
                    "view": view.to_json(),
                })
                continue
            uav_payloads.append(view.to_json())

        if not uav_payloads:
            errors.append({
                "code": "command_stream.missing_uav",
                "message": "Trying to dispatch compute, but no UAV is bound.",
            })

        if errors:
            return

        dispatch = normalized_dispatch(command.get("dispatch", {}))
        call_count = min(max(int(command.get("dispatchCallCount", 1)), 1), 256)
        groups_per_call = dispatch["x"] * dispatch["y"] * dispatch["z"]

        state.computeShader = compute_shader
        state.computeConstantBuffers = list(command.get("constantBuffers", []))
        state.computeShaderResources = list(command.get("shaderResources", []))
        state.computeUavs = uav_payloads
        state.computeSamplerStates = list(command.get("samplerStates", []))
        trace.append({
            "op": "bindComputeShaderStage",
            "computeShader": state.computeShader,
            "constantBuffers": list(state.computeConstantBuffers),
            "shaderResources": list(state.computeShaderResources),
            "uavs": list(state.computeUavs),
            "samplerStates": list(state.computeSamplerStates),
        })

        state.computeDispatchCalls += call_count
        state.computeThreadGroups += groups_per_call * call_count
        for view in state.computeUavs:
            set_resource_access(state, view, "UAVWrite")
        trace.append({
            "op": "dispatchCompute",
            "dispatch": dispatch,
            "callCount": call_count,
            "threadGroups": groups_per_call * call_count,
        })

    def restore(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        state.restore_compute_stage(saved_state)
        trace.append({"op": "restoreComputeShaderStage"})

    return NativeCommand("ComputeShaderStage", prepare, update, restore)


def normalized_dispatch(dispatch: dict[str, Any]) -> dict[str, int]:
    return {
        "x": max(int(dispatch.get("x", 1)), 0),
        "y": max(int(dispatch.get("y", 1)), 0),
        "z": max(int(dispatch.get("z", 1)), 0),
    }


def make_draw_instanced_indirect_command(command: dict[str, Any]) -> NativeCommand:
    def noop(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        return

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        pipeline_error = validate_draw_pipeline(state)
        if pipeline_error is not None:
            errors.append(pipeline_error)
            return

        args_buffer = command.get("argsBuffer")
        if not args_buffer or not args_buffer.get("buffer") or not args_buffer.get("srv"):
            errors.append({
                "code": "command_stream.missing_indirect_args_buffer",
                "message": "Trying to issue indirect draw call, but draw args buffer is null.",
                "argsBuffer": args_buffer,
            })
            return

        if state.computeShader is not None or state.computeUavs:
            state.computeShader = None
            state.computeConstantBuffers = []
            state.computeShaderResources = []
            state.computeUavs = []
            state.computeSamplerStates = []
            trace.append({"op": "unbindComputeBeforeIndirectDraw"})

        state.drawCalls += 1
        state.indirectDrawCalls += 1
        trace.append({
            "op": "drawInstancedIndirect",
            "argsBuffer": dict(args_buffer),
            "alignedByteOffsetForArgs": int(command.get("alignedByteOffsetForArgs", 0)),
            "topology": state.topology,
            "vertexShader": state.vertexShader,
            "pixelShader": state.pixelShader,
            "renderTargetViews": list(state.renderTargetViews),
        })

    return NativeCommand("DrawInstancedIndirect", noop, update, noop)


def make_draw_command(command: dict[str, Any]) -> NativeCommand:
    def noop(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        return

    def update(state: RenderState, trace: list[dict[str, Any]], errors: list[dict[str, Any]]) -> None:
        pipeline_error = validate_draw_pipeline(state)
        if pipeline_error is not None:
            errors.append(pipeline_error)
            return
        if "draw" not in command.get("commandOps", []):
            errors.append({
                "code": "command_stream.missing_draw_op",
                "commandOps": command.get("commandOps"),
            })
            return

        state.drawCalls += 1
        state.triangles += 1
        trace.append({
            "op": "draw",
            "meshId": command.get("meshId"),
            "material": command.get("selectedMaterialId"),
            "topology": state.topology,
            "vertexBuffer": state.vertexBuffer,
            "indexBuffer": state.indexBuffer,
            "vertexShader": state.vertexShader,
            "pixelShader": state.pixelShader,
            "constantBuffers": list(state.constantBuffers),
            "shaderResources": list(state.shaderResources),
            "samplerStates": list(state.samplerStates),
            "rasterizerState": state.rasterizerState,
            "viewports": list(state.viewports),
            "renderTargetViews": list(state.renderTargetViews),
            "depthStencilView": state.depthStencilView,
            "unorderedAccessViews": list(state.outputMergerUavs),
            "blendState": state.blendState,
            "depthStencilState": state.depthStencilState,
            "depthStencilReference": state.depthStencilReference,
            "blendFactor": list(state.blendFactor),
            "blendSampleMask": state.blendSampleMask,
        })

    return NativeCommand("Draw", noop, update, noop)


def record_resource_access(
    state: RenderState,
    trace: list[dict[str, Any]],
    view: Any,
    access: str,
    reason: str,
) -> None:
    texture_id = texture_id_for(view)
    if texture_id is None:
        return

    before = state.resourceAccess.get(texture_id)
    if is_unordered_access_write(before) and before != access:
        state.resourceBarriers += 1
        trace.append({
            "op": "resourceBarrier",
            "textureId": texture_id,
            "before": before,
            "after": access,
            "reason": reason,
        })
    state.resourceAccess[texture_id] = access


def is_unordered_access_write(access: str | None) -> bool:
    return access in ["UAVWrite", "UnorderedAccessWrite"]


def set_resource_access(state: RenderState, view: Any, access: str) -> None:
    texture_id = texture_id_for(view)
    if texture_id is not None:
        state.resourceAccess[texture_id] = access


def texture_id_for(view: Any) -> str | None:
    if isinstance(view, TextureViewHandle):
        return view.textureId
    if isinstance(view, dict):
        texture_id = view.get("textureId")
        if texture_id:
            return str(texture_id)
    return None


def validate_draw_pipeline(state: RenderState) -> dict[str, Any] | None:
    if state.topology is None or state.vertexBuffer is None or state.indexBuffer is None:
        return {
            "code": "command_stream.missing_input_assembler",
            "message": "Trying to issue draw call, but input assembler state is incomplete.",
        }
    if state.rasterizerState is None or not state.viewports:
        return {
            "code": "command_stream.missing_rasterizer",
            "message": "Trying to issue draw call, but rasterizer state is incomplete.",
        }
    if state.vertexShader is None or state.pixelShader is None:
        return {
            "code": "command_stream.missing_shader_stage",
            "message": "Trying to issue draw call, but pixel and/or vertex shader are null.",
        }
    if not state.renderTargetViews:
        return {
            "code": "command_stream.missing_output_merger",
            "message": "Trying to issue draw call, but output merger state is incomplete.",
        }
    return None
