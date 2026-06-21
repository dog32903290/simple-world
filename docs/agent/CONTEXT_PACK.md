# CONTEXT PACK — 每個 subagent 工單的第零份讀物

工單只需寫：「讀 docs/agent/CONTEXT_PACK.md，然後 <你的任務>」。
這份檔取代每個 agent 自己重新導航 30-50k tokens 的開銷。**過時內容比沒有更毒：每批結帳時校一遍。**

## 一、鐵律（ARCHITECTURE.md 濃縮，違者 check-arch 咬）
- 五區：`runtime`(純計算)/`app`(產品行為)/`ui`(imgui)/`platform`(原生接口)/`verify`(眼耳手)。
- 依賴單向：ui→app→runtime；app→platform；葉子(runtime/platform/verify)不往上、不互相。
- verify 是葉：業務碼只留一行 hook，肉在 verify/。
- 單檔 <400 行（爆了沿職責縫拆）；每子系統有 `--selftest-*`；重複樣板改資料驅動。
- 設計權威 = `external/tixl`（唯讀，嚴禁 pull，鎖 SHA 395c4c55）。分岔照 TiXL；fork 必具名（註解+回報）。
- 上螢幕 UI 字串一律英文（imgui 無 CJK atlas）；註解中文 OK。

## 二、工具（工單第零步～驗證一條龍）
```bash
# worktree agent 第零步（ff 舊基底+symlink third_party+ccache build；熱編 ~3s）
bash "<主repo>/tools/agent_worktree_setup.sh"
# 全表 selftest + 牙咬合掃描（kTable 直接解析，永不漂移）
tools/run_all_selftests.sh --bite        # 必須 FAILED:[] 且 NO-BITE:[]
# 架構絆線
tools/check_arch.sh
# 活體驅動（mtime-wait；touch+sleep 禁用）
tools/sw_drive.sh shot clean|full|map    # 截圖/座標表
tools/sw_drive.sh do "click 242 48"      # hand 指令（hand_pending 輪詢到排空）
tools/sw_drive.sh state                  # state.json（timelineSelection 等機讀）
# 活體牙（可重放 scenario；活體可證的行為交付時要附 .scn）
tools/sw_scenario.sh run tests/scenarios/<x>.scn
```

## 三、雷區（血債清單，踩過的才進來）
- **殺 app**：用 sw_scenario.sh 的 repo_pids 模式（cwd 歸屬判定）。pkill cmdline 比對已三度燒傷
  （`simple_world$` 漏 --open 實例／絕對路徑漏相對啟動／裸 `build/simple_world` 誤殺 worktree）。
- 多實例同寫 `.eye` = pos/map 亂跳；ASan app 死得慢，殺完要等真死再起新的。
- hand：`keychord cmd Z` 非 ctrl（Mac imgui 把 Cmd 換進 io.KeyCtrl）；DragScalar 雙擊編輯後
  `text` 是**附加**不覆蓋（先 `keychord cmd a`）；碰到視窗的 drag 之後 map 全是幻影，必重抓。
- 點節點要點**標題**非中心（@SYM^ 語法）；節點被浮窗壓住時右鍵會被遮擋閘吃掉（挑沒重疊的）。
- 點 canvas 空白會反選節點→Inspector 變空；undo/Delete 要對應視窗焦點；full.png 是 retina 2x。
- rg 會把搜尋詞渲染成 `n`：查識別字用 grep/Read。zsh 不斷詞：迴圈用 while read。
- pipe 後 `$?` 是 tail 的。`git apply -3` 會 stage 進 index。Cmd+S 走 NSMenu（hand 打不到；C/V 不攔）。

## 四、交付規格（dossier——你的報告就是下一棒的上下文，省它重讀）
回報必含：
1. **改動點**：檔:行 + 一句為什麼。
2. **TiXL 依據**：檔:行 + **原文引述 2-5 行**（refuter 先對引文，存疑才開 TiXL=省三遍重讀）。
3. **fork 具名**：每一條與 TiXL 的分岔 + 理由。
4. **牙清單**：每顆牙咬什麼回歸 + injectBug 紅證。
5. **測試尾段**：run_all --bite 輸出 + check-arch。
6. **疑慮/盲區**：你沒把握的地方明講（refuter 的攻擊清單從這裡長）。
- worktree agent 另附：worktree 絕對路徑 + 改動檔清單。**不 commit**，合流歸 orchestrator。

## 五、驗收閘（品質是結構撐的，與模型無關）
牙（--bite 零 NO-BITE）→ refuter 否證（**含參數覆蓋閘：golden 必碰 NodeSpec 每個宣告參數，未覆蓋的 refuter 逐一點名＝BLOCK，旋轉 bug task_eef5757e 同類；柏為 2026-06-22 批准。路徑覆蓋 flat＋resident 為難兄弟，暫靠判斷**）→ scenario 回歸 → orchestrator 親手復跑後才 commit。
活體可證的新行為，交付清單含對應 `.scn`（與 selftest 牙同地位）。
