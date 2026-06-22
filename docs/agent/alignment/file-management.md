# 檔案/專案管理 — TiXL parity gap + 規格

> 來源:tixl-parity-gap-audit workflow(2026-06-22,6 區 12 agent 唯讀清查;每條 gap 對 simple_world 實際碼 verify 過真偽)。
> 計數:真 gap 5 · 部分 5 · stale已做 1。

## 真 gap(已驗證確實未做)

### [important] 一檔一 symbol（.t3）+ 命名空間資料夾 vs 單一 .swproj 巨檔

- **對齊規格**:刻意 divergence（已具名 S20 named fork，compound_save.h:21）。記為 divergence 而非 bug。回收條件＝協作/大專案 merge 衝突痛點出現。對齊（若日後收）：改資料夾式 package（每 compound 一檔），或保留單檔但繼續強化 per-symbol 穩定排序＋最小化 diff（目前已 sort by id + omit-at-default，部分達成）。短期建議：不動，在 doc/SEAM 記錄回收條件。
- **驗證證據**:確認單一檔：compound_save.cpp:127 libToJsonV2 把整個 lib（所有 compounds）序列化進一個 root；:255 saveLibToFile 寫單一檔案。document.cpp:191-224 doSaveAs/doSave 寫單一 .swproj（filter 只有一個 swproj 副檔名）。但這是 compound_save.h:21 明寫的『S20: single-file library — named fork』刻意決策，非 bug。已有部分 diff 最小化：compound_save.cpp:154 symbols sort by id、各段 omit-at-default、connections/animator/annotations 皆排序。

### [important] child / slot / symbol id 用 int/字串，TiXL 一律 Guid

- **對齊規格**:刻意 divergence（已具名，compound_save.cpp:162-164 nextChildId 防 id 復活；TiXL 用 Guid 天生免疫）。對齊（若 Guid 化）：child id 改 string Guid、移除 nextChildId 與其載入回填、copy-paste 跨 symbol 直接保留 id；風險＝現有檔全 int id 需遷移層。建議短期不動，但驗 combine/paste 跨 symbol 路徑 int id 不會撞（add 路徑已有 nextChildId 守，paste 路徑需另驗）。
- **驗證證據**:確認 child id 是 int：compound_graph.h:73 `int id`、:48 `kSymbolBoundary=0`；compound_save.cpp:173 `co["id"]=(number)c.id`。symbol id/slot id 是 string（compound_graph.h:30,113）。nextChildId 防 id 復活機制存在且序列化：compound_graph.h:124 `int nextChildId=1`、compound_save.cpp:164 寫出、compound_load.cpp:156-157 讀回。atomicUuidTable（compound_save.cpp:19-44）給 atomic type 固定 UUID。這是已具名決策（int child-id 是 per-symbol 區域命名空間，需 nextChildId 地板）。

### [polish] .t3ui child UI 欄位（Comment/Size/Style/SnapshotGroupIndex/ConnectionStyleOverrides/CollapsedIntoAnnotationFrameId）未存

- **對齊規格**:逐欄：Position 已有。缺：(1) Comment—若 UI 有節點附註功能就補 child.comment；(2) Size+Style—需確認我方節點是否支援多 style，無則延後；(3) CollapsedIntoAnnotationFrameId—我方 annotation 框選是 live 幾何查詢非持久 membership（annotation.h:6-9 已具名 fork），記刻意 divergence，但 child 收納 flag 是 annotation.h:9 自承的『批 B/C territory』待做；(4) SnapshotGroupIndex—我方無 variation 系統，延後；(5) ConnectionStyleOverrides—延後。建議只補我方 UI 已實作但沒存的（最可能 Comment/Style），其餘標 missing-by-design 或 feature-not-yet。
- **驗證證據**:確認 SymbolChild（compound_graph.h:72-102）只有 id/symbolId/overrides/x,y/name/isBypassed/disabledOutputs/triggerOverrides/strOverrides——無 comment/size/style/snapshotGroup/connectionStyle/collapsedInto 任一欄。compound_save.cpp:171-202 child 寫出段也無這些。grep child 段：只命中 annotation.h:9 提到 CollapsedIntoAnnotationFrameId『lives on SymbolChild's UI side... 批 B/C territory』＝明確標為尚未實作的後續批次。Position(x/y) 已有（compound_save.cpp:191-192）。

### [polish] per-symbol Description/SymbolTags/Links/TourPoints（.t3ui 後設資料）未存

