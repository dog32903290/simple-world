# TiXL to Vuo Porting Index

Generated during the 2026-06-04 research pass.

This is the commander-level map. Detailed node cards live in `namespaces/`, and source/grade rules live in `reports/`.

## Source Corpus

- TiXL spec pack: `external/tixl-spec/TIXL_CLONE_SPEC_20260604`
- TiXL source: `external/tixl`
- Vuo source / examples: `external/vuo`
- Local Vuo custom nodes already proven:
  - `vuo-nodes/myworld.tixl.remap.c`
  - `vuo-nodes/myworld.tixl.lerp.c`

## Coverage

| area | nodes covered | full cards | A | B | C | D | document |
|---|---:|---:|---:|---:|---:|---:|---|
| numbers | 236 | high-value set | 71 | 132 | 10 | 23 | `namespaces/numbers.md` |
| image | 120 | 64 | 0 | 5 | 95 | 20 | `namespaces/image.md` |
| render / mesh / point | 297 | 36 | 0 | 6 | 234 | 57 | `namespaces/render_mesh_point.md` |
| field / particle | 79 | 22 | 0 | 0 | 48 | 31 | `namespaces/field_particle.md` |
| io / flow / string / data | 147 | 43 | 26 | 15 | 21 | 85 | `namespaces/io_flow_string_data.md` |
| blank namespace / global | 56 | inventory only | TBD | TBD | TBD | TBD | `reports/source_inventory.md` |

Global inventory:

- 935 TiXL nodes
- C# path coverage: 915 / 935
- `.t3` coverage: 915 / 935
- docs coverage: 756 / 935
- confidence split:
  - 579 `doc_and_csharp_verified`
  - 179 `csharp_verified_no_doc_record`
  - 177 `doc_verified_no_csharp_match`

## First Build Pool

Start with `A` grade value/control/string/data nodes.

Best immediate candidates:

1. `Lib.numbers.float.adjust.Clamp`
2. `Lib.numbers.float.process.SmoothStep`
3. `Lib.numbers.float.process.Ease`
4. `Lib.numbers.float.process.Damp`
5. `Lib.numbers.float.process.Spring`
6. `Lib.numbers.float.process.Accumulator`
7. `Lib.numbers.float.trigonometry.Sin`
8. `Lib.numbers.float.trigonometry.Cos`
9. `Lib.string.convert.FloatToString`
10. `Lib.string.list.SplitString`

Already built:

- `Lib.numbers.float.adjust.Remap` -> `TiXL Remap`
- `Lib.numbers.float.process.Lerp` -> `TiXL Lerp`

## Main Warnings

- TiXL `SmoothStep` is actually smootherstep behavior. Do not map it blindly to a simpler smoothstep without checking source.
- TiXL `Sin` / `Cos` use radians. Vuo built-in trig uses degrees, so direct use would drift.
- `Vector4` must be judged per node: it may be color-like or point4-like.
- `Gradient`, `Curve`, `Int2`, `Command`, `Texture2D`, `Buffer`, `BufferWithViews`, `ShaderGraphNode`, and DX11 resource types do not have direct one-to-one Vuo mappings.
- `.t3` adjacency is usage evidence, not type-law proof.
- `doc_verified_no_csharp_match` nodes need source re-check before implementation.

## Strategic Split

### Vuo-First

Good for:

- numbers
- strings
- simple data nodes
- light color/vector helpers
- selected image operations where Vuo has a close built-in or shader path

This is the fast path for growing visible TiXL vocabulary inside a working node canvas.

### My World Runtime Needed

Required for:

- render / mesh / point graph semantics
- `Command` execution flow
- `EvaluationContext`
- `BufferWithViews`
- `ShaderGraphNode`
- SDF/raymarch graph codegen
- GPU particle simulation
- DX11 / Direct3D resource staging

These should remain documented until a My World-native scene/mesh/shader/point-buffer contract exists.

## Verification Gate For Each Built Node

Each node implementation needs:

1. Source evidence: C# + `.t3` defaults + docs, or a written Unknown.
2. A semantic fixture with at least one edge case.
3. A Vuo source contract test if implemented as a Vuo C node.
4. Vuo compiler/load check from log.
5. A tiny composition proof when the node affects visible image/scene/audio.

## Next Batch Recommendation

Batch 1 should be 8-12 `A` grade nodes from `numbers.md`, using the exact workflow used for `Remap` and `Lerp`:

```text
TiXL source behavior -> semantic fixture -> Vuo C node -> install -> Vuo compiler log -> tiny composition proof
```

Do not mix shader/image/mesh nodes into Batch 1. They need a separate runtime contract.
