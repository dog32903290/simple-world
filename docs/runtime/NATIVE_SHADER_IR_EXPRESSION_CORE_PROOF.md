# Native Shader IR Expression Core Proof

NativeShaderIrExpressionCoreProof answers:

Can ShaderIR carry reusable expression trees for uniforms, math, and color
composition instead of only node-type fragment templates?

Acceptance line:

```text
NodeSpec expression fields -> ShaderExpressionIR -> generated MSL -> Metal compile artifact
```

## Required Claims

- coreExpressionLanguage: true
- metalCompiled: true
- completeShaderLanguage: false
- unsafeExpressionBlocked: true

## Expression Boundary

The core expression language admits constants, uniforms, UV reads, swizzles,
basic arithmetic, `sin`, `smoothstep`, `mix`, and vector construction. It emits
diagnostics for unknown or unsafe operations before source generation.

This is not a complete shader language, not HLSL-to-MSL translation, not loops,
not user-defined functions, and not TiXL shader parity. It is the product
runtime expression core needed by NodeSpec-driven shader params and inspectors.
