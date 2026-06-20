# BLUEPRINT — list-routing seam（補縫計劃階段 1 第二塊，~26 解鎖）

> Plan scout a49fe9f 設計（2026-06-20），read-only。SEAM_COMPLETION_PLAN §2 階段 1。
> **build 前置：string-rail 先 commit，本塊 rebase 其上**（同動 point_graph.cpp / graph.cpp / evalFloat）。

## 0. 核心發現（決定整個架構）
`evalFloat`（graph.cpp:161 純遞迴，回 1 個 float）與 host-list cook driver（point_graph.cpp `cookFloatListNode` + Impl::floatListBuf）**是兩個不相通世界**：evalFloat 看不到 floatListBuf（PointGraph Impl 私有）。FloatList 葉子 `evaluate=nullptr` → 下游 Float input 接它走 evalFloat:174 `if(!s->evaluate) return 0.0f` → **評 0.0**。這就是 census「list 算出來下游接不到」+ string-rail fork-6（StringLength.length 存 floatListBuf 但下游接不到）的根因。

**precedent = AudioReaction**：它是 stateful node，Float output 不來自純 evaluate，而來自每幀 cook 寫 `Node::outCache[]`；evalFloat:170-173 有專屬逃生口讀它（resident_eval_graph.cpp:58-65 鏡像）。**橋 = 把這逃生口從「只認 AudioReaction」推廣成「認所有 host-scalar 輸出的 cook-driven 葉子」。**

## 1. 橋架構（FloatList→Float）
**推薦 B+C：host-scalar consumer 寫 `Node::outCache[outIdx]`（鏡像 cookStringLength）+ evalFloat 逃生口放寬。**
- **cook-side**（point_graph.cpp cookFloatListNode 區 ~619-792）：新 host-scalar consumer branch，`cook 上游 list（復用 cookFloatListNode walker）→ 算 scalar → 寫 node->outCache[outIdx]`。terminal dispatch 加 preview 分支（鏡像 StringLength 早攔）。
- **eval-side**（graph.cpp:170）：stateful 逃生口判據從硬編 `type=="AudioReaction"` 放寬成 `s->evaluate==nullptr && isHostScalarOp(type)`（registry 查詢，非再硬編 type 名）。**resident_eval_graph.cpp:58-65 必同步放寬**（否則 flat/resident 漂移）。
- **通道抉擇**：scalar 必須進 evalFloat 讀得到的 `Node::outCache`（非 floatListBuf——evalFloat 讀不到）。floatListBuf transport 保留（StringLength debugCookedFloatList readback 不破）。
- **不走 eval-side 讀 floatListBuf**（候選 A 否決）：evalFloat 在 runtime 葉，不該依賴 PointGraph（破依賴單向 + Impl 私有 + evalFloat 是純函數無句柄）。

## 2. IntList / ColorList 取捨
- **IntList = Float-fold（零成本，本批）**：int→float dissolve（Cut32 全域慣例對 list 元素同成立）→ IntList 共用 floatListBuf。FloatListToIntList/IntListToFloatList 退化 identity/round（fork 具名）。⚠ 假設：無 int-list op 依賴整數溢位/位元語意（開工前逐顆 backward-trace 證）。
- **ColorList = 4×float interleave（層 3 副 seam，延後）**：需 Vec4 output 接線（與階段 3 vec-color-field-output 同類），count=len/4。

## 3. 解鎖分層（~26 切成「橋直接解 ~14 / 副 seam ~12」）
- **層 1 — 純 FloatList→Float 橋（橋一建即解，最大宗 R1）**：FloatListLength(=StringLength 雙胞胎) / PickFloatFromList(index.Mod(count)) / SumRange / AnalyzeFloatList(Min/Max/Mean/AllValid multi-output) / IntListLength / PickIntFromList(int-fold免費) **+ StringLength 補寫 outCache（串 fork-6 下游接線活）**。
- **層 2 — FloatList→FloatList producer（橋無關，cook-rail 擴充 R1）**：CombineFloatLists/MergeFloatLists/RemapFloatList/AmplifyValues/KeepFloatValues + IntsToList/MergeIntLists（int-fold免費）。走既有 cookFloatListNode。
- **層 3 — 疊副 seam（延後）**：ColorList 族（需 Vec4 output）/ ComposeVec3FromList（Vec3 output）/ SetFloatListValue·DampFloatList·SmoothValues（stateful list）/ ValuesToTexture（Cut a4a1827 已部分活）。

