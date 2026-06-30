# SEAM1_FANOUT_BUILD_PLAN — buffer ops fan-out + resident-leg mirror

> 2026-06-30 Opus architect blueprint (read-only, vs main `111aa77` + TiXL `395c4c55`). Keystone已建已merged
> (SwBuffer currency, buffer_op_registry, FloatsToBuffer, GetBufferComponents, ExecuteBufferUpdate, flat-leg
> cookFlatBuffer, byte-parity selftest). **`bufferSpecSink()` 已 aggregated 進 node_registry.cpp:156,197 → 純-leaf op 不需 node_registry edit，靠 `BufferOp` registrar 自註冊。**

## ★Backward-trace 定案（修正 keystone 的猜測）
- **IntsToBuffer GUID — 反轉 keystone 的指示**：const-buffer `IntsToBuffer.cs`(`2eb20a76`,numbers/int/process) = **78 consumer 複合**；`IntsToBufferWithViews.cs`(`c036b4f2`,render/_dx11/buffer) = **0 consumer**。→ **port const-buffer 變體，NOT WithViews**（SEAM1_BUFFER_BUILD_PLAN §2/§6 說 WithViews＝backward-trace 證錯，照本檔）。輸出 GUID `f5531ffb`＝FloatsToBuffer 同一 output GUID(IntsToBuffer.cs:11/FloatsToBuffer.cs:8，spec dedup 別混)。**16-byte-pad**：`IntsToBuffer.cs:24` arraySize=ceil(intCount/4)*4，尾補 0＝忠實 load-bearing byte 差異。
- **SrvFromTexture2d / GetSRVProperties type-collapse（refuter 最高不確定點）定案**：兩者非 producer/consumer 對、都不逼新 SwBuffer 欄。
  - SrvFromTexture2d(`c2078514`)→`Slot<ShaderResourceView>`(`DC71F39F`)，~120 複合 wired 但 output GUID **0 graph-wire 消費**（餵 shader SRV binding rail 非 Buffer port）。Metal texture 直接可採→**output dataType=Texture2D passthrough，放 tex flow（texReg）非 Buffer flow，避免碰 cook-core**。fork `srv-is-texture-on-metal`。
  - GetSRVProperties(`bc489196`)：input buffer-SRV→ElementCount(int)+Buffer passthrough。~80 point/mesh 複合。Metal:**input=SwBuffer，ElementCount=SwBuffer.elementCount，Buffer 直傳＝GetBufferComponents 的薄版**。無新欄。

## 工作項
### WO-A IntsToBuffer（純 leaf）
建 `buffer_ops_intstobuffer.cpp`(~75，clone floatstobuffer)。clone `IntsToBuffer.cs:19-49` const 變體：gather int Params→padded arraySize(:24)→memcpy int32 進 requestBytes(arraySize*4)，stride=4,count=arraySize(padded)。int 走 float rail(sw 無 int 通貨，evalFloat→cast，fork `intstobuffer-int-via-floatrail`)，**Params port 宣告 "Float" multiInput 如 FloatsToBuffer→cookFlatBuffer Float 分支已填 floatInputs，零 cook-core edit**。fork：const-to-shared/int-via-floatrail/16byte-pad-faithful。harness:`runIntsToBufferSelfTest` 餵[7,8,9]→count==4(pad),bytes[7,8,9,0],stride==4,-bug 掉尾。**shared touch:NONE 純 leaf**（自註冊+自己的 selftest 檔，別擠 selftests_buffer.cpp）。

### WO-B GetSRVProperties（純 leaf，建議 spike 首顆=最薄）
建 `buffer_ops_getsrvproperties.cpp`(~55,clone getbuffercomponents)。clone `GetSRVProperties.cs:18-34`：讀 inputBuffers[0]→*output=*in(Buffer 直傳)+ElementCount=in->elementCount(:28)。scalar int output 延後(如 GetBufferComponents scalar-deferred)。fork:collapse-to-mtlbuffer/scalar-deferred/srv-is-buffer。harness:FloatsToBuffer→GetSRVProperties assert elementCount==19。**shared touch:NONE 純 leaf**。

