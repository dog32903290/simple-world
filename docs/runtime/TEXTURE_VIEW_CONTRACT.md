# TextureView Contract

This is the native GPU-view contract for My World / TiXL ports.

## Boundary

For TiXL DX11 texture-view adapter nodes:

```text
TextureView := typed access identity for an existing GPU texture
TiXL namespace := Lib.render._dx11.api
visible node names := my_SrvFromTexture2d, my_UavFromTexture2d, my_RtvFromTexture2d, my_DsvFromTexture2d
primary type color := TiXL ColorForTextures / #9F008A
host proof := contract fixture only; Vuo cannot prove DX11/Metal view identity
```

This contract depends on `Texture2D` and `RenderTarget`, but it does not create
or render a texture. It only creates a view that tells a later shader/render
stage how an existing texture may be accessed.

## Node Forces

Each TextureView node carries exactly one force.

```text
my_SrvFromTexture2d answers:
  how can this Texture2D be sampled/read by a shader?

my_UavFromTexture2d answers:
  how can this Texture2D be read/written by a compute or unordered stage?

my_RtvFromTexture2d answers:
  how can this Texture2D be bound as a color render output?

my_DsvFromTexture2d answers:
  how can this Texture2D be bound as a depth/stencil output?
```

They do not answer:

```text
what image exists this frame?
which command renders into the texture?
how should the texture be filtered or color graded?
how should previous frames be remembered?
```

Those stay separate:

```text
Texture2D
my_RenderTarget
image filter nodes
my_KeepPreviousFrame
```

## Source Evidence

Source files:

- `Operators/Lib/render/_dx11/api/SrvFromTexture2d.cs`
- `Operators/Lib/render/_dx11/api/SrvFromTexture2d.t3`
- `Operators/Lib/render/_dx11/api/UavFromTexture2d.cs`
- `Operators/Lib/render/_dx11/api/UavFromTexture2d.t3`
- `Operators/Lib/render/_dx11/api/RtvFromTexture2d.cs`
- `Operators/Lib/render/_dx11/api/RtvFromTexture2d.t3`
- `Operators/Lib/render/_dx11/api/DsvFromTexture2d.cs`
- `Operators/Lib/render/_dx11/api/DsvFromTexture2d.t3`

Spec evidence:

- `external/tixl-spec/TIXL_CLONE_SPEC_20260604/TIXL_CLONE_NODE_SPECS_ALL.md`
- `docs/tixl-porting/namespaces/render_mesh_point.md`

TiXL class GUIDs:

```text
SrvFromTexture2d := c2078514-cf1d-439c-a732-0d7b31b5084a
UavFromTexture2d := 84e02044-3011-4a5e-b76a-c904d9b4557f
RtvFromTexture2d := 57a1ee33-702a-41ad-a17e-b43033d58638
DsvFromTexture2d := 4494473b-1868-460e-8ac3-b5d57c8a156e
```

There are no official operator docs for these nodes in the merged TiXL docs.
Use C# behavior as the semantic source.

## Contracts

### my_SrvFromTexture2d

```text
conversion: Texture2D -> ShaderResourceView
input: Texture: Texture2D
output: ShaderResourceView: ShaderResourceView
```

Observed behavior:

- If the same texture object is already viewed, return without recreating.
- If a new non-null texture arrives, dispose the previous SRV before creating a
  new one.
- If the texture has `DepthStencil` bind flags, create the SRV with explicit
  `Format.R32_Float`.
- Otherwise create a default `ShaderResourceView`.
- If the texture is null, dispose the current SRV.
- On exception, log an error once until a successful update resets the complaint
  flag.

Common TiXL graph use:

- feeds `Lib.render._dx11.fxsetup.SetPixelAndVertexShaderStage`
- feeds image FX setup nodes
- accepts outputs from `RenderTarget`, `LoadImage`, `UseFallbackTexture`, and
  texture generator nodes

### my_UavFromTexture2d

```text
conversion: Texture2D -> UnorderedAccessView
input: Texture: Texture2D
output: UnorderedAccessView: UnorderedAccessView
```

Observed behavior:

- If the texture input dirty flag is not dirty, do nothing.
- If the texture is null or disposed, return without creating a view.
- Create a UAV only when `BindFlags.UnorderedAccess` is present.
- If the bind flag is missing, log a warning and do not synthesize a new
  texture or capability.
- On exception, log an error and set the UAV output to null.

