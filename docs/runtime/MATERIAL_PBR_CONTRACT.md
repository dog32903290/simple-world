# Material / PBR Binding Contract

This is the first material-context contract for My World / TiXL ports.

## Boundary

For first-pass PBR material binding:

```text
MaterialBinding := PbrMaterial + scoped context mutation -> ordered Command subtree
TiXL donor nodes := Lib.render.shading.SetMaterial, Lib.render.shading.DefineMaterials, Lib.mesh.draw.DrawMesh
support donors := GetPbrParameters, SetContextTexture, SetEnvironment
visible node names keep TiXL names with only my_ prefix
host proof := headless material-binding fixture; Vuo cannot prove TiXL PbrMaterial / SRV / constant-buffer parity
```

This contract depends on `Command Stream`, `TextureView`, `RenderTarget`, and
`Mesh Draw`, but it is not a full lighting renderer and not a texture loader.
It defines how material state becomes visible to draw commands without hiding
global context mutation.

## Node Force

`my_SetMaterial` answers one question:

```text
what PBR material should the connected command subtree see while it cooks?
```

It does not answer:

```text
how is a mesh generated?
how is a shader graph fragment compiled?
how is image-based lighting prefiltered?
how are point lights, shadows, or fog evaluated?
```

Those stay separate:

```text
my_DrawMesh
my_SetEnvironment
my_SetPointLight
shadow / fog nodes
```

## Source Evidence

Primary donors:

- C#: `Operators/Lib/render/shading/SetMaterial.cs`
- compound graph: `Operators/Lib/render/shading/SetMaterial.t3`
- GUID: `0ed2bee3-641f-4b08-8685-df1506e9af3c`
- C#: `Operators/Lib/render/shading/DefineMaterials.cs`
- compound graph: `Operators/Lib/render/shading/DefineMaterials.t3`
- GUID: `0bd77dd6-a93a-4e2e-b69b-bbeb73cb5ae9`
- C#: `Operators/Lib/mesh/draw/DrawMesh.cs`
- compound graph: `Operators/Lib/mesh/draw/DrawMesh.t3`
- GUID: `a3c5471e-079b-4d4b-886a-ec02d6428ff6`

Support donors:

- `Operators/Lib/render/shading/_/GetPbrParameters.cs`
- `Operators/Lib/render/shading/_/GetPbrParameters.t3`
- `Operators/Lib/render/shading/_/SetContextTexture.cs`
- `Operators/Lib/render/shading/SetEnvironment.cs`
- `Operators/Lib/render/shading/SetEnvironment.t3`
- `Core/Rendering/Material/PbrMaterial.cs`
- `Core/Rendering/Material/PbrContextSettings.cs`

## TiXL Donor Semantics

### my_SetMaterial

```text
conversion: material controls + subtree Command -> scoped Command
source: Lib.render.shading.SetMaterial
```

Inputs and defaults:

```text
SubTree: Command = null
BaseColor: Vector4 = [1,1,1,1]
BaseColorMap: Texture2D = null
EmissiveColor: Vector4 = [0,0,0,1]
EmissiveColorMap: Texture2D = null
Specular: float = 1
Roughness: float = 0.25
Metal: float = 0
NormalMap: Texture2D = null
RoughnessMetallicOcclusionMap: Texture2D = null
MaterialId: string = ""
Output: Command
Reference: PbrMaterial
```

Observed behavior:

- Lazily creates one retained `PbrMaterial`.
- Rebuilds the material parameter buffer when scalar/color inputs are dirty.
- Creates SRVs for connected texture inputs.
- Falls back to default SRVs when a texture is null, disposed, or SRV creation
  fails.
- Saves `context.PbrMaterial`, assigns the new material, appends it to
  `context.Materials`, evaluates `SubTree`, removes the appended material, and
  restores the previous material.
- Publishes the same material on `Reference`.

### my_DefineMaterials

```text
conversion: PbrMaterial[] + subgraph Command -> scoped material list Command
source: Lib.render.shading.DefineMaterials
```

Observed behavior:

- Collects connected material references.
- Ignores null material inputs.
- Appends valid materials to `context.Materials`.
- Evaluates `SubGraph`.
- Removes only the materials it appended.
- Does not change `context.PbrMaterial` by itself.

### my_DrawMesh

```text
conversion: MeshBuffers + PBR context + draw controls -> ordered Command compound
source: Lib.mesh.draw.DrawMesh
```

Inputs and defaults:

```text
Mesh: MeshBuffers = null
Color: Vector4 = [1,1,1,1]
AlphaCutOff: float = 0
BlendMode: int = 0
FillMode: int = 3
Culling: CullMode = Back
Shading: int = 0
SpecularAA: float = 0.5
EnableZTest: bool = true
EnableZWrite: bool = true
Filter: Filter = MinMagMipLinear
WrapMode: TextureAddressMode = Wrap
UseMaterialId: string = ""
FragmentField: ShaderGraphNode = null
ShaderDefines: string = ""
Output: Command
```

Observed behavior:

- Caches available `context.Materials` for the material dropdown.
- Saves `context.PbrMaterial`.
- If `UseMaterialId` is non-empty and matches a material name in
  `context.Materials`, temporarily assigns that material.
- If `UseMaterialId` is empty or has no match, leaves the current material in
  place.
- Evaluates the internal draw compound.
- Restores the previous material after the draw subtree cooks.

Important internal route:

```text
my__MeshBufferComponents
-> my_InputAssemblerStage
-> my_GenerateShaderGraphCode(template: Lib:shaders/3d/mesh/mesh-Draw.hlsl)
-> my_VertexShader(entry: vsMain)
-> my_PixelShader(entry: psMain)
-> my_GetPbrParameters
-> my_SetPixelAndVertexShaderStage
-> my_Rasterizer / my_OutputMergerStage
-> my_Draw
-> my_Execute
```

