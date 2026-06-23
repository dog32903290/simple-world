# PARTICLE_FIELD_BLUEPRINT.md — `particle-field / ApplyVectorField` 縫施工藍圖

> Read-only Plan pass, 2026-06-23, HEAD `bcccd87`/`cef8094`. file:line 對 authoring 時 HEAD，動碼前 re-confirm。Mirror 格式：`S3_FLOW_BLUEPRINT.md` / `CAMERA3D_BLUEPRINT.md`。

## 0. 一句話判決（這縫不是「蓋 ApplyVectorField」）

**這縫的本質是把 runtime-assembled 的 field MSL（`GetField()` 那段 codegen 字串）接進已經在跑的 particle force compute kernel——而 force kernel 早就存在、早就在 `Velocity +=` 整合，只是 field 被 baked 成常數 `(1,1,1)`。**

證據三連：
1. **VectorFieldForce 已經 ported 且在 production 路徑跑**（`point_ops.cpp:215`，`FORCE_KIND_VECTORFIELD` 分支，`runSimOpSelfTest` 綠）。它的 kernel `vector_field_force.metal:52` 寫死 `float4 f = float4(1,1,1,1)`——named **fork-VFF**。整個整合半邊（`Velocity += f.xyz*Amount*f.w*variation`，`vector_field_force.metal:60-64`）**已忠實 TiXL `VectorFieldForce-sg.hlsl:60-68`**。
2. **field codegen 已經會產 `GetField`-相容的 MSL**：`assembleFieldMSL`（`field_graph.h:151`）吐 `{GLOBALS}/{FLOAT_PARAMS}/{FIELD_CALL}` 三洞填好的 source string + packed float-param buffer + srcHash。ToroidalVortexField 的 `fToroidalVectorField` 回 `float4(swirl+radial, decay)`——**f.xyz velocity 已經算在裡面，只是沒人在 GPU 上讀**。
3. **唯一缺的橋**：force kernel 是 **靜態編譯的固定 lib PSO**（`makeComputePSO(dev,lib,"vector_field_force")`，`point_ops.cpp:115`），它不知道怎麼把一段 **runtime 生成的 field 字串** 注進去。field 那側有 `cachedSourcePSO`（render-only，`tex_op_cache.h:65`）+ `cachedComputePSO`（prebuilt-lib-by-name，`tex_op_cache.h`）——但**沒有 source-compiled COMPUTE PSO cache**。

**真縫 = field-MSL 注入 + source-compiled compute PSO cache + 把 assembled field-param buffer 綁到第二個 buffer slot。** 就像 S2 結果是「MultiInput Command collector」不是「new draw pipeline」，S3 是「thread ContextVarMap」不是「build context-var」——這縫是「把 field source 注進 force compute 模板」，不是「寫位移積分器」（積分器已在）。

## 0.5 ★PROBE 結果（2026-06-23，probe=B）→ 多一塊 PF-0 keystone（scope 比原 §0 大）

§7 結尾 FLAG（resident flatten 帶不帶 wired field child）已用 probe golden 驗證（`--selftest-particlefield-probe`，commit `f5fe112`，主樹常駐 tooth）：**結果 = B，且比最壞情況更廣——不是 resident 漏，是 flat+resident 兩條 cook path 都完全沒有任何 field-input 投影。** 原 §0「force kernel 早就在跑、只缺注入橋」是對的，但**漏看了「wired field 根本到不了 force cook」**。直接建 §3 PF-a = flat 綠 prod 黑的假完成（probe 兩腿 mean 都 `(7.699,7.699,7.699)` anisotropy=0，field 被丟）。**所以 PF-a 之前必須先補 PF-0。**

**probe 證據（file:line，2026-06-23 HEAD）**：
- `node_registry_particle.cpp:44-54` — VectorFieldForce NodeSpec **只有** force/Amount/Randomize/_ForceKind，自帶註解「TiXL's VectorField (ShaderGraphNode) input is OMITTED (no field type on the contract yet)」。wired field 無處可落。
- `point_graph.cpp:232-309`（flat）+ `point_graph_resident.cpp:284-338`（resident）按 dataType gather input（Points/Texture2D/Mesh/Gradient/Curve）——grep `"Field"`/`ShaderGraphNode`/`FieldNode`/`fieldRoot` 兩檔皆 **零**。resident 還 `cc.graph=nullptr`（`:330`）→ op 連回走 graph 找 field 都不行。
- `renderField2d`（`field_render.cpp:21`）/`makeFieldNode` 唯一 caller = field render golden（`field_render_golden.cpp:103,129`）——production 無 graph→FieldNode 橋。field op 有 `"Field"` output port（`field_ops_toroidalvortexfield.cpp:216`）故 graph-addable，但無人消費成 runnable tree。

