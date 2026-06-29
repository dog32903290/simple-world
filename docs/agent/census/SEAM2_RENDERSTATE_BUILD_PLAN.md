# SEAM2_RENDERSTATE_BUILD_PLAN — DX11 render-state → Metal PSO collapse

> 2026-06-30 Opus architect blueprint (read-only, vs main 7f12d7f + TiXL 395c4c55). Parity ref =
> `DX11_METAL_CONVERSION_TABLE.md` (14 closed-form / depth-bias emergent / dual-source+logic-op guards).
> **The blueprint RAN the states-census** (refuter prerequisite) — results below bound the whole build.
> 承重 seam 全工法：census(step0) → Plan(此) → Opus build → Opus refuter → orchestrator reverify → merge.

## 0. Settled architecture
4 TiXL ops = immediate-context state mutators w/ save/set/restore (Rasterizer.cs:15-40, InputAssemblerStage.cs:16-37,
OutputMergerStage.cs:17-101) terminated by one `deviceContext.Draw` (Draw.cs:49). State built by RasterizerState.cs +
BlendState.cs (per-RT RenderTargetBlendDescription[] + A2C + IndependentBlend). sw is retained-mode: `cookRenderTarget`
already bakes immutable MTL::RenderPipelineState + DepthStencilState per DrawKind (point_ops_rendertarget.cpp:62-111,164-179,
encoder 206-209). **Collapse = accumulate TiXL mutations into a descriptor, materialize one cached PSO at the Draw leaf.**
render_command.h:9-17 already names the move ("Prepare/Restore are EXECUTOR-owned, not carried by the op"); Seam 2 appends
a frozen render-state tuple to RenderDrawItem exactly like camera-stamp/group-SRT fields (render_command.h:81,138-208).

## 1. ★STATES-CENSUS (RAN @395c4c55 — re-run as build step 0, commit to SEAM2_STATE_USAGE.md)
Method: `grep -rl "<guid>" external/tixl --include='*.t3' | wc -l` for consumers; `grep -rih -A4 "<input-guid>" | grep '"Value"' | sort|uniq -c` for wired values.
Consumers: RasterizerState 74 / BlendState 25 / RenderTargetBlendDescription 25.

| State | wired values | note |
|---|---|---|
| CullMode | None×61, Back×12 | only 2 of 3 (no Front) |
| FillMode | Solid (1 explicit, else default) | **Wireframe NEVER wired** → fork dormant |
| DepthBias | 0×19, **-6×1** | one consumer trips EMERGENT fork |
| SlopeScaledDepthBias | 0.0×9 | always 0 → slope inert |
| AlphaToCoverage | false×20 | **never enabled** → dormant |
| IndependentBlend | false×21 | **always single-RT** → no per-RT divergence |
| BlendEnabled | true×22, false×11 | both |
| SourceBlend | SourceAlpha×11, One×3, Zero×1, InvDestColor×1 | 4 factors |
| DestinationBlend | InvSrcAlpha×10, One×2, InvSrcColor×2, Zero×1, SrcColor×1, SrcAlpha×1 | 6 factors |
| BlendOperation | Add×3, **ReverseSubtract×2, Minimum×1** | 3 of 5 ops |
| Src/Dst/AlphaOp | One/SrcAlpha/InvSrcAlpha · InvSrcAlpha/Zero/One/DstAlpha · Add | small |
| ColorWriteMask | **hardcoded All** (RenderTargetBlendDescription.cs:24, input commented :43-44) | no port |
| LogicOp / Src1 dual-source | **grep src1/source1/logicop → ZERO** | Bucket-C guards = provable no-ops |