`my_GetPbrParameters` exposes the current material resources to shader binding:

```text
PbrParameterBuffer
AlbedoColorMap
EmissiveColorMap
RoughnessMetallicOcclusionMap
NormalMap
BrdfLookupMap
PrefilteredSpecularMap
```

## Environment Scope

`my_SetEnvironment` is not the same node as `my_SetMaterial`, but it feeds the
PBR route through context textures:

```text
my_SetEnvironment
-> my_SetContextTexture(Id: PrefilteredSpecular)
-> my_GetPbrParameters.PrefilteredSpecularMap
-> my_DrawMesh shader resources
```

First-pass support only requires the scoped context-texture rule. It does not
claim TiXL cubemap conversion, specular prefilter, background rendering, or DDS
asset parity.

## Native Port Shape

Portable material binding should emit:

```text
PbrMaterial
  id
  parameters:
    baseColor
    emissiveColor
    roughness
    specular
    metal
  resources:
    albedoSrv
    emissiveSrv
    roughnessMetallicOcclusionSrv
    normalSrv
  diagnostics

MaterialScopeCommand
  materialId
  referenceMaterialId
  pushedToMaterialList
  subtreeCommand
  previousMaterial
  restorePolicy
```

DrawMesh PBR command should emit:

```text
MeshPbrDrawCommand
  meshId
  selectedMaterialId
  shaderSource: Lib:shaders/3d/mesh/mesh-Draw.hlsl
  vertexShaderEntry: vsMain
  pixelShaderEntry: psMain
  constantBuffers:
    transforms
    context
    pointLights
    pbrParameters
  shaderResources:
    albedo
    emissive
    roughnessMetallicOcclusion
    normal
    brdfLookup
    prefilteredSpecular
  commandOps:
    inputAssembler
    shaderStage
    rasterizer
    outputMerger
    draw
```

## Failure Behavior

### Texture SRV Fallback

If a material texture is null, disposed, or cannot create an SRV:

```text
use the matching default SRV
record a diagnostic for SRV creation failure
do not fail the whole material scope
```

Default SRVs:

```text
BaseColorMap -> DefaultAlbedoColorSrv
EmissiveColorMap -> DefaultEmissiveColorSrv
RoughnessMetallicOcclusionMap -> DefaultRoughnessMetallicOcclusionSrv
NormalMap -> DefaultNormalSrv
```

### Material Scope Restore

`my_SetMaterial`, `my_DefineMaterials`, and `my_SetContextTexture` are scoped
context mutations. They must restore previous context values after the subtree
cooks.

Required diagnostics:

```text
materialScope.push
materialScope.restore
contextTexture.push
contextTexture.restore
```

If restore fails in a native backend:

```text
mark the command stream invalid
do not keep mutating global material or environment state silently
```

### Material Lookup

If `my_DrawMesh.UseMaterialId` is empty:

```text
use current context.PbrMaterial
```

If `UseMaterialId` does not match any available material:

```text
use current context.PbrMaterial
record selectedMaterialId as current material
record requestedMaterialId as unresolved
do not synthesize a new material
```

### PBR Binding Gate

`my_DrawMesh` must not pretend PBR parity exists unless the command trace shows
the PBR parameter buffer and all first-pass texture slots:

```text
PbrParameterBuffer
AlbedoColorMap
EmissiveColorMap
RoughnessMetallicOcclusionMap
NormalMap
BrdfLookupMap
PrefilteredSpecularMap
```

Missing `PrefilteredSpecularMap` may fall back to default environment only when
an explicit default context texture service owns that policy.

## Risk Closure Matrix

```text
closed now:
  SetMaterial parameter/default contract
  texture SRV fallback policy
  material list push/restore policy
  DrawMesh UseMaterialId selection policy
  PBR binding trace shape

blocked until native renderer:
  exact PbrMaterial constant-buffer layout parity
  SharpDX SRV lifetime/dispose parity
  cubemap prefilter / DDS environment parity
  lighting and shadow visual parity
```

## Evidence

Current proof:

```text
docs/runtime/MATERIAL_PBR_CONTRACT.md
docs/runtime/fixtures/material_pbr_scope.graph.json
docs/runtime/scripts/material_pbr_scope_shell.py
docs/runtime/artifacts/material_pbr_scope/material_scope_trace.json
docs/runtime/artifacts/material_pbr_scope/mesh_pbr_draw_command.json
docs/runtime/artifacts/material_pbr_scope/pbr_binding_errors.json
tests/material_pbr_contract.test.js
```

The CLI proof emits:

```text
material_scope_trace.json
  defineMaterials.push
  contextTexture.push
  drawMesh.pbrBinding
  contextTexture.restore
  defineMaterials.restore

mesh_pbr_draw_command.json
  selectedMaterialId
  PBR constant buffer slot
  first-pass PBR shader resource slots
  commandOps

pbr_binding_errors.json
  nonfatal SRV fallback diagnostics
```

Native closure condition:

```text
material_scope_trace.json
mesh_pbr_draw_command.json
pbr_binding_errors.json
nonblack frame artifact with material parameter changes
```

First command-to-frame proof:

```text
docs/runtime/RENDERER_PROOF_CONTRACT.md
docs/runtime/scripts/native_render_shell.py
docs/runtime/artifacts/native_renderer/native_render_trace.json
docs/runtime/artifacts/native_renderer/native_render_errors.json
docs/runtime/artifacts/native_renderer/frame.ppm
docs/runtime/artifacts/native_renderer/frame_stats.json
```

This renderer proof consumes `mesh_pbr_draw_command.json` and produces a
nonblack frame artifact, but it still does not close native GPU / TiXL visual
parity.
