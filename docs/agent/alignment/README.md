# TiXL ⇄ simple_world 對齊 gap + 規格清單

> 來源:`tixl-parity-gap-audit` workflow(2026-06-22,柏為下令徹底清查)。6 區並行,12 agent 唯讀:每區 survey(讀 TiXL 源碼 + 我方碼 → gap+規格)→ verify(每條 gap 對 simple_world 實際碼驗真偽,防 stale)。
> 這是「**還沒做的細節 + 詳細規格**」的 SSOT。每個 area 檔列:真 gap(已驗證未做)/ 部分完成 / verify 戳破的 stale。

## ★ 2026-06-23 完整普查補強(節點外覆蓋 100%)

第一份是**抽樣**(深但不全)。柏為再令「節點外的細節要每個被讀到」→ 跑覆蓋普查(`tixl-nonnode-coverage-sweep`,30 agent 讀光 838 個非節點檔 + 22 漏檔補讀,**覆蓋 100% 機械驗證**):
- **[_COVERAGE.md](_COVERAGE.md)** — 覆蓋證明(838/838 union vs scope)+ 校準(animation/audio 其實已深做,非節點完成度被低估)。
- **[missing-subsystems.md](missing-subsystems.md)** — 抽樣 audit **整塊漏掉**的子系統,★最重 = **Variation/Snapshot/Blend(VJ 現場核心,sw 完全沒有,附完整實作規格)**;另 Gradient authoring / IO 互動層 / Audio 匯出 / 效能 overlay / 外部節拍同步 / SliderLadder…
- **[_sweep-gaps-full.md](_sweep-gaps-full.md)** — 329 條原始 gap dump(供查)。
- **節點(Operators)本輪刻意撇除**:柏為定用到才抄。

## 計數

| 區 | 真 gap | 部分 | stale(已做) |
|---|---|---|---|
| [UI 介面與互動](ui-surface.md) | 9 | 4 | 1 |
| [節點分類機制與 library](node-classification.md) | 8 | 0 | 0 |
| [渲染/輸出頁邏輯](render-output-page.md) | 10 | 1 | 0 |
| [不同的模式](modes.md) | 6 | 2 | 0 |
| [檔案/專案管理](file-management.md) | 5 | 5 | 1 |
| [節點/op 覆蓋缺口](node-coverage.md) | 2 | 7 | 0 |

verify 抓出 **2 條假 gap**(survey 說沒做、其實做了:框選矩形顏色已對 TiXL / .t3 可讀名已用 `name` 欄位達成)——防 stale 的關生效。

## 三個主題(40 條 gap 收斂後的形狀)

**A. 剩的節點是「大島」不是「散葉」。** 360/800 會騙人——好採葉子早採完,剩的幾乎全卡 seam 後面的承重島:numbers 236(list/dict/iterator)/ **render 155**(3D 渲染圖+Layer2d 合成+Execute 命令系統,最大)/ image 127 / point 135 / io 73(=階段6)/ field 23 / mesh 51(僅港7)/ flow 35(控制流整類)。

**B. UI 互動是「範式級」不同,不是細修。** ① 畫布:TiXL 是 MagGraph 磁吸網格(節點固定寬140列高35貼齊共邊),我方是自由擺放 imgui-node-editor。② 節點瀏覽:TiXL 是 namespace 分類樹+多因子相關度排序,我方是扁平清單+純子字串——**我方 NodeSpec 連 category/namespace 欄位都還沒有**。

**C. 做了「編輯器」,缺「演出/輸出」那半。** 無獨立 Player(全螢幕演出執行檔)/ 無 Focus Mode / 無輸出解析度選擇 / 預覽貼圖固定 512×512 從不貼合真輸出 / 無截圖 / 無影片匯出。對 VJ 演出場景,輸出這半實質缺。

## 校準

「45% by 節點」只算節點;**UI 範式 + 演出/輸出**兩維度不在節點計數裡,比 45% 早得多。離「TiXL 完整 clone」比節點數字看起來遠——這次清查把隱形的部分顯形。

## 嚴重度約定

`core` 核心功能/範式 · `important` 重要工作流 · `polish` 細修。建議施作序:先補主題 C 的 core(演出/輸出半)讓它能上台用 → 主題 B 的 core(分類軸+畫布範式,影響所有節點怎麼擺)→ 主題 A 的承重島(render/flow seam)。柏為拍板。
