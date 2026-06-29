# DX11 → Metal Conversion Table — SEAM 2 VERIFICATION AUTHORITY

**Scope:** DX11 render-state → Metal PSO (pipeline / fixed-function state) for the
Tooll3/TiXL → metal-cpp port. This is the authority for how each Seam 2 pipeline-state
difference gets *verified* in simple_world.

**Source:** deep-research report `wwbzxhsrd` (106 agents, 23 primary sources, 25 claims
adversarially voted, 18 confirmed / 7 killed). Findings rest on primary specs — Microsoft
Learn / DirectX-Specs functional spec, Apple Metal docs, Khronos Vulkan spec — plus MoltenVK
main-branch source read directly.

**Core finding:** the large majority of Seam 2 state seams are DETERMINISTIC closed-form
transforms verifiable by formula *on-machine, with no rendered TiXL image*. Only a small,
bounded set is EMERGENT (needs a real reference frame) or NO-METAL-EQUIVALENT (must be
guarded, not translated).

---

## 1. Per-difference conversion table

Classification key: **CLOSED-FORM** = computable transform, assert by formula, no picture.
**EMERGENT** = numeric output is spec-permitted to vary on both sides → needs rendered-output
comparison. **NO-METAL-EQUIVALENT** = no standard public-Metal path → detect + guard at port time.