**Census verdict**: unmappable Bucket-C (dual-source/logic-op) NEVER appear → guards never fire. Blend surface = ~7 BlendOptions + 3 ops (not 19×5). Wireframe/A2C dormant (ship field+guard, don't build path). **Only DepthBias=-6 (1 consumer) is the non-closed-form residual** → single deferred TiXL-reference golden. (Snapshot of 395c4c55; re-run grep step 0 to confirm before trusting the bound.)

## 2. PSO-descriptor-accumulator (core)
**FrozenRenderState** appended to render_command.h (same posture as camera/group fields) = full frozen-state tuple keying the PSO cache (refuter amendment a: key on tuple NOT DrawKind):
- FrozenRasterState{fillMode,cullMode(default 2=Back),frontCCW(default false=CW),depthBias,slopeScaledDepthBias,depthBiasClamp}
- FrozenBlendRT{enabled,srcRGB,dstRGB,opRGB,srcA,dstA,opA} (no colorWriteMask — TiXL hardcodes All)
- FrozenBlendState{alphaToCoverage,independentBlend,rtCount,rt[8],logicOpEnabled(guard),dualSourceUsed(guard)}
- FrozenDepthState{compare(3=Less default,LessEqual for mesh),writeEnabled}
- dynamic (encoder, NOT in key): blendColor[4](OM BlendFactor), stencilRef, viewport[6]; + a stable hash over {raster.fill,cull,frontCCW,blend.*,depth.*,colorPixelFormat} = PSO cache key.
Add ONE field to RenderDrawItem (render_command.h:81): `FrozenRenderState frozen;` defaulted to reproduce today's hardcoded behavior byte-for-byte (every existing item unchanged — append-after-viewExtent rule :78-80).
**Per-op mutation**: accumulator = a CmdCookCtx field (`FrozenRenderState* renderStateAccum`, point_graph_cook_ctx.h:130-168, borrowed single-frame). Render-state op cooks by mutating `*cc.renderStateAccum` then forwarding subtree items; driver seeds/restores around subtree (save/set/cook-child/restore — TiXL Restore Rasterizer.cs:34/OM:83 = driver pop, like SetRequestedResolution/SetFloatVarCmd node_registry_draw.cpp:249-280). Stamped onto every RenderDrawItem the subtree produces (= Camera-stamp :138-160 / Group-SRT :195-208 mechanism).
**Draw materialization**: Draw leaf reads stamped it.frozen → `materializeFrozenPSO(dev,lib,vs,fs,pf,frozen)` (extends makeDrawPSO :62-111; blend from frozen.blend.rt[0] not BlendMode switch; A2C via rpd->setAlphaToCoverageEnabled). descriptor→PSO cache `unordered_map<uint64_t,MTL::RenderPipelineState*>` replaces per-DrawKind lazy build (:125-138,220-221).
**Dynamic-vs-frozen split**: PSO(frozen)=fill,blend,A2C,colorPF,depthPF. DepthStencilState(precompiled,encoder)=depth compare+write. Encoder dynamic(NOT in key)=setCullMode/setFrontFacingWinding(:495-503), setViewport, setDepthBias(bias,slope,clamp), setBlendColor, setStencilReferenceValue.

## 3. Per-op port plan
| New leaf | clones | sets | mapping |
|---|---|---|---|
| point_ops_rasterizer.cpp | Rasterizer.cs:15-40+RasterizerState.cs:20-39 | raster.fill/cull/frontCCW/depthBias/slope, viewport | Fill/cull/winding(encoder), defaults |
| point_ops_inputassembler.cpp | InputAssemblerStage.cs:16-37 | PrimitiveTopology→MTL::PrimitiveType | **FORK**: InputLayout/VertexBuffers/IndexBuffer dropped (sw VS=SV_VertexID-driven render_command.h:54-58, buffers bound by Draw leaf). IA=topology only |
| point_ops_outputmerger.cpp | OutputMergerStage.cs:17-101+BlendState.cs+RenderTargetBlendDescription.cs | blend.*, depth.*, blendColor, stencilRef | blend factor/op, descriptor shape, depth; **FORK**: RTV/UAV/DSV resource binds dropped (RenderTarget executor owns attachments :182-204) |
| point_ops_draw_explicit.cpp | Draw.cs:23-50 | RenderDrawItem{kind from topology, frozen=accum, count=VertexCount} | Draw leaf = materialize point; VertexStart→drawPrimitives baseVertex |
Enum→enum tables (Cull/Fill/7 BlendOptions/3 ops) → **dx11_metal_state_map.h** (data-driven, CONTEXT_PACK rule7); the closed-form goldens assert against it. Register via registerRenderStateOps() in point_ops_register_draw.cpp + NodeSpecs node_registry_draw.cpp.

## 4. Named forks (per conversion table — all dormant/never-fire per census)
1. fork-S2-dual-source (Bucket C): dualSourceUsed&&rtCount>1 → port-time guard error. Census never triggers.
2. fork-S2-logic-op (Bucket C): LogicOpEnable → guard (MoltenVK-private). TiXL exposes no LogicOp input → never set.
3. fork-S2-wireframe (dormant): Wireframe→setTriangleFillMode(.lines) closed-form, but never wired. Ship field+1-line call.
4. **fork-S2-depth-bias (Bucket B EMERGENT)**: bias/slope/clamp pass-through to setDepthBias (param 1:1) but numeric output not formula-portable (DX11 2^(exp−mantissa) scale). 1 consumer wires -6 → deferred fork, single Bucket-B pixel-reference golden, don't block common path.
5. fork-S2-independent-blend (dormant): always false → ship rt array, only rt[0] non-default.

## 5. Harness (closed-form goldens, on-machine, no TiXL render — 14/15)
- --selftest-rasterizerstate: Back→CullModeBack, None→CullModeNone, Solid→.fill, frontCCW false→WindingClockwise. bug: swap cull.
- --selftest-blendstate: 7 census BlendOptions+3 ops → dx11_metal_state_map.h lookup == table MTL enum; A2C false→setAlphaToCoverageEnabled(false). bug: corrupt a row.
- --selftest-pso-cache: identical frozen tuple→same cached PSO ptr; one differing field→distinct PSO (proves full-tuple key). bug: key on DrawKind→false hit.
- --selftest-om-dynamic-split: blendColor/stencilRef/viewport→encoder not key. bug: blendColor in key→cache explodes.
- --selftest-s2-guards: dual-source+MRT & logic-op→guard fires; valid state→must NOT fire.
- **--selftest-s2-depthbias (Bucket B DEFERRED)**: 1 pixel golden vs stored TiXL reference for depthBias=-6; skipped until reference exists.
Register in selftests_point.cpp:13-44. dx11_metal_state_map.h must cover every census value (static_assert/selftest).

## 6. Owner-lock / file map
**New leaves (parallel, no contention)**: point_ops_{rasterizer,inputassembler,outputmerger,draw_explicit}.cpp, dx11_metal_state_map.h, SEAM2_STATE_USAGE.md.
**Cook-core/shared (OWNER-LOCKED, serial — contention points)**:
- render_command.h — append FrozenRenderState + RenderDrawItem.frozen. **~249 includers**, append-only tail keeps ABI. **Highest shared-file risk; serial vs any other seam touching it (e.g. vec4-currency).**
- point_graph_cook_ctx.h — CmdCookCtx::renderStateAccum.
- point_ops_rendertarget.cpp — makeDrawPSO→materializeFrozenPSO + tuple cache + encoder dynamic state. **Owner-locked render-executor; serial vs any cook-core touching makeDrawPSO (vec4-currency/blend work).**
- point_graph_command_cook.cpp + point_graph_resident_command_cook.cpp — driver save/set/restore of renderStateAccum. **EDIT BOTH LEGS IDENTICALLY** (S2c/S3a blood lesson render_command.h:163-164,229-235: resident-only miss = silent production divergence). Serial, paired.
- node_registry_draw.cpp + point_ops_register_draw.cpp + selftests_point.cpp — registration (low-contention appends).

## 7. Spike + split + highest risk
**SPIKE = Rasterizer (after census step 0)**: smallest closed-form surface (cull/fill/winding all Bucket A, encoder/PSO-split already proven by mesh path :495-503); forces FrozenRenderState-append + accumulator-stamp + driver save/restore into existence; golden pure formula. Landing it end-to-end de-risks the accumulator/cache architecture before blend table / OM forks.
**Serial spine (one owner, in order)**: render_command.h append → point_graph_cook_ctx.h → dual command-cook legs → point_ops_rendertarget.cpp materialize+cache. The keystone every op rides.
**Parallel (after spine)**: 4 op leaves + goldens + dx11_metal_state_map.h independent.
**★HIGHEST RISK = flat-vs-resident command-cook divergence on accumulator save/restore** (render_command.h:163-164,229-235: resident-only miss = silent non-crashing wrong-render flat selftests won't catch). **De-risk**: renderStateAccum push/pop = a SINGLE shared helper both legs call (mirror loopRunIterations/switchSelectIndex render_command.h:264-275) + a selftest driving the SAME render-state subtree through BOTH legs asserting byte-identical frozen tuples.
