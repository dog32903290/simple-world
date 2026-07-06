# FABLE_VERIFY_REPORT — 原子節點健檢終帳（2026-07-06）

> 執行 `FABLE_VERIFY_WORKORDER.md`。10 個獨立審計 agent 平行掃（A 軌 Tier 0-4 + 新節點五 lane + B 軌前哨），
> 每個 TiXL 錨都開原檔對行（引用審計），關鍵期望值獨立重算。orchestrator 親驗所有動刀前的宣稱。
> 基線：sweep 691/0/0、NO-BITE 空、lint 綠。終帳：全部修復後 sweep 綠、lint 綠（見 commit）。

## Tier 0 校準（管線有沒有牙）

種毒實驗：BoxSDF 本體公式 ×1.1（乘法毒——只有 probe 離開零水平面才咬得到，同時測 P2）→
golden 三 probe 全紅（diff 4.9e-02）；對照組 gradient 保持綠（紅是特異的）；復原即綠。**管線有牙，且 probe 不坐恆等點。**

## 判決總表（每檔一行，格式照工單）

### Tier 1 承重葉子（9 顆：8 乾淨 1 帶病）
```
value_op_animvec3_golden.cpp    | 乾淨(P2窄縫註記)  | AnimVec3.cs:28-40+AnimMath.cs:37-52(手推4值全中) | Bias=0.5/Ratio=1 全 case 恆等(SchlickBias 臂無牙,家族性)
value_op_perlinnoise3_golden.cpp| 乾淨(P3嫌疑判合法)| PerlinNoise3.cs:16-46+MathUtils.cs:16-41(15 want 獨立Python重推,4顆位元級全中) | :155 want-flip=合法路徑分支(翻後值=CASE B錨定真值)
pointlist_golden.cpp            | 乾淨 | RadialPointsCpu.cs:38-58/LinePointsCpu.cs:23-31/LinearPointsCpu.cs:45-48/RepeatAtPointsCpu.cs:35-48+Point.cs:27-35 全親驗 | -
composevec3fromlist_golden.cpp  | 帶病(P2窄+註釋反) | ComposeVec3FromList.cs:37-48+SpringDamp(MathUtils.cs:484-498,DAMP軌跡位元級重算全中) | ✅已修:damp種子=raw f(cs:41-42)非remapped;補LEG E非恆等remap牙(0.986125 vs 錯種0.5)+改反話註釋
floatlist_golden.cpp            | 乾淨 | FloatsToList.cs:15-21+transport恆等不變量 | -
floatlist_animfloatlist_golden.cpp| 乾淨 | AnimFloatList.cs:32-46(含cs:43詭異+offset時移項,親驗TiXL真的這樣寫) | 同 anim 家族 Bias/Ratio 恆等縫
floatlist_smoothvalues_golden.cpp | 乾淨 | SmoothValues.cs:18/33-54(8 leg 全閉式重算) | -
colorlist_golden.cpp            | 乾淨 | ColorsToList.cs:15-22(4-parallel-channel 已申報 fork) | -
colorlist_fanout_golden.cpp     | 乾淨 | ColorList.cs:16-20(sparse checkout 外,git object store 撈原檔對行)+CombineColorLists.cs:14-33+ReadPointColors.cs:39-43 | -
```

### Tier 2 有狀態（5 顆：4 乾淨 1 帶病；跳過 particlefield_probe=自承故意 RED）
```
particle_sim_integrate_parity_golden.cpp | 帶病(P2+假default宣稱) | ParticleSystem.hlsl:106,111(逐字核實) | ✅已修:補 Speed=2 探針(pow-form 終於有牙;Speed=1 時 pow≡linear 恆等);假宣稱「0.02=.t3 default」改真(.t3=0.005);★牽出 production 漂移已修(下)
audio_playback_golden.cpp       | 乾淨 | AdsrCalculator.cs(六幀env逐幀重推全中)+AudioPlayer.cs+PlayAudioClip.cs:51-67 | 工單「無錨」判斷=錯,錨在;剩檔頭補行號(順手級)
afterglow_golden.cpp            | 乾淨 | AfterGlow.t3 全鏈 GUID 親驗+DecayRate ratio 0.75 閉式 | -
afterglow2_golden.cpp           | 乾淨(checklist缺口) | 結構同上;0.75 錨存在但檔內沒引行號(審計員已補驗 AfterGlow2.cs:16-17→.t3:377-381) | 補引註即可
advancedfeedback_golden.cpp     | 乾淨 | FeedbackAdjustImage.hlsl:148 逐字全中+AdvancedFeedback.t3:419-424 | Displace.hlsl 引行漂1-3行(無實害)
```

