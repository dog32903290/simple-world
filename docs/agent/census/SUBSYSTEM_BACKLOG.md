# SUBSYSTEM_BACKLOG — L1/L3/L5/L6 子系統開採 backlog（按狀態分桶）

> 與 [OP_BACKLOG.md](OP_BACKLOG.md) 同位：OP_BACKLOG 是 **L4 節點**（~800 op）的 pull-list，本檔是 **非節點子系統 lane（L1/L3/L5/L6）** 的 pull-list。
> **目的**：把 alignment 普查的「真 gap / 部分 / stale」欄、L5/L6 施工藍圖、MASTER_PLAN lane-state 三處散落的待採項，重切成「下一顆從這裡 pull」的狀態分桶表——讓這四條 lane 像 L4 一樣，採集時不必每次從敘事重推 work-list。
>
> **這是 derived dashboard，不是 SSOT。** 規格肉的 SSOT 仍是 `alignment/`（gap+規格）+ `census/L5_*/L6_*_BLUEPRINT`（施工設計）+ git/碼（DONE 真偽）。本檔只負責「狀態 + 還缺什麼一句 + blocker + 來源指標」。**採前必對 git/碼 ground-truth**（debt/spec 會 stale，見 memory `gate-or-it-rots` / `sw-rotation-bug-gate`）。
>
> **桶**：A=DONE（git 證）→ B=READY-NOW（harness 已綠、踩已建地基、現在可採）→ C=BLOCKED（按擋住的 seam / sync-point / 跨 lane 依賴分組）→ D=柏為殘留 / by-design SKIP（裝置 + 手感 + 刻意 divergence，異步不擋）。
> **嚴重度**：`core` 範式/承重 · `important` 重要工作流 · `polish` 細修（沿用 alignment/README 約定）。
>
> **範圍**：L1/L3/L5/L6（MASTER_PLAN line 69「零依賴，立刻開」四 lane）。**L2（UI 範式）不收**——taste-gated + 與 `ui/tixl-node-skin` worktree 協調，不適合 autonomous pull-list；**L4=OP_BACKLOG**。

---

## L1 — Variation / Snapshot / Blend（新子系統 + app/document override）

> SSOT：[alignment/missing-subsystems.md](../alignment/missing-subsystems.md) §CORE-Variation（5 子規格，附 TiXL 源碼依據）。
> **★ 全 stack 目前 harness-only、零 production wiring**——「引擎完成(machine-verified)」≠「接線完成」。下表 READY 全指**引擎葉**（golden 可機器驗），真正「驅動 app」在桶 C。

### 桶 A — DONE
| 項 | 內容 | git | 嚴重度 |
|---|---|---|---|
| Variation harness | springDamp + mixFloat golden，TiXL 公式 byte-faithful | `10e7845` | core |
| snapshot pool + 2-way crossfader | delete-then-capture / EnabledForSnapshots filter / scan-by-activationIndex；midi 127→blendAmount，springDamp 20/(1/60)，\|vel\|<0.0005 commit+flip | `b970758` | core |
| document-override bridge | `buildBlendTowardsVariationCommand`（graph-facing undo-able override，Lerp/undo 忠實，infra-only） | `7eb0081` | core |

### 桶 B — READY-NOW（引擎葉，踩 value-graph + ChangeInputValueCommand，golden 可機器驗）
| 項 | 還缺什麼 | seam/地基 | 風險 | 嚴重度 | 來源 |
|---|---|---|---|---|---|
| **N-way 加權 Mix** | per-param 正規化加權平均按型別分支（float/vec2/3/4）+ 缺值 fallback 當前值；套用走 MacroCommand of ChangeInputValueCommand（undoable） | value-graph + mixFloat golden 範本（已有） | R1-R2 | core | missing-subsystems §4 ExplorationVariation.Mix |
| **scatter RNG** | `value += random(-scatter,scatter)*ParameterScale*ScatterStrength`；per-param ScatterStrength/Scale/Min/Max/ClampMin/Max | 同上（疊在 Mix 上） | R2 | important | missing-subsystems §4 |
| **on-disk JSON pool** | `ToJson/TryLoadVariationFromJson`；per-symbol/per-document pool 檔；讀檔逐一驗 input 仍存在+型別仍對、跳過 ExcludedFromPresets、缺失不爆 | round-trip golden（仿 `--selftest-asset-ref`） | R1 | important | missing-subsystems §1 Variation.cs |
| **SnapshotActions 格位語意** | 格位陣列（數字鍵/MIDI pad）；每格「有→套，沒→存當前」，修飾鍵覆蓋，另鍵刪；直接套用時設 active 格 + 清 blend 對側 | value-graph（純狀態機） | R1 | important | missing-subsystems §5 SnapshotActions.cs |
| NIT: untracked-param→DefaultValue blend | TiXL 主動把未追蹤 param 拉向 DefaultValue（`b970758` leaf 留著不動）→ document-override batch 補 | 接 document-override bridge | R1 | polish | MASTER_PLAN `b970758` 2 NIT ① |
| NIT: startBlendingTowards 歸零 velocity | TiXL 只在 target 變時重置 dampedWeight；接 live spring loop 時補 | live spring loop | R1 | polish | MASTER_PLAN `b970758` 2 NIT ② |