- **對齊規格**:確為 missing。這些是 operator-library 後設資料，主要服務 TiXL 內建 op 文件系統與 Add 選單分類；我方 op 註冊在 C++ registry。建議：Description（symbol 說明）可能對 compound 命名有用可考慮補；Tags/Links/TourPoints 標 feature-not-yet（除非有對應 UI 功能否則不為存而存）。記 missing-by-scope。
- **驗證證據**:確認無對應碼：grep compound_graph.h / compound_save.cpp 對 description/symbolTags/tourPoint/links 零命中。Symbol 結構（compound_graph.h:112-136）只有 id/name/atomic/inputDefs/outputDefs/children/connections/nextChildId/animator/annotations——無任何 op-library 後設資料欄位。

### [polish] compositionPath（當前檢視位置 / breadcrumb）TiXL 持久化，我方 session-only

- **對齊規格**:確為 missing（刻意，document.h:18-19）。TiXL 把 compositionPath + 導覽歷史持久化以回復檢視位置。對齊（若要）：存進使用者層 session/settings（非 .swproj 本身，TiXL 也分開存，view state 不污染 symbol 定義檔）。屬 polish（不影響資料正確性，只影響『重開回到上次看的地方』）。建議延後，歸到 user-settings 持久化（FILE-11）一起做。
- **驗證證據**:確認 session-only：document.h:18-19 明寫 g_compositionPath『Session/view state — never serialized』；compound_save.cpp 整個 libToJsonV2 不含 compositionPath。唯一出現序列化的地方是 main.cpp:296，但那是 verify 系統的 state.json 寫出（eye::writeText），給驗證 agent 讀的快照，非專案持久化。導覽歷史（keymap.cpp:320-321 s_navBack/s_navFwd）也是 static in-memory、session-only，不落盤。

## 部分完成(做了一半)

### [important] 輸入值/override 只有 float rail，TiXL 是 type-tagged 多型值

- **剩什麼**:剩下的真缺口比上一棒小很多：(1) on-disk 沒有 type discriminator——override/def 只憑 inputDef.dataType（讀回時用 refSym->inputDefs 比對）解析，沒在 value 旁標型別，跨 enum-reorder/型別變更時靠 dataType 對位即可，目前可運作；(2) resource 型（Points/Texture2D/Command/Field/Mesh/Gradient/Curve）本就是 wire-only、無可序列化的 inline 值，TiXL 也寫 null，我方根本不存 override＝等價。建議：保留現有 float+string 雙 rail（已涵蓋所有 scalar/vec/color/bool/enum 的實際需求），不為 parity 補 type-tag 物件格式（會是純樣板成本，無丟值風險）。若日後出現『單一 port 帶不可分解的多值且非 N-Float 分解』的新型別才需回頭。
- **現況證據**:確認 override rail 是 float-only + string-only：compound_save.cpp:181 `ov[kv.first]=finiteOr0(kv.second)`（float）、:186-189 strOverrides（string）；slotDefToJson:57 `def`=float、:59 `strDef`=string。BUT 上一棒「任何非純量參數一律無法持久化＝存檔丟值」的前提錯了。grep 全 runtime：沒有任何 Bool/Int/Vec2/Vec3/Vec4/Color 當作『單一帶值 input 的 dataType』——value-carrying input 只有 "Float" 與 "String"。graph.h:18-26 註解明寫『Vec = a run of consecutive Float ports (ids <base>.x/.y/.z/.w)... a vector is just N scalars wearing one widget so the buffer/save/value-spine model is unchanged』；node_registry_draw.cpp:27 等處 Color 實際是 4 個 `{"Color.x","Color","Float",...,vecArity=4}` 全 dataType==Float。Bool/Int 走 Widget::Bool/Enum（129 檔用），graph.h:13 明寫『Enum/Bool still store a float in params』。所以 vec3/color/bool/int 都已能透過 float rail 持久化，不丟值。

### [important] per-symbol ProjectSettings 多欄 vs 只存 bpm+soundtrackPath+volume

- **剩什麼**:確為 partial。真正 core 缺口＝per-symbol vs library-level 歸屬（TiXL 每 compound 可有自己的 ProjectSettings，靠 breadcrumb 往上找；我方掛 lib 根）。缺欄：AudioSource/Syncing/AudioGain/Decay/BeatLocking/BeatLockOffset/operator-volume/mute/resync-threshold/Export 區。建議：先確認 simple_world runtime 實際消費哪幾欄（目前只 bpm+soundtrack），未消費的記為『TiXL 有但我方 runtime 無對應功能』延後，不為 parity 存死欄位。
- **現況證據**:確認只存三欄且掛 lib 根（非 per-symbol）：compound_save.cpp:142-148 comp 只寫 bpm/soundtrackPath/soundtrackVolume，掛在 root["composition"]；compound_graph.h:153 `CompositionSettings composition` 是 SymbolLibrary 的成員（library-level，非 Symbol 的成員）。compound_load.cpp:125-139 讀回三欄＋bpm clamp [1,999]。TiXL 的 Audio 區（operatorVolume/mute/resync）與 Playback 區（audioSource/syncing/gain/decay/beatLock）我方都沒有。audio_settings.{h,cpp} 只持久化『輸入裝置 UID』（device selection prefs，savePrefs/loadPrefs），不是 TiXL 的 Audio composition block。

