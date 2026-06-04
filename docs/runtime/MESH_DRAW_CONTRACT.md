# Mesh Draw Contract

This is the first mesh-to-command contract for My World / TiXL ports.

## Boundary

For first-pass unlit mesh drawing:

```text
MeshDraw := MeshBuffers + material controls -> ordered Command compound
TiXL donor node := Lib.mesh.draw.DrawMeshUnlit
visible node name := my_DrawMeshUnlit
support donors := CubeMesh, _MeshBufferComponents, InputAssemblerStage, SetPixelAndVertexShaderStage, Rasterizer, OutputMergerStage, Draw, Execute
host proof := headless mesh-draw fixture; Vuo can render scene objects but cannot prove TiXL MeshBuffers / Command compound parity
```

This contract depends on `Command Stream`, `TextureView`, and `Vuo Mesh
Adapter`, but it is not a pure mesh converter and not a full material system.
It defines the first route where a mesh becomes a render command.

## Node Force

`my_DrawMeshUnlit` answers one question:

```text
how should this MeshBuffers value be drawn as an unlit render command?
```

It does not answer:

```text
how is the mesh generated?
how is a PBR material evaluated?
how does lighting/shadow/fog modify the scene?
how does a render target own color/depth textures?
```

Those stay separate:

```text
my_CubeMesh / mesh generators
my_SetMaterial / material nodes
light and shadow nodes
my_RenderTarget
```

## Source Evidence

Primary donor:

- docs: `.help/docs/operators/lib/mesh/draw/DrawMeshUnlit.md`
- C#: `Operators/Lib/mesh/draw/DrawMeshUnlit.cs`
- compound graph: `Operators/Lib/mesh/draw/DrawMeshUnlit.t3`
- GUID: `4499dcb1-c936-49ed-861b-2ad8ae58cb28`

Support donors:

- `Operators/Lib/mesh/generate/CubeMesh.cs`
- `Operators/Lib/mesh/generate/CubeMesh.t3`
- `Operators/Lib/mesh/_/_MeshBufferComponents.cs`
- `Operators/Lib/mesh/_/_MeshBufferComponents.t3`
- `Operators/Lib/render/_dx11/api/InputAssemblerStage.cs`
- `Operators/Lib/render/_dx11/api/InputAssemblerStage.t3`
- `Operators/Lib/render/_dx11/api/Draw.cs`
- `Operators/Lib/render/_dx11/fxsetup/SetPixelAndVertexShaderStage.cs`

Spec evidence:

- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/TIXL_CLONE_NODE_SPECS_ALL.md`
- `docs/tixl-porting/namespaces/render_mesh_point.md`

## TiXL Donor Semantics

Official TiXL meaning:

```text
DrawMeshUnlit draws incoming geometry and mesh nodes without shading.
It can be combined with SetMaterial, SetFog, SetPointLight, and scene/render nodes.
```

Inputs:

```text
Mesh: MeshBuffers, required
Color: Vector4 = [1,1,1,1]
BlendMode: int = 0
FillMode: int = 3
Culling: CullMode = Back
EnableZTest: bool = true
EnableZWrite: bool = true
Texture: Texture2D = null
UseCubeMap: bool = false
AlphaCutOff: float = 0
BlurLevel: float = 0
TextureWrap: TextureAddressMode = Wrap
UseVertexColor: bool = false
Output: Command
```

Observed compound pressure:

- `Mesh` is decomposed through `_MeshBufferComponents`.
- `_MeshBufferComponents` fails when mesh, vertex buffer, index buffer, or SRV
  views are missing.
- `InputAssemblerStage` participates in the command stream and defaults to
  `TriangleList`.
- The unlit shader source is `Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl`.
- The vertex shader entry point is `vsMain`.
- Depth, rasterizer, blend, texture SRV, shader stage, draw, and execute nodes
  are internal parts of the compound.

## Support Contracts

### my_CubeMesh

```text
conversion: cube params -> MeshBuffers
source: Lib.mesh.generate.CubeMesh
```

Important behavior:

- Outputs `Data: MeshBuffers`.
- Default segments are `{X:1,Y:1,Z:1}`.
- Segment values are clamped to `1..10000` and then incremented by one for grid
  vertex construction.
- Produces `PbrVertex` vertex data and `Int3` face index data.
- Creates structured buffers plus SRV/UAV views for vertices and indices.
- Publishes `VertexBuffer` and `IndicesBuffer` into `MeshBuffers`.

The portable My World mesh contract must not require TiXL's exact DX11
`BufferWithViews` object. Native ports should expose a backend-neutral
`MeshHandle` plus view handles only when a GPU backend exists.

### my__MeshBufferComponents

```text
conversion: MeshBuffers -> vertices BufferWithViews + indices BufferWithViews + chunk defs
source: Lib.mesh._._MeshBufferComponents
```

Important behavior:

- If `MeshBuffers` is null, warning: `Undefined Mesh?`.
- If vertex buffer or vertex SRV is missing, warning: `Vertex buffer undefined`.
- If index buffer or index SRV is missing, warning: `Indices buffer undefined`.
- On success, outputs vertex, index, and optional chunk definition buffers.

### my_InputAssemblerStage

```text
conversion: primitive topology + input layout + vertex/index buffers -> Command
source: Lib.render._dx11.api.InputAssemblerStage
```

Important behavior:

- Defaults `PrimitiveTopology` to `TriangleList`.
- Reads `InputLayout`, `VertexBuffers`, and `IndexBuffer`.
- Saves and restores previous primitive topology.
- Current TiXL source visibly sets topology but does not visibly bind vertex or
  index buffers in this node. Treat actual buffer binding as requiring deeper
  source/backend proof.

## Compound Shape

First supported conceptual route:

```text
my_CubeMesh
-> my__MeshBufferComponents
-> my_InputAssemblerStage
-> my_SetPixelAndVertexShaderStage
-> my_Rasterizer
-> my_OutputMergerStage
-> my_Draw
-> my_Execute
-> my_RenderTarget
```

`my_DrawMeshUnlit` may be exposed as a user-facing compound, but its child route
must stay inspectable. Do not hide missing mesh buffers, shader stage failure,
or render target absence behind a single opaque command.

## Native Port Shape

Portable mesh draw command should emit:

```text
MeshDrawCommand
  meshId
  topology
  vertexBuffer
  indexBuffer
  shaderProgram
  material:
    color
    texture
    alphaCutOff
    useVertexColor
  renderState:
    blendMode
    fillMode
    culling
    enableZTest
    enableZWrite
    textureWrap
  commandOps:
    inputAssembler
    shaderStage
    rasterizer
    outputMerger
    draw