## 4. 第一批消費葉子（防 orphan）
1. **FloatListLength**（橋最小證）：FloatsToList([a,b,c])→FloatListLength→Float→**下游 evalFloat 讀到 3.0**（證橋通，非只 transport）。
2. **StringLength 下游接線活**（串 fork-6）：FloatToString→StringLength.Length→Multiply.A→evalFloat(Multiply.out)=length×B。**直接交付 string 家族下游解鎖**。
3. **PickFloatFromList**（選做）：[10,20,30]+Index=4→Mod(3)=1→20.0。
golden 與 string-rail 關鍵差異：**不只 debugCookedFloatList readback，要再接下游 value op 用 evalFloat 讀**（證橋非 transport）。

## 5. fork（具名）
- fork-floatlist-scalar-via-outcache（橋本體）/ fork-intlist-folds-to-floatlist / fork-colorlist-interleave(層3) / fork-evalfloat-stateful-generalized（AudioReaction 逃生口 registry 化）。

## 6. 零回歸（最危險面=改共用 evalFloat）
- evalFloat 放寬必「加法」：現有 value op 全有 evaluate!=nullptr → 永不進新分支 → 位元同構。
- **resident path 同步放寬**（否則 flat/resident 對同 graph 評不同值，refuter 必查漂移）。
- floatListBuf transport 保留（string golden 不破，只加寫 outCache）。
- check_arch：橋「寫」在 cook-side(point_graph.cpp)、「讀」用 graph.h Node::outCache + runtime 內 isHostScalarOp registry（不跨區）。
- RED：hostScalarInjectBug 毀真 cook 輸出（outCache 寫 -999/list 丟末元素）→ 下游 evalFloat 讀錯 → RED。每顆 ≥1 typical + ≥1 boundary（空 list/index 超界/單元素）。

## 7. 合流順序（硬約束）
1. string-rail 先 commit（含 fixer 改完 FloatToString）。
2. list-routing rebase string-rail commit 之上才 build（同動 point_graph.cpp cookStringLength 區 + graph.cpp evalFloat；引用 cookStringLength 作模板）。
3. consumer cook fn 放獨立 leaf .cpp（降撞）；point_graph.cpp 只加最小 dispatch hook；evalFloat 改動集中成一個 isHostScalarOp 查詢點。

## 8. 風險 / 盲區
- **R-1（最硬）：cook 拿不到可寫 Node**（cook 收 const Graph&，橋要寫 Node::outCache）。傾向 (a) const_cast outCache（最貼 AudioReaction precedent，outCache 語意本就「外部每幀 cook 寫」）；備選 (c) PointGraph 自己的 hostScalarBuf + evalFloat callback。**開工前拍板。**
- R-2：cook/eval 時序（AudioReaction 靠 main 每幀先 cook 後 eval；host-scalar consumer 同理，跨 graph cook 順序需 trace main 幀迴圈，scout 未讀 main.cpp 標假設）。
- R-5：resident path 覆蓋——橋改 evalFloat（flat+resident 各一份），只改 flat 會漂移；須兩份同步（擴大「只走 flat」原 seam 範圍，需確認 resident 是否 live production）。
- R-6：fixer 正改 string_ops_stringlength.cpp（comment+port?）→ **rebase 後重讀 StringLength leaf 再定第一批葉子 #2**（"Length" port index 假設以 fixer commit 後為準）。
- R-3：census ~26 是分類估，每顆開工前 .cs backward-trace 校準。

## Critical Files
- point_graph.cpp（cookFloatListNode 區 + cookStringLength 模板 + terminal dispatch；host-scalar branch 落點；⚠ string-rail 同檔 rebase 其上）
- graph.cpp（evalFloat:161-195 stateful 逃生口放寬=橋 eval 端）+ graph.h（Node::outCache:94 通道）
- resident_eval_graph.cpp（:58-65 resident 逃生口同步放寬防漂移）
- floatlist_op_registry.h（consumer leaf 自登記 sink + injectBug RED 鉤）
- precedent 讀不改：string_ops_stringlength.cpp + string_rail_golden.cpp（橋模板）/ floatlist_ops_floatstolist.cpp（producer 模板）/ node_registry_math.cpp（AudioReaction outCache precedent）
