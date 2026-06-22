# 非節點覆蓋證明 + 校準

> 柏為要的「驗證他們真的有把 TiXL 每個細節都讀到」——這份是機械證明。

## 方法

兩道 workflow + 一次補漏:
1. **抽樣 audit**(`tixl-parity-gap-audit`,6 區 12 agent,深但不全)→ 六區 alignment 檔。
2. **覆蓋普查**(`tixl-nonnode-coverage-sweep`,30 agent,撇除 Operators 的 838 個非節點檔切塊,每 agent 讀光並回報 `files_read`)。
3. **補漏**(22 個漏檔 → 1 agent 補讀)。

## 覆蓋證明(union vs scope）

- 非節點源碼總數(撇除 Operators/tests/templates):**838 檔**。
- 普查讀到:**816**(union 去重,對 832 basename = 810,漏 22）。
- 補漏讀到:漏的 **24 路徑(22 basename）** 全補(都在 `Editor/Gui/Interaction/`:Snapping/Timing/Variations/WithCurves/SliderLadder/ScalableCanvas）。
- **合計:838/838 非節點檔被某 agent 實際讀到 = 覆蓋 100%。**

驗證法:`all_files_read` basename `comm -23` 對 `_all.txt` basename → 列出漏的 22 → 補讀 → 重驗。**不是宣稱,是逐檔比對。**

## 校準(grounded surprises——讀出來的,推翻第一次失敗跑的幻覺)

- **animation/timeline 引擎 + audio 分析其實已做得很深**:`curve.cpp` 連 weighted tangent/tension/outside-curve cycle 都港了;audio 有 capture/playback varispeed/spectrum/AudioReaction/ingest-replay 整鏈。**這兩塊不是缺口。**
- **ColorVariation、timeline 的 snapping/raster/scalable-canvas 數學已 port**(只是沒抽成通用介面)。
- 結論:**非節點子系統比「45% by 節點」看起來完成得多**——跟 UI/輸出那半的不足相反。節點數字嚴重低估非節點完成度。

## 最大發現

第一次 6 區 audit **整塊漏掉的子系統**(普查 + 補漏挖出),最重的是 **Variation/Snapshot/Blend 系統**(TiXL VJ 現場核心,simple_world 完全沒有)。完整清單與規格見 [missing-subsystems.md](missing-subsystems.md);329 條原始 gap 見 [_sweep-gaps-full.md](_sweep-gaps-full.md)。

## 仍未涵蓋(誠實邊界)

- **節點(Operators 1606 檔)本輪刻意撇除**——柏為定:用到才抄(depth-on-demand),census 增量處理。
- 覆蓋證明的是「**檔被讀到**」;「**每個細節都被抽出**」仍受 agent 注意力限制(讀了 ≠ 抽光)。本輪靠普查覆蓋 + critic 雙重降低漏抽,但非零。