**★PF-0 — field-input projection keystone（PF-a 之前先做，cook-core 序列、owner-lock、與 S4 拆 point_graph 協調同檔）**：
1. `node_registry_particle.cpp` — VectorFieldForce NodeSpec 加 `"Field"` input port。
2. `point_graph.cpp`（flat）— 加 `"Field"` gather 分支，把 wired field node 走成 FieldNode tree（**新 graph→FieldNode builder**，目前不存在；per-type `makeFieldNode`+recurse children，鏡像 `assembleFieldMSL` 的 tree 但 source 自 graph 連線非 test factory）。
3. `point_graph_resident.cpp` — resident 鏡像（`ResidentInput::Driver::Connection` 分支，仿 Gradient gather `:288-295`），把 field child 投影過 resident flatten。
4. `PointCookCtx` 加 field-tree channel（目前無 field slot，鏡像 `inputGradients`/`meshVtx`）。
5. **然後** 才是 §3 PF-a（cachedSourceComputePSO + force-compute 模板 + slot-1 field-param bind）。

**probe golden 是 executable FLAG**：no-bug PASS=斷言當前 gap 形狀（兩腿 baked-isotropic，regression tripwire）；`-bug` RED=斷言未來綠狀態（wired field 有 anisotropic 效果）。PF-0+PF-a 落地後 implementer 翻轉它（改 no-bug 斷言 anisotropy≠0）。**這條 tooth 鎖住 seam 的驗收標準在測試裡，不只在 prose。**

## 1. TiXL ground truth（cited）

兩個**不同**的 velocity 消費者，世界觀互斥，必須分清：

**A. `VectorFieldForce`（particle/force）— STATEFUL integrate**
`external/tixl/.../particles/VectorFieldForce-sg.hlsl:60-68`：
```
float3 pos = Particles[gi].Position;
float4 f = GetField(float4(pos, 0));
float3 variationFactor = hash11u(i.x) * Variation + 1;
float3 velocity = f.xyz * Amount * f.w * variationFactor;
if (!isnan(velocity.*)) Particles[gi].Velocity += velocity;   // ← cross-frame accumulate
```
- **`+=` 進 RWStructuredBuffer<Particle>**——這是 cross-frame state（velocity 累積，下一幀 integrator `pos += vel` 消費）。但 **state 已經住在 sw 的 `SimState::particles`**（`point_ops.cpp`，persistent device buffer，seeded once）。**這縫不新增 cross-frame state**——它沿用 ParticleSystem 既有的 persistent particle buffer。`.cs`（`VectorFieldForce.cs:9-16`）：input `ShaderGraphNode VectorField` + `Amount` + `Randomize`。

**B. `ApplyVectorField`（field/use）— STATELESS sample（SRV→UAV）**
`external/tixl/.../points/_research/ApplyVectorField-template.hlsl:74-119`：
```
Point p = SourcePoints[i.x];        // t0 SRV
float4 f = GetField(float4(p.Position,0));
float3 d = f.xyz; float l = length(d)+eps; d/=l;
... ClampLength / ScaleLength / Normalize ...
float4 r = qLookAt(d, UpVector);
p.Rotation = qSlerp(p.Rotation, r, strength);   // ← 不寫 position!寫 Rotation + FX1/FX2
ResultPoints[i.x] = p;              // u0 UAV
```
- **無 cross-frame state**：一條 SRV（SourcePoints）→ UAV（ResultPoints）。`.cs`（`ApplyVectorField.cs:9-37`）：`Points`(BufferWithViews) + `Strength` + `StrengthFactor`(FModes enum) + `Normalize` + `VectorField` + `UpVector` + `ClampLength` + `ScaleLength` + `SetFx1To`/`SetFx2To`(SetFxModes)。**輸出是 `qLookAt` 的朝向 + FX 寫入，不是位移**。「velocity field 驅動 point 位移」的語義其實是 **B 改朝向**（供 DrawBillboards 之類用），**A 改 velocity**（供 integrator 下一幀位移）。