### 桶 C — BLOCKED
| 項 | 擋在哪 | 嚴重度 | 來源 |
|---|---|---|---|
| **「Variation 真正驅動 app」** | frame-loop **live-driver**（每幀讀 crossfader→寫 graph）+ **UI handler**（ui/ 域）+ 柏為 UI 手感簽收。非 L1 引擎 lane 能單獨關閉。 | core | MASTER_PLAN line 9 |
| **active pool 跟 UI 焦點** | EnabledForSnapshots 旗標要綁真 composition 子節點 + UI 焦點決定 preset/snapshot pool + 擋 `Lib.` 共用 op。需 node flag（graph.h additive）+ ui/ 焦點。 | core | missing-subsystems §2 VariationHandling.cs |

### 桶 D — 柏為殘留 / taste
- **preset canvas UI**（PosOnCanvas/thumbnails/FindFreePositionForNewThumbnail）— ui/ 域 visual 簽收。
- **MIDI/OSC live blend 控制** — 需 L5 真裝置 + 柏為現場（loopback 那半在 L5 已綠）。

---

## L3 — 檔案 / 專案管理（app/ document）

> SSOT：[alignment/file-management.md](../alignment/file-management.md)（真 gap 5 / 部分 5 / stale 1）+ missing-subsystems §AssetLibrary。
> **★ 本 lane 大半是刻意 divergence + polish**——核心資料 parity（存載 round-trip / asset-index）已綠，剩的多在桶 D（named fork，記回收條件不主動採）。

### 桶 A — DONE
| 項 | 內容 | git | 嚴重度 |
|---|---|---|---|
| compound save/load v2 round-trip | `runSaveV2SelfTest`（.swproj byte-stable，sort by id + omit-at-default） | 既有 | core |
| image asset-reference round-trip | `--selftest-asset-ref`（LoadImage `Lib:` key + float params 存載 byte-stable + CJK key + 篡改 RED） | `829a698` | important |
| asset-index 資料模型 | 列舉 `Lib:` keys（dedup）+ missing-asset predicate（resolver fn-ptr 注入、headless 可測） | `cf16065` | important |
| relink mutation | `relinkAsset` 改寫 missing key across instances + soundtrack（golden 4 + `-bug` RED） | `18673b1` | important |

### 桶 B — READY-NOW（app/ document，round-trip harness 已存在）
| 項 | 還缺什麼 | 採前確認 | 風險 | 嚴重度 | 來源 |
|---|---|---|---|---|---|
| **per-symbol ProjectSettings 擴欄** | 現只存 bpm+soundtrackPath+volume 掛 lib 根。缺：per-symbol 歸屬（breadcrumb 往上找）+ AudioSource/Syncing/AudioGain/Decay/BeatLocking/BeatLockOffset/operator-volume/mute/resync/Export 區 | **先確認 runtime 實際消費哪幾欄**（未消費的記「TiXL 有但 sw 無功能」延後，不為 parity 存死欄） | R1-R2 | important | file-management.md 部分①ProjectSettings |
| FormatVersion app-version 字串 + 集中 migration 表 | 存檔寫 build 版本字串；compound_save.h 集中記每版格式變更（現散在散文註解） | — | R1 | polish | file-management.md 部分④FormatVersion |
| .t3ui child UI 欄（Comment/Style） | 只補「sw UI 已實作但沒存」的（最可能 Comment/Style）；其餘標 missing-by-design | sw 節點是否支援多 style / 附註功能 | R1 | polish | file-management.md 真gap③ |

