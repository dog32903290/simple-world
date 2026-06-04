# TiXL Field SDF Raymarch Audit

This is the first TiXL to Vuo proof lane for:

```text
SphereSDF -> RaymarchField
```

This proof lane keeps the two TiXL donor nodes visible as separate Vuo body-layer adapters. There is no audited TiXL node named `SphereRaymarch`, so `my_SphereSDF_RaymarchField` is not counted as a TiXL node in this batch.

## SphereSDF

| Item | Audit |
| --- | --- |
| TiXL source | `external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs` |
| TiXL defaults | `external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.t3` |
| TiXL UI docs | `external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.t3ui` |
| TiXL category | `Operators/Lib/field/generate/sdf` |
| Creator-facing title | `my_SphereSDF` |
| Vuo class for exact node | `my.field.generate.sdf.sphereSdf` |
| Primary output | `Result: ShaderGraphNode` |
| TiXL type color | `ShaderGraphNode -> ColorForShaderGraph -> #D142B3` |
| Inputs | `Center: Vector3 = (0, 0, 0)`, `Radius: float = 0.5` |
| Execution domain | Shader graph node; updates through `ShaderNode.Update(context)` |
| Shader behavior | Emits signed distance `length(p.xyz - Center) - Radius` into field `.w`; stores local space in `.xyz` |

## RaymarchField

| Item | Audit |
| --- | --- |
| TiXL source | `external/tixl/Operators/Lib/field/render/RaymarchField.cs` |
| TiXL defaults | `external/tixl/Operators/Lib/field/render/RaymarchField.t3` |
| TiXL UI docs | `external/tixl/Operators/Lib/field/render/RaymarchField.t3ui` |
| TiXL shader template | `external/tixl/Operators/Lib/Assets/shaders/img/generate/RaymarchSDFFieldWithMatTemplate.hlsl` |
| TiXL category | `Operators/Lib/field/render` |
| Creator-facing title | `my_RaymarchField` |
| Vuo class for exact node | `my.field.render.raymarchField` |
| Primary output | `DrawCommand: Command` |
| TiXL type color | `Command -> ColorForCommands -> #22B8C2` |
| Secondary output | `ShaderCode: string -> ColorForString -> #779552` |
| Required input | `SdfField: ShaderGraphNode = null` |
| Key defaults | `MinDistance = 0.002`, `MaxSteps = 100`, `MaxDistance = 300`, `StepSize = 1`, `Color = (1, 1, 1, 1)` |
| Execution domain | Render command / shader assembly, not a pure data value |

## My World Runtime Fixture

| Item | Contract |
| --- | --- |
| Fixture | `docs/runtime/fixtures/sphere_sdf_raymarch.graph.json` |
| Runtime shell | `docs/runtime/scripts/compile_shadergraph_shell.py` |
| Artifact directory | `docs/runtime/artifacts/e1_shadergraph` |
| Cook order | `sphere_sdf_1 -> raymarch_field_1` |
| Published artifact | `shaderCode` as inspectable shader source |
| Failure behavior | Missing or unsupported SDF input writes `errors.json` and empty shader source |

The fixture deliberately carries TiXL node law as structured graph data before Vuo. It does not claim to be TiXL's DX11/HLSL renderer.

## Vuo Body Proof Nodes

| Item | Contract |
| --- | --- |
| Vuo node | `vuo-nodes/my.field.generate.sdf.sphereSdf.c` |
| Visible title | `my_SphereSDF` |
| Vuo class | `my.field.generate.sdf.sphereSdf` |
| TiXL category | `Operators/Lib/field/generate/sdf` |
| Primary TiXL output | `Result: ShaderGraphNode` |
| Fill color | `ShaderGraphNode -> ColorForShaderGraph -> #D142B3` |
| Vuo adapter output | `Result: VuoText` carrying a serialized ShaderGraphNode contract |
| Data inputs | `Center`, `Radius` |

| Item | Contract |
| --- | --- |
| Vuo node | `vuo-nodes/my.field.render.raymarchField.c` |
| Visible title | `my_RaymarchField` |
| Vuo class | `my.field.render.raymarchField` |
| TiXL category | `Operators/Lib/field/render` |
| Primary TiXL output | `DrawCommand: Command` |
| Fill color | `Command -> ColorForCommands -> #22B8C2` |
| Vuo adapter input | `SdfField: VuoText` carrying a serialized ShaderGraphNode contract |
| Vuo adapter output | `DrawCommand: VuoImage` as the visible body proof |
| Event input | `renderTick` triggers render cooking |
| Const-input rule | Width and height are sanitized into local variables before rendering |

The Vuo composition wires `Fire on Display Refresh -> my_SphereSDF:update`, `Fire on Display Refresh -> my_RaymarchField:renderTick`, and separately wires the render window refresh event. `update` is the Vuo event that recomputes the field contract; `Center` and `Radius` remain semantic data values.