**關鍵分岔**：題目說「讓 velocity field 真正驅動 point/particle 位移」——**位移是 A(VectorFieldForce)的事**（velocity→integrator→pos）。B(ApplyVectorField) 是 sample 進**朝向**，不直接位移。**建議第一顆驗證 op 用 A(VectorFieldForce un-fork)**，因為：(a) 它已 ported，只需 un-bake field；(b) 它直接 GPU-verify ToroidalVortexField 的 `f.xyz`(velocity) + `f.w`(decay 一起進公式)；(c) 位移可手算（見 §6）。**B 作為第二階**（stateless，語義不同，獨立葉）。

## 2. 脊椎 S* 還是並行 lane?判定

**動 cook-core，但只動 particle/field lane 的局部核心——介於兩者之間，判為「半脊椎」：owner-lock 但 scope 窄。**

理由：
- **動的核心檔**：`field_graph.h`(`AssembledField` 可能加 compute 變體 metadata)、`tex_op_cache.h/.cpp`（新增 source-compiled compute PSO cache，**device-global 共享狀態**——這是真核心）、`point_ops.cpp`（force dispatch 迴圈，`cookParticleSim`，**ParticleSystem lane 核心**）、`particle_params.h`(`VecFieldForceParams` 加 field-param buffer binding)。
- **不動**：`point_graph.h`/`point_graph.cpp`/`point_graph_resident.cpp` 的 **CmdCookCtx / Command collector**（那是 S2/S3 的地盤），不動 `frame_cook.cpp` 的 pass-ordering，不動 EvaluationContext。**ApplyVectorField(B) 若做 point-buffer SRV→UAV 版，則會動 point cook 的 buffer 路徑**，但 sw 目前 point_ops 是 per-op 自帶 device buffer（`SimState::particles`），沒有通用 BufferWithViews SRV/UAV 抽象——B 的乾淨落法是**也走 force-pass 模板**（particle buffer in-place），把「SRV→UAV 雙 buffer」這個 TiXL 特性標為 **named fork [fork-VFF-inplace]**（sw 單 buffer in-place，如同既有 force ops）。

**結論：owner-lock 序列（半脊椎），但不與 S2/S3/S4 撞檔。** `tex_op_cache` 的 source-compute-PSO cache 是唯一 device-global 新狀態，須 owner 序列化；其餘是 particle lane 局部。可與 camera3d / point-camera lane **並行**（不同檔）。`S4`（拆 point_graph 862/710 超 cap）若同時動 `point_ops.cpp`，須 **與 S4 owner 協調**（同檔）——標 FLAG。

## 3. 分階 keystone（每階配一顆真 op 驗證，避免 orphan）

### PF-a — source-compiled **compute** PSO cache + field-MSL → force 模板（keystone，最小最高槓桿）
- **新增** `cachedSourceComputePSO(dev, mslSource, srcHash, kernelName)` 到 `tex_op_cache`（鏡像 `cachedSourcePSO` 的 render 版，但建 compute pipeline）。這是 §1 缺的橋——device-global cache，owner-lock。
- **新增** force-compute 模板 `app/shaders/templates/vector_field_force_template.metal`：把 `vector_field_force.metal` 的 body 改成帶 `/*{GLOBALS}*/ /*{FLOAT_PARAMS}*/ /*{FIELD_CALL}*/` 三洞 + `float4 GetField(float4 p){ float4 f=1; /*{FIELD_CALL}*/ return f; }`（對齊 `VectorFieldForce-sg.hlsl:40-45`）。field-param struct 綁到 **第二個 buffer slot**（force 自己的 params 留 slot 0，field params slot 加一）。
- **cook 改 `cookParticleSim` 的 `FORCE_KIND_VECTORFIELD` 分支**（`point_ops.cpp:~210`）：若 force 節點有 wired field（`VectorField` input），`assembleFieldMSL(fieldRoot, forceTemplate)` → `cachedSourceComputePSO` → bind force params(slot0) + field-param buffer(slot1) → dispatch。**無 field wired → fallback 到既有 baked PSO**（fork-VFF 保留，不破壞既有 graph）。
- **驗證 op = VectorFieldForce + ToroidalVortexField**（第一顆，直接 GPU-verify ToroidalVortexField `f.xyz`）。
- **Golden `--selftest-vectorfieldforce-field`**：RadialPoints → ParticleSystem(VectorFieldForce wired ToroidalVortexField, Amount 大, step N frames)。closed-form：見 §6。`-bug`：斷開 field（回 baked (1,1,1)）→ 各向同性對角漂移，**不是** toroidal swirl 的特徵幾何 → RED。

