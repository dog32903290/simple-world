# 預設值對齊軸（loop 第3軸）施工圖 — scout 2026-07-09

> 柏為 loop 第3軸「每個節點預設值跟 TiXL 一樣」。工具 `tools/default_value_parity.sh`+`default_value_parity.py`：sw 側=`--dump-nodespec <Type>`（可信）、TiXL 側=`.t3` default（SSOT）、join=精確 param name。**漂移 census SSOT=工具現算，別信本檔數字**（會 stale）。

## 可信度分層（scout 手核 295 條）
- **float 176 / enum 26 / string 38＝name-matched 可信、可一行修**（PortSpec aggregate-init 的字面值，golden-safe∵golden 都顯式 override 參數）。identity: float 113atom/47flat/16nonspec、enum 19/7/0、string 36/0/2。
- **float-vs-zero 53 + string-vs-zero 1＝地雷，別盲修**：join miss 拿 sw 值比製造的 0。手核四桶：
  - **9 sw-internal**（`_ForceKind`×8 pinless kernel discriminator `force_params.h:251`、疑 `TriggerAnim.VariableName`/`TimeDisplace.ArraySize`）→ **whitelist 出 gate，非修**。
  - **22 比對器 join-miss、sw 已對** → **修工具（別名/大小寫）不是修節點**：TiXL 錯字 `Constrast/ForgroundRatio/ImageBrightess`、大小寫 `a/b↔A/B` `.X/.Y↔.x/.y`、值相符改名 `CollapseVertices.Strength≡Amount`/`SetRequestedResolution.Multiply≡ScaleResolution`/`ShardNoise.Sharpness≡Sharpen`/`SubdivideLinePoints.InsertCount≡Count`/`Blob.Fill≡Color`/`KeyColor.KeyColor≡Key`。
  - **20 真結構缺口＝HOLD 獨立調查**（sw 暴露 TiXL 現行 `.t3` 沒有的 param）：`DefineMaterials`(BaseColor/Roughness/Specular)、`RaymarchField`(Background/Glow/Specular)、`DrawSphereGizmo`(Rings/Segments)、`SetPointAttributes`(Stretch.xyz) 等——是刻意 surplus 還是 stale-vs-舊TiXL 版？需讀，別批量修、別 whitelist。
  - **~2-3 真修**（如 `DrawLineGrid.SegmentsX/Y` 8→0）→ 併該節點檔修。

## Lane 結構（harness=`default_value_parity.sh <Type>` 前後 0 drift；lane 邊界=registry 檔不重疊）
- **L6 工具修（先，oracle 誠實化）**：`default_value_parity.py` 大小寫 join + 改名/錯字別名表 + `_ForceKind` whitelist → 免費消 ~22+9 假陽性。**2026-07-09 派中（af96bea5…）**。
- **L1 net/io**：`node_registry_math_net_io.cpp` 17 節點（Artnet/Sacn/Serial/Tcp/UDP/WebServer/WebSocket/DMX），純字面、atom、headless、零 golden 風險＝**最安全起點**。
- **L2 stateful anim**：`node_registry_math_stateful.cpp` 12（Damp*/Ease*/Spring*/FreezeValue/DeltaSinceLastFrame）；Damp/Spring 預設影響動態手感→修後 smoke。
- **L4 generators**：`node_registry_generators.cpp`(+`_extra`) GridPoints/HexGridPoints/LinePoints/RadialPoints/SpherePoints/RepetitionPoints；flattened-adjacent 但現修安全。
- **L5 string ops**：`string_ops_*`/`stringlist_ops_*` ~15，純字面複製**除** `ValueToRate.Rates`/`SequenceAnim.Sequence`/`JoinStringList.Separator`（TiXL 用 `\n`、sw 空白分隔，需先讀 parser）。
- **L7 結構 HOLD**：上述 20 條，獨立調查 spin-off。

## 排序 / 避撞退場軸
- 151 漂移節點=99 atom / 42 flattened / 10 nonspec。退場現卡 kernel-porting（flattened 短期不退）→ **修預設值非白工**。但改 NodeSpec/registrar 與未來退場同家族撞→**先修 99 atom（永久零撞）**，再 flattened/nonspec。
- 起手＝**L6 工具修 ∥ L1 net/io**（檔不重疊）。golden 風險低（goldens 顯式 override 參數，非賴 inspector default；除非 golden 斷言 dump-nodespec 輸出本身＝本 survey 沒發現）。
