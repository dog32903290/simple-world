# ConstantImage Contract

This is the first visible runtime proof node moved from `my-world` into the Vuo-hosted `simple_world` lane.

## Boundary

```text
my-world donor := /Users/chenbaiwei/Projects/my-world/fixtures/runtime/top_constant_to_output.graph.json
runtime type := image.constant
browser alias := top.constant
Vuo visible node := my_ConstantImage
primary output := Texture2D / VuoImage
TiXL type color := ColorForTextures / #9F008A
```

This node answers one question:

```text
what constant Texture2D exists for this frame?
```

It does not answer:

```text
who owns the GPU texture?
what command rendered into it?
does the texture have SRV/UAV/RTV views?
is there previous-frame memory?
```

Those stay in `RenderTarget`, `TextureView`, `Command`, and `FeedbackState`.

## Vuo Body Proof

Proof composition:

```text
vuo-compositions/myworld-constant-image-pipeline-proof.vuo
```

Visible pressure lines:

```text
my_MainClock -> all render/cook event ports
my_ConstantImage -> my_Blend
my_ConstantImage -> my_KeepPreviousFrame
my_ConstantImage -> my_RenderTarget
my_ClearRenderTarget -> Render Image to Window
```

`my_MainClock` is the proof-level scheduler adapter. In Vuo it still receives a
host display-refresh event, but downstream nodes no longer wire directly to
`Fire on Display Refresh`. This keeps the creator-facing graph closer to TiXL:
one main clock drives live preview, and a future translator can hide the Vuo
host event node completely.

The `my_ClearRenderTarget` lane is a bounded command proof: it makes the clear command visible as a cleared color buffer, but it does not prove DX11 RTV/DSV identity or command stream prepare/update/restore parity.
