---
name: tixl-vuo-node-port
description: Use when porting, naming, validating, or debugging TiXL visual nodes into My World/Vuo prototypes, especially shader, SDF, raymarch, mesh, image, or renderTick nodes.
---

# TiXL to Vuo Node Port

Use this skill when a TiXL node is being studied, cloned, renamed, tested in the My World runtime tools, or packaged as a Vuo custom node.

The goal is not to copy TiXL blindly. The goal is to preserve TiXL's useful node law, prove the node in our own harness, then give it a Vuo body without letting Vuo become the final runtime contract.

## Naming Contract

- The creator-facing visible title is `my_<ExactTiXLNodeName>`.
- Preserve the TiXL node name exactly after `my_`: capitalization, acronyms, and word boundaries stay as TiXL wrote them.
- Classify by TiXL `Operators/Lib` path. Keep that category in docs, folders, menus, and Vuo class names.
- Vuo class names cannot use spaces. Use `my.<tixl_category_path>.<lowerCamelNodeName>`.
- If a node is a compound proof made from several TiXL nodes, mark it as compound and list donor nodes. Do not pretend it is an exact TiXL port.
- Stop and rename before adding behavior when a node still uses a generic `myworld.*` or marketing name.

Examples:

```text
TiXL name -> My World visible title:
`SphereSDF` -> `my_SphereSDF`
`RaymarchField` -> `my_RaymarchField`

TiXL Operators/Lib/field/generate/sdf/SphereSDF.cs
visible title: my_SphereSDF
Vuo class:    my.field.generate.sdf.sphereSdf

TiXL Operators/Lib/field/render/RaymarchField.cs
visible title: my_RaymarchField
Vuo class:    my.field.render.raymarchField

Compound proof: SphereSDF + RaymarchField
visible title: my_SphereSDF_RaymarchField
Vuo class:    my.field.render.sphereRaymarch
```

## Color Contract

Use TiXL type color, not our own category palette.

TiXL classifies location by namespace, but it colors graph nodes and cables from the node's primary data type:

```text
node category: TiXL Operators/Lib path
node color:    primary output type through TypeUiRegistry
```

Evidence in TiXL:

```text
external/tixl/Editor/Gui/MagGraph/Model/MagGraphLayout.cs
- first output sets item.PrimaryType

external/tixl/Editor/Gui/MagGraph/Ui/MagGraphCanvas.DrawNode.cs
- TypeUiRegistry.GetPropertiesForType(item.PrimaryType)
- ColorVariations.OperatorLabel.Apply(typeColor)

external/tixl/Editor/Gui/MagGraph/Ui/MagGraphCanvas.DrawConnection.cs
- connection type uses TypeUiRegistry.GetPropertiesForType(type)

external/tixl/Editor/UiModel/InputsAndTypes/TypeUiProperties.cs
external/tixl/Editor/Gui/Styling/UiColors.cs
- base datatype colors
```

Base colors to mirror:

```text
`Value/default`      `ColorForValues`      rgba(0.525, 0.550, 0.554, 1.0)  `#868C8D`
`string`             `ColorForString`      rgba(0.468, 0.586, 0.320, 1.0)  `#779552`
`Texture2D`          `ColorForTextures`    rgba(0.625, 0.000, 0.540, 1.0)  `#9F008A`
`DX11/shader types`  `ColorForDX11`        rgba(0.840, 0.460, 0.440, 1.0)  `#D67570`
`Command`            `ColorForCommands`    rgba(0.132, 0.722, 0.762, 1.0)  `#22B8C2`
`GPU data`           `ColorForGpuData`     rgba(0.720, 0.200, 0.180, 1.0)  `#B8332E`
`ShaderGraphNode`    `ColorForShaderGraph` rgba(0.820, 0.260, 0.700, 1.0)  `#D142B3`
```

Rules:

- Do not color nodes by namespace. `Lib.field.*` is a category path, not a color key.
- Pick the node color from the primary output type, matching TiXL's first output behavior.
- Color cables by their carried value type, not by source node category.
- For `SphereSDF`, `RaymarchField`, and other SDF/field shadergraph nodes, use `ShaderGraphNode -> ColorForShaderGraph` unless the exact TiXL node's primary output says otherwise.
- If a Vuo proof fuses several TiXL nodes, color it by the proof node's creator-facing output type and list the donor node colors in docs.

## Workflow

1. **TiXL source audit**
   - Find the TiXL source first: `.cs`, `.t3`, shader templates, and descriptions under `external/tixl/Operators/Lib`.
   - Record donor path, category, ports, default values, output type, hidden resources, and shader/runtime stage.
   - Record the primary output type and mapped TiXL type color.
   - For shader nodes, trace the generated code path, not only the visible node wrapper.

2. **Contract before code**
   - Write the node contract: inputs, outputs, defaults, failure behavior, execution domain, and whether it is exact or compound.
   - Data ports are semantic values. Event ports trigger cooking. Do not use data values as hidden event carriers.
   - Render/cook nodes that should update every frame expose `renderTick`.

3. **Our runtime harness first**
   - Create or update a graph fixture under `docs/runtime/fixtures`.
   - Compile through the My World shadergraph shell before touching Vuo.
   - For GLSL/WebGL, use the persistent WebGL shader server for repeated checks; cold browser startup time is not shader compile time.
   - Save artifacts such as generated shader source, compile JSON, cook order, and error logs.

4. **Contract-to-Vuo proof gate**
   - Every new contract needs a matching Vuo trial before the contract is called done.
   - The trial can be one exact `my_<ExactTiXLNodeName>` Vuo node, or a small proof composition that wires several related nodes together when one node alone cannot show the line.
   - The Vuo trial must answer: what can 柏為 see or manipulate that proves this contract is not just text?
   - For contracts that Vuo cannot truly prove, create a bounded body-layer adapter and say exactly what it proves and what it does not prove.
   - Do not add more headless-only command/resource contracts without either a Vuo body-layer proof, a Vuo-hosted related-node composition, or a written reason why Vuo cannot expose that force yet.

5. **Vuo body-layer proof**
   - Put custom Vuo nodes in `vuo-nodes/`, then install to `~/Library/Application Support/Vuo/Modules/`.
   - Install means physically copy the changed `.c` node into `~/Library/Application Support/Vuo/Modules/`; keeping it only in `vuo-nodes/` will show `Not Installed` in Vuo and fail `vuo-compile`.
   - Use `VuoImageRenderer` for image-output shader proofs.
   - Vuo inputs are const; sanitize into local variables such as `renderWidth`, `renderHeight`, or `renderRadius`.
   - Wire `Fire on Display Refresh -> renderTick` for live rendering.
   - Wire the render window node's event input too, such as `Fire on Display Refresh -> Render Image to Window`.
   - Keep Vuo composition comment labels CLI-safe. Put detailed Chinese notes in the file header, and keep `vuo.comment` labels short ASCII/single-line unless `vuo-compile` proves they parse.

6. **Visual proof**
   - Open the proof composition with `tools/vuo_harness.py open <composition>`.
   - Run, screenshot, and inspect logs with `tools/vuo_harness.py status`, `run`, `screenshot --name ...`, and `logs --minutes ...`.
   - Prefer `tools/vuo_harness.py cli-proof <composition> --seconds 3 --name <proof-name>` after installing custom nodes. It performs real Vuo SDK compile/link/run, captures a runner screenshot, and reports `mostlyBlack`.
   - If screenshot focus is unreliable, use the harness logs plus a user screenshot or improve the harness before claiming GUI control.

## Known Pits

- **Not Installed**: read Vuo logs first. A common cause is illegal C in the node source, including assigning to const input parameters.
- **Installed in repo only**: Vuo does not load from `vuo-nodes/` by itself. Copy changed nodes into `~/Library/Application Support/Vuo/Modules/`, restart/reset Vuo if needed, then confirm logs say `Loaded into user environment`.
- **CLI comment parser**: a composition can open in Vuo Editor but fail Vuo CLI on `bad label format` when `vuo.comment` labels contain multiline or non-ASCII text. Move rich notes to the header and keep graph comment labels CLI-safe.
- **Black window**: check event wiring first. The image data cable alone does not force continuous recompute. Add `renderTick`, then use a debug gradient or pure debug color before touching raymarch math.
- **Slow test loop**: do not restart Chrome for every shader check. Use the persistent WebGL shader server.
- **False TiXL match**: a Vuo proof can fuse TiXL nodes for speed, but docs and naming must say it is compound.
- **Event pollution**: `Fire on Display Refresh` should hit render/cook event ports, not mutate semantic control values every frame.
- **Module cache warning**: Vuo may warn that a module cache is out-of-date or used by another process. Treat this as not fatal if compile/link/run succeeds and the node appears in `loadedUserNodes`; reset/quit old Vuo composition processes when it blocks rebuild.
- **Editor proof only**: seeing a Vuo window is not enough. For visual nodes, require compile/link/run plus screenshot evidence where `mostlyBlack` is false before calling the proof done.

## Required Verification

Run the narrow checks closest to the changed node:

```bash
node --test tests/runtime_shadergraph_shell.test.js
node --test tests/runtime_shadergraph_webgl_compile.test.js
node --test tests/runtime_shadergraph_webgl_server.test.js
node --test tests/vuo_sphere_raymarch_node.test.js
node --test tests/vuo_sphere_raymarch_composition.test.js
```

For Vuo nodes, also confirm the app log says the node loaded into the user environment and capture visual evidence when the output is visual.

For a real Vuo host proof, install the custom node sources first, then run:

```bash
python3 tools/vuo_harness.py cli-status
python3 tools/vuo_harness.py cli-proof vuo-compositions/<proof>.vuo --seconds 3 --name <proof-name>
```

Accept the proof only when:

- Vuo SDK compile and link return `0`.
- `loadedUserNodes` includes the custom node classes.
- the screenshot exists and `mostlyBlack` is `false`.
- warnings are either explained as non-fatal module cache / Pro-license noise or fixed.

## Sphere Raymarch Lesson

The first compound proof should be named `my_SphereSDF_RaymarchField` unless we later find an exact TiXL node named `SphereRaymarch`.

```text
TiXL SphereSDF / RaymarchField audit
-> My World graph fixture
-> shell shader generation
-> WebGL compile smoke
-> persistent WebGL compile server
-> Vuo custom image node
-> Vuo composition with renderTick
```

The important discovery was not the sphere. The important discovery was the boundary:

```text
TiXL gives node law.
My World harness proves generated runtime behavior.
Vuo gives a visible host body.
renderTick carries time/event pressure.
data ports carry meaning.
```

## Material PBR Lesson

The Material/PBR session proved a second body-layer pattern:

```text
TiXL SetMaterial / DrawMesh PBR contract
-> headless Material/PBR fixture
-> my_SetMaterial emits PbrMaterial contract text
-> my_DrawMeshPbrProof consumes that contract and renders a visible adapter image
-> Vuo CLI proof installs nodes, compile/link/runs, captures non-black screenshot
```

Name boundary:

```text
my_SetMaterial: exact TiXL visible node name, Vuo body-layer adapter.
my_DrawMeshPbrProof: proof adapter only, not full my_DrawMesh.
```

Do not claim MeshBuffers, BufferWithViews, InputAssemblerStage, native GPU renderer, or TiXL DrawMesh parity from this Vuo proof. It only proves that runtime/headless contract state can be carried into connectable Vuo nodes and inspected visually.
