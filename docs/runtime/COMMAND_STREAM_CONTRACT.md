# Command Stream Contract

This is the native render-command contract for My World / TiXL ports.

## Boundary

For TiXL command-producing render nodes:

```text
Command := ordered frame operation with optional prepare/update/restore phases
TiXL primary type := T3.Core.DataTypes.Command
primary donor family := Lib.render._dx11.api, Lib.render._dx11.fxsetup, Types.Gfx.ComputeShaderStage, Lib.flow.Execute
visible node names keep TiXL names with only my_ prefix
host proof := headless command-stream fixture; Vuo cannot prove TiXL Command/RestoreAction semantics
```

This contract depends on `Texture2D`, `TextureView`, and `RenderTarget`, but it
does not create those resources. It defines how render-stage nodes compose into
an ordered command stream that can mutate GPU state, draw, dispatch, and restore
state without hiding the dependency order inside one giant renderer node.

## Node Force

`Command` nodes answer one question:

```text
what render operation should run at this point in the frame, and what state must be restored afterward?
```

They do not answer:

```text
what texture exists?
what view identity does this texture expose?
what shader source should be generated?
how should a frame be retained for feedback?
```

Those stay separate:

```text
Texture2D
TextureView
ShaderGraph
FeedbackState
RenderTarget
```

## Source Evidence

Source files:

- `Operators/Lib/flow/Execute.cs`
- `Operators/Lib/render/_dx11/api/OutputMergerStage.cs`
- `Operators/Lib/render/_dx11/api/OutputMergerStage.t3`
- `Operators/Lib/render/_dx11/api/Rasterizer.cs`
- `Operators/Lib/render/_dx11/api/Viewport.cs`
- `Operators/Lib/render/_dx11/api/ClearRenderTarget.cs`
- `Operators/Lib/render/_dx11/api/Draw.cs`
- `Operators/Lib/render/_dx11/api/Draw.t3`
- `Operators/Lib/render/_dx11/api/DrawInstancedIndirect.cs`
- `Operators/Lib/render/_dx11/api/CalcDispatchCount.cs`
- `Operators/Lib/render/_dx11/api/CalcInt2DispatchCount.cs`
- `Operators/Lib/render/_dx11/fxsetup/SetPixelAndVertexShaderStage.cs`
- `Operators/Lib/render/_dx11/fxsetup/SetPixelAndVertexShaderStage.t3`
- `Operators/TypeOperators/Gfx/ComputeShaderStage.cs`
- `Operators/TypeOperators/Gfx/ComputeShaderStage.t3`

Spec evidence:

- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/nodes_by_namespace/Lib.render._dx11.api.md`
- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/TIXL_CLONE_NODE_SPECS_ALL.md`
- `docs/tixl-porting/namespaces/render_mesh_point.md`

There are no official operator docs for most of these render-stage nodes in the
merged TiXL docs. Use C# behavior as the semantic source.

## Command Execution Model

TiXL `Lib.flow.Execute` is the contract anchor:

```text
if IsEnabled:
  for each command: command.PrepareAction(context)
  for each command: command.GetValue(context)
  for each command: command.RestoreAction(context)
clear Command dirty flag
```

Native My World must preserve these phases:

```text
prepare -> update -> restore
```

The important part is not C# delegates. The important part is that some command
nodes mutate global GPU state and must be restored after the ordered command
batch runs.

First API scaffold:

```text
docs/runtime/scripts/native_command_stream_api.py

RenderState
NativeCommand
CommandStream
make_input_assembler_command()
make_shader_stage_command()
make_rasterizer_command()
make_output_merger_command()
make_clear_render_target_command()
make_compute_shader_stage_command()
make_draw_instanced_indirect_command()
make_draw_command()
```

The API currently binds InputAssembler mesh-buffer identity from Material/PBR
command data, binds shader-stage state, binds Rasterizer viewport/state, binds
`RTV`/`DSV` TextureView handles plus blend/depth state from
`native_resource_api.py`, executes the draw command from the bound state, then
restores input-assembler, shader, rasterizer, and output-merger state. It is
data-backed, not a GPU backend, but it makes command execution consume real
state identity instead of merely listing available command fields.

For compute routes, the same API binds compute shader state plus UAV identity,
dispatches a bounded number of groups, then restores compute bindings. This is
still a data-backed command proof, not a native GPU compute backend.

The API also keeps a lightweight resource-access ledger. A compute dispatch
marks bound UAV TextureViews as `UAVWrite`; OutputMerger UAV binding marks them
as `UnorderedAccessWrite`. Later shader SRV reads, RTV writes, or other
non-UAV accesses to the same texture emit a `resourceBarrier` trace and update
the texture's latest access. This is a contract proof for backend hazards, not a real GPU resource barrier.

