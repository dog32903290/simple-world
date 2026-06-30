# SEAM1 vec4 — UNRESOLVED-MATRIX-SOURCE GATE 施工圖

> ✅ LANDED 2026-06-30（gate=`--selftest-buffer-vec4` G3，RED→GREEN flat=0/res=0 → flat=1/res=1；542 全綠零回歸，
> byte-parity 不受影響）。4 edit 全照下圖：buffer_op_registry.h/.cpp（counter+noteUnresolvedMatrixSource）、
> point_graph_resident_buffer.cpp（matrixFromColorOut rn==0）、point_graph_buffer_cook.cpp（flat 對稱）、
> selftests_buffer_vec4.cpp（G3）。
>
> 2026-06-30 Opus 設計（接 SEAM1_VEC4_CURRENCY_BUILD_PLAN.md 的殘留風險 #1）。**防的不是某個缺的 producer
> （那是 chip task_0da4258a），是「下一個缺的 producer 會靜默歸零」這個系統性失效模式。** 把靜默零矩陣
> 變成可機械偵測的信號 + warn-once，並用一個 selftest 把它變成回歸閘。

## 防什麼（精準的失效）
resident `matrixFromColorOut`（point_graph_resident_buffer.cpp）：只要 Vec4Params 上有 Connection，就算
source 是 sw 沒有的 producer（output port 不存在 → `outIdx<0`，或 extColorOut 該 key 空），它**照樣 push 一個
全零 16-float 矩陣塊**。後果：buffer 大小對、byte-parity 過（測試只用真 producer），但下游 shader 讀到**零矩陣
（不是單位矩陣）→ 所有點塌到原點**。沒有任何 assert 攔得住。flat `cookColorListNode` 回 nullptr 時同樣靜默。

**偵測條件（統一，不分子情況）**：gather 一條 Vec4Params wire，最終拿到的 16-float 矩陣**沒有任何有效 row**
（rows 為空）→ 判定 unresolved。在正確 production 時序下（cookMatrixOutputNodes/cookColorListNodes 先跑），
真 producer 的 rows 不空，所以「rows 空」只發生在「sw 沒有這個 producer」。可接受、可穩定觸發、不誤判時序。

## 施工（4 個小 edit，零 byte-parity 破壞）
真 producer 不觸發 counter → 既有 G1/G2/floatstobuffer/buffer-resident 全部不受影響。`-bug` 也不觸發。

### 1. test-visible counter + warn-once（buffer_op_registry.h/.cpp）
仿 `bufferInjectBug()`（buffer_op_registry.cpp:23）：
```cpp
// .h
uint32_t& bufferUnresolvedMatrixSources();  // test-visible：一條 Vec4Params wire gather 不到 rows 時 ++
// .cpp
uint32_t& bufferUnresolvedMatrixSources() { static uint32_t n = 0; return n; }
```
warn-once 用既有 `warnCookDepthOnce` 範式（point_graph_resident.cpp:63，static bool 一次）：新增
`pgdetail::warnUnresolvedMatrixSourceOnce()`（印一行：「Vec4Params wired to a source with no matrix rows —
buffer matrix block is ZERO; missing producer? (e.g. TransformMatrix.ResultInverted / GetMatrixVar)」）。

### 2. resident 偵測（point_graph_resident_buffer.cpp `matrixFromColorOut`）
helper 末端：若最終 `rn == 0`（沒抓到任何 row：outIdx<0 或 extColorOut 空）→
`bufferUnresolvedMatrixSources()++; pgdetail::warnUnresolvedMatrixSourceOnce();`。仍回零矩陣（行為不變，
只加可見性 + 計數）。**注意**：只在「有 Connection」的路徑呼叫 matrixFromColorOut，所以 counter 只算真的有 wire
的 unresolved，不算「Vec4Params 沒接線」的正常空。

### 3. flat 偵測（point_graph_buffer_cook.cpp Vec4Params 分支）
每條 wire：`cookColorListNode(srcId)` 回 nullptr 或 `rows->empty()` → 同樣 `++` + warn-once（再 push 零矩陣）。
對稱 resident（兩腿都偵測，維持 cook_ctx.h both-legs 對稱）。

### 4. 回歸閘 selftest（selftests_buffer_vec4.cpp 新 G3 leg）
```
// G3 — unresolved matrix source is DETECTED, not silent.
建 graph：Const(id 30) -> FloatsToBuffer(id 1).Vec4Params(port 1)  // Const 永不寫 extColorOut → rows 空
bufferUnresolvedMatrixSources() = 0;          // reset（像 bufferInjectBug()=false）
cookResident（+ flat 各一次）;
assert bufferUnresolvedMatrixSources() >= 1;  // 閘偵測到了
（可選）assert buffer 矩陣塊 == 全零;          // 記錄已知缺陷狀態
```
**這個 leg 本身是閘**：之後誰把偵測拿掉 → counter==0 → assert fail → RED。它不靠 `-bug`（它測的是偵測存在，
不是 -bug 污染）；要它能 RED 的破壞情境＝偵測被移除。run_all --bite 會把它當一個 tooth（assert>=1 可失敗）。

## 刻意不做（先求簡單，沒人踩的延伸不織）
- **不標 buffer invalid / 不傳 invalid 下游**：buffer 目前**沒有任何 consumer 出畫面**（point_graph.cpp:673
  「No Buffer VISUALIZER」）。invalid-propagation 等 buffer 有真 consumer（binding rail）再做，否則是改 byte-parity
  語意 + 織還沒人踩的線。
- **不改 SwBuffer 結構、不碰 cookFloatsToBuffer**：偵測全收在 gather 端 + 一個 global counter。
- **不自動修復**（不 fallback 單位矩陣）：零 vs 單位都是錯，自動換單位會掩蓋「缺 producer」這個真問題。閘要吵，不要安撫。

## 工作量 / 風險
4 個小 edit + 1 selftest leg，全在 runtime leaf + 一個 test-visible global。零既有 byte-parity 破壞（真 producer 路徑不觸發）。
line-count：matrixFromColorOut 加 ~3 行、registry 加 ~4 行——注意 point_graph_internal.h 已逼近 cap（475），
warn-once decl 放 buffer_op_registry.h 不要塞 internal.h。
