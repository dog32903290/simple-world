# TiXL -> Vuo Porting Grade Rules

These rules refine the Node Card schema grades in `docs/tixl-porting/README.md`. They are for research triage and worker consistency. They are not permission to write Vuo node code.

## Source Rules Before Grading

Use this evidence order:

1. C# source in `external/tixl`
2. Official docs in `external/tixl/.help/docs/operators` as reflected by the clone spec
3. `.t3` / `.t3ui` defaults and graph adjacency
4. Workflow/troubleshooting notes with their original certainty labels
5. Inference, explicitly marked `Unknown` or `needs_validation`

Grade is not just implementation difficulty. It is the intersection of:

- source completeness: C# + docs + `.t3`
- type portability: whether TiXL port types have a direct Vuo value type or built-in node family
- semantic risk: state, rendering, device I/O, timeline, GPU buffers, shader graph, or app-specific context
- verification fixture: whether we can write a small test or smoke composition that would catch drift

If any required evidence is missing, grade can still be assigned, but the missing evidence must remain visible in `Risks / unknowns`.

## Grade A

Use A only when all of these are true:

- Node is pure value/control logic: scalar math, bool logic, string text transforms, simple enum selection, list transforms with native Vuo list equivalents.
- TiXL input/output types map directly to stable Vuo value types.
- Runtime behavior is stateless or has small explicit state that can be tested without renderer/device/timeline.
- Source evidence is C# verified or source behavior is otherwise directly inspectable.
- Verification can be a deterministic C-level or composition-level fixture.

Typical TiXL types:

- `float`, `int`, `bool`, `string`
- simple `List<float>`, `List<int>`, `List<string>` if list semantics are explicit
- `Vector2`, `Vector3`, `Vector4` only when used as plain numeric vectors, not color/material/render state

Typical fixtures:

- numeric golden input/output cases
- boundary values: zero, negative, NaN/Infinity if relevant, min/max, empty string/list
- enum allowed-value tests
- Vuo composition JSON smoke test for generic list/value nodes

Do not grade A if the node touches `Texture2D`, `MeshBuffers`, `BufferWithViews`, `Command`, `ShaderGraphNode`, Direct3D/DX11, device I/O, or timeline state.

## Grade B

Use B when the node is likely portable but the type shape or semantic role needs care.

Conditions:

- Value/list/vector/color/curve behavior is source-backed.
- Vuo has a plausible type family, but mapping is not one-to-one or may need wrapper conversion.
- Verification is deterministic, but needs tolerance, list-shape checks, color-space checks, or enum migration.

Common B cases:

- `Vector4` that might be `VuoColor` or `VuoPoint4d`; decide from port role and docs.
- `Gradient` to Vuo color-list or image-gradient workflow; there is no automatic one-field equivalence.
- `Curve` / animation math where interpolation and timeline assumptions must be pinned.
- `Texture2D` only when the operation is a simple image operation already covered by Vuo image built-ins and does not depend on Direct3D internals.
- `MeshBuffers` only for simple geometry generation that can be expressed as `VuoMesh` without preserving TiXL GPU buffer layout.

Typical fixtures:

- approximate numeric/vector equality with tolerance
- color RGBA and premultiplication/non-premultiplication checks
- list length/order/empty-list checks
- simple image dimensions/hash/sample-pixel checks
- mesh vertex/element count and bounding-box checks

## Grade C

Use C when the node is important but needs renderer, image, mesh, scene, shader, point-buffer, or Vuo-specific design work.

Conditions:

- TiXL behavior is source-backed enough to describe, but not enough to directly translate as a Vuo C node.
- Mapping crosses a runtime model boundary, such as TiXL `Command` render chain to Vuo `VuoSceneObject`/`VuoLayer` composition, or TiXL point buffers to Vuo mesh/scene/list structures.
- Verification requires visual/render smoke checks, GPU resource checks, or composition-level fixtures.

