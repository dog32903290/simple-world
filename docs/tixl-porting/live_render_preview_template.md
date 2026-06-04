# my_LiveRenderPreview Template

`vuo-compositions/my-live-render-preview-template.vuo` is the first medium-proof version of a TiXL-style live preview host.

It is intentionally a Vuo template, not a custom C node.

## Contract

The template owns the frame clock:

```text
Fire on Display Refresh
requestedFrame -> render/image node refresh or renderTick
requestedFrame -> Render Image to Window refresh
```

Field/generate nodes should stay semantic. They should not each carry their own display-refresh cable.

## How To Use

1. Open `vuo-compositions/my-live-render-preview-template.vuo`.
2. Replace `Example Image Source` with the image or render node you want to preview.
3. Keep `my_LiveRenderPreview Clock`.
4. Connect the clock to the node that actually cooks the image.
5. Connect the image output to `my_LiveRenderPreview Window`.

For the current SDF proof, the intended shape is:

```text
my_SphereSDF -> my_RaymarchField -> my_LiveRenderPreview Window
                      ^
                      |
my_LiveRenderPreview Clock
```

`my_SphereSDF` should not need a display-refresh cable unless its output adapter is still event-only. The render pressure belongs to `my_RaymarchField` and the preview window.
