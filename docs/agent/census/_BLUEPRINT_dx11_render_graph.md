# Build Blueprint: dx11-equiv-render-graph (block #2) — DrawScreenQuad + ClearRenderTarget

Scout output (Cut 88, 2026-06-20). **HEADLINE: the seam is ~70% already built** in the point lane. World-view rule: rebuild TiXL's render-graph dispatch BEHAVIOR on Metal, NOT port the DX11 API. Biggest risk = OVER-building (re-porting RTV/DSV/BlendState/OM/IA sub-ops the retained-mode Metal executor already subsumes). Single batch.

## 1. Seam def (observable behavior, not DX11 API)
Compose a chain of GPU draw/clear/state Commands targeting a render-target texture, each Command carrying its own pixel/vertex program + param cbuffer + texture-SRV input + blend mode; an Execute runs the chain into one texture in order. Missing pieces (ranked by proving-op need):
1. Textured fullscreen-quad draw Command (DrawScreenQuad): source = Texture2D + Params cbuffer (not point buffer), drawn as 6-vertex clip-space quad sampling input, tinted by Color, positioned by Position/Width/Height. TiXL output to reproduce: `vs-draw-viewport-quad.hlsl psMain` = `clamp(Color * InputTexture.SampleLevel(uv,0), 0, 1000)` (HDR-permissive clamp 0..1000 is the TiXL constant, bake verbatim). VS = 6 hardcoded clip verts × (Width,Height) + Position; texCoord = quadVertex*(0.5,-0.5)+0.5. **ObjectToClipSpace mul is COMMENTED OUT → NO camera dep (clean leaf).**
2. Per-Command blend-mode selection in executor (today hardcodes points-opaque/lines-srcalpha). Need SharedEnums.BlendModes table. **THIS BATCH = Normal + Add only** (pure Metal blend equations); defer Screen (1-(1-s)(1-d)) / Multiply (dst*src) — need factor-table trace + maybe shader-side.
3. (DEFERRED) Transforms cbuffer / ObjectToClipSpace = camera3d seam = block #2-next, needed only by Layer2d (its quad VS does `mul(ObjectToClipSpace)`, draw-Quad-vs.hlsl:54).

NOT the seam (over-build trap): RTV/DSV/UAV/SRV view objects, OutputMerger/InputAssembler/Rasterizer/RasterizerState/DepthStencilState ops, Execute Prepare/Restore closure. render_command.h:9-17 documents why: TiXL Prepare/Restore inject into immediate-mode DX11 ctx; sw is retained-mode, pass boundaries owned by executor not per-op. The 19 dx11 sub-ops in DrawScreenQuad.t3 = TiXL's way of expressing ONE Metal render pass = one cookDrawScreenQuad + executor case. Do NOT port sub-ops.

## 2. Existing infra (reuse, file:line)
- RenderCommand stream: `render_command.h:46-62` RenderDrawItem{points,count,viewExtent,kind,color[4],lineWidth,size} + RenderCommand{vector<items>}.
- Executor (cmdBuffer→pass→encoder→clear→multi-draw): `point_ops_rendertarget.cpp:87-163` cookRenderTarget.
- Blend PSO builder: `point_ops_rendertarget.cpp:49-76` makeDrawPSO(...,blend).
- Resolution pin: `point_ops_rendertarget.cpp:169-187` resolveRenderResolution.
- Three-flow cook (Buffer/Command/Texture2D) + tables: `point_graph.h:82-171` (CmdCookCtx/TexCookCtx/registerCmdOp/registerTexOp); driver `point_graph.cpp:55-56,323-464`.
- Recursive Texture2D-input gather: `point_graph.cpp:342-404` cookTexNode threads inputTextures[]; TexCookCtx::inputTexture/inputTextures `point_graph.h:124-143`.
- Tex→Tex image-filter PSO + scratch cache: `tex_op_cache.h:39-84` (cachedTexPSO keys vs+fs+format).
- Terminal dispatch tex>cmd>preview: `point_graph.cpp:135-163,464` defaultDrawTarget + cookTexNode.
GAPs: per-item blend-mode enum; textured-quad Command op; (deferred) Transforms cbuffer.

## 3. Proving ops (backward-traced .t3→.hlsl, the BLOOD LESSON)
**(A) DrawScreenQuad — PRIMARY.** `.cs`: render/basic/DrawScreenQuad.cs:1-37 (NO Update, pure-compound). `.t3`: 19 sub-ops. Dispatches Lib:shaders/img/analyze/vs-draw-viewport-quad.hlsl vsMain+psMain. NO camera, depth defaults off. Needs textured-quad draw item + Texture2D source on Command path + blend table.
**(B) ClearRenderTarget — SECONDARY (trivial).** `.cs`: _dx11/api/ClearRenderTarget.cs:17-34 clears RTV to ClearColor + DSV depth 1.0. Maps to Metal LoadActionClear with color when it's the FIRST chain item (faithful + free); non-first would need clear-quad (defer). Proves Command-chain ordering (clear→draw) with a 2nd op type.
**(C) DrawScreenQuadAdvanced — DEFER** (DepthBuffer input → depth-stencil seam).