### Tier 3+4 簡單 op 批掃＋SDF 抽樣（16 顆：15 乾淨 1 帶病；SDF 抽 3 放 24——抽樣非全覆蓋）
```
buildrandomstring/string_builder/blendstrings/valuestotexture/valuetorate/swaptextures/
loadsvgastexture2d/drawlinegrid/drawspheregizmo/connect_cooks/selectpointswithsdf/texttopoints | 全乾淨 | 各檔 TiXL .cs 行號親驗(string_builder 用 git show 撈 TypeOperators 原檔) | texttopoints=TiXL-absent 破例,錨=閉式 fixture 幾何,LEG6 誠實標 smoke
readpixel_golden.cpp            | 帶病(P1) | 閉式 byte-exact readback | ✅已修::106-112 did-not-trip 改 return 0
field_ops_boxframesdf/cappedtorussdf/repeatpolar(抽樣3) | 乾淨 | .cs Globals helper 逐字 replica+.t3 default 親驗+手算 probe 全中 | 「幾何不變量」錨型成立→同型 ~24 顆放過(未逐顆掃)
```

### B 軌前哨（4 顆）
```
resident_mixed_multiinput_golden.cpp | 乾淨(P3形式判合法red-first) | 宣告序不變量+resident_eval_flatten.cpp:258-315 單pass親讀 | ★記憶「閘沒補」已stale——閘在(selftests_graph.cpp:55),真咬序
t3import_radialgradient_golden.cpp   | 帶病(P1變體)→✅已修 | .t3 GUID×5 對行全中+RadialGradient.hlsl:46-60 | 已非 smoke(三層真parity);死牙 exit 2 已改 0(家族12顆全修)
t3import_remapcolor_golden.cpp       | 同上→✅已修 | GUID 全中+ColorRemap.hlsl:42-43 | 同上
t3import_bubblezoom_golden.cpp(快掃) | 修的還在 | MID-FIELD pin (48,32) 坐發散中段,血案已制度化 | -
```

### 新節點 · io-video/CV（7 顆全乾淨）
```
io_video_timing/transform/stream/pointscan/camera_calibrate/camera_checkerboard_golden + video_decode_selftest.mm
| 乾淨 | PlayVideo.cs/VideoDeviceInput.cs/VideoStreamInput.cs/Video2DPointScanner.cs:374-418/CameraCalibrator.cs 全 line-exact;期望值逐個手算複核 | ChessboardSize 假標籤已修(下)
5febddc 的 video-capture 牙=真(packBgraFrame 是 production 碼,牙腐蝕真 stride step)
```