| Difference | Exact conversion (formula / state) | Classification | Confidence + vote | Source URL(s) |
|---|---|---|---|---|
| **NDC depth range (Z)** | DX11 clip Z∈(0,Wp] → after divide [0,1]; Metal NDC Z=[0,1]. **Identity, no remap.** | CLOSED-FORM | high · 3-0 | [MS Transform Pipeline](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/the-direct3d-transformation-pipeline) · [Apple MTLRenderCommandEncoder](https://developer.apple.com/documentation/metal/mtlrendercommandencoder) |
| **NDC XY clip bounds** | DX11 clip XY∈[-Wp,Wp] → [-1,1]; Metal NDC XY=[-1,1]. **Identity, no XY transform.** (half-open vs closed interval is a tie-break detail, not a range diff) | CLOSED-FORM | high · 3-0 | same as above |
| **Pixel coordinate origin** | DX11 origin = upper-left of RenderTarget; pixel centers offset **(0.5,0.5)** from integer locations. Matches Metal top-left. SV_Position of first pixel = (0.5,0.5). | CLOSED-FORM | high · 3-0 | [D3D11.3 Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm) |
| **Texel origin** | DX11 texel origin = top-left, "consistent with the Pixel Coordinate System." Metal also top-left. **Convention match.** | CLOSED-FORM | high · 3-0 | [D3D11.3 Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm) |
| **Top-left fill rule** (edge tie-break) | Normative rule: "sample on a top or left edge is inside the triangle." Exactly specified, *not* hardware-defined → reproducible. (Caveat: MSAA sample locations + AA-line raster ARE hardware-defined; this row covers only the standard top-left tie-break.) | CLOSED-FORM | high · 3-0 | [D3D11.3 Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm) |
| **Y-axis / vertical flip** | A literal **sign negation**: `gl_Position.y = -gl_Position.y`, equivalently negative-viewport-height or projection-row negation. Three interchangeable closed-form routes. **NOTE: the flip arises from framebuffer/texture-origin handling, NOT a clip-space Y-range difference** (see §below — the "DX viewport scale stage flips Y" claim was refuted 0-3; DX and Metal share top-left NDC-Y and top-left texture origin). | CLOSED-FORM | high · 3-0 | [MoltenVK Whats_New](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md) · [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/README.md) |
| **Blend descriptor shape** | Metal per-color-attachment (`MTLRenderPipelineDescriptor.colorAttachments[n]`) mirrors DX11 `D3D11_BLEND_DESC.RenderTarget[8]` + `IndependentBlendEnable`. Field-for-field per-attachment. (Closed-form but NOT total — logic-op + dual-source+MRT are exceptions.) | CLOSED-FORM | high · 2-1 | [MTLRenderPipelineDescriptor](https://developer.apple.com/documentation/metal/mtlrenderpipelinedescriptor) · [D3D11_BLEND_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_blend_desc) |
| **Blend factor + op mapping** | Flat enum→enum lookup applied independently to RGB and alpha. Op: ADD/SUBTRACT/REVERSE_SUBTRACT/MIN/MAX → Add/Subtract/ReverseSubtract/Min/Max (5 cases). Factor: 19 cases, each defined as an exact per-component equation (SRC_ALPHA=(As,As,As); ONE_MINUS_DST_COLOR=(1-Rd,1-Gd,1-Bd); CONSTANT_COLOR=(Rc,Gc,Bc)). DX 'INV'(1-x)==Metal 'OneMinus'. **Assert the table.** | CLOSED-FORM | high · 3-0 | [MVKPipeline.mm](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm) · [VkBlendFactor](https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendFactor.html) · [D3D11_BLEND](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_blend) |
| **Dual-source (Src1) blend factors** | All four map 1:1: `SRC1_COLOR/INV_SRC1_COLOR/SRC1_ALPHA/INV_SRC1_ALPHA` → `MTLBlendFactorSource1Color / OneMinusSource1Color / Source1Alpha / OneMinusSource1Alpha`. Reads the fragment fn's 2nd color output (o1/SV_TARGET1). Present since Metal 2 / macOS 10.12. **Mappable closed-form.** | CLOSED-FORM | high · 3-0 | [MTLBlendFactor](https://developer.apple.com/documentation/metal/mtlblendfactor) · [D3D11_BLEND](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_blend) |
| **Default blend state** | `CD3D11_BLEND_DESC(DEFAULT)`: AlphaToCoverage=FALSE, IndependentBlend=FALSE; per-RT {FALSE, ONE, ZERO, ADD, ONE, ZERO, ADD, WRITE_ALL} → MTLBlendFactorOne/Zero, MTLBlendOperationAdd, MTLColorWriteMaskAll. Metal's own descriptor defaults coincide. Set `isAlphaToCoverageEnabled=false` explicitly (lives on the pipeline descriptor, not the attachment). | CLOSED-FORM | high · 3-0 | [CD3D11_BLEND_DESC(DEFAULT)](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-cd3d11_blend_desc-cd3d11_blend_desc(cd3d11_default)) |
| **Default rasterizer state** | DX11 default: FillMode=SOLID, CullMode=BACK, FrontCounterClockwise=FALSE. Metal defaults DIFFER (no culling, CCW front-face) → must set explicitly: setCullMode(.back), setFrontFacingWinding to match CW-front. | CLOSED-FORM | high · (fetch) | [D3D11_RASTERIZER_DESC default](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-cd3d11_rasterizer_desc-cd3d11_rasterizer_desc(constd3d11_rasterizer_desc_)) |
| **Default depth-stencil state** | DX11 default: DepthEnable=TRUE, DepthWriteMask=ALL, DepthFunc=LESS → MTLDepthStencilDescriptor depthWriteEnabled=YES, depthCompareFunction=Less. | CLOSED-FORM | high · (fetch) | [CD3D11_DEPTH_STENCIL_DESC default](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-cd3d11_depth_stencil_desc-cd3d11_depth_stencil_desc(cd3d11_default)) |
| **Fill / wireframe mode** | 1:1 enum lookup: D3D11_FILL_SOLID→FILL, D3D11_FILL_WIREFRAME→LINE (Metal `setTriangleFillMode(.lines)`). No parameters. | CLOSED-FORM | high · (dxvk src) | [dxvk d3d11_rasterizer.cpp](https://github.com/doitsujin/dxvk/blob/master/src/d3d11/d3d11_rasterizer.cpp) |
| **Cull / winding routing** | Cull/front-face/viewport are **live encoder dynamic state** (setCullMode, setFrontFacingWinding, setViewport), NOT pipeline-descriptor properties. Depth/stencil compare → precompiled MTLDepthStencilState (setDepthStencilState). Structural routing fact a port must respect. | CLOSED-FORM | high · 3-0 | [MTLRenderCommandEncoder](https://developer.apple.com/documentation/metal/mtlrendercommandencoder) |
| **Depth-bias (numeric output)** | DX11 float-depth: `Bias = DepthBias*2^(exponent(max z in primitive) − r) + SlopeScaledDepthBias*MaxDepthSlope`, r=mantissa bits (23 for f32). Per-primitive, exponent-dependent. Metal `setDepthBias(_:slopeScale:clamp:)` is a **plain NDC-depth constant — does NOT reproduce the exponent scaling.** Constant r and slope-max m are spec-permitted to differ on BOTH sides. Parameter-passing maps, numeric output does not. | **EMERGENT** | high · 3-0 (the naive "3 params map 1:1" claim was refuted 0-3) | [MS depth-bias](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage-depth-bias) · [Apple setDepthBias](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/setdepthbias(_:slopescale:clamp:)) · [vkCmdSetDepthBias](https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdSetDepthBias.html) |
| **Logic-op blending** | No standard public-Metal property. MoltenVK enables it ONLY behind `#if MVK_USE_METAL_PRIVATE_API` (private *MVK selectors). VkPhysicalDeviceFeatures::logicOp=false by default. Documented workaround = bitwise ops in fragment shader. **Detect DX11 LogicOpEnable/LogicOp → guard, don't translate.** | **NO-METAL-EQUIVALENT** | high · 3-0 | [MVKPipeline.mm](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm) · [Color-attachment descriptor](https://developer.apple.com/documentation/metal/mtlrenderpipelinecolorattachmentdescriptor) |
| **Dual-source + >1 render target** | Metal "doesn't support multiple render targets when using dual-source blending" (mutually exclusive; index-1 occupies RT1's slot, other RTs undefined). DX11 itself restricts dual-source to RT0 → such a pipeline is largely invalid on both sides → **validation check / guard.** | **NO-METAL-EQUIVALENT** | high · 3-0 | [gpuweb #4283](https://github.com/gpuweb/gpuweb/issues/4283) · [gfx-rs/gfx #3203](https://github.com/gfx-rs/gfx/issues/3203) |
| **Point-fill polygon mode + exact sub-pixel snap** | DX11 n.8 (1/256) fixed-point snap is specified, but Metal's sub-pixel snap is not guaranteed to match → bit-exact edge coverage is emergent. Point-fill (VK_POLYGON_MODE_POINT) has no clean Metal equivalent. **Guard / treat as emergent, do not translate.** (Noisiest item — see §below; related pool claims refuted 0-3.) | EMERGENT / guard | **medium · split** | [MoltenVK Whats_New](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md) · [D3D11.3 Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm) |

---

## 2. Verification-method mapping (the actionable part)

Every difference falls in exactly one of three buckets. This is HOW each gets verified in sw.

### Bucket A — CLOSED-FORM → formula golden (on-machine, NO TiXL picture)

These seams get a `--selftest-*` that **computes the expected transformed state/value and
asserts**. No Windows, no rendered reference. (14 differences)

1. Blend factor + op mapping — assert the full enum→enum table, RGB and alpha independently.
2. Dual-source blend — all 4 Src1 factors (Src1Color / OneMinusSrc1Color / Src1Alpha / OneMinusSrc1Alpha).
3. Default blend constants — {ONE, ZERO, ADD, ONE, ZERO, ADD, WRITE_ALL}, blending-off.
4. Default rasterizer constants — SOLID / BACK / CW-front.
5. Default depth-stencil constants — DepthEnable / WriteAll / LESS.
6. Depth-range identity — DX11 [0,1] == Metal [0,1], no remap.
7. XY clip identity — DX11 [-1,1] == Metal [-1,1], no transform.
8. Pixel origin top-left + (0.5,0.5) center.
9. Texel origin top-left.
10. Top-left fill rule (edge tie-break).
11. Y-flip sign-negation (`-gl_Position.y` ≡ negative viewport height ≡ projection-row negate).
12. Fill/wireframe enum (SOLID→FILL, WIREFRAME→LINE).
13. Blend descriptor shape (per-attachment field-for-field).
14. State-routing fact (depth/stencil → MTLDepthStencilState; cull/winding/viewport → encoder).

### Bucket B — EMERGENT → reserve per-pixel image comparison (needs a TiXL reference frame)

**This is essentially just depth-bias.** Per the research, depth-bias is the ONLY residual
that genuinely needs a Windows-TiXL picture:

- DX11 float-depth bias scales the constant term by `2^(exponent(max z in primitive) − mantissa_bits)`
  — per-primitive, exponent-dependent. Metal's plain `depthBias` constant does not replicate it.
- Constant `r` and slope-max `m` are **spec-permitted to differ** on both DX11 and Metal/Vulkan
  (VK_EXT_depth_bias_control exists precisely because r is not spec-fixed). A published Apple
  formula would still not make it byte-portable.
- **Rarely used:** raw `Draw` + depth-bias is rare per the Seam 2 refuter — depth-bias is a
  shadow/decal-offset feature, not a common-path one. So this image-comparison reservation is
  **deferrable as a named fork until/unless a draw graph actually wires depth-bias.**

(The point-fill / exact-sub-pixel-snap item rides in the same bucket as guard-rather-than-translate,
at medium confidence — see §3 nuance. It is not a translated seam either way.)

### Bucket C — NO-METAL-EQUIVALENT → detect + named-fork guard (port-time validation error, NOT translated)

These have no standard public-Metal path. They become **explicit fork-guards that error at
port time** when a DX11 pipeline requests them. (2 capabilities)

1. **Logic-op blending** — only reachable via MoltenVK's private-API path
   (`MVK_USE_METAL_PRIVATE_API`); no public Metal property. Guard on DX11 `LogicOpEnable`.
   (Optional emulation path = bitwise ops in the fragment shader, if ever needed.)
2. **Dual-source blend combined with >1 render target** — mutually exclusive on Metal.
   Guard on (any Src1 factor) ∧ (RT count > 1). Note DX11 itself forbids this, so it is
   effectively a validation assertion that should never fire on valid input.

---

## 3. Net implication for Seam 2

The original Seam 2 plan claimed it needed **per-pixel golden vs TiXL for everything** — a
hollow gate, because there is no on-machine TiXL (it only runs on Windows). This research
collapses that gate:

- **Nearly all of Seam 2 is formula-verifiable on-machine** (Bucket A, 14 differences) — assert
  the transformed state/value directly, no picture.
- **The Windows-TiXL reference is needed ONLY for the depth-bias residual** (Bucket B) — and that
  is deferrable as a *named fork* until/unless a draw graph actually wires depth-bias. Depth-bias
  is rare on the common path.
- **Two capabilities become guarded forks** (Bucket C) — logic-op and dual-source+MRT error at
  port time rather than silently mis-rendering.

**Conclusion: Seam 2 is no longer Windows-blocked for the common path.** The common path is
closed-form-verifiable today; the Windows-TiXL dependency shrinks to a single deferrable
depth-bias residual plus two guard checks that should never fire on valid TiXL input.

---

## Nuance log (low-confidence / contradictory findings captured correctly)

- **Y-flip mechanism (refuted sub-claim, 0-3).** The claim "the DX viewport scale stage explicitly
  flips Y, and that is the seam where DX vs Metal Y-origin differences arise" was **refuted 0-3.**
  The flip is **real**, but it does NOT arise from a clip-space Y-range difference — DX11 and Metal
  **share** top-left NDC-Y and top-left texture origin. The practical flip arises from
  **framebuffer / texture-origin handling**, and is applied as a closed-form sign negation. Do not
  encode "DX viewport flips Y in clip space" anywhere — it is false. The flip stays in Bucket A
  (closed-form) but for the right reason.

- **Depth-bias param-passing vs numeric output.** The "3 params map 1:1 → depth-bias is
  deterministic" claim was **refuted 0-3.** Correct nuance: the *parameter-passing* seam maps 1:1,
  but the *numeric output* is not formula-portable. Hence Bucket B, not A.

- **Point-fill / sub-pixel snap (medium · split).** Both "Metal lacks VK_POLYGON_MODE_POINT" and
  "DX11 n.8 snap is a portable closed-form computation" were **refuted 0-3** in the pool — but the
  refutations were against asserting them as *confirmed mappings*, i.e. uncertainty, not a clean
  equivalent. Net: wireframe/fill/cull/winding map cleanly (Bucket A), but point-fill and
  bit-exact sub-pixel edge coverage do **not** portably translate → guard, medium confidence.
  This is the noisiest region of the report.

- **Blend-table-shape (2-1) and default-blend-state (2-1).** Dissent concerned *completeness /
  wording* (the table is closed-form but NOT total — logic-op and dual-source+MRT are the holes),
  not the core fact. Confidence on the core mapping stays high.

### Open questions the research left for sw to answer (not blockers)

- Does TiXL's node set actually USE any genuinely-unmappable feature (logic-op, dual-source+MRT,
  point-fill)? If none appear, Bucket C guards are pure no-ops and the *entire* pipeline-state
  surface is closed-form-verifiable.
- For depth-bias: what tolerance band is acceptable for the port's shadow/decal use cases, and can
  a calibrated per-format constant approximate DX11's exponent-scaling well enough to skip per-pixel
  comparison for the depth formats actually used?
- Stencil-op and depth-compare-function enum mapping: verify as closed-form too (analogous to blend
  factors)? The research located the seam but did not enumerate enum-level completeness.
