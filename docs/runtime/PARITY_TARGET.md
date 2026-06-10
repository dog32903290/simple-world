# PARITY_TARGET — TiXL 戶口（閘 0）

> 所有「照 TiXL」的引文、稽核、golden、passport，一律錨定在下面這個 commit。
> 沒有這個錨，所有 `檔案:行號` 引文都是對浮動樹的裸指標——誰 `git pull` 一次全體腐爛。

- **Repo**：`external/tixl`（upstream = https://github.com/tixl3d/tixl.git，branch `main`）
- **鎖定 SHA**：`395c4c55ae36c7449cd6459f614fff87cb251010`（2026-06-03）
- **鎖定日**：2026-06-10（全 repo parity 健檢，`TIXL_PARITY_HEALTH_2026-06-10.md`）
- **以此 SHA 驗過的契約**：compound 設計契約（含健檢修正 C1–C5/S1–S20）、CONTRACT_ALIGNMENT_LEDGER banner、POINT_OP_PARITY_LEDGER。

## 規則

1. **嚴禁在 `external/tixl` 順手 `git pull`**。它不是 submodule、git 不會幫你擋。
2. **升級儀式**（要追新版時）：
   a. `git -C external/tixl fetch` 後先 `git diff 395c4c55..<new> --stat` 圈出受影響領土；
   b. 對受影響領土的契約/帳本條目跑 drift 稽核（否證式 re-refute）；
   c. 全部處理完才 re-pin：改本檔 SHA + 鎖定日，並在 `TIXL_PARITY_HEALTH_*` 加一條升級記錄。
3. 新寫的 spec/passport 引 TiXL 源碼時不用重複寫 SHA——預設繼承本檔。
