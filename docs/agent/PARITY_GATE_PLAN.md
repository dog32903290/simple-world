# PARITY_GATE_PLAN — 有狀態重節點的 parity 閘 retrofit

> 柏為 2026-06-29 02:38 下令：**今晚 sw-batch 自走第一優先。** 自走整個過程修好「當初沒走過 sw-batch parity 驗證方式」的有狀態重節點。
> 起因：預設面板 RadialPoints/TurbulenceForce/ParticleSystem/DrawPoints「結果跟 TiXL 不一樣」。查證後發現不是個案，是一整類「有狀態/渲染重節點」的系統性驗證盲區。全程查證見 memory [[sw-stateful-node-parity-gap]]。

---

## 病灶（已查證，別重 scout，直接信）

驗證強度按節點類型裂開：
- **無狀態葉子**（value-op / field SDF / image filter）＝**扎實**：內嵌 golden + injectBug，對 TiXL 手算確定值。
- **有狀態模擬/渲染重節點**＝**只到 smoke**：「有動 / 非黑 / 點移出環 > 0.1」這種**手定鬆閾值**，**對倍率/相位偏差結構性瞎**。

血證：TurbulenceForce 的 smoke 斷言是「粒子移出環 > 0.1」——**Amount=15（現預設）和 Amount=1（TiXL）都輕鬆通過**。閾值性質斷言分不出 1× 和 15×，這就是偏差滲入還亮綠的機制。

**已驗證「污染未擴散」**：下游 golden（afterglow 等）用手定閾值性質斷言 + 主動覆寫生成器參數（`afterglow_golden.cpp:125 params["Count"]=0`），**不吃生成器預設值**，所以 RadialPoints 偏差污染不了它們。verified 層不需整體重審計，問題範圍 = 下面這張清單本身。

---

## 不可繞過的鐵律（防洗白，違反即停止報告）

**這是整份計劃的命根。寫鬆了，自走會建個假模板把偏差洗白成綠燈，比現狀更糟。**

### 鐵律 1 — RED-FIRST（模板自己的試壓關）
每顆節點的 parity-golden **寫完後、修任何節點碼之前**，必須先證明它對**現狀偏差亮 RED**。具體 tooth：
- Turbulence golden：對 `Amount=15`（現預設）**必須 RED**，對 `Amount=1`（TiXL .t3）才 GREEN。
- Radial golden：對 `Count=2048/Radius=2`（現預設）**必須 RED**，對 `100/1`（TiXL .t3）才 GREEN。
- DrawPoints 性質探針：對現狀單點 4px 死點實作**必須 RED**（探針＝點寬度>1px / 有 billboard 朝向），換 quad 後才 GREEN。

**若新 golden 對現狀是 GREEN（沒紅）→ 模板無牙 → 立刻停止、報告「模板沒咬住已知偏差」、不准修節點碼。** 這條是唯一防止「建鬆模板洗白偏差」的保險。

### 鐵律 2 — 期望值錨 TiXL，不錨 sw 自己
每個斷言的期望值＝**TiXL 源碼手算/公式**，斷言旁註明 TiXL 出處（`.cs`/`.hlsl`/`.t3` 行號）。**禁止**用「sw 當前 readback 快照」當期望（那是自洽不是 parity，柏為 Cut47「差不多滑成只自洽」）。

### 鐵律 3 — 分類天花板（別把純像素硬塞數值 parity）
- **可手算類**（位置/積分/參數值）→ CPU readback 對 TiXL 公式手算確定值，**達真 parity**。今晚主力。
- **純像素類**（DrawPoints 的 sprite 長相）→ 本批只到**緊性質探針**（寬度/顏色/blend 結構級）。**像素級 byte 比對做不到**（DX11 vs Metal 跨 GPU，rasterize/blend/抗鋸齒必差最後幾 bit，逐像素相等是 GPU 噪音誤報）。真像素 parity ＝容差比對（SSIM/per-pixel 容差），**需 Windows TiXL reference 錄製 lane**（[[windows-tixl-copilot-kit]]），標記延後、別在本批空轉。

### 鐵律 4 — golden 必須 cook-through production（refuter 血證，2026-06-29 Stage1 踩）
parity-golden 必須**走 production cook path、用 NodeSpec default cook**，不准繞過 cook 直接 dispatch kernel 用手設參數。Stage1 turbulence 首版 direct-kernel 假綠：NodeSpec default 沒改（Amount 還是 15）、fixer 改的 cook-side fallback 是死碼（`resolveNodeParams` 對每個 Float port 從 `p.def` 填、`cookInputParam` fallback 永不 fire），但 golden 繞過 cook 所以沒咬到 → production 實際還是 15× 卻 GREEN。**只有 cook-through 才守得住 NodeSpec default。** RadialPoints 做對了（cook-through，clean-base 全 RED）。

### 鐵律 5 — 下游 golden 不准吃 cook fallback default
共用場景的參數（如生成器 Radius）必須在 golden 場景裡**顯式 pin**，不准依賴 cook fallback default。Stage1 particlefield_probe 隱性耦合：resident leg 只 override Count 沒 override Radius → 吃了 cook fallback default → 改 production default 時 probe 翻紅（非 production bug）。Stage 2 裝閘時加這道 lint。

---

## 階段（順序固定，pilot 先試壓模板）

### Stage 0 — 前提閘（啟動即檢）
- 樹乾淨、HEAD 對齊、**確認無另一 session 在動 particle/point/render 檔**（[[sw-batch-no-parallel-launch]]）。menu/UI lane 不撞本計劃，但若它在跑須確認不雙寫 MASTER_PLAN。