### WO-C SrvFromTexture2d（leaf，放 tex flow）
建 `buffer_ops_srvfromtexture2d.cpp`(~50)。clone `SrvFromTexture2d.cs:18-54`＝near-noop 傳 inputTexture。**output=Texture2D→建議實作為 tex flow(texReg) 的 Texture2D→Texture2D passthrough，避免碰 cook-core**（若硬塞 Buffer flow＝cookFlatBuffer 要加 Texture2D output 分支＝cook-core touch，不建議）。fork `srv-is-texture-on-metal`。harness 可延後(無 graph-wire consumer，fork `srvfromtexture2d-binding-rail-only`)。**leaf（tex-flow placement）**。

### WO-D TransformsConstBuffer（leaf + camera bridge＝cook-core edit；最須小心 packing）
建 `buffer_ops_transformsconstbuffer.cpp`(~120,最大 leaf <400)。clone `TransformsConstBuffer.cs:48-70`+`TransformBufferLayout.cs:5-62`。**640 byte=4·4·4·10**，10 矩陣 offset 0/64/.../576(:33-61) 序:CameraToClipSpace/ClipSpaceToCamera/WorldToCamera/CameraToWorld/WorldToClipSpace/ClipSpaceToWorld/ObjectToWorld/WorldToObject/ObjectToCamera/ObjectToClipSpace，各 derive(:14-17)後**transpose**(:19-30 HLSL CB row-major)。stride=640,count=1。
- **★TRANSPOSE/Metal 慣例 fork（silent-corruption 風險）**：TiXL transpose 因 HLSL CB row-major+System.Numerics row-major；sw simd::float4x4＝**column-major**、MSL float4x4 ctor column-major。leaf 必須**emit TiXL row-major transposed bytes byte-for-byte**(複製 TiXL transpose，非套 simd 慣例)。fork `transformsconstbuffer-hlsl-rowmajor-bytes`，leaf header 必引，refuter 主攻點。**de-risk:byte-parity selftest pin 640-byte 對 TiXL 公式 in-test，任何 row/col swap 在 shader 讀前就 fail**。
- ping-pong _cbA/_cbB→Buffer+PrevBuffer。**PrevBuffer 需第二 buffer/key＝可能小 cook-core ctx 擴充；建議 spike 只 emit Buffer，PrevBuffer 延後 fork `transformsconstbuffer-prevbuffer-deferred`(motion-blur consumer 才需)**。
- camera bridge:clone fillPointCamera(point_graph_internal.h:87-97)，需 CameraToClipSpace+WorldToCamera+ObjectToWorld(:58-60)。**keystone BufferCookCtx 無 camera 欄→加 hasCamera+source matrices 進 buffer_op_registry.h + cookFlatBuffer camera-fill gather＝cook-core edit**。fork +`transformsconstbuffer-camera-from-default`。harness:餵已知 camera assert 640 bytes，-bug 擾一元素。**shared touch:buffer_op_registry.h+point_graph_buffer_cook.cpp+selftests＝cook-core edit，非純 leaf**。

