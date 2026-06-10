# Render-Target Pivot — Handoff / Sub-Ledger

> 這是 lane A 的 **draw/render 子家族** sub-ledger。Dashboard 仍是
> `docs/superpowers/plans/2026-06-07-imgui-metal-pivot-master-progress.md`。
> 設計權威 = `external/tixl` + `docs/runtime/RENDER_TARGET_CONTRACT.md`。

## 這個 pivot 在做什麼

把「點怎麼變成畫面」對齊 TiXL 真正的三流模型，取代原本 `DrawPoints` 直接「buffer→像素」的折疊做法：

```
BufferWithViews (MTL::Buffer of SwPoint)   ← 點生成/變換 op（PointCookFn，9 顆，未動）
        │  DrawPoints = Command op
        ▼
Command (RenderCommand, render_command.h)   ← 一串 RenderDrawItem，純資料、非 closure
        │  RenderTarget = Texture op（執行者，擁有 render pass）
        ▼
Texture2D (MTL::Texture)                     ← 可顯示影像，解析度在這裡釘（RESOLUTION PIN）
```

- `DrawPoints`：draw op → **command op**（`Points → RenderCommand`），輸出 `Slot<Command>`，不碰 texture。
- `RenderTarget`：新 **texture op**（`Command → Texture2D`），開一個 render pass，clear 一次、把 command chain 的每個 item 依序畫上去（合成 = 串接，不是 N 個 closure 互相 clear）。
- 三張 registry：`cookReg`(buffer) / `cmdReg`(command) / `texReg`(texture)，外加 retired-in-batch-4 的 `drawReg`(legacy)。

## 批次狀態

| 批 | 內容 | 狀態 |
|---|---|---|
| batch 0 | Command stream 型別 + registry（`render_command.h`, `cmdReg`）孤立證明 | ✓ commit `4620f3a` |
| batch 1 | RenderTarget texture op + 解析度釘（`point_ops_rendertarget.cpp`, `texReg`, `RenderResolution`）孤立證明 | ✓ commit `4ebe350` |
| **batch 2** | **`cook()` 終端調度接上三流 + DrawPoints 改 cmd op + RenderTarget 接執行者** | **✓ 本 session（全 selftest 綠）** |
| batch 3 | RenderTarget **NodeSpec** + `Command`/`Texture2D` port 型別 + Inspector 解析度 enum + DrawPoints 參數家族（ClearColor/Color/Blend/PointSize）| ⬜ 柏為開（= 完成定義：柏為能加+接 RenderTarget 節點） |
| batch 4 | 退役 `PointDrawFn`/`drawReg`：把 golden selftest 的 capture shim 從 draw op 遷到 cmd op（`point_graph_selftest.cpp` 已示範 `stubDrawCapture`），刪 legacy 路 | ⬜ |

## 本 session 修了什麼（batch 2 之前是半接，整棵 red）

**症狀**：11 顆點 op golden selftest red（`n=0`，讀不到點）+ 任何 DrawPoints 終端的 live 畫面變黑。

**根因**：batch 2 WIP 把 `DrawPoints` 翻成 cmd op、`RenderTarget` 翻成 tex op、`defaultDrawTarget` 改查 tex/cmd，
**但 `cook()` 的終端調度還只認 legacy `drawReg`**，而且把 legacy 感知從 `cook()` 跟 `defaultDrawTarget` 兩邊都拔掉了——
可是 legacy draw 路按設計要活到 batch 4（golden selftest 靠它當 capture 接縫）。三處沒接齊：

1. `cook()` 終端只調度 `drawReg` → 給它 cmd/tex 終端時 miss → 落到 else 又找空的 `drawReg["DrawPoints"]` → `clearTarget()` → 黑。
2. selftest 表 + `point_graph.h` 還引用已刪的 `runCommandStreamSelfTest` → 編不過。
3. 新 `point_graph_selftest.cpp` 沒進 CMakeLists → link 不到 `runPointGraphSelfTest`。
4. `defaultDrawTarget` 不再查 `drawReg` → 自帶 capture shim 的葉測（line/grid/sphere，只在 drawReg 註冊 DrawPoints）找不到終端 → 回 0 → 黑。

**修法**（全在 `point_graph.cpp` + 兩個接線檔，零 test churn）：
- `cook()` 終端調度補成四路：**legacy draw**（drawReg 有就用，production 永遠空=死路）→ **tex 終端**（RenderTarget：concat 上游 Command 輸入→執行）→ **cmd 終端**（DrawPoints：cook 出 1-item chain→經 RenderTarget 執行者畫進 target）→ **Points 預覽**（pin 任一 Points 節點：合成 1-item chain→執行）。
- `defaultDrawTarget` 補第三 fallback：tex → cmd → **drawReg**（讓只在 drawReg 的測試終端可被發現）。
- 把 `point_graph_selftest.cpp` 加進 CMakeLists；移除 `cmdstream` selftest 表列（cmd 流現在被整合路徑〔pointgraph/drawop/rendertarget〕覆蓋）。

## 過渡不變式（batch 2→3 期間別踩）

- **production 不註冊任何 draw op**：`registerBuiltinPointOps()` 只註冊 RadialPoints/ParticleSystem(cook) + DrawPoints(**cmd**) + RenderTarget(**tex**) + 其餘點 op。所以 live app 的 `drawReg` 是空的，legacy 路是死碼，零回歸。
- **golden selftest 用 `registerDrawOp("DrawPoints", captureXxx)` 當 capture 接縫**：這是它們讀回 cooked bag 的方式，驗的是生成/變換 kernel 數學、不是 draw 路。batch 4 才把它們遷到 cmd op capture。
- **RenderTarget 是唯一執行者**：cmd 終端 + Points 預覽都呼叫 `texReg()["RenderTarget"]` 把 chain 畫進 `p_->target`。live target 就是顯示貼圖（batch 3 才有解析度釘的獨立 RenderTarget 貼圖）。
- viewExtent 沿用 3.5（== ParticleSystem `kRadius*1.75`），零視覺漂移；batch 3 變 DrawPoints 參數。

## 驗證（本 session）

- build 乾淨、`check-arch` 綠（zone 依賴單向）。
- **32 顆 clean selftest 全 PASS**；pivot 5 顆 bug 變體全 RED（teeth 在）。
- `drawop`：真 RadialPoints→真 DrawPoints(cmd)→真 RenderTarget 執行者→texture readback = 亮環、中心黑、2064 nonBlack px = **live render 路徑親證**。
- `rendertarget`：CPU 點 bag→RenderCommand→RenderTarget 貼圖 readback 亮 + 解析度合約（HD1080→1920×1080、WindowFollow→windowSize）。
- 觸不到的剩項：柏為親手啟動 app 看粒子雲（機器證據已齊；imgui harness eye/map/hand 也綠）。

## 下一步（柏為的決策點，不自動開）

batch 3 = 讓柏為**真的能在介面加一顆 RenderTarget 節點並接線**（= 完成定義）。承重順序（contract 先，葉子後）：
1. `Command` / `Texture2D` port 型別（NodeSpec dataType + 連線色/相容檢查）— 契約層，Opus 自己做。
2. RenderTarget NodeSpec（Command in、Texture2D out、Resolution enum + CustomW/H、ClearColor）+ DrawPoints 補 Command out port。
3. Inspector：Resolution enum 下拉（WindowFollow/HD720/HD1080/4K/Custom，`resolveRenderResolution` 已就緒）。
4. DrawPoints 參數家族（Color/Blend/PointSize/viewExtent→參數）。