For indirect draw routes, the API requires a valid indirect args buffer and
explicitly unbinds compute shader/UAV state before issuing
`DrawInstancedIndirect`, matching the source-level hazard that compute output
can otherwise leak into draw state.

## First Node Set

### my_Execute

```text
conversion: Command[] + IsEnabled -> Command
source: Lib.flow.Execute
```

Behavior:

- When enabled, call all prepare phases in input order.
- Then update all commands in input order.
- Then restore all commands in input order.
- When disabled, do not prepare, update, or restore.
- Clear the command input dirty flag after evaluation.

### my_SetPixelAndVertexShaderStage

```text
conversion: shaders + constant buffers + SRVs + samplers -> Command
source: Lib.render._dx11.fxsetup.SetPixelAndVertexShaderStage
```

Inputs:

- `VertexShader`
- `PixelShader`
- `ConstantBuffers`: multi `Buffer`
- `ShaderResources`: multi `ShaderResourceView`
- `VariousResources`: multi object, currently used to collect additional SRV lists
- `SamplerStates`: multi `SamplerState`

Output:

- `Output`: `Command`

Behavior:

- Save previous VS/PS shaders and previous VS stage buffers/resources/samplers.
- Pull shader inputs first, because shader generation may update dependent
  shader graph nodes.
- Bind VS/PS when present.
- Bind samplers, constant buffers, ordinary SRVs, and additional SRVs.
- Restore previous shader stage state in `RestoreAction`.

Risk:

- TiXL source restores samplers on PS but does not symmetrically restore every
  VS sampler path. Native ports should define explicit per-stage restore
  coverage instead of preserving accidental asymmetry.

### my_InputAssemblerStage

```text
conversion: primitive topology + vertex/index BufferWithViews identity -> Command
source: Lib.render._dx11.api.InputAssemblerStage
```

Current native API behavior:

- Accepts only `TriangleList`.
- Requires `vertexBuffer.buffer`, `vertexBuffer.srv`, `indexBuffer.buffer`, and
  `indexBuffer.srv`.
- Binds topology plus vertex/index buffer identity into `RenderState`.
- Draw refuses to run if InputAssembler state was not bound first.
- Restores previous InputAssembler state in `RestoreAction`.

Boundary:

- This proves command-state pressure and failure semantics, not real GPU buffer
  binding, index validation, input layout parity, or TiXL `BufferWithViews`
  lifetime parity.

### my_OutputMergerStage

```text
conversion: RTV/DSV/UAV + blend/depth state -> Command
source: Lib.render._dx11.api.OutputMergerStage
```

Inputs:

- `BlendState`
- `DepthStencilView`
- `RenderTargetViews`: multi `RenderTargetView`
- `UnorderedAccessViews`: multi `UnorderedAccessView`
- `DepthStencilState`
- `DepthStencilReference`
- `BlendFactor`
- `BlendSampleMask`

Output:

- `Output`: `Command`

Behavior:

- Save previous render targets, depth target, blend state, blend factor, and
  sample mask.
- Bind DSV + RTVs when DSV exists.
- Bind RTVs without DSV when RTVs exist.
- Bind UAVs at unordered access slot 1 when present.
- Bind blend/depth state.
- Restore previous render targets and blend state in `RestoreAction`.

Current native API behavior:

- Requires at least one valid `RTV` before Draw can run.
- Accepts valid `UAV` TextureViews through `UnorderedAccessViews` and keeps
  them in `outputMergerUavs`, separate from compute-stage UAV state.
- Binds `blendState`, `depthStencilState`, `depthStencilReference`,
  `blendFactor`, and `blendSampleMask` into `RenderState`.
- Emits bound `unorderedAccessViews` in `bindOutputMerger` and draw traces.
- Marks bound output-merger UAV textures as `UnorderedAccessWrite` in the
  resource-access ledger.
- Draw refuses to run if OutputMerger state was not bound first.
- Restores previous render targets, depth target, output-merger UAVs, blend
  state, and depth state in `RestoreAction`.

Boundary:

- This proves render-target, output-merger UAV, and blend/depth state pressure,
  plus data-level UAV separation from compute state. It does not prove real GPU
  UAV slot binding, UAV initial counts, multi-target blend parity, driver depth
  testing, or native GPU state objects.

Risk:

- TiXL source does not restore previous UAV bindings. Native ports must track
  and restore UAV state explicitly.

### my_Rasterizer

