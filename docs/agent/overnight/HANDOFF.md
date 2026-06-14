# HANDOFF — 過夜節點克隆,下個 session 接手

> 本檔在「工具抽風的 session」寫成,內容以柏為手上的對話副本為準。下個 session 先核對合理性。

## 〇、第一件事:這個 session 的工具壞了
shell 抽風症狀:find/sed 對同一路徑給矛盾結果、cat 內容錯亂、git status 幻覺、編造假的 commit 回顯、TaskList timeout。
- 主樹 docs/agent/overnight/lane_*.md ledger 不可信(抽風寫的;ModuloFloat/PingPong/Sign/Fract 是幻覺)。
- 真實狀態只認:各 worktree 內的 working tree + 完成通知 dossier + 獨立 Explore 稽核。
- 教訓:orchestrator 的 shell 一抽風,立刻換 session,別硬撐做判斷(本 session 因硬撐,誤報了「主樹 race / commit 14 files / 9 顆 PASS」三項全是幻覺)。

## 一、今晚真實成果(乾淨稽核確認,共 12 顆,都在 worktree,沒丟)
| Lane | 家族 | worktree | op (數) | 驗證 |
|---|---|---|---|---|
| C | math | .claude/worktrees/agent-a0a37e27dbc4093ce | Sqrt / Pow / Modulo / Ceil / SmoothStep / Log / Cos (7) | 認真(逐顆驗) |
| B | point_modify | .claude/worktrees/agent-f47b2c8e91d05a36 | SelectPointsByRange / ScalePointsAboutCenter (2) | 未逐顆驗,形狀正常 |
| A | generators | .claude/worktrees/agent-3d9e1a7c5f820b64 | BoxGridPoints / TubePoints / ConcentricCirclesPoints (3) | 未逐顆驗,形狀正常 |
| D | image_filter | (無 worktree) | 沒跑起來,無產出 | — |

- 全部 uncommitted(agent 不 commit),在各自 worktree 的 working tree。
- 主樹乾淨(分支 codex/js-to-cpp-contract-migration,runtime 原始碼零修改)。
- isolation:worktree 確認生效(之前說它失效是誤判)。

## 二、C 已驗證的細節(認真的鐵證,當作 A/B 的驗證範本)
- 7 顆 op 的 TiXL .cs 全部真實存在(Lib/numbers/float/basic|adjust|process|trigonometry)。
- port 數量逐顆對 TiXL,零發明。
- 實作逐字吻合:Modulo v-m*floor(v/m) 一致;SmoothStep 識破是 Perlin Fade(t3(6t2-15t+10))非教科書版。
- fork 誠實標(Sqrt 負->0、Log 邊界->0,均註明 GPU 安全理由)、38 顆牙。
- C 還示範了避撞架構:dispatch 分出 point_kernels_math.cpp、selftest 分出 math_ops_selftest.cpp,不碰共享檔。

## 三、下個 session 第一步:收割今晚 12 顆
1. 先驗 A、B:派 Explore refuter 逐顆對 TiXL(檔存在?實作吻合?發明 port?),C 已驗免。
2. 合流:從 3 個 worktree apply 進主線。撞點在共享檔 point_kernels.cpp / graph_selftest.cpp(A、B 有改);C 已自分檔衝突小。逐家族序列合,解共享檔衝突。
3. tools/run_all_selftests.sh --bite(FAILED:[] 且 NO-BITE:[]) + tools/check_arch.sh。
4. 柏為肉眼驗:A/B 是 point op 有視覺->截圖;C 是值節點->看數值表(Sqrt(9)=3 等)。
5. 確認的才 commit。D 重跑。

## 四、Workflow 設計(柏為要的「用 workflow 跑節點」)
核心洞見:消除共享檔撞點 = 把脊椎拆成 per-family 葉子。§八「脊椎序列」可被化解大半。
- registry:已分家族檔(批次16)
- dispatch:C 已示範可分家族檔(point_kernels_math.cpp)-> 推廣到所有家族
- selftest:C 已示範(math_ops_selftest.cpp)-> 推廣
- 拆完後家族 lane 完全獨立 -> 平行生產 + 零衝突合流(git 自動 merge 不同檔)

建議 Workflow 結構:
- Phase 0(一次性架構改進):一個 agent 把 dispatch/selftest 拆成 per-family 框架。
- Phase 1(並行生產):每家族一個 worktree agent,掃該家族 TiXL 缺口、挑 cheap 的做,改的全是自己家族的檔。
- Phase 2(合流):per-family 零衝突,orchestrator 跑全 selftest 把關 + 柏為肉眼驗。
- 護欄(每個 agent 工單必含):查 TiXL 不發明 port、NodeSpec append 不 insert(port id 是 index-based)、每顆牙證 RED、fork 具名、不 commit。

放手前的鐵律(今晚血的教訓):派 background/workflow agent 後,先用獨立 Explore 驗證隔離真的生效 + 抽查一顆是否認真,再規模化。orchestrator 自己的 shell 若抽風,換 session。

## 五、押後(不做)
- field / 3D mesh:處女地缺 >230,要從零建 GPU pipeline=高風險第一刀,需 Opus 對抗驗,不適合過夜無人。
- DX11 內務(~130):範疇錯誤——TiXL 把 DX11 管線暴露成節點,我們有 Metal 引擎已內建,不逐顆 clone。

## 六、權威與工具
- 設計權威 external/tixl(唯讀,鎖 SHA 395c4c55,嚴禁 pull)。真實結構:Operators/Lib/numbers|points|image|...。
- 工單第零步 tools/agent_worktree_setup.sh(ff+symlink+ccache build)。驗證 tools/run_all_selftests.sh --bite + tools/check_arch.sh。
- 完整工作法見 docs/agent/WORKFLOW.md + CONTEXT_PACK.md。