### [polish] input/output UI（GroupTitle/Usage/Description/Min/Max/Scale）只在 .t3ui，我方 slotDef 只存 boundary x/y

- **剩什麼**:確為 partial：boundary position 已 parity（compound_graph.h:40-43 已具名 inline）。缺參數面板 UI 後設資料：GroupTitle（inspector 分組）/Usage（控制項型式）/Min/Max/Scale（拖曳範圍——atomic 在 PortSpec 有，但 compound boundary slotDef 沒存）/Description（tooltip）。對齊需先確認 simple_world inspector 是否支援這些（分組/範圍/dropdown）；有則補 slotDef.min/max/scale/groupTitle，無則標 feature-not-yet。
- **現況證據**:確認 slotDef（compound_graph.h:29-44）只有 id/name/dataType/def/strDef/x/y。x/y 是 boundary node canvas 位置（compound_graph.h:40-43 明寫＝TiXL IInputUi.PosOnCanvas 的 single-file inline 等價），compound_save.cpp:61-62 寫出。缺 GroupTitle/Usage/Description/Min/Max/Scale——grep slotDef 全無。NOTE：min/max/scale 其實存在於 runtime 的 PortSpec（graph.h:31 `minV/maxV`、Widget），但那是『code-defined 在 registry』，NOT 持久化進 slotDef（compound symbol 的外部 port def 不帶這些）。

### [polish] FormatVersion 機制：有版本號但無 TixlVersion 字串 / 無 change-log / migration 表

- **剩什麼**:確為 partial（核心 formatVersion+newer-warn+v1→v2 遷移已運作）。缺：(a) app-version 字串欄位（低成本，存檔時寫 build 版本，警告時可告知用哪版存的）；(b) 集中 change-log/migration 表（未來加版本時遷移邏輯會散落）。建議補 app-version 字串 + 在 compound_save.h 集中記每版格式變更。屬 polish。
- **現況證據**:確認核心機制已 parity：compound_save.cpp:136 寫 `formatVersion=2`；compound_load.cpp:108-114 legacy v1（無 formatVersion）→ libFromGraph 遷移；:117-119 `fmt>2` 印 newer-than-app 警告但不擋。缺：(a) 未寫 app/build 版本字串——grep appVersion/TixlVersion/app_version 全 src 零命中；(b) 無集中 change-log/migration 表——grep FormatChange/Changes[] 零命中，版本處理只散在 compound_save.h:17-21 的散文註解與 load 各容錯點。

### [polish] 無 recent-files / window-layout / user-settings 持久化

- **剩什麼**:修正為 partial（不是全 missing）：已有 audio device UID 的檔案式 prefs（audio_settings.cpp），但無 recent-files、無 window-layout、無 timeline/render/編輯器層 user-settings。對齊需把現有的零散 prefs（audio device）擴成統一 user-settings 持久化層（與 .swproj 分離，TiXL 也分離）：最近開啟專案、視窗版面（imgui ini 機制可掛，目前刻意關）、timeline/render 設定。屬 polish/體驗層，不影響資料 parity。建議與 FILE-10 合併成一條『editor session/settings 持久化』seam，待資料層（FILE-01/04）穩固後再做。
- **現況證據**:大致確認 missing，但有一塊已存在的 prefs 層需修正『完全沒有』的說法。window-layout：app_delegate.cpp:115 `io.IniFilename=nullptr`——刻意不讓 imgui 落盤版面，確認無 window-layout 持久化。recent-files：grep recent 全 src 零命中（keymap 的 navBack/navFwd 是導覽歷史非 recent files）。BUT user-settings 並非全無：audio_settings.cpp:17-62 有檔案式 prefs（prefsPath/savePrefs/loadPrefs，ofstream/ifstream）持久化『音訊輸入裝置 UID』跨 launch——這是一個已存在的小 user-settings 持久化點。TiXL 的 timeline/render/recording/window-visibility settings 我方無。

## verify 戳破的 stale(survey 說沒做、其實做了)

### .t3 寫人類可讀名稱註解（/*Name*/），我方無

- 確認 TiXL 的『Guid 旁附可讀名稱註解』動機在我方已用 name 欄位達成：compound_save.cpp:160 `o["name"]=s->name`（symbol 可讀名）、:179 `co["name"]=c.name`（child 可讀名，非空才寫）。且我方 compound id 是真實字串 id、child id 是 int，本身就比 Guid 可讀。crude_json 也不支援 JSON 註解語法。所以 TiXL『因 Guid 不可讀才需註解』的前提在我方不成立——等價人讀性已透過 name 欄位 + 可讀 id 達成。
