# OUTPUT PIN VIEWER — 接縫契約 (view ⊥ graph)

狀態：**規格 / 未實作**。此文件只定義接縫與可驗收條件，不含實作。
對齊：TiXL `OutputWindow` + `ViewSelectionPinning`（pin 是 session 狀態、不是圖裡的一條線）。
北極星：「我在組什麼」（graph）與「我在看什麼」（view）是兩件事。

---

## 1. 一句話

讓使用者能 **pin 任意節點到觀景窗**，立刻看到那個節點產出什麼，**完全不改任何接線**。
今天我們相反：螢幕顯示什麼焊死在圖裡的 `DrawPoints` 終端，要看別的節點得重新接線。

---

## 2. 地基現況（只讀核對，2026-06-09）

### 已就緒（他已 commit，不必我們動）

- **cook 已參數化**：`PointGraph::cook(g, ctx, reg, targetNodeId)` — `app/src/runtime/point_graph.h:97`。
  target 是**任意節點 id**，不再焊死終端。commit `8da3073 "cook from any target node, not the wired terminal (view ⊥ graph)"`。
- **draw 已資料驅動**：`drawReg()` / `registerDrawOp(type, fn)` / `PointDrawFn` — `app/src/runtime/point_graph.cpp:31-59`。
  `DrawPoints` 註冊成一個 draw op，**可被當成「對任意 Points buffer 的可視化器」獨立呼叫**。
- **預設行為保留**：`defaultDrawTarget(g)` 回第一個 draw 節點，live loop 傳這個 = 今天的行為 — `point_graph.h:99-100`。
- **pin 的未來形狀他已寫進註解** — `point_graph.h:94-96`（"a future pin any node ... just passes another id"）。門開好了。

### 三個缺口（= 本契約要填的）

| # | 缺口 | 位置 | 撞不撞他 |
|---|------|------|---------|
| **C** | `previewTexture()` 還指向舊 monolith `g_particles->target()`，PointGraph 還沒接進 live loop | `app/src/main.cpp:54` | **硬依賴他的 A.1（monolith→PointGraph 通電）** |
| **B** | pin 一個 Points 節點時，cook 現在 `clearTarget()`（畫黑），"no visualizer for a raw Points node yet" | `app/src/runtime/point_graph.cpp:225-226` | **改他的檔，會撞** |
| **A** | 沒有「現在 pin 哪個節點」的狀態；live loop 寫死傳 `defaultDrawTarget()` | app/ui 層（新增） | **不撞，可獨立做** |

---

## 3. 依賴順序鎖（契約層順序）

```
C（A.1 把 PointGraph 通到 previewTexture）  ← 他現在在做，硬前置
        ↓ 通電後
[ B + A ] 一條乾淨實作分支，一路能親手測到畫面亮
```

**結論：在 C（A.1 通電）落地前，不開實作分支。** 現在開會撞 B、卡 C，做完看不到畫面 = 不能親手測 = 不算完工。

---

## 4. 三個接點的可驗收契約

### A — pin 狀態層（app/ui，view ⊥ graph 的守門）

- 新增一個 session 狀態 `g_pinnedNode`（int node id；`0` = 不 pin = 用 `defaultDrawTarget()`），對位現有 `g_selectedNode`（`app/src/ui/editor_ui.cpp:33`，那是 inspector 選擇、不影響渲染）。
- live loop 把 cook 的 target 從 `defaultDrawTarget(g)` 改成 `g_pinnedNode ? g_pinnedNode : defaultDrawTarget(g)`。
- UI：節點上一個 pin 鈕 / 右鍵「Pin to output」；目前被 pin 的節點有視覺標記。
- **鐵律：pin 不進 `.swproj`。** `.swproj` 只存 `toJson(g_graph)`（`app/src/app/document.cpp:49`）。
  先例：裝置設定也 live outside `.swproj`（`app/src/app/audio_settings.cpp:16`）。pin 跟它同類 = 我的視角，不是作品。
- **v1 持久化策略：in-memory only**（關 app 歸零）。是否存進 editor session sidecar（對應 TiXL 的 `.t3ui`）留作後續決定，見 §6。

### B — typed-preview wrapper（runtime point_graph，取代 clearTarget）