```text
conversion: viewport/scissor/rasterizer state -> Command
source: Lib.render._dx11.api.Rasterizer
```

Behavior:

- Save previous viewports and rasterizer state.
- Bind new rasterizer state.
- Bind viewports when present.
- Restore previous viewports and state.

Current native API behavior:

- Requires at least one viewport.
- Refuses non-positive viewport width/height.
- Accepts culling values `None`, `Front`, or `Back`.
- Binds `rasterizerState` and `viewports` into `RenderState`.
- Draw refuses to run if Rasterizer state was not bound first.
- Restores previous Rasterizer state in `RestoreAction`.

Boundary:

- This proves viewport/state ordering and restore semantics, not full DX11
  rasterizer parity, scissor rectangles, fill-rule parity, or driver clipping.

Risk:

- TiXL source reads scissor rectangles but does not visibly set them in this
  node. Treat scissor support as unproven until native source audit is deeper.

### my_ClearRenderTarget

```text
conversion: RTV/DSV + clear color -> Command
source: Lib.render._dx11.api.ClearRenderTarget
```

Behavior:

- If RTV exists, clear it to `ClearColor`.
- If DSV exists, clear depth to `1.0`.
- Does not own render target binding or restore.

Current native API behavior:

- Requires every color clear target to be a valid `RTV` TextureView.
- Requires the depth clear target, when present, to be a valid `DSV`
  TextureView.
- Emits `clearRenderTargetView` for each RTV with the requested `clearColor`.
- Emits `clearDepthStencilView` for the DSV with depth `1`.
- Tracks `clearCalls`.
- Marks cleared RTV textures as `RenderTargetWrite` and cleared DSV textures as
  `DepthStencilWrite` in the resource-access ledger.
- Does not bind OutputMerger state, does not satisfy Draw's OutputMerger
  requirement, and has no restore mutation.

Boundary:

- This proves clear command ordering, TextureView type validation, clear stats,
  and resource-access visibility. It does not clear a real GPU texture, validate
  clear color format compatibility, clear stencil, or prove backend load/store
  action parity.

### my_Draw

```text
conversion: VertexCount + VertexStartLocation -> Command
source: Lib.render._dx11.api.Draw
```

Inputs:

- `VertexCount`: int, default 3
- `VertexStartLocation`: int, default 0

Output:

- `Output`: `Command`

Behavior:

- Read the current device shader stage.
- If vertex shader or pixel shader is missing, warn once and do not draw.
- Otherwise issue `Draw(vertexCount, vertexStartLocation)`.
- Track render stats: triangles and draw calls.

### my_DrawInstancedIndirect

```text
conversion: indirect args Buffer + AlignedByteOffsetForArgs -> Command
source: Lib.render._dx11.api.DrawInstancedIndirect
```

Behavior:

- If draw args buffer is missing, warn and do not draw.
- Unbind compute shader and several UAV slots before drawing.
- Flush the device context.
- Issue `DrawInstancedIndirect(buffer, alignedByteOffsetForArgs)`.

Current native API behavior:

- Requires the same draw pipeline state as `my_Draw`: InputAssembler,
  ShaderStage, Rasterizer, and OutputMerger.
- Requires `argsBuffer.buffer` and `argsBuffer.srv`.
- If compute state or UAVs are still bound, emits
  `unbindComputeBeforeIndirectDraw` and clears compute bindings before drawing.
- Tracks `indirectDrawCalls` as a separate stat while also incrementing
  `drawCalls`.

Boundary:

- This proves indirect-draw gating and compute/draw state cleanup pressure. It
  does not execute a real indirect draw, validate args buffer layout, flush a
  GPU device context, or prove native buffer lifetime parity.

### my_ComputeShaderStage

```text
conversion: compute shader + resources + dispatch settings -> Command
source: Types.Gfx.ComputeShaderStage
```

Inputs:

- `ComputeShader`
- `Dispatch`: `Int3`, default `{X:16,Y:16,Z:1}`
- `DispatchCallCount`: int, default 1, clamped to `1..256`
- `ConstantBuffers`: multi `Buffer`
- `ShaderResources`: multi `ShaderResourceView`
- `VariousResources`: multi object, currently used to collect additional SRV lists
- `Uavs`: multi `UnorderedAccessView`
- `UavBufferCounter`: int, default -1
- `SamplerStates`: multi `SamplerState`

Output:

- `Output`: `Command`

Behavior:

- If no UAVs or compute shader is null, return without dispatch.
- Save current output merger render targets and depth target.
- Bind compute shader, constant buffers, SRVs, additional SRVs, samplers, and
  UAVs.
