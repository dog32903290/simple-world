# Native Shader IR Codegen Registry Proof

NativeShaderIrCodegenRegistryProof answers:

Can shader IR/codegen move from per-shell hand adapters into a reusable node
codegen registry for the current bounded texture vocabulary?

Acceptance line:

```text
NodeSpec registry -> ShaderIR -> codegen cache -> generated MSL
```

## Required Claims

- registryDrivenCodegen: true
- perShellHandAdapter: false
- completeShaderLanguage: false
- hlslToMslTranslation: false

## Registry Boundary

The registry admits a node type only when it declares:

- creator-facing node type;
- ShaderIR operation;
- fragment template id;
- parameters with defaults and literal encoders;
- resource reads and writes;
- diagnostics for missing node specs.

unknown nodes emit diagnostics and block source generation.

## Boundaries

- This is not a complete shader language.
- This is not HLSL-to-MSL translation.
- This is not TiXL shader parity.
- This does not replace future node admission for every image/filter/material
  node.
