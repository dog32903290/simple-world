# RenderTarget / TextureView Contract

This is the first render-resource contract for My World / TiXL-to-Vuo proofs.

## Boundary

For first-pass scene-to-image render nodes:

```text
RenderTarget := frame-domain render resource that owns Texture2D outputs
TiXL donor node := Lib.image.generate.basic.RenderTarget
visible node name := my_RenderTarget
Vuo body proof := vuo.scene.render.image2 -> VuoImage
primary output color := TiXL ColorForTextures / #9F008A
```

This contract depends on `Texture2D / VuoImage`, but it is not a stateless
image filter. It owns the act of rendering into an image-sized GPU resource.

It does not make Vuo pretend to have DirectX11 SRV/UAV/RTV handles. Texture
views are named here as a separate native-runtime contract because Vuo only
proves the body-layer route:

```text
scene/command -> offscreen image -> window
```

## Node Force

`my_RenderTarget` answers one question:

```text
what Texture2D buffers result from rendering this command/scene at this resolution?
```

It does not answer:

```text
how should a Texture2D be sampled in a shader?
how should a compute shader write into a Texture2D?
which previous-frame state should be retained?
which post effect should be applied?
```

Those stay separate:

```text
my_SrvFromTexture2d
my_UavFromTexture2d
my_RtvFromTexture2d
my_KeepPreviousFrame
image filter nodes
```

## TiXL Donor Semantics

Source evidence:

- C#: `Operators/Lib/image/generate/basic/RenderTarget.cs`
- .t3 defaults: `Operators/Lib/image/generate/basic/RenderTarget.t3`
- docs: `.help/docs/operators/lib/image/generate/basic/RenderTarget.md`
- GUID: `f9fe78c5-43a6-48ae-8e8c-6cdbbc330dd1`

Observed TiXL behavior:

```text
inputs:
  Command: Command required
  Resolution: Int2 = [0,0]
  Clear: bool = true
  ClearColor: Vector4
  Multisampling: int = 4
  TextureFormat: Format = R16G16B16A16_Float
  WithDepthBuffer: bool = true
  WithNormalBuffer: bool = false
  GenerateMips: bool = false
  TextureReference: RenderTargetReference = null
  EnableUpdate: bool = true

outputs:
  ColorBuffer: Texture2D
  DepthBuffer: Texture2D
  NormalBuffer: Texture2D
```

When `Resolution` is `[0,0]`, TiXL uses `context.RequestedResolution`. Invalid
sizes are ignored. `Format.Unknown` falls back to the default texture format.

At cook time TiXL:

1. Allocates or reallocates color/depth/normal textures when size, format,
   sample count, mip policy, or buffer options change.
2. Saves the previous viewport, render targets, camera matrices, background,
   foreground, and requested resolution.
3. Binds its render target views and optional depth/normal buffers.
4. Clears the buffers when `Clear=true` or before the first successful clear.
5. Sets a default camera and requested resolution for the rendered subtree.
6. Cooks `Command.GetValue(context)`.
7. Restores the previous render state.
8. Resolves MSAA color/depth/normal buffers when multisampling is enabled.
9. Generates mips when `GenerateMips=true`.
10. Publishes `ColorBuffer`, `DepthBuffer`, and `NormalBuffer`.

## Conversion

```text
Command + Resolution + render state -> Texture2D + Texture2D + Texture2D
VuoList_VuoSceneObject + camera + width/height -> VuoImage + VuoImage
```

The Vuo proof maps TiXL's `ColorBuffer` to Vuo's `image` output, and maps
TiXL's `DepthBuffer` pressure to Vuo's `depthImage` output. Vuo does not expose
a normal buffer on `vuo.scene.render.image2`, so `NormalBuffer` remains a native
renderer requirement.

## Parameters

```yaml
- id: Command
  classification: operation
  default: required
  affects: scene/command subtree rendered into the target
  safe_to_change_live: yes
  saved: graph connection

- id: Resolution
  classification: operation
  default: [0,0]
  affects: texture allocation, viewport, requested resolution
  safe_to_change_live: yes, but reallocates
  saved: yes

- id: Clear
  classification: state policy
  default: true
  affects: whether buffers are cleared before rendering
  safe_to_change_live: yes
  saved: yes

- id: ClearColor
  classification: operation
  default: [0,0,0,1]
  affects: color buffer background
  safe_to_change_live: yes
  saved: yes

- id: Multisampling
  classification: operation
  default: 4
  affects: texture allocation and resolve path
  safe_to_change_live: yes, but reallocates
  saved: yes

- id: TextureFormat
  classification: operation
  default: R16G16B16A16_Float
  affects: texture allocation and downstream precision
  safe_to_change_live: yes, but reallocates
  saved: yes

- id: WithDepthBuffer
  classification: operation
  default: true
  affects: depth texture allocation and z sorting
  safe_to_change_live: yes, but reallocates
  saved: yes

- id: WithNormalBuffer
  classification: operation
  default: false
  affects: normal texture allocation
  safe_to_change_live: yes, but reallocates
  saved: yes

- id: GenerateMips
  classification: operation
  default: false
  affects: downstream mip sampling support
  safe_to_change_live: yes
  saved: yes

- id: TextureReference
  classification: state/routing
  default: null
  affects: feedback routes through UseRenderTarget
  safe_to_change_live: yes
  saved: graph connection

- id: EnableUpdate
  classification: state policy
  default: true
  affects: whether the command subtree is rendered this cook
  safe_to_change_live: yes
  saved: yes
```

## TextureView Edge

Texture views are now split into their own contract:

```text
docs/runtime/TEXTURE_VIEW_CONTRACT.md
```

`my_RenderTarget` owns the creation and publication of `Texture2D` buffers.
TextureView nodes own the typed access identity for those buffers:

```text
my_SrvFromTexture2d
my_UavFromTexture2d
my_RtvFromTexture2d
my_DsvFromTexture2d
```

These are not Vuo body nodes. They belong to the future native renderer lane.

## Failure Behavior

Invalid resolution:

- do not reallocate
- do not publish a new target
- diagnostic includes requested width/height and max size

Unknown texture format:

- warn once
- fall back to `R16G16B16A16_Float`
- diagnostic records fallback format

Missing command:

- render clear/background only if the host renderer can produce a valid target
- diagnostic records missing command

MSAA resolve or mip generation failure:

- keep the last valid output only if a valid texture already exists
- diagnostic records failed stage
- downstream nodes may cook only when the color buffer is populated

View bind flag mismatch:

- `my_UavFromTexture2d` returns no new UAV
- `my_RtvFromTexture2d` returns no new RTV
- `my_SrvFromTexture2d` may create a view only when shader-resource semantics
  are valid for the texture

## Vuo Host Proof

Proof composition:

```text
vuo-compositions/myworld-render-target-proof.vuo
```

Proof shape:

```text
Fire on Display Refresh:requestedFrame
-> Make shader / transform / cube / camera
-> Build VuoList_VuoSceneObject
-> Render Scene to Image
-> Render Image to Window
```

The proof is accepted only if:

- Vuo SDK compile/link succeeds.
- The runner window is captured by CoreGraphics window id.
- The content crop is not black.
- The composition uses `vuo.scene.render.image2`.
- The composition exposes `image` and `depthImage` pressure.
- The composition does not claim Vuo has TiXL DX11 SRV/UAV/RTV parity.

## Next Door

After this lane holds, the next heavier risks are:

```text
native Command stream
native RenderTarget allocation
native TextureView identity
FeedbackFx nodes such as AfterGlow / AdvancedFeedback
```