### 新節點 · io-net 協定（9 顆：5 乾淨 4 帶病→已修/申報）
```
io_visca_protocol_golden.cpp | 乾淨 | ViscaCamera.cs 行號全對,期望 byte 全硬編碼 | -
io_onvif_digest_golden.cpp   | 乾淨 | OnvifCamera.cs:706-714+digest 常數 Python 獨立重算吻合 | -
io_ptz_send_golden.cpp       | 帶病(P5輕)→✅已修 | ONVIF digest 外部常數 rzdNqUL++OuapYSPw8Ui1xx1ZUQ=(orchestrator 親算驗證)釘死原本只驗「28字元」的弱檢查
selftests_io_ws_live.cpp     | 乾淨(誠實live harness) | RFC6455 accept 常數硬編碼+真接縫 | env-skip 假咬(工作項)
selftests_io_socket.cpp      | 帶病(P3×3+P5窗)→✅已修 | 三顆 want-flip 改腐蝕送出端+want固定;masked frame 補 RFC6455§5.7 原版線上 bytes(81 85 37 fa 21 3d 7f 9f 4d 51 58);env-skip 假咬改誠實 return 0
selftests_io_dmx.cpp         | 帶病(錨引用偽造+未申報fork)→✅fork已申報 | byte 值逐欄對真 .cs 幾乎全對 | ✅fork-sacn-sync-root-vector 申報(sw 0x08=E1.31 規格正確,TiXL SacnOutput.cs:378 寫 0x04=上游bug);假行號重引→chip task_950c0031
selftests_io_misc.cpp        | 乾淨(1 fork 該申報) | FreeD/PSN/W2F 行號全真,0xAE checksum 手推吻合 | FreeDOutput 28B 上游 bug 的 fork 註(工作項)
selftests_http.cpp           | 乾淨 | RequestUrl.cs:19+LoadImageFromUrl.cs:25-28,bug 接縫真 | -
selftests_net_nodes.cpp      | 帶病(輸出edge牙錨向未拍板) | input 牙好 | ★拍板項(下)
```

### 新節點 · midi/osc/dataset（7 顆：4 乾淨 3 帶病→已修/申報）
```
selftests_io_nodes.cpp       | 帶病(1牙P5+1無牙)→✅已申報 | MidiInput.cs:208-210 等行號全對,Remap/pitchbend 手算全中 | ✅sysex cadence=named fork(TiXL toggle 幾乎確定上游bug,sw edge-gate 申報);✅MidiNoteOutput 無牙+stale 註釋改誠實(補牙=debt)
device_input_golden.cpp      | 乾淨 | KeyboardInput.cs:22-43 等全對;g_anyBite 是全批最好的 P1 寫法 | -
midiclip_golden.cpp + dict_ops_midiclip_golden.cpp | 帶病(P5+假引用,key scheme 兩軸)→✅已修 | SMF fixture=規格硬編碼(真) | ★真產品 parity bug 已修(下)
dataset_golden.cpp           | 乾淨(全批最扎實) | DataSetCache/MidiDataRecording/SimulateIoData/TimeRangeMapping 行號全中,IsInside 左開右閉邊界牙正中 | -
selftests_datapointconverter/importexport.cpp | 乾淨 | ParseCsvRecord:292/ParseRecord:281-309 等正中;LEG5 誠實繞開 TiXL lossy round-trip | 一處引用偏12行(化妝品)
```

### 新節點 · audio/link（7 顆：4 乾淨 3 帶病→已修）
```
audio_mixer_golden.cpp   | 乾淨 | OperatorAudioStreamBase.cs:344/:337;07-01 修的牙確認真咬(audio_mixer.mm:296/:299) | -
playbackfft_golden.cpp   | 乾淨(正名) | PlaybackFFT.cs:21-43 對行 | 它是「路由 golden」非 FFT 數學(sentinel 選對陣列);FFT bin 本身=spectrum_analyzer 地盤
spatialaudio_golden.cpp  | 帶病(孤兒脊椎)→✅已申報 | SpatialOperatorAudioStream.cs:257-269 等對行,閉式親算全對 | ✅golden+registry 檔頭改誠實:「math library only,節點 evaluate==nullptr 零 production 呼叫者」;接 cook 時必走 golden'd 函式
tonesynth_golden.cpp     | 帶病(P2局部wrap假牙)→✅已修 | 公式 verbatim 命中(行號 stale 50-77 行) | ✅sine 是週期函數分不出 floor/fmod;換 saw@-1.0 真 wrap 牙(+0.5453520911 vs fmod -1.0547)
setplayback_golden.cpp   | 乾淨(本批範本級) | SetPlaybackTime/Speed.cs 逐句 verbatim(行號漂3-9) | -
bpm_transport_golden.cpp | 乾淨 | SetBpm.cs:38-39 EXACT;PlaybackUtils.cs 錨=樹外檔(Editor 未 vendor,明標本地不可驗) | -
link_sync_selftest.cpp   | 帶病(P3-lite bug2)→✅已修 | AbletonLinkSync.cs:66-74 EXACT+閉式拍速手驗;真 vendored Link 過 dylib 邊界 | ✅bug2(測試側swap)拔除,bug1(真anchor腐蝕)扛咬
```