### 桶 C — BLOCKED（需 ui/ 或先確認 inspector 能力）
| 項 | 擋在哪 | 嚴重度 | 來源 |
|---|---|---|---|
| **AssetLibrary 資源瀏覽器** | 拖資產→自動建 load-op、點相容資產→改寫 op FilePath（undoable）、OS 檔操作。**資料底座（asset-index）已 DONE**，缺的瀏覽 UI 在 ui/（L2）。 | important（柏為 authoring） | missing-subsystems §POLISH-AssetLibrary |
| input/output slotDef metadata | GroupTitle/Usage/Min/Max/Scale/Description 只在 .t3ui。需先確認 inspector 是否支援分組/範圍/dropdown，有才補 slotDef。 | polish | file-management.md 部分③ |

### 桶 D — by-design divergence / SKIP（記回收條件，不主動採）
- **單一 .swproj 巨檔 vs per-symbol .t3 資料夾**（S20 named fork，compound_save.h:21）— 回收條件＝協作/大專案 merge 衝突痛點。
- **int/字串 child-id vs Guid**（named fork，nextChildId 防復活）— 回收條件＝Guid 化遷移；採前驗 paste 跨 symbol int id 不撞。
- **compositionPath / breadcrumb 持久化**（session-only by design）— 併入 user-settings seam（FILE-11）。
- **recent-files / window-layout / user-settings 持久化**（FILE-10/11 seam）— 把現有零散 prefs（audio device UID）擴成統一層，待資料層穩固後做。
- per-symbol Description/SymbolTags/Links/TourPoints — op-library 後設資料，sw op 註冊在 C++ registry，missing-by-scope。

---

## L5 — IO / 硬體（platform/）

> SSOT：[census/L5_IO_HARNESS_BLUEPRINT.md](L5_IO_HARNESS_BLUEPRINT.md)（loopback 施工）+ missing-subsystems §IMPORTANT-IO 互動層。
> **注意**：io **節點**（~73）在 OP_BACKLOG（B7 + io seam 群）；本 lane 是 IO **基礎設施 / 互動層**（loopback harness / LiveSource 綁定 / 裝置驅動），不重複。

### 桶 A — DONE
| 項 | 內容 | git | 嚴重度 |
|---|---|---|---|
| OSC loopback + 虛擬 CoreMIDI | localhost UDP + virtual MIDI machine-verified 半（refuter 抓 MIDI channel off-by-one 0→1-based 已修） | `5364ff8` | core |

### 桶 B — READY-NOW（platform/，loopback harness 已綠）
| 項 | 還缺什麼 | seam/地基 | 風險 | 嚴重度 | 來源 |
|---|---|---|---|---|---|
| **LiveSource→graph 參數綁定** | app 層 hook：`registerIoLiveSources`，OSC/MIDI `lastValue/lastCcValue` → LiveSource register → 綁 node parameter（normalize 0..1）。**S1 已完成 → EvalContext binding 可靠。** | SourceRegistry（已存）+ S1 ✅ | R1-R2 | core | L5 BLUEPRINT §4.1 |
| network-io 通用 UDP 原語 | 抽出 TCP/UDP/WebSocket/HTTP 底層（macOS Network.framework）。**建通用 UDP＝osc/artnet-dmx/camera-tracking 三族 io 節點工作量大減**（那些節點在 L4，但底層 infra 在此 lane） | macOS Network.framework | R1 | important | SEAM_GRAPH #12 network-io |

### 桶 C — BLOCKED（需 L1 Variation）
| 項 | 擋在哪 | 嚴重度 | 來源 |
|---|---|---|---|
| **IO 編輯器互動層** | 8 種 MIDI 裝置 note/LED map（按硬體鍵觸發 snapshot、燈號回饋當前狀態）、SpaceMouse HID 3D 相機、IO Events 視窗顯示錄製中 DataSet。**綁在 Variation/Snapshot 上 → 卡 L1。** 柏為硬體 VJ 控制真實工作流。 | core | missing-subsystems §IMPORTANT-IO 互動層 |