### Stage 1 — Pilot（用一顆逼出模板＋閘，建議 RadialPoints 或 ParticleSystem）
位置可手算的代表先做。建 `<op>_parity_golden.cpp`：固定 seed / 固定幀數 → CPU readback 位置/屬性 → 斷言對 TiXL 公式手算確定值。
- **驗收**：同寫法套上 Turbulence，`Amount=15` 變 RED（鐵律 1）。
- readback golden 基建已存在（particle_decay / particlefield_probe / point_ops_selftest 的 CPU readback），**復用別重造**。
- pilot 過了 → 抽成可複用 harness。

### Stage 2 — 裝閘（gate-or-it-rots：沒閘下批 smoke 照樣滑過）
- 建一張「有狀態節點清單表」（下面 §清單），每顆狀態：`has-parity-golden` / `pixel-deferred-windows` / `todo`。
- 接進 `--bite` / `check_arch.sh`：清單上 `todo` 且無 parity-golden 又沒標 deferred ＝ **紅燈**。ratchet 式（仿 `linecount-grandfather.txt`，只准 todo→done 不准回退）。
- **這步是 [[gate-or-it-rots]] 要的真閘**——光修節點不裝閘，下個自走批次製造新 smoke 債。

### Stage 3 — Fan-out（逐顆，全 [G]，red-first→修→green→refuter→commit）
照 §清單逐顆。每顆：parity-golden red-first 證牙 → 修（預設值/解 wall-clock/補砍掉的 param）→ green → refuter → commit。

---

## 清單（已查證偏差，直接修，別重 scout）

### 可手算類（真 parity，今晚主力）

**TurbulenceForce** `[G]`（+`[Y]` 觀感註記）
- Amount 預設 **15→1**（TiXL .t3=1.0）。
- Frequency 預設 **1.2→1.0**（TiXL .t3=1.0）。
- **Phase 解除 wall-clock 硬綁**：`point_ops.cpp:323` 無條件 `tp.Phase=time`，無視已註冊的 Phase param。改吃 inspector param（預設 0）。**這是 divergence**（TiXL Phase 是 user input，只有接 Time 才動）+ **打死離線決定性 render**。
- 補 Variation / VariationGroupCount param（現 hardcode 0、無 inspector，feature 死掉）。
- ⚠ **觀感變化**：解綁後預設 Phase=0 → turbulence 變靜止（除非接 Time）。這是**照 TiXL 對齊非品味分岔**（改進規則：會變 render 但照 TiXL＝合法），但明顯 → 標 `[Y]` 落待驗收讓柏為事後看一眼。
- 噪聲數學本身（simplex→curlNoise）已 1:1 忠實，**別動**。

**RadialPoints** `[G]`
- Count 預設 **2048→100**（TiXL .t3=100）。
- Radius 預設 **2.0→1.0**（TiXL .t3=1.0）。
- 補 param（現全 baked，golden 未覆蓋）：orientation quaternion（現 baked identity）/ FX1FX2（現 baked 0，TiXL 預設=1.0）/ GainAndBias / CloseCircleLine / Scale。逐個補或明確標延後。
- 位置數學已忠實（`radial_points.metal:39-42` ≡ TiXL hlsl:82-91），**別動**。

**ParticleSystem** `[G]`
- ⚠ **dt/wall-clock 不是 divergence，別亂改**：integrator 是 frame-count 決定性，wall-clock 只標出生/年齡＝**符合 TiXL 的 Time 模型**。integrator kernel 已 1:1 忠實。
- 真偏差在 host 砍掉的 input：EmitVelocity（現 hardcode 0）/ LifeTime / RadiusFactor / OrientTowardsVelocity / SetFx1To/SetFx2To / EmitMode / EmitVelocityFactor。逐個接回或明確標延後。
- MaxParticleCount/IsAutoCount 的 pool-recycle 是**具名 sw fork**（`particle_params.h:76-103`）——這個要柏為拍板留不留（非純 parity），標 `[?]` 不自走改。

**各 Force**（FieldDistanceForce / FieldVolumeForce / RandomJumpForce / AxisStepForce / SnapToAnglesForce / VectorFieldForce…）`[G]`
- 完全裸奔（NONE）或 weak。逐顆 parity-golden（force 對 velocity 的數學對 TiXL .hlsl 手算）。

**point/render 可手算**（MoveToSDF / PointToMatrix / SnapPointsToGrid / TransformFromClipSpace / DoyleSpiralPoints2 / Transform / Shear / RotateAroundAxis）`[G]`
- 逐顆位置/矩陣數學對 TiXL 手算。

### 純像素類（本批只到緊性質探針，像素 reference 延後）

**DrawPoints** `[G]` 結構部分 + `pixel-deferred-windows`
- **換回 DrawPoints2 的 6-vert camera-facing quad 實作**（現為退化單點 4px 死點，11 個 TiXL param 全砍）。忠實 quad port 已存在於 `draw_points2.metal`，**別從零寫**。
- 接回 param：PointSize / Color / BlendMode / ScaleFactor / 深度。
- 緊性質探針：點寬度>1px / 有 billboard 朝向 / 吃 blend / 顏色正確。
- 像素級容差比對 → 標 deferred，等 Windows reference lane。

---

## 完成定義
1. §清單每顆：有 parity-golden（red-first 證牙）或明確標 deferred/[?]。
2. Stage 2 閘已裝（清單 ratchet 進 --bite/check-arch）。
3. 全綠：golden byte-identical + refuter SURVIVES + --bite + check-arch。
4. `[Y]`/`[?]` 項落「## 待柏為驗收」佇列，非阻塞。