### 新節點 · 視覺/mesh/相機（12 顆：11 乾淨 1 帶病→已修）
```
keepintexturearray_golden.cpp | 乾淨 | KeepInTextureArray.cs:43/:112/:120-126;ring probe ri=1/2/3 離端點 | -
timedisplace_golden.cpp  | 乾淨(P2-lite註記) | TimeDisplace.hlsl:43-47 逐行 | 灰底非整數 probe(工作項)
pbr_currency_golden.cpp  | 乾淨 | SetPointLight/SetFog/SetMaterial.cs 全對 | 檔頭「NO consumer」當天已 stale(工作項:升級-bug走真seam)
pbr_shading_golden.cpp   | 帶病(P1,keystone 上)→✅已修 | pbr.hlsl+pbr-render.hlsl 逐行轉錄全對,oracle 親算(208,156,104) | ✅:239 did-not-trip 改 return 0;措辭「seam is hollow」進 lint 詞彙
actioncamera/blendcameras/camerawithrotation/camera_value_golden | 乾淨 | 全部 .cs 對行+矩陣 16 元素親手重算;轉置鐵則無違 | camera_value=本批最扎實
mesh_cube_uv/icosahedron_uv/delaunay/loadobj_golden | 乾淨 | CubeMesh.cs:308-442/IcosahedronMesh.cs:355-719/空圓性質親驗/LoadObj.cs:115-169+TBN | delaunay case4 誠實 smoke 有標
```

### 新節點 · anim/stateful frame-cook（10 顆：9 乾淨 1 帶病→已修）
```
keyframes/easekeys/sequenceanim/wastrigger/randomchoice/delayboolean/adsrenvelope/datetimeinsecs/once
| 乾淨 | FindKeyframes.cs/EaseKeys.cs/SequenceAnim.cs/WasTrigger.cs/RandomChoiceIndex.cs/DelayBoolean.cs/AdsrEnvelope.cs(git 確認上游 commit 非自錨)/DateTimeInSecs.cs/Once.cs 全對行,ease/envelope 值手推復算 | easekeys 引註偏一函數(InOut vs In);once 預設值註釋反向(工作項)
frame_cook_triggeranim_selftest.cpp | 帶病(錨假×1)→✅已修 | TriggerAnim.cs:58/130-144/168/179-182 | ★HasCompleted latch parity bug 已修(下)
```

## ★ 真產品 parity bug（本次健檢的魚，全部已修+targeted 綠/咬）

1. **MidiClip dict key 兩軸假錨**（`midi_smf.cpp/.h` + 兩顆 golden）：sw 產 `/channel0/C4`，真 TiXL
   （NAudio v2.3.0，TiXL Core.csproj:32 釘的版本）產 `/channel1/C5`——octave 無 -1（NoteEvent.cs:157）
   ＋channel 1-based（MidiEvent.cs）。下游對 key 的 .t3 圖會整條斷。修：runtime 兩軸+golden keys+
   LEG1b 斷言全對齊；審計 agent 抓包時直接驗了 NAudio v2.3.0 tag 原始碼。
2. **TriggerAnim HasCompleted pulse→latch**（`stateful_value_ops_anim2.cpp`）：TiXL 只在 trigger 沿清
   false（cs:60）、完成設 true（cs:141）=latch；sw 每 frame 重設=pulse，未申報 fork 卻掛 TiXL authority。
   修：state s[5] 持久化+沿上清除；golden f5 want 0→1。
3. **ParticleSystem Drag 預設漂移**：sw registry+cook fallback=0.02，TiXL .t3=0.005（t3:33-34 親驗）。
   golden 假宣稱「0.02=.t3 default」幫漂移蓋章。修：兩處改 0.005+golden 出處註釋改真。
4. **CameraCalibrator ChessboardSize 預設漂移**：registry 7×6（=.cs InputSlot default），.t3 DefaultValue=16×9
   （親驗）。registry 檔頭自稱 mirror .t3 → 改 16×9+兩顆 golden 假標籤改真。
