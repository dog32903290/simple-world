# TiXL Mesh Draw Stage MRT Matrix Proof

TixlMeshDrawStageMrtMatrixProof answers:

```text
can the bounded handwritten mesh draw adapter claim TiXL donor source semantics
for vertex/pixel stages, MRT outputs, fragment derivatives/discard, and D3D
mul(vector, matrix) order without claiming HLSL-to-MSL translation?
```

Runtime path:

```text
tixl_mesh_draw_stage_mrt_matrix.graph.json -> tixl_mesh_draw_stage_mrt_matrix_shell.py -> source-backed result/trace/errors
```

## Boundary

This lane parses the TiXL donor HLSL:

```text
external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl
```

It also consumes the current source audit, t8 ShaderGraph resources expansion,
b5 native packing, and texture/sampler binding artifacts only to prevent stale
or widened upstream claims.

This lane combines source-backed donor semantics with a tiny explicit Metal
render probe for the handwritten adapter shape. It is not HLSL-to-MSL
translation, not a TiXL donor HLSL Metal probe, not TextureCube `SampleLevel`
or `GetDimensions`, not full PBR resource binding, not backend replacement, not
TiXL runtime parity, and not PBR visual correctness.

## Proven Source Facts

The donor source must show all of these facts:

```text
vsMain(uint id : SV_VertexID)
FaceIndices[faceIndex][faceVertexIndex]
PbrVertices[...]
psInput.pixelPosition : SV_POSITION
psOutput.Color : SV_Target0
psOutput.Normal : SV_Target1
psMain(psInput pin) : SV_TARGET
output.Color = litColor
output.Normal = float4(worldNormal, 1.0)
ddx(...)
ddy(...)
discard
mul(vector, matrix)
```

The resulting claim is:

```text
handwrittenMeshDrawAdapterStageMrtMatrixProof: true
sourceBackedOnly: true
actualMetalProbeRan: true
tixlDonorHlslMetalProbeRan: false
```

Forbidden claims stay false:

```text
hlslToMslTranslation: false
fullPbrResourceBinding: false
textureCubeSampleLevelGetDimensionsProven: false
backendReplacementReady: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
rendererIntegrationComplete: false
constantBufferAdapterComplete: false
fullTextureSamplerMapping: false
```

The explicit Metal probe renders one 1x1 triangle using `[[vertex_id]]`, passes
sentinels through `[[stage_in]]`, writes two `RGBA8Uint` color attachments, and
checks exact readback:

```text
target0: [13, 90, 111, 255]
target1: [31, 37, 41, 255]
```

## Blocked Cases

The shell exits `1` before publishing success if the donor HLSL loses the
vertex stage semantic, `SV_POSITION`, either MRT target, either fragment output
assignment, `ddx`/`ddy`, `discard`, or `mul(vector, matrix)` evidence.

It also exits `1` if the fixture or any consumed upstream artifact widens into
backend replacement, HLSL-to-MSL translation, full PBR binding, TextureCube
proof, TiXL runtime parity, or renderer integration.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_stage_mrt_matrix.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_stage_mrt_matrix_shell.py <tixl_mesh_draw_stage_mrt_matrix.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json
docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_trace.json
docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_errors.json
```