- 取代 `point_graph.cpp:225-226` 的「畫黑」：當 target 是 Points-producing op（非 draw 節點），**按其輸出型別**選一個預設可視化器，把它的 output buffer 畫進 `target()`。
- **v1 只填一格：輸出型別 = Points → 重用 `DrawPoints` 的 `PointDrawFn`**（`point_graph.h:60`：`void(*)(PointCookCtx&, MTL::Texture*, const MTL::Buffer* points)`）。
  作法：拿 `drawReg()["DrawPoints"]`，建一個 `PointCookCtx`（nodeId/count/ctx 來自被 pin 節點），對其 output buffer 呼叫。**不蓋新 registry、不新 shader。**
- 架構上留 `outputType → previewFn` 的口（這就是 TiXL typed-OutputUi 的形狀），但**第一版只實作 Points 格**。其餘型別走 §5 的「未支援」行為。

### C — previewTexture 切到 PointGraph（依賴 A.1）

- `previewTexture()`（`main.cpp:54`）改回傳 `PointGraph::target()` 而非 `g_particles->target()`。
- **這由 A.1 主導**，不是本契約搶做。本契約只要求：A.1 通電時，previewTexture 的 seam 形狀保持「回傳 PointGraph 當前 target」，這樣 A/B 接上即亮。

---

## 5. 型別覆蓋（誠實的邊界）

不是每種輸出都能畫——TiXL 也是（沒有 OutputUi 的型別就不顯示）。v1 明確分級：

| 被 pin 節點的輸出型別 | v1 行為 |
|---|---|
| **Points**（RadialPoints / ParticleSystem 的 result） | ✅ 重用 DrawPoints 畫出來 |
| **ParticleForce**（TurbulenceForce） | ⚠️ **未支援**：畫黑 + UI 明示「此型別尚無預覽」。**不崩潰、不殘留上一幀**。 |
| **Float**（Time / AudioReaction / Const…） | ⚠️ 未支援：同上（純量未來可做數字/波形 overlay，非本期） |

「未支援」是明確契約行為，不是 bug。

---

## 6. 驗收條件（柏為親手測得到，非 selftest 綠）

C 通電後，實作分支完成的判準：

1. **pin RadialPoints** → 螢幕顯示原始 emitter 的點環（還沒被模擬推動的樣子）。
2. **pin ParticleSystem** → 顯示模擬後的點（drag/gravity 作用過）。
3. **不 pin（或 pin DrawPoints）** → 完全回到今天的最終畫面（零行為改變）。
4. **pin TurbulenceForce（Force 型別）** → 畫黑 + UI 標示「尚無預覽」，**不崩、不殘影**。
5. **切換 pin** → 不用碰任何接線，畫面即時換成另一個節點的產出。
6. **view ⊥ graph 守得住**：存一個 `.swproj`、用文字編輯器打開，**裡面找不到任何 pin 資訊**；把專案給別人開，他看到的是他自己的視角（v1 = 預設終端），不是我 pin 的那個。

---

## 7. 明確不做（守住簡單，不長對角線）

- ❌ 不蓋完整的 typed-preview registry——只填 Points 一格，留口給未來。
- ❌ 不改 graph 序列化 / `.swproj` 格式——pin 永不進圖。
- ❌ 不新增 shader / draw kernel——B 重用既有 DrawPoints。
- ❌ **不在 C（A.1 通電）落地前開實作分支**——避免撞他的檔、避免對著沒通電的管線寫。

---

## 8. 開放問題（標假設，動手前要釘）

1. **pin 持久化**：v1 in-memory；要不要做 editor-session sidecar（對應 TiXL `.t3ui`，重開同專案記得上次看哪）？— 假設先不做。
2. **Force/Float 可視化器**：未來各自一格 previewFn（力場箭頭、純量波形）——非本期，但 §4-B 的 `outputType → previewFn` 口要為它們留好。
3. **多 draw 節點**：圖裡有兩個 DrawPoints 時，`defaultDrawTarget` 取第一個；pin 可指定任一。需確認 A.1 的 `defaultDrawTarget` 行為與此一致。
4. **count 來源**：B 重用 DrawPoints 時，`PointCookCtx.count` 取被 pin 節點的 output count（`point_graph.cpp` 內 `outCount[node]`）——實作時核對。

---

## 9. 落地時的觸發信號

當 `app/src/main.cpp:54` 的 `previewTexture()` 改成回傳 PointGraph 的 target（= C 落地、A.1 通電），
即可開實作分支，照 §4 接 A+B、§6 驗收。在此之前，本文件是唯一交付。