Common C cases:

- image FX that use `Texture2D` but not raw DX11 state
- mesh draw/generate/modify nodes that rely on `MeshBuffers`
- point generation/modification/drawing nodes using `BufferWithViews`
- render scene/camera/group nodes using `Command`
- SDF/field nodes using `ShaderGraphNode`, unless we intentionally choose a separate Vuo shader/SDF subsystem
- audio-reactive or timeline-aware value nodes if their behavior depends on project playback state

Typical fixtures:

- offscreen render smoke image with nonblank pixel check
- image sample comparison at fixed resolution
- mesh count/bounds/normal/UV checks
- scene hierarchy smoke render
- point-count and attribute-preservation checks after choosing a Vuo representation
- timeline/audio fixture with deterministic input buffer

## Grade D

Use D for document-only or defer-first-pass nodes.

Any of these is enough for D unless a deliberate Vuo subsystem design exists:

- Direct3D/DX11 API objects, shader-stage state, UAV/SRV/RTV/DSV, sampler/rasterizer/input assembler/output merger types
- `BufferWithViews` where the node's behavior is really GPU buffer/view orchestration, not a simple point list
- `ShaderGraphNode` where the node builds TiXL's shader graph language
- `Command` nodes whose behavior is app-level render-command composition rather than local value transformation
- proprietary/device/app-specific dependencies: NDI, Spout, MediaPipe, ONVIF/PTZ, PosiStage, WebSocket/HTTP server runtime, file/render export app state
- nodes missing C# source when docs alone cannot prove runtime behavior
- internal, obsolete, experimental, `_dx11`, `_internal`, or helper namespaces with no user-facing docs

Typical fixtures:

- documentation fixture only: source map, negative knowledge entry, and explicit Unknown behavior
- device mock only after the target device/API contract is designed
- renderer integration smoke only after a dedicated Vuo design exists

## TiXL Type -> Vuo Type Candidates

Candidate means "start here, then verify role." It is not an automatic mapping.