### PF-b — `ApplyVectorField`（stateless，sample→朝向，純選擇 + qLookAt，中風險）
- **新葉** `point_ops_applyvectorfield.cpp` + 模板（沿用 PF-a 的 field-MSL 注入機制，但 kernel body 是 `ApplyVectorField-template.hlsl:74-119` 的 `qLookAt`/`qSlerp`/FX 邏輯）。**named fork [fork-VFF-inplace]**：TiXL 雙 buffer（SourcePoints t0 → ResultPoints u0）→ sw 走 point-op output buffer（已有 `c.output`），源從 `c.inputs[0]`。
- **驗證 op = ApplyVectorField + ToroidalVortexField**。
- **Golden `--selftest-applyvectorfield`**：GridPoints → ApplyVectorField(ToroidalVortexField, Strength=1, UpVector=(0,1,0)) → 讀回 `Rotation`，斷言每個 point 的朝向 = `qLookAt(normalize(f.xyz), up)`（closed-form per-texel，見 §6）。`-bug`：Strength=0 → `qSlerp(orig, r, 0)` = 原朝向不變 → RED。

### PF-c —（可選，延後）`RepeatFieldAtPoints` / `SampleFieldPoints`（field-at-points，雙向 seam）
- 需 point-buffer 在 field codegen 裡被 sample（field 反過來吃 point list）。**這是相反方向的縫**（point→field），依賴 point-buffer 通用 buffer 抽象（尚無）。**標 BLOCKED，不在本縫 scope**，列入解鎖路線供 orchestrator 排序。

**落地序：PF-a → PF-b →（PF-c 延後）。** PF-a 是橋，一切靠它；PF-b 證 stateless sample 路徑；PF-c 是反向 seam，延後。（鏡像 S3 的 keystone-first。）

## 4. flat + resident mirror checklist

**好消息：這縫的 cook 路徑在 ParticleSystem lane，不在 Command-collector 雙腿。** ParticleSystem(`cookParticleSim`) 是**單一 cook fn**，flat 與 resident **共用同一個 `PointCookCtx` cook**（force 是 buffer input，經 `cookInputParam(c, 1, ...)` 讀參數，兩腿走同一段）。所以**沒有 S2c-style 雙腿黑洞風險的主體**。

但仍須逐項確認：

| 機制 | flat leg | resident leg | 漏鏡的後果 |
|---|---|---|---|
| force 的 `VectorField` input 接線解析 | `cookInputParam(c, 1, ...)` 已共用 | 同 `cookInputParam` | 共用路徑，低風險；**但** field 是 **第二類 input**（不是 float param），須確認 resident flatten 有把 wired field 節點投影成可取的 field-root（見下） |
| field-root 取得（從 wired ShaderGraphNode → FieldNode tree） | flat graph 直接拿 node | resident `ResidentNode` 須能 rebuild FieldNode tree | **resident 漏 → production 拿不到 field → 回 baked (1,1,1) → prod-only 退化成對角漂移**（flat golden 綠）。**這是本縫唯一的雙腿陷阱，須 resident `-bug` 獨立 tooth** |
| source-compute PSO cache（device-global） | 共用 cache | 共用 cache | 共用，但確認 selftest `clearTexOpCache()` 兩腿都清，否則跨 run 髒 PSO |
| field-param buffer 上傳（slot 1） | `setBytes`/`newBuffer` 同 force-pass | 同 | 共用 dispatch helper `runForce`，低風險 |

**血淚錨點**：`field-root 取得` 這一格就是 S2c 的等價陷阱——**field 怎麼從 wired node 流到 cook**，flat 與 resident 取法不同（resident 走 `extraConns`/flatten，`resident_eval_graph.h:101` strInputs 之類），**必須兩腿都能 rebuild FieldNode tree**。**Golden 紀律：每個 PF golden 跑 flat + resident 兩腿，resident `-bug` = 故意讓 resident 拿不到 field（回 baked）→ prod-only RED。**

## 5. 風險 / 本質複雜點