### 桶 D — 柏為殘留
- **真裝置**（實體 MIDI controller / phone / serial / video-in / audio-out）走同 decode path — 裝置半，異步簽收。

---

## L6 — 音訊匯出 + 維運（platform/audio + ui/）

> SSOT：[census/L6_MAINT_HARNESS_BLUEPRINT.md](L6_MAINT_HARNESS_BLUEPRINT.md)（維運施工）+ missing-subsystems §Audio匯出/§效能/§外部節拍/§BPM偵測。

### 桶 A — DONE
| 項 | 內容 | git | 嚴重度 |
|---|---|---|---|
| auto-backup | asset-sibling bundle soundtrack + disk-derived index（重啟安全）+ ms-timestamp + .pending atomic | `dea9155` | important |
| backup retention + restore | binary-thinning survivor-set bit-exact（268K samples 0 mismatch）+ crash-recovery restore + soundtrack path-relink | `96ef27b` | important |

### 桶 B — READY-NOW
| 項 | 還缺什麼 | seam/地基 | 風險 | 嚴重度 | 來源 |
|---|---|---|---|---|---|
| **★ audio 匯出 / 離線 mixdown**（★狀態剛升）| 離線 audio mixdown（配影片匯出）+ live recording→WAV + FFT→waveform PNG 縮圖。藍圖標「S1 依賴」，**但 S1 已 `44234aa` 完成 → 從 BLOCKED 升 READY。**（暫停即時器、建臨時 mixer、消音軌——仿 TiXL AudioRendering） | platform/audio；S1 ✅ 解鎖 | R2-R3 | important | L6 BLUEPRINT §3；missing-subsystems §Audio匯出 |
| **perf overlay / FrameTimeGrader** | cook/resident + cook/dynamic ms + node/child count + audio rms/spectrum[0..7]；P99 評分 + mini-graph + VSync toggle + CSV。**60fps 演出直接相關（卡頓即翻車）** | ui/；統計來源全已存（frame_cook 計時 / audio_monitor） | R1-R2 | important | L6 BLUEPRINT §2.2；missing-subsystems §效能 |
| **外部節拍同步** | tapProvider+bpmProvider 抽象 + sync mode 切換（Timeline vs Tapping）+ OSC `/beatTimer` listener（UDP，每拍一訊號）；Tap 同步 TriggerSyncTap/ResyncMeasure | **L5 OSC ✅**（loopback 綠） | R2 | important | missing-subsystems §外部節拍同步 |
| **BPM 自動偵測** | 25 秒低頻能量滑動 buffer→smooth→對 80..180 BPM 自相關能量差掃描 + FocusFactor（偏好當前值防跳）；「別每 frame 算」 | audio_monitor（已有 RMS/spectrum） | R2 | important | missing-subsystems §BPM偵測 BpmDetection.cs |
| console log 視窗 | ImGui 視窗（類 output_window），debug output sink | ui/ | R1 | polish | L6 BLUEPRINT §2.3 |
| NIT: backup golden production-incremental | 現 golden 測 one-shot，補 production-incremental tooth | 接 auto-backup | R1 | polish | task_6e64a956 |

### 桶 D — 柏為殘留 / taste
- perf overlay visual 手感簽收、console UI 手感（ui/ taste）。
- startup crash-recovery prompt（ui/ 域）。
- `-minimal` backup toggle（單檔 N/A）。
- chip task_8a55df9b（perf_overlay）。

---

## 維護紀律

1. **DONE 桶以 git 為準**——加 DONE 項必附 commit；採前 grep 碼確認沒 stale（memory `gate-or-it-rots`）。
2. **狀態升降要追**——seam/脊椎完成會把 BLOCKED 項升 READY（如本檔 L6 audio 匯出隨 S1 升）。每次脊椎 commit 後掃一遍桶 C 看誰該升。
3. **本檔不放規格肉**——一句「還缺什麼」+ 來源指標即可，肉在 alignment/ + 藍圖。膨脹成第 4 份 copy = 反模式。
4. **pull 動作**：orchestrator 組批時從桶 B pull 未阻塞 + 不撞檔項；L1/L5/L6 桶 B 多動 platform/app 新檔，與 L4 動 point_graph/registry **不撞**，可並行。