5. **fork-sacn-sync-root-vector 申報**：sw sync packet root vector 0x08=E1.31 規格正確；TiXL SacnOutput.cs:378
   寫 0x04（上游 bug）。原本躲在假引用 "(cs:1455)" 後面（檔案只有 637 行）——fork 已具名申報兩處。

## ★ Harness/閘 補強（gate-or-it-rots）

- **golden_lint.sh 三重補強**：①glob 從 `app/src/*_golden.cpp` 擴到 `app/src/app/` + `app/src/runtime/`
  （41 顆 golden 原本整批在 P1 硬閘與 P3 --audit 之外）；②觸發詞彙 +`toothless`+`tooth cannot bite`+
  `seam is hollow`（兩個實際逃逸樣本的措辭）；③窗邏輯認得三元 `? 1 : N`（t3import 家族的 exit-2 型）。
- **34 顆 golden 死牙極性歸位**：t3import×12（`return bites?1:2`→`1:0`）、field 家族×19（注入點失蹤
  →return 0）、readpixel、pbr_shading、point_ops_group/transform。牙死掉時 --bite 的 NO-BITE 名單
  從此看得見。
- **io_socket**：3 顆 want-flip 改腐蝕送出端；masked frame 從自洽窗升級成 RFC6455§5.7 規格 bytes；
  env-skip 假咬改誠實 no-bite。
- 修復過程自傷記錄：批次腳本曾把 field 家族「correctly RED」的合法 return 1 誤翻成 0（post-polarity
  sweep NO-BITE 爆 19 顆=自傷非真死牙），已精準回填並以 targeted+全 sweep 復驗。**那 19 顆牙本來就是活的**
  （Tier 0 種毒可證）；家族真正的病只有注入點失蹤時的遮蔽極性。

## 拍板項 → 已裁定（柏為 07-06 委任：「選對我有利的」）

- **Artnet/Sacn/DMX/WLED 輸出 held 語義 → 照 TiXL 走 level-streaming（已修，同日落地）**。
  裁定理由（物理優先於律法）：燈光協定是刷新制——E1.31 收端 ~2.5s 無封包判 source loss、Art-Net
  節點 timeout 回黑場；且 edge-only 下 enable 著改值送不出去=「燈跟音樂動」直接壞。設計落地：
  send gate 分兩家——EVENT senders（UDP/TCP/WS/Serial 手動 trigger）維持 rising-edge（離散訊息不
  該連發）；REFRESH senders（Artnet/Sacn/DMX/WLED）enable/Connect held 就每 frame 發 marker（app
  drain 層轉真送）。牙=injectBug mode 1 把兩家語義互換（event→level、refresh→edge），兩家 golden
  同時咬（net-nodes -bug 6 處齊紅親驗）。net_node_cook.cpp 註釋與 code 從此一致；DMX golden 原本
  「檔頭宣稱每 frame、斷言只驗第一格」的脫鉤也一併封死；補 UDPOutput edge counter-golden。

## 剩餘工作項（記帳不擋路）

- MidiNoteOutput 補 release-edge leg+level-fire 牙（現誠實標無牙）
- dmx/net selftest 假 TiXL 行號重引 → chip task_950c0031 已開
- anim 家族 Bias/Ratio 恆等縫（SchlickBias 臂在 value-rail 零 golden 牙）；TriggerAnim Delay/AnimMode 1/2/Shape 1-5 無牙
- timedisplace 灰底非整數 probe；pbr_currency 檔頭 stale+bug 升級走真 seam;io-video cooked-duplicate 牙型統一
- io-video 八 spec evaluate==nullptr：接 cook 時工單必令「走 golden'd 函式」（io_visca 同理）
- FreeDOutput 28B 上游 bug fork 註;once 預設值註釋反向;tonesynth/setplayback 行號 stale 機械校正
- node_health.sh 規則(d) 被註解裡 "NodeSpec" 字樣騙到（forceparams 假警報）
- golden_lint P1 可再升級：片語窗→「if(injectBug) 塊內兩分支同 return 1」結構掃描（pbr 逃逸的教訓）