| TiXL type | Vuo candidate | grade pressure | notes |
|---|---|---|---|
| `float`, `Single` | `VuoReal` | A | Vuo `VuoReal` is `double`; TiXL `float` is typically single precision. Use tolerance. |
| `int`, `Int32` | `VuoInteger` | A | Vuo integer is signed 64-bit. Enum-like ints need allowed values. |
| `bool`, `Boolean` | `VuoBoolean` | A | Direct value mapping. |
| `string`, `String` | `VuoText` | A | Check null/empty behavior and path/URL semantics. |
| `Vector2` | `VuoPoint2d` | A/B | Good numeric fit, but role may be point, offset, UV, scale, or size. |
| `Vector3` | `VuoPoint3d` | A/B | Good numeric fit; rotation/scale/color-like triples need role labels. |
| `Vector4` | `VuoPoint4d` or `VuoColor` | B | Color ports should usually map to `VuoColor`; generic 4D values to `VuoPoint4d`. |
| `Int2` | `VuoPoint2d` or custom pair | B | Vuo has no direct integer point type in the inspected type list. Do not silently lose integer semantics. |
| `Int3`, `Int4` | custom struct/list or `VuoPoint3d/4d` with caveat | B/C | Unknown until role is clear. |
| `List<float>` | `VuoList_VuoReal` | A/B | Check list order, empty-list behavior, aggregation semantics. |
| `List<int>` | `VuoList_VuoInteger` | A/B | Check enum/list role. |
| `List<string>` | `VuoList_VuoText` | A/B | Check text encoding/path semantics. |
| `List<Vector*>` | `VuoList_VuoPoint*d` or `VuoList_VuoColor` | B | Role must be explicit. |
| `Texture2D` | `VuoImage` | B/C | TiXL uses Direct3D texture concepts; Vuo `VuoImage` is GL texture-backed. |
| `Texture3dWithViews` | Unknown / no direct candidate | D | TiXL DX11 view container. |
| `MeshBuffers` | `VuoMesh` | B/C | Only safe for semantic mesh geometry, not raw TiXL buffer layout. |
| `BufferWithViews` | Unknown / Vuo mesh, list, or custom point buffer after design | C/D | No direct Vuo type. Decide point/attribute representation first. |
| `Command` | `VuoSceneObject`, `VuoLayer`, or composition pattern | C/D | TiXL render command flow is not a Vuo data type. |
| `ShaderGraphNode` | Unknown / Vuo shader subsystem after design | C/D | No direct Vuo SDF graph type. |
| `Gradient` | `VuoList_VuoColor`, Vuo gradient image workflow, or custom type | B/C | Stop and inspect semantics: color stops, interpolation, wrap. |
| `Curve` | `VuoCurve` or custom curve data | B/C | Vuo has `VuoCurve`, but TiXL animation curves may carry timeline semantics. |
| `Dict<float>` / `Dict<T>` | `VuoDictionary_*` only for supported key/value pairs | B/C | Vuo inspected dictionaries include text-text and text-real; other shapes Unknown. |
| `Object`, `object` | Unknown | C/D | Must inspect actual runtime type and downstream use. |
| `DataSet`, `StructuredList` | Unknown / custom data model | C/D | App-specific data model. |
| `ParticleSystem` | Unknown / Vuo scene/mesh/list design | C/D | TiXL particle runtime is not a Vuo primitive. |
| `PbrMaterial` | `VuoShader` or material-building nodes | C | Vuo uses `VuoShader` material concepts, not a direct PBR material value. |
| `SceneSetup` | `VuoSceneObject` hierarchy or composition pattern | C/D | Requires scene design. |
| `RenderTargetReference` | Unknown | C/D | TiXL app/render reference state. |
| `DateTime` | Vuo time type if available, else custom/text | B/C | Needs Vuo time type audit before claiming. |
| `GizmoVisibility` | enum/custom | B/C | Likely editor/debug specific; check whether runtime-visible. |

## TiXL Types Not Directly Portable

Treat these as D or C-with-design, not A/B:

- Direct3D/DX11 namespace and object types: `Direct3D11.*`, `DXGI.Format`, `ShaderResourceView`, `UnorderedAccessView`, `RenderTargetView`, `DepthStencilView`, `DepthStencilState`, `RasterizerState`, `SamplerState`, `BlendState`, `InputLayout`, `PrimitiveTopology`, `RawViewportF`, `RawRectangle`.
- Shader stage types: `PixelShader`, `VertexShader`, `GeometryShader`, `ComputeShader`.
- TiXL GPU buffer containers: `Buffer`, `BufferWithViews`, `Texture3dWithViews`.
- TiXL graph/runtime language types: `ShaderGraphNode`, `Command`.
- App/runtime references: `RenderTargetReference`, `SceneSetup`, broad `Object`/`object`.
- TiXL-specific systems: `ParticleSystem`, `LegacyParticleSystem`, `StructuredList`, `DataSet`.

If one of these appears only as an internal helper dependency but the public node has a simple Vuo equivalent, grade the public node from the public contract, and list the helper as a risk.

## Vuo Built-In Families Observed

The local Vuo checkout has relevant type and node families that can guide candidate mapping:

