# TiXL to Vuo Porting Research

This folder is the working knowledge base for porting TiXL node vocabulary into Vuo / My World.

Source evidence:

- TiXL clone spec pack: `external/tixl-spec/TIXL_CLONE_SPEC_20260604`
- TiXL source checkout: `external/tixl`
- Vuo source and SDK examples: `external/vuo`
- Vuo installed user modules: `~/Library/Application Support/Vuo/Modules`

## Node Card Schema

Each researched node should use this shape.

```text
## <NodeName>

- TiXL full path:
- Namespace:
- Clone status:
- Source evidence:
  - C#:
  - .t3 defaults:
  - docs:
  - related shader / helper source:
- Purpose:
- Conversion:
- Inputs:
  - <name>: type, default, enum/range, semantic role
- Outputs:
  - <name>: type, semantic role
- Runtime behavior:
  - formula / state / event / GPU behavior
  - important edge cases
- Observed graph usage:
  - common incoming nodes
  - common outgoing nodes
- Vuo mapping:
  - Vuo input types:
  - Vuo output types:
  - direct built-in Vuo equivalent, if any:
  - missing Vuo support:
- Porting grade:
  - A: pure value/control, immediate Vuo C node candidate
  - B: value/list/color/vector, doable with type care
  - C: image/shader/mesh/scene, needs renderer or Vuo-specific design
  - D: DX11/device/app-specific/proprietary dependency, document only for now
- First implementation recommendation:
- Verification fixture:
- Risks / unknowns:
```

## Research Rules

- Do not invent ports, defaults, enum values, or behavior.
- Prefer C# slot declarations and `.t3` default values over prose docs when they disagree.
- Use docs for semantic descriptions and user-facing purpose.
- Use `.t3` graph adjacency only as usage evidence, not as type-law proof.
- If behavior is in a helper, shader, or runtime class, name the source path and summarize the dependency.
- If source behavior cannot be verified, mark it as an unknown instead of filling the gap.

## Porting Grades

`A` means we can batch-build after a focused test contract.

`B` means likely portable, but the Vuo type shape must be decided first.

`C` means important for My World, but requires renderer, shader, image, mesh, or scene design work.

`D` means do not build in Vuo first pass. Keep it in the reference map.
