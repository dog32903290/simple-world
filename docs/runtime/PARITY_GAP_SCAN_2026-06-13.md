# PARITY GAP SCAN — 2026-06-13（批次11 四路掃描綜合）

四路 Explore（op 庫/UI 視覺/互動快捷鍵/子系統）對照 external/tixl（鎖 SHA 395c4c55）的
缺口總帳。**這份是批次12 起的 Resume 候選原料庫**；單批選 3-5 項照施工圖 Resume 段走。
掃描原始報告未存檔（subagent 上下文），錨點已抽查（playback 鍵真缺/double-click 假缺/
S4 文檔錨點全真）；**採信規則：動工前 implementer 仍要親開 TiXL 源碼複核錨點**。

## 已過濾的假缺口（掃描器報缺、實際已有——別重做）
- double-click compound 鑽入/背景 double-click 出（批次3 N3）
- canvas 右鍵選單（combine 批次5 + Add Node submenu 批次10-B1）、param 右鍵 Animate（批次7）
- node rename（右鍵 dialog，批次6）；Cmd+Z/C/V/S、Delete、snap Shift、tangent 把手（各批）

## A. op 庫（我方 17 顆 vs TiXL ~500+；image filter 族 0 顆）
權威錨點：Operators/Lib/。既有盤點：POINT_OP_PARITY_LEDGER.md / PARITY_TARGET.md。
1. **Blur（高斯）= 第一顆 image filter**＋Texture2D gather 直通（修C 後 production 入口
   已開）——Cut 16 點名，解鎖 55+ filter 全線。次選：Displace / ColorGrade（後者依賴 Curve 型別）。
2. **Math 基礎 fan-out**（Add/Sub/Div/Clamp/Remap/Abs/Floor/Lerp…）：~150 顆缺口的頭批，
   資料驅動表量產（一顆=葉檔+registry 一行）；每顆仍要對 TiXL 源碼（attribute/邊界語義）。
3. Point 族補完（draw: DrawLines/DrawBillboards；modify: AddNoise/FilterPoints/SelectPoints；
   組合: BlendPoints[lane A 既有 queued]）——GPU 面大，逐批。
4. Particle 力族（DirectionalForce/VectorFieldForce…17 顆）；Flow 族（Switch/Loop，閘 walker 碼化）。

## B. UI 節點視覺（路線 B：視覺也一模一樣；錨點=Editor/Gui/MagGraph/*）
節點本體配色已對齊（bg/border variation 公式一致，node_draw.cpp）。缺口按可見度：
1. **canvas 雙層網格**（MagGraphCanvas.Drawing.cs:377-426；UiColors.CanvasGrid=(0,0,0,0.15)，
   zoom fade ramp）——小工小見效大。
2. **連線型別色**（DrawConnection.cs:32-42；ConnectionLines variation b=1,s=1,op=0.8 乘
   型別色；型別色表=UiColors.cs:96-104）＋hover/選中加粗。
3. **節點圓角 5px 隨 zoom 縮放**（DrawNode.cs:126，<0.5x 關）vs 我方常數 3px；hover 邊框
   Blink 動畫。
4. **idle fade**（60 幀無 output 更新→暗 60%，DrawNode.cs:49-50）——需 cook 端「有更新」訊號，中工。
5. **Annotation 註解框**（DrawAnnotation.cs 全套：資料模型+繪製+拖/改名/收合+存檔）——子系統級，中大工。

## C. 互動/快捷鍵（~117 action 我方接 ~6；錨點=Interaction/Keyboard/FactoryKeyMap.cs）
1. **Playback 鍵**：Space toggle/L 正放/J 倒放/K 停/Shift+←→ frame-step（cs:24-34）——
   已驗證全缺；transport API 全在，純接線。
2. **Cmd+D duplicate**（節點＋timeline keys；cs:14）——copy/paste 命令已有，小工。
3. **F focus selection**（鏡頭框住選取；cs:13）＋ G auto-layout（cs:52，演算法中工）。
4. **Cmd+F / double-click 空白 = quick-add 節點面板**（cs:56）——中工，作曲速度關鍵。
5. 第二梯隊：keyframe 鍵（C/B/N）、Alt+←→ 導航、bookmark/layout F1-F10、P pin output。
6. 結構債：TiXL 是 UserActions enum+KeyMap 表+context flags 中央制；我方 io.KeyCtrl 散打。
   接快捷鍵超過 ~10 個時改資料驅動表（鐵律7），別繼續散。

## D. 子系統級（戰略層，多週級；錨點已驗存在）
按柏為工作流價值：①Fullscreen Output Window（ProgramWindows.cs；演出/驗收）②真實資源
載入（圖片/視訊，ResourceManager.Graphics.cs；RESOURCE_LIFETIME_CONTRACT.md 已有規劃）
③獨立 Player（Player/Program.cs；發佈）④Preset/Variations（VariationsWindow.cs；設計探索）
⑤MIDI/OSC（MidiInConnectionManager.cs；已有 bespoke POC）⑥多 project/namespace。
單批塞不下；進場前先開專屬 spec 批（契約先行照工作法）。

## 批次12 拍板（orchestrator 定，照「排修優先→北極星推進→小工大見效」）
- **lane I（Opus）**：Blur op＋Texture2D gather 直通（A1）
- **lane V（Sonnet）**：視覺小包=網格+連線型別色+圓角縮放+hover（B1-B3；不碰快捷鍵分發）
- **lane K（Sonnet）**：互動小包=playback 鍵+Cmd+D+F focus（C1-C3 前半；快捷鍵改資料驅動表起步；不碰 canvas 繪製）
- **lane 序列**：math fan-out（A2）排 lane I 合流後（registry 中央套撞檔）。