| Vuo family | local evidence | useful for |
|---|---|---|
| value types | `external/vuo/type/VuoReal.h`, `VuoInteger.h`, `VuoBoolean.h`, `VuoText.h` | Grade A scalar/string logic |
| point/vector types | `VuoPoint2d.h`, `VuoPoint3d.h`, `VuoPoint4d.h` | TiXL `Vector2/3/4` numeric roles |
| color type | `VuoColor.h` | TiXL color ports, `Vector4` color roles |
| image type/nodes | `VuoImage.h`, `external/vuo/node/vuo.image/*` | TiXL `Texture2D` image operations after Direct3D risks are isolated |
| mesh type/nodes | `VuoMesh.h`, `external/vuo/node/vuo.mesh/*` | simple mesh geometry, not raw TiXL `MeshBuffers` internals |
| scene type/nodes | `VuoSceneObject.h`, `external/vuo/node/vuo.scene/*` | render/scene/camera/group design, not direct `Command` translation |
| shader type | `VuoShader.h`, `external/vuo/node/vuo.scene/vuo.scene.shader.*` | material/shader mapping after shader model design |
| audio nodes | `external/vuo/node/vuo.audio/*` | audio analysis and buffers after TiXL timeline semantics are separated |
| MIDI/file/tree/dictionary nodes | `external/vuo/node/vuo.midi/*`, `vuo.file/*`, `vuo.tree/*`, `vuo.dictionary/*` | I/O/data nodes when device/runtime behavior is source-backed |

## Verification Fixture Types

Every Node Card should name one fixture type, even if the fixture is "Unknown until source audit."

| fixture type | use for | must check |
|---|---|---|
| numeric golden | scalar math, remap, range, trig, random with seed | exact or tolerance, edge cases, enum modes |
| vector/color golden | vector compose/decompose, color conversion | component order, alpha, color space, premultiply behavior |
| list fixture | list math, sort, combine, split | empty list, one item, ordering, length mismatch |
| string/text fixture | string search/transform/random/date formatting | Unicode, empty input, locale/timezone if relevant |
| image fixture | image FX/generate/use | dimensions, format/depth, alpha, sample pixels or perceptual hash |
| mesh fixture | mesh generate/modify/analyze | vertex count, element count, bounds, normals, UVs |
| point-buffer fixture | point generate/modify/draw | chosen representation, count, attributes, orientation, downstream draw behavior |
| scene/render smoke | `Command`, camera, group, draw, layer, render target | nonblank render, camera visibility, scale, hierarchy |
| shader/SDF fixture | `ShaderGraphNode`, raymarch, shader FX | generated shader text or rendered sample after a shader subsystem exists |
| audio/timeline fixture | audio reaction, FFT, playback, animation | deterministic input buffer, timeline/playback state, remap range |
| device/mock fixture | MIDI/OSC/DMX/serial/PTZ/video/web/network | mocked I/O contract, failure mode, unavailable device behavior |
| documentation fixture | D-grade/internal/docs-only nodes | source map, negative knowledge, explicit Unknowns |

## Negative Knowledge To Preserve

- Do not mention `PointsOnImage`, `ImageToPoints`, or `GetPosition` as TiXL nodes unless a newer source search verifies them.
- Use `LinearSamplePointAttributes`, not singular `LinearSamplePointAttribute`.
- Do not convert Texture2D into points by naming a nonexistent node. Use verified material workflows or verified point-sampling workflows only.
- Do not treat `LoadObj`, `CubeMesh`, SDF, or point buffers as visible by themselves. Visibility depends on the correct draw/render chain.
- Do not upgrade workflow notes marked inferred/needs validation into verified node behavior.
- Do not treat `.t3` adjacency as type proof. It is graph usage evidence.

## Worker Grading Checklist

When assigning a grade in a Node Card:

- Name the source behavior first, then the Vuo target behavior.
- Confirm source evidence tier: C# + docs + `.t3`, C# only, docs only, mismatch, or Unknown.
- Identify every TiXL port type and mark direct, candidate, blocked, or Unknown.
- Decide whether `Vector4` is color or point before writing `VuoColor` or `VuoPoint4d`.
- If `Command`, `BufferWithViews`, `ShaderGraphNode`, Direct3D/DX11, or device/app runtime appears, explain why the node is not A/B.
- Preserve clone-note certainty labels: verified, inferred, needs_validation.
- Select one verification fixture that would catch semantic drift.
- If the fixture cannot be built yet, grade lower or mark the blocker explicitly.
