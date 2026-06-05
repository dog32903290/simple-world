# Native Product Canvas Surface Proof

NativeProductCanvasSurfaceProof answers:

Can a command-authored graph leave the architecture-only headless shell and bind
to a real native app window and Metal canvas surface?

Acceptance line:

```text
commandGraph -> native app window -> MTKView canvas -> runtime frame artifact
```

## Required Claims

- nativeWindowCanvasSurface: true when AppKit creates an NSWindow and MetalKit
  creates an MTKView backed by a Metal device.
- commandGraphOnlyMutation: true; UI, AI, importer, and fixture mutations must
  stay on commandGraph.
- runtimeGraphAttached: true; the runtime cook graph is derived from replayed
  commands and is attached to the canvas frame artifact.
- headlessShellRenamed: false; this is not a headless shell renamed as product.

## Native Surface Boundary

- AppKit owns the app/window surface.
- MetalKit owns the MTKView canvas.
- MTKView exposes a CAMetalLayer-backed drawable surface for runtime frames.
- The proof publishes app, canvas, runtimeGraph, frame, command log, result, and
  error artifacts.

## Boundaries

- This is not the final human-facing GUI skin.
- This is not a complete interaction model, inspector, menu bar, or timeline.
- This is not a complete texture runtime; it only proves product shell/canvas
  attachment for a command-authored runtime frame.
- This is not a headless shell renamed as product.