Golden discipline (deterministic shader, NO fwidth/temporal → clean): DrawScreenQuad = RenderTarget(known solid)→DrawScreenQuad(Color tint)→RenderTarget, readback, assert closed-form vs shader arithmetic clamp(Color*c,0,1000): uniform gray 0.5 × Color=(2,1,1,1) → center R≈clamp(1.0)=1.0, G/B≈0.5. injectBug: drop texture wire / Color=0 → black → FAIL. Blend golden: 2 stacked items Add → assert sum. ClearRenderTarget: Clear(red)→readback all-red; injectBug skip clear → black FAIL. Mirror runRenderTargetSelfTest/runDrawLinesSelfTest discipline (point_ops_rendertarget.cpp:194, point_ops_drawlines.cpp:47).

## 4. Scope ONE batch — SHIP:
1. Extend RenderDrawItem (render_command.h, APPEND-ONLY per file convention :45): `const MTL::Texture* srcTexture=nullptr;` + DrawKind::ScreenQuad + blendMode enum field.
2. Port vs-draw-viewport-quad.hlsl → app/shaders/draw_screenquad.metal (vsMain+psMain ~30 lines; PRECOMPILED into shaders.metallib, NOT runtime-compiled — static, metal_compile.h:9-16 philosophy).
3. DrawKind::ScreenQuad case in cookRenderTarget: bind srcTexture as fragment texture, Params cbuffer (Color/Position/Width/Height), draw 6 verts.
4. BlendModes factor table (Normal+Add this batch) + per-item blend selection in makeDrawPSO/executor.
5. Register DrawScreenQuad (registerCmdOp) + ClearRenderTarget (chain-clear) as proving consumers.
6. Goldens + injectBug RED each, run_all --bite green, check-arch.
DEFERRED (anti-over-build): Layer2d+23 sub-ops (camera3d cbuffer); other ~21 _dx11/api+fxsetup ops (most subsumed by retained-mode executor, NEVER port standalone; some already READY-LEAF e.g. GetTextureSize/CalcDispatchCount/ShowTexture2d/FirstValidBuffer ops-render.md:273-288 → Phase C); camera3d(~50); depth-buffer; pbr; postfx/gizmo; DrawScreenQuadAdvanced; Screen/Multiply blend.

## 5. Architecture forks (ACCEPTED scout recommendations; all internal/reversible, none for 柏為)
- FORK#1 texture→Command op: ADD Texture2D-input gather to Command path (CmdCookCtx.inputTexture), cookDrawScreenQuad borrows upstream tex ptr → draw item. Mirrors TiXL (DrawScreenQuad outputs Slot<Command>, takes InputSlot<Texture2D>); cook driver already cooks upstream tex (cookTexNode) → small extension of existing gather, keeps Command/Texture flow faithful.
- FORK#2 ScreenQuad = NEW DrawKind::ScreenQuad in EXISTING single-pass executor (render_command.h:33-41 design: executor reads discriminator → PSO+primitive, chain MIXes kinds in one pass = TiXL Execute-collects-Commands). Reuse one encoder/clear/pass.
- FORK#3 blend = per-(kind,blendmode) PSO variant, LAZY + CACHED via tex_op_cache (extend cachedTexPSO key with blendmode; folds the existing "follow-up caching" todo at cookRenderTarget:90-160).
Forced-by-parity (no choice): clip-space-only (commented mul), clamp(Color*tex,0,1000), BlendModes enum ordering/semantics (backward-trace factor table from _dx11/fxsetup BlendState type-ops at build).

## 6. Risks
1. **BlendModes factor table accuracy** — exact src/dst factors per SharedEnums.BlendModes must be backward-traced from BlendState type-operators. Located consumers, NOT factor table. Screen/Multiply not single Metal blend eq. MITIGATION: scope batch to Normal+Add (pure Metal eq), defer Screen/Multiply.
2. **Texture-ptr lifetime on Command path** — RenderDrawItem borrows, must not outlive one cook (render_command.h:18-21). srcTexture is PointGraph-owned (cookTexNode output), same single-frame lifetime as points borrow. Route through per-frame gather, do NOT retain.
3. **Unwired Texture fallback** — DrawScreenQuad.t3 uses UseFallbackTexture+LoadImage default. sw has open unwired-input fallback lane (task_3fc122a2). DrawScreenQuad with no texture → defined result (black/empty), NOT crash. Decide up front.
Size: ONE batch ~4-6 files (render_command.h, point_ops_rendertarget.cpp, new point_ops_drawscreenquad.cpp, draw_screenquad.metal, point_graph.h/.cpp CmdCtx texture field, CMake/selftest). ~Seam A (Cut 75) scope. If blend factor trace brittle → scope Normal+Add, defer exotic (clean sub-cut, not forced chunking).