Common TiXL graph use:

- compute/image transform routes such as `Crop`
- feedback/swap routes such as `SwapTextures`
- compute shader stage write targets

### my_RtvFromTexture2d

```text
conversion: Texture2D + ArrayIndex -> RenderTargetView
inputs:
  Texture: Texture2D
  ArrayIndex: int = 0
output: RenderTargetView: RenderTargetView
```

Observed behavior:

- If neither `Texture` nor `ArrayIndex` is dirty, do nothing.
- Clamp `ArrayIndex` to `0..10000`, then clamp to the texture array range if
  needed.
- Create an RTV only when `BindFlags.RenderTarget` is present.
- If the texture is a cube map, create a `Texture2DArray` RTV with six slices.
- If `ArrayIndex > 0`, create a one-slice array RTV.
- Otherwise create a default `RenderTargetView`.
- If the bind flag is missing, log a warning and do not synthesize a new
  texture or capability.

Known TiXL source wart:

- The catch block builds a warning message but sets `_lastErrorMessage = null`;
  clone implementations should report the error rather than copying this status
  bug.

Common TiXL graph use:

- feeds `Lib.render._dx11.api.OutputMergerStage`
- feeds `Lib.render._dx11.api.ClearRenderTarget`

### my_DsvFromTexture2d

```text
conversion: Texture2D -> DepthStencilView
input: Texture: Texture2D
output: DepthStencilView: DepthStencilView
```

Observed behavior:

- If the texture input dirty flag is not dirty, do nothing.
- If a non-null texture arrives, create a `DepthStencilView` using
  `Format.D32_Float` and `Texture2D` dimension.
- On exception, log a warning.
- The current C# source does not explicitly check `BindFlags.DepthStencil`.
  Native ports should validate depth-stencil capability instead of relying on
  backend failure.

Common TiXL graph use:

- depth side of render target / output merger routes
- clear-depth routes

## Native Port Shape

The native runtime should represent texture capabilities explicitly:

```text
Texture2DHandle
  id
  width
  height
  format
  arraySize
  sampleCount
  optionFlags
  bindFlags:
    ShaderResource
    UnorderedAccess
    RenderTarget
    DepthStencil
```

View creation is deterministic:

```text
Texture2DHandle + ViewKind + optional ArrayIndex -> TextureViewHandle | diagnostic
```

View handles must not imply texture ownership. The source texture owns lifetime;
views retain only enough identity to invalidate when the texture changes or is
disposed.

First API scaffold:

```text
docs/runtime/scripts/native_resource_api.py

Texture2DHandle
TextureViewHandle
TextureResourceRegistry
create_texture_view()
allocate_render_target_resources()
```

This API is still data-backed, not GPU-backed. Its job is to make the texture
resource and view rules callable by the native runtime lane before Metal/DX11
objects exist.

## Failure Behavior

Missing texture:

- no new view
- SRV disposes the current view
- UAV/RTV/DSV keep output policy explicit in native diagnostics

Disposed texture:

- no new view
- diagnostic stage: `disposed texture`
- downstream shader/render stages must not cook with the invalid view

Missing bind capability:

- SRV requires shader-readable semantics, with special depth SRV handling
- UAV requires `UnorderedAccess`
- RTV requires `RenderTarget`
- DSV requires `DepthStencil` in the native port, even though TiXL's C# relies
  on backend creation failure

Texture identity changed:

- dispose or invalidate the previous view
- create a new view only if the capability check passes
- diagnostic includes old texture id and new texture id

Array index out of range:

- clamp to the last valid array slice
- diagnostic records requested and resolved index

Backend creation failure:

- no fake view
- record view kind, texture id, format, bind flags, and backend error
- downstream nodes may cook only if the required view is populated

## Evidence

Current proof is a headless contract fixture:

```text
tests/texture_view_contract.test.js
tests/native_resource_api.test.js
```

There is intentionally no Vuo proof composition for this contract. Vuo can
prove `scene -> image`, but it cannot prove DirectX11 or Metal SRV/UAV/RTV/DSV
identity. This line moves to a native renderer proof when we have a low-level
GPU backend.

## Next Door

After TextureView holds, the next high-risk contract is:

```text
Command stream / render stage nodes
```

That is where `OutputMergerStage`, `SetPixelAndVertexShaderStage`,
`ComputeShaderStage`, `Draw`, and dispatch nodes become meaningful instead of
floating GPU handles.