### WO-E Resident-leg Buffer mirror（★OWNER-LOCKED cook-core，serial，最高 care）
今:resident Buffer terminal 落 point_graph_resident.cpp:542 cookNode→dataType!="Points"→clearTarget(:551)＝graceful zero no-op(refuter 證)。**production 跑 resident→Buffer op 現在 live app 產不出東西**。
- edit `point_graph_resident.cpp`(OWNER-LOCKED)：加 `cookResidentBuffer` walker，mirror flat cookFlatBuffer + 既有 cookResidentPointList(:365)/Gradient(:404)。
- 鍵:resident 用 per-**PATH** key 非 flatKey(id)；存進**同一** Impl::bufferMeta[path]/rawBuf[path]/ensureRawBuffer(已 keyed by path-or-#flat，internal.h:125,167-170)＝無新儲存，只 gather 餵 path。
- **★PARAM-PATH 分歧（最高 care）**：flat cookFlatBuffer 讀 host 矩陣 stand-in 從 `n->params["Vec4Params.<m>.<k>"]`(raw Node::params string key,buffer_cook.cpp:73-83)；resident **無 Node::params**，靠 resolveResidentFloatInputs→paramsMemo[path](:107-115) 只投宣告的 Float input port→合成 "Vec4Params.0.k" key **不會出現**。cookResidentBuffer 必須換法 source 矩陣(WO-D 走 resident fillPointCamera；Float multiinput Params 直接 mirror resident PointList/Float gather over ResidentInput::Connection :193-205)。**byte-identical 才算對**。fork `cookresidentbuffer-vec4-from-resident-params`。
- terminal:加 Buffer 分支於 :518-553，parallel flat findBufferOp(point_graph.cpp:672-678)，置 :542 cookNode fallthrough 前。
- **gate:resident byte-output 必 byte-identical flat(cook_ctx.h:163-164 both-legs rule)**。
- **shared touch:point_graph_resident.cpp(OWNER-LOCKED)＋selftests。SERIAL，不可與 Seam 2 build 同跑(都寫 resident cook-core)**。

## Sequencing / 並行
- **spike 首:WO-B**(最薄 clone GetBufferComponents，零風險驗 harness 範式，快出牙)。
- **可並行純 leaf(無共享檔撞):WO-A / WO-B / WO-C**——各自 .cpp+自註冊，**唯一共享=selftests_buffer.cpp→各給自己的 selftests_buffer_<op>.cpp 完全並行**。node_registry 不需 edit。
- **不可與 leaf 並行(cook-core writer):WO-D**(buffer_op_registry.h camera ctx+buffer_cook.cpp)，leaf 落地後或 serialize cook-core 支。
- **WO-E＝serial OWNER-LOCKED**(point_graph_resident.cpp)，**不可與 Seam 2 build 同跑**，獨佔 cook-core owner lane。

## ◧ Vec4-currency 通貨 CLOSED；producer 覆蓋 PARTIAL (2026-06-30, refuter+ground-truth 校正)
**通貨橋已落地且 byte-parity**(SEAM1_VEC4_CURRENCY_BUILD_PLAN.md)：`FloatsToBuffer.Vec4Params` 兩條腿都從
**ColorList 通貨**(4 float4 rows = 一矩陣,fork-matrix-as-4-vec4-on-extColorOut)gather WIRED 矩陣 — flat 走
`cookColorListNode`(Node::params 降 fallback)、resident 走上游 `extColorOut`(`matrixFromColorOut`,
cookMatrixOutputNodes/cookColorListNodes 在 pg.cookResident 前 settle,frame_cook.cpp:367<387 已驗)。
gate=`--selftest-buffer-vec4`(G1 ColorsToList both-legs byte-parity + G2 TransformMatrix resident keystone vs
閉式,兩 leg `-bug` 都 bite),542 全綠零回歸。
- **★別再 overclaim「17 複合全通」**：橋只 SERVE source 是 sw 有的 producer 的複合 = `TransformMatrix.Result
  (751E97DE)`(ground-truth 確認 TransformPoints/VolumeForce 第一 wire …)。**殘留 producer-seam(非橋缺陷,已開 chip)**：
  `TransformMatrix.ResultInverted(ECA8121B)`=deferred fork 未 port(AttributesFromImageChannels/VolumeForce 第二
  wire/SamplePoint… → 零矩陣塊);`GetMatrixVar.Result(1EEAB949)`=op 在 sw 不存在(DrawMeshChunksAtPoints…)。
- 殘留(latent,inferred-sound,未建 golden):多矩陣(>1 wire 進同一 Vec4Params,如 VolumeForce)flat-vs-resident
  順序——與已 golden 的 Float-MultiInput(buffer-resident LEG1)同機制,推得一致但未 pin。

## 最高風險 + de-risk（歷史記錄,已 closed 見上）
**WO-E resident Vec4Params/param-path 分歧**(疊 WO-D transpose 風險)：flat 讀 Node::params["Vec4Params..."]，resident 無此 store、resolveResidentFloatInputs 不 surface 合成 key→naive clone 產**不同於 flat 的 bytes**(parity-gate fail 或 production 矩陣靜默歸零)。
**de-risk:寫 cookResidentBuffer 前先加 resident byte-parity selftest**(clone runFloatsToBufferSelfTest 驅 resident cook)，先對未改 resident 跑＝捕 zero-no-op RED；再實作到 resident bytes **byte-identical flat debugCookedSwBuffer**(test 內 assert flat==resident)。parity assert(非 shader)先抓分歧。
