# FeedbackState / PreviousFrame Contract

This is the first stateful image contract for My World / TiXL-to-Vuo proofs.

## Boundary

For first-pass image memory nodes:

```text
FeedbackState := frame-domain image memory across display refreshes
Vuo body proof := vuo.image.feedback
TiXL donor node := Lib.image.use.KeepPreviousFrame
visible node name := my_KeepPreviousFrame
primary color := TiXL ColorForTextures / #9F008A
```

This contract depends on the earlier `Texture2D / VuoImage` contract, but it is
not a stateless texture filter. It owns memory across frames.

## Node Force

`my_KeepPreviousFrame` answers one question:

```text
what image from the previous kept frame is available beside the current kept frame?
```

It does not answer:

```text
how is the image rendered into a framebuffer?
which DX11 view owns the texture?
which feedback FX should be applied?
how should a texture be blended or transformed?
```

Those stay separate:

```text
RenderTarget
TextureView
Blend
TransformImage
FeedbackFx
```

## TiXL Donor Semantics

Source evidence:

- C#: `Operators/Lib/image/use/KeepPreviousFrame.cs`
- .t3 defaults: `Operators/Lib/image/use/KeepPreviousFrame.t3`
- docs: `.help/docs/operators/lib/image/use/KeepPreviousFrame.md`
- research row: `docs/tixl-porting/namespaces/image.md`

Observed TiXL behavior:

```text
inputs:
  ImageA: Texture2D
  Keep: bool = true

outputs:
  CurrentFrame: Texture2D
  PreviousFrame: Texture2D
```

When `Keep=true` and `ImageA` is connected and populated:

1. The node allocates two internal textures when format/size/mip/sample state changes.
2. It copies `ImageA` into one internal texture.
3. It outputs the copied texture as `CurrentFrame`.
4. It outputs the other internal texture as `PreviousFrame`.
5. It toggles the internal buffer index.

When `Keep=false`, `ImageA` is missing, or `ImageA` is null:

```text
do not copy
do not toggle buffers
do not publish a new current/previous pair
```

The first kept frame has no meaningful previous image content. Treat
`PreviousFrame` as uninitialized/invalid until at least two kept frames have
successfully copied.

## Conversion

```text
Texture2D + Bool -> Texture2D + Texture2D
VuoImage + VuoBoolean -> VuoImage + VuoImage
event -> state
state -> image
```

This is a state node, not a pure image filter.

## Parameters

```yaml
id: Keep
label: Keep
type: Bool
default: true
owner: NodeInstance
classification: state policy
affects: whether the incoming frame is copied and buffers toggle
safe_to_change_live: yes
saved: yes
```

## Failure Behavior

Invalid input:

- visible error: mark node state invalid after diagnostics are available
- output policy: retain last published pair only if the node had a valid pair
- downstream cooking: allowed only when `CurrentFrame` is populated
- diagnostics: input connected, input populated, width, height, format if known,
  keep value, copied frame count, previous valid flag

Format/size change:

- dispose/reallocate the two internal buffers
- reset previous-valid state
- first new frame after reallocation has invalid previous content

Runtime copy failure:

- do not toggle buffers after failed copy
- keep last valid pair if available
- publish diagnostic error

## Vuo Host Proof

Vuo has a built-in feedback image node:

```text
vuo.image.feedback
```

This is only a host-layer pressure test for frame-domain image memory. It is
not exact parity for TiXL `KeepPreviousFrame` because Vuo's built-in produces a
single feedback image, while TiXL exposes two outputs:

```text
CurrentFrame
PreviousFrame
```

Proof composition:

```text
vuo-compositions/myworld-feedback-state-proof.vuo
```

Proof shape:

```text
Fire on Display Refresh:requestedFrame
-> Make Stripe Image:refresh/angle
-> Make 2D Transform:refresh
-> Blend Image with Feedback:refresh/image/feedbackTransform
-> Render Image to Window:refresh/image
```

The proof is accepted only if:

- Vuo SDK compile/link succeeds.
- The runner window is captured by CoreGraphics window id.
- The content crop is not black.
- Runtime evidence shows a GPU image renderer.
- The composition uses `vuo.image.feedback` and does not use a fake stateless
  blend as proof.

## Next Door

After this lane holds, the next heavier risks are:

```text
RenderTarget
TextureView / DX11 SRV-UAV-RTV
FeedbackFx nodes such as AfterGlow / AdvancedFeedback
```