- If `DispatchCallCount > 1`, append/update a dispatch-call-index constant
  buffer and dispatch repeatedly.
- Restore output merger targets after dispatch.
- Unbind UAVs, samplers, SRVs, and constant buffers after dispatch.
- Track render stats: compute shader updates and dispatch group total.

Current native API behavior:

- Requires `computeShaderEntry`.
- Requires at least one valid `UAV` TextureView.
- Binds compute constant buffers, SRVs, UAVs, and samplers into `RenderState`.
- Clamps `dispatchCallCount` to `1..256`.
- Tracks `computeDispatchCalls` and `computeThreadGroups`.
- Marks each dispatched UAV texture as `UAVWrite` in the resource-access
  ledger.
- Emits `resourceBarrier` before a later ShaderStage reads the same texture as
  `ShaderResourceRead`, or before OutputMerger writes it as `RenderTargetWrite`.
- Restores compute bindings in `RestoreAction`.

Boundary:

- This proves compute command ordering, UAV identity, dispatch count clamping,
  cleanup pressure, and data-level resource hazard visibility. It does not
  execute shader code, validate thread group dimensions against shader source,
  synchronize GPU writes, issue real resource barriers, or prove Metal/DX11 UAV
  parity.

Risk:

- This node lives under `Types.Gfx`, not `Lib.render._dx11.api`, but it is part
  of the same command stream because it outputs `Command`.

### my_CalcDispatchCount

```text
conversion: Count + ThreadGroupSize -> Int3
source: Lib.render._dx11.api.CalcDispatchCount
```

Behavior:

- If `ThreadGroupSize.X > 0`, output `{X: Count / ThreadGroupSize.X + 1, Y:1, Z:1}`.
- Otherwise output `Int3.Zero`.
- This preserves TiXL's over-dispatch-by-one behavior.

### my_CalcInt2DispatchCount

```text
conversion: Size + ThreadGroups -> Int3
source: Lib.render._dx11.api.CalcInt2DispatchCount
```

Behavior:

- If `ThreadGroups.X` or `ThreadGroups.Y` is zero, do not update output.
- Otherwise output `{X: Width / ThreadGroups.X + 1, Y: Height / ThreadGroups.Y + 1, Z:1}`.
- This preserves TiXL's over-dispatch-by-one behavior.

## Native Port Shape

Command handles should be serializable and inspectable:

```text
CommandOp
  id
  kind
  inputs
  prepareOps[]
  updateOps[]
  restoreOps[]
  requiredState[]
  writesState[]
  diagnostics[]
```

Minimum graph fixture:

```text
Texture2D -> RtvFromTexture2d -> OutputMergerStage
ShaderGraph/Shader -> SetPixelAndVertexShaderStage
Viewport -> Rasterizer
OutputMergerStage + Rasterizer + SetPixelAndVertexShaderStage + Draw -> Execute
Execute -> RenderTarget
```

For compute:

```text
Texture2D -> UavFromTexture2d
ComputeShader + UavFromTexture2d + CalcInt2DispatchCount -> ComputeShaderStage
ComputeShaderStage -> Execute
```

## Failure Behavior

Missing shader before `my_Draw`:

- do not draw
- warn once until a successful draw resets the complaint flag
- downstream render remains valid only if previous commands produced a valid
  frame

Missing draw args buffer before `my_DrawInstancedIndirect`:

- do not draw
- diagnostic stage: `missing indirect args buffer`

Missing RTV/DSV/UAV before stage binding:

- stage command may still bind the state that is present
- diagnostics must report which view list was empty
- downstream draw may still fail if required output target is absent

Missing compute shader or missing UAVs before `my_ComputeShaderStage`:

- do not dispatch
- do not synthesize UAVs
- diagnostic stage: `missing compute shader` or `missing UAV`

Restore failure:

- mark the frame invalid
- include command id, restore op, previous state snapshot id, and backend error
- do not keep mutating global GPU state silently

Backend unavailable:

- no command updates run
- command stream emits structured diagnostics instead of crashing

## Evidence

Current proof is a headless contract fixture:

```text
tests/command_stream_contract.test.js
```

There is intentionally no Vuo proof composition for this contract. Vuo can
prove scene rendering through `vuo.scene.render.image2`, but it cannot prove
TiXL `Command`, `PrepareAction`, `RestoreAction`, DX11 stage binding, or compute
dispatch semantics.

## Next Door

After this lane holds, the next high-risk contracts are:

```text
Mesh draw command -> docs/runtime/MESH_DRAW_CONTRACT.md
Material/shader stage binding
Point/BufferWithViews
Native renderer proof
```