| # | 點 | 本質 or 意外 | 處置 |
|---|---|---|---|
| 1 | **cross-frame velocity state** | **本質，但已封裝**——`SimState::particles` 已是 persistent device buffer，VectorFieldForce 只是再加一個 `+=` pass。不新增 state。 | 沿用既有 ParticleSystem buffer。乾淨。 |
| 2 | **source-compiled compute PSO**（目前不存在） | **本質**——field 是 runtime codegen，kernel 必須 runtime 編。 | 鏡像 `cachedSourcePSO` 加 compute 版，srcHash 鍵，zero 每幀重編（同 render 路徑已驗的工法）。 |
| 3 | **GPU buffer aliasing**：force 自身 params(slot 0) vs field params(slot 1) 撞 binding | **意外**——只要分清 slot。 | force params 留原 slot；field-param buffer 綁新 slot；`particle_params.h` 加 `FORCE_FieldParams` enum（單一真相）。static_assert 守 struct size。 |
| 4 | **field-param prefix 衝突**：`P.<prefix>Name` vs force 的 `P.Amount` | **意外**——field 已自帶 `ToroidalVortexField_<id>_` prefix（`field_graph.h` BuildNodeId），force params 是裸名，不撞。 | 確認模板把 field params 包進**獨立 struct**（`FieldParams`），force params 另一 struct。 |
| 5 | **velocity integrate 數值穩定**：`+=` 每幀累積 + decay 飽和 | **本質**（TiXL 同）——但 decay∈[0,1] + Amount 有界，且 integrator 有 Drag。 | 忠實 TiXL，不加阻尼。golden 用**少幀數 + closed-form 首幀位移**避開累積發散（見 §6）。 |
| 6 | **particle count** mismatch：field 對任意 pos 取值，pool 大小無關 | **非問題**——field stateless 對 position 取值，count 只是 dispatch 範圍。 | dispatch `pool`，既有 `calcDispatchCount`。 |
| 7 | **resident field-root rebuild**（§4 血淚） | **本質**——resident flatten 須能還原 FieldNode tree。 | 確認 resident 投影 field input；resident `-bug` tooth 守。**FLAG builder**：若 resident 目前無 field-input 投影，這是本縫最大未知，標「需驗 resident flatten 是否帶 field child」。 |
| 8 | **NaN guard**：field 在 rho<eps 早返 `(0,0,0,0)` | **本質**（TiXL `fToroidalVectorField` early-return + kernel isnan guard） | 已在 `.metal:49,63`，模板保留。 |

## 6. golden 怎麼設（closed-form，避退化點）

**PF-a(VectorFieldForce + ToroidalVortexField) 首幀位移手算**：
- 取一顆 particle 在 **非退化** field-space 位置，避開 `rho<eps`（early-return 0）與 `m=Radius`（rho=0）。
- 用 ToroidalVortexField 既有 golden 已驗的 spot（`toroidalvortexfield_golden.cpp:27-30`）：`Radius=0.5, Range=0.5, decayK=2`，position 投到 `px=0.25,py=0,z=0` → `m=0.25, rho=0.25, decay=1-(0.5)²=0.75`。
- 該點 velocity：`phi=atan2(0,0.25)=0`，`e_r=(1,0,0)`，`e_phi=(0,1,0)`，`C=(0.5,0,0)`，`r=p-C=(-0.25,0,0)`，`rho=0.25`。`vSwirl=normalize(cross((0,1,0),(-0.25,0,0)))*1*0.75 = normalize((0,0,0.25))*0.75=(0,0,0.75)`（swirl 純 +z）。`vRadial=(-r/rho)*1*0.75=((0.25/0.25,0,0))*0.75=(0.75,0,0)`（指向 ring，+x）。`v=(0.75,0,0.75)`。
- **首幀** `Velocity += v*Amount*decay`... 注意 `VectorFieldForce-sg.hlsl:65` 是 `f.xyz*Amount*f.w`——f.xyz **已含 decay**，再乘 f.w 是 **decay²**。手算：`velocity=(0.75,0,0.75)*Amount*0.75*variation`。Variation=0 → `variationFactor=1`。`Amount=A` → 首幀 `vel=(0.5625A, 0, 0.5625A)`。integrator 下一幀 `pos += vel*Speed*scale`。
- **斷言**：單 particle 種在 `(0.75,0,0)`（world，投到 px=0.25 須對齊 field-space 映射——**用既有 toroidal golden 的 field-space convention**），step 1 frame，讀回 velocity == `(0.5625A,0,0.5625A)`（±eps）。**避退化**：不取 center（rho=0.5→decay=0，velocity≈0）、不取 ring 上（rho<eps→early-return 0）。
- **`-bug`**：斷 field → baked (1,1,1) → `vel=(A,A,A)*0.75...` 各向同性 → **z 與 x 相等但 y≠0** → 與 toroidal 的 `y=0` 特徵不符 → RED。