```

Minimum inspectable artifacts for a native proof:

```text
mesh_draw_command.json
mesh_draw_trace.json
mesh_draw_errors.json
```

## Failure Behavior

## Risk Closure Matrix

These are the current pit closures for this lane:

```text
closed now:
  missing mesh / missing vertex buffer / missing index buffer / missing SRV
  unsupported topology policy
  missing VS/PS before draw

blocked until native renderer:
  Vuo parity for MeshBuffers / BufferWithViews / InputAssemblerStage
closed now:
  native CommandStream binds InputAssembler state before Draw
  Draw refuses to run without topology, vertex buffer, and index buffer state
closed now:
  native CommandStream binds Rasterizer viewport/state before Draw
  Draw refuses to run without viewport and rasterizer state
```

### Closed: Mesh Buffer Validation

Mesh buffer validation is not a note anymore. It is a required gate before
`my_DrawMeshUnlit` may produce a draw command:

```text
MeshBuffers
-> my__MeshBufferComponents
-> valid vertices BufferWithViews + valid indices BufferWithViews
-> draw command may be built
```

Required failure diagnostics:

- null mesh: `Undefined Mesh?`
- missing vertex buffer or vertex SRV: `Vertex buffer undefined`
- missing index buffer or index SRV: `Indices buffer undefined`

Downstream policy:

- do not build `inputAssembler`, `shaderStage`, `outputMerger`, or `draw` ops
- do not keep a fake last-valid mesh unless a future explicit state node owns
  that policy

### Closed: Topology Gate

This first lane only admits:

```text
TriangleList
```

Unsupported topology is a hard failure:

```text
unsupported topology: <topology>
```

Do not triangulate, fan-convert, strip-convert, or reinterpret topology inside
`my_DrawMeshUnlit`. Topology conversion must be a separate visible mesh
transform node with its own proof.

### Closed: Shader Stage Gate

`my_Draw` is the final draw-call gate. If either vertex shader or pixel shader
is missing, the command route must stop before drawing and preserve TiXL's
diagnostic:

```text
Trying to issue draw call, but pixel and/or vertex shader are null.
```

The command trace may show earlier setup ops such as `inputAssembler`,
`shaderStage`, `rasterizer`, and `outputMerger`, but it must not include
`draw`.

### Closed: Output Merger Gate

`my_Draw` must also stop before drawing when no render target has been bound.
The native command stream now requires OutputMerger state before Draw:

```text
RTV/DSV identity
blendState
depthStencilState
depthStencilReference
blendFactor
blendSampleMask
```

This closes the fake-success hole where a mesh and shader could appear valid
but no render target/blend-depth state existed for the draw call.

### Not Closed: Vuo Parity For TiXL MeshBuffers

This cannot be closed with Vuo alone.

Vuo can prove:

```text
VuoSceneObject / VuoMesh -> Vuo renderer -> visible frame
```

Vuo cannot prove:

```text
TiXL MeshBuffers
TiXL BufferWithViews
TiXL InputAssemblerStage
TiXL Command / RestoreAction
```

Closure condition:

```text
native renderer proof emits:
  mesh_draw_command.json
  mesh_draw_trace.json
  mesh_draw_errors.json
  nonblack frame artifact
```

Until then, Vuo remains a host visual proof, not a parity proof for this lane.

Missing mesh:

- do not draw
- diagnostic: `Undefined Mesh?`
- downstream render command invalid unless a caller explicitly allows last-valid

Missing vertex buffer or vertex SRV:

- do not draw
- diagnostic: `Vertex buffer undefined`

Missing index buffer or index SRV:

- do not draw
- diagnostic: `Indices buffer undefined`

Missing shader stage:

- do not draw
- preserve `my_Draw` warning: pixel and/or vertex shader are null

Unsupported topology:

- do not silently triangulate
- diagnostic includes requested topology and supported topology list

Missing render target:

- mesh draw command can still be built as an abstract command
- final render proof must fail because no output target is bound

## Evidence

Current proof is a headless contract fixture:

```text
tests/mesh_draw_contract.test.js
```

There is intentionally no Vuo proof composition for this contract. Vuo scene
nodes can prove host rendering, but they cannot prove TiXL `MeshBuffers`,
`BufferWithViews`, `InputAssemblerStage`, or `DrawMeshUnlit` compound parity.

## Next Door

After this lane holds, the next high-risk contracts are:

```text
Material / SetMaterial / PBR binding -> docs/runtime/MATERIAL_PBR_CONTRACT.md
BufferWithViews / PointBuffer
Native renderer proof
```
