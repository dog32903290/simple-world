# Texture2D / Image Contract

This is the first high-risk image contract for My World / TiXL-to-Vuo proofs.

## Boundary

For stateless first-pass image nodes:

```text
Texture2D := frame-local image resource
Vuo body := VuoImage
primary color := TiXL ColorForTextures / #9F008A
```

This contract does not cover render-target state, previous-frame feedback, mip generation, DX11 views, UAV/SRV/RTV handles, depth buffers, or persistent GPU ownership.

## Node Force

Texture image nodes answer one question:

```text
what image exists for this frame?
```

They do not answer:

```text
where is this image stored across frames?
which command rendered into it?
which GPU API object owns it?
```

Those are separate future contracts:

```text
RenderTarget
TextureView
FeedbackState
Command
```

## First Conversion

```text
Texture2D -> Texture2D
VuoImage -> VuoImage
```

Allowed in this lane:

- stateless source images
- stateless image filters
- stateless image blends/composites
- explicit width/height or resolution params
- frame clock through `FireOnDisplayRefresh:requestedFrame`

Forbidden in this lane:

- hidden previous-frame memory
- implicit render target creation
- DX11-specific view extraction
- texture arrays, cube maps, depth textures
- treating `Command` and `Texture2D` as the same value

## TiXL Donor Pressure

The first donor pressure is `Lib.image.use.Blend`.

Evidence:

- TiXL path: `Operators/Lib/image/use/Blend.cs`
- TiXL shader helper: `Operators/Lib/Assets/shaders/img/fx/Blend.hlsl`
- Research row: `docs/tixl-porting/namespaces/image.md`
- Vuo anchor: `vuo.image.blend`

This is a body-layer parity proof, not a full TiXL clone. TiXL `Blend` still has unresolved surface area: `AlphaMode`, `BlendMode`, `ColorA`, `ColorB`, `GenerateMips`, `Resolution`, `ScaleMode`, and DX11 render-target behavior.

## Current Vuo Proof

```text
Fire on Display Refresh:requestedFrame
-> Make Color Image:refresh
-> Make Checkerboard Image:refresh
-> Blend Images:refresh
-> Render Image to Window:refresh

Make Color Image:image -> Blend Images:background
Make Checkerboard Image:image -> Blend Images:foreground
Blend Images:blended -> Render Image to Window:image
```

Proof composition:

```text
vuo-compositions/myworld-texture2d-blend-proof.vuo
```

Expected evidence:

- Vuo SDK compile/link succeeds.
- The CLI harness captures the named runner window by CoreGraphics window id.
- The content crop is not black.
- The visual output contains a blended color/checkerboard image.

## Failure Behavior

If an input image is missing or invalid, the proof should fail visually and in diagnostics. Do not synthesize a fake texture silently.

For future custom nodes:

- visible error: node output should be visibly invalid or pass through according to explicit policy
- last valid output: forbidden unless the node is a state/feedback node
- downstream cooking: allowed only when output is populated
- diagnostics: image width, height, color depth, populated flag, and source node id

## Next Door

After this lane is stable, the next risk is `FeedbackState / PreviousFrame`, not more stateless blends.