**PF-b(ApplyVectorField + ToroidalVortexField) 朝向手算**：
- 同點 `f.xyz=(0.75,0,0.75)` → `d=normalize=(0.707,0,0.707)`。`UpVector=(0,1,0)`。`qLookAt(d,up)` closed-form（TiXL `quat-functions.hlsl` qLookAt）→ 斷言讀回 `Rotation` == 該四元數（±eps），`Strength=1` 全量。
- **`-bug`**：`Strength=0` → `qSlerp(orig,r,0)=orig` → Rotation 不變 → RED。

**雙腿**：兩 golden 都跑 flat + resident，resident `-bug` = field-root rebuild 漏 → prod-only RED。

## 7. 解鎖清單（蓋完這縫解鎖哪些 op）

從 `OP_BACKLOG.md` + census 撈，**直接解鎖**（本縫 PF-a/PF-b 完）：

| op | 來源 | 階段 | 備註 |
|---|---|---|---|
| **VectorFieldForce(真 field)** | particle/force | PF-a | un-fork fork-VFF；直接 GPU-verify ToroidalVortexField f.xyz |
| **ApplyVectorField** | field/use | PF-b | stateless sample→朝向；fork-VFF-inplace |
| **ToroidalVortexField f.xyz 直驗** | field（已 ported） | PF-a | 補上 cef8094 留的 verification ceiling（velocity-text → GPU-direct） |

**級聯解鎖**（field-MSL→compute 橋一通，這些只差各自的小 seam）：
- **RandomJumpForce**（particle/force）— ShaderGraphNode 為**可選** input（census `ops-particle-flow.md:55`），本縫的 field-注入機制直接可用 → half-faithful → full-faithful。
- **CustomForce / FieldDistanceForce**（SEAM_GRAPH `shader-graph` 下游 4 force）— 同 field-into-force 橋。
- **RaymarchPoints / SdfReflectionLinePoints / SampleFieldPoints**（field/use，`ops-field-mesh.md:27`）— 同 field+point compute 模板，但各帶次要 seam（raymarch / reflection）。
- **RepeatFieldAtPoints / ExecuteRepeatFieldAtPoints**（field/space，`ops-field-mesh.md:21`）— **反向 seam(point→field)**，PF-c，需 point-buffer 通用 buffer 抽象，延後。

**規模**：直驗解鎖 3 顆 + 級聯約 4-7 顆（各帶次要 seam）。比 point-buffer(~90) 小，但**這縫是 field 島 velocity 半邊的唯一出口**，且補上 ToroidalVortexField 的驗證債——戰略價值高於 op 數。

---

### Critical Files for Implementation
- `app/src/runtime/point_ops.cpp`（`cookParticleSim` 的 `FORCE_KIND_VECTORFIELD` 分支 `:~210`，`simStateNew` PSO 建立 `:115` — 注入 field-MSL + source-compute PSO + field-param bind）
- `app/src/runtime/tex_op_cache.cpp`（+`.h:65` — 新增 `cachedSourceComputePSO`，device-global owner-lock 新狀態）
- `app/src/runtime/field_graph.h`（`assembleFieldMSL` `:151` / `AssembledField` `:137` — 餵 force-compute 模板的同一 codegen 出口）
- `app/shaders/vector_field_force.metal`（現 baked-(1,1,1) fork-VFF `:52` — 改成帶 `{GLOBALS}/{FLOAT_PARAMS}/{FIELD_CALL}` 的 force-compute 模板）
- `app/src/runtime/particle_params.h`（`VecFieldForceParams` / `FORCE_*` binding enum — 加 field-param buffer slot + static_assert）
- ref-only：`external/tixl/.../particles/VectorFieldForce-sg.hlsl`（stateful integrate ground truth） + `.../points/_research/ApplyVectorField-template.hlsl:74-119`（stateless sample→朝向 ground truth） + `external/tixl/Operators/Lib/field/use/ApplyVectorField.cs` / `particle/force/VectorFieldForce.cs`（input slots）

---

**一個關鍵未知須 builder 開工前先驗（FLAG）**：resident flatten（`point_graph_resident.cpp` / `resident_eval_graph.h`）目前**是否把 wired field 節點投影成可在 cook 時 rebuild 的 FieldNode tree**。force 的數值 param 走 `cookInputParam` 已驗，但 **field input 是 ShaderGraphNode(非 float)，resident 是否帶這個 child 連線**是本縫最大未知（§4/§5#7）。若無，PF-a 須先補 resident field-input 投影——那會是額外 keystone。建議 PF-a 第一步即跑一個 probe golden 確認 resident 能取到 wired field root。
