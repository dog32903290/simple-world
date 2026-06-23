# BLUEPRINT — L5 IO/硬體 loopback 自檢金（第一交付）

> Lane L5（MASTER_PLAN.md §並行lane 表 row 5），讀-only scout 2026-06-23。
> **本檔 = 機器可驗那半的全設計；裝置那半 = 柏為殘留。**

## 0. 一句話結論

L5 分雙軌：
1. **loopback golden（虛擬 MIDI/localhost UDP）— 本檔覆蓋，機器驗可交**
   - OSC 本地迴圈：app 開 UDP 127.0.0.1:port → sendto 127.0.0.1:port (自己) → 讀 OSC Message → 解析 Float value
   - MIDI 虛擬迴圈：本地 virtual MIDI 埠 → MIDI Note/CC event → 解析 velocity/value
   - 兩個 `--selftest-io-osc-loopback`、`--selftest-io-midi-loopback`，新檔 `platform/osc_loopback.{h,cpp}` / `platform/midi_loopback.{h,cpp}` 各自隔離；platform 層純 I/O，app 層 hook 做數值映射

2. **實體裝置（真 MIDI controller / audio-in / serial）= 柏為簽收，非 lane 阻擋**

---

## 1. TiXL 地表 IO 實裝（ground-truth）

### 1.1 OSC 架構（外/tixl/Core/IO/OscConnectionManager.cs）
- **入口**：`OscConnectionManager.RegisterConsumer(IOscConsumer, port)` — port 多消費者並行聽
- **接收迴圈**（L145-164）：`OscReceiver(port).Connect()` → async Task → `_receiver.TryReceive(out OscPacket)` 每 2ms 輪詢
- **分支**：
  - `OscBundle`（L144-152）：迭代內部 OscMessage
  - `OscMessage`（L156-159）：轉發給所有註冊 Consumers
- **介面**（L73-76）：`IOscConsumer { void ProcessMessage(OscMessage msg); }` 
- **訊息檢視**（L219-231）：`TryGetFloatFromMessagePart(object arg) -> float` — float/int/bool/double/string all coerce to float; NaN = parse fail
- **位址索引**（L195-216）：每訊息掃過參數型別序列（"fibs..."），儲存 `ScannedAddresses[addr] = "fibs..."`
- **訊息路徑助手**（L233-243）：`BuildMessageComponentPath(OscMessage, index)` → addr.x/addr.y/.../addr.n（4 char channel xyzw，超過用序號）

### 1.2 MIDI 架構（外/tixl/Core/IO/MidiInConnectionManager.cs）
- **入口**：`RegisterConsumer(IMidiConsumer)` — 先掃設備 `ScanAndRegisterToMidiDevices()`（L196-263 ✅）
- **設備掃描**（L205-235）：迭代 `MidiIn.NumberOfDevices` → `new MidiIn(index)` → `newMidiIn.Start()` → 存 `_devicesByMidiIn[midiIn] = deviceInputInfo`
- **事件分發**（L76-82）：掛 `.MessageReceived += consumer.MessageReceivedHandler` (NAudio 線程)
- **介面**（L126-136）：
  ```csharp
  interface IMidiConsumer {
    void MessageReceivedHandler(object sender, MidiInMessageEventArgs msg);
    void ErrorReceivedHandler(object sender, MidiInMessageEventArgs msg);
    void OnSettingsChanged();
  }
  ```
- **NAudio 型別**：`MidiInMessageEventArgs`（sender=MidiIn, MidiEvent 內含 Msg/Velocity/Channel/DeltaTicksPerQuarterNote）
- **MIDI 訊息型別**：NoteOn/NoteOff/ControlChange/PitchWheelChange/ChannelAfterTouch（各轉 Value 0..127）

### 1.3 虛擬測試邊界（外/tixl/Core/IO/SimulatedIoBus.cs）
- **角色**：並行分發路由 — 真設備/錄製資料集都走同一消費者路徑（replay golden）
- **MIDI 事件**（L38）：`record SimulatedMidiEvent(string DeviceProductName, MidiEvent Event)`
- **OSC 事件**（L44）：`record SimulatedOscEvent(int Port, string Address, object Value)`
- **分發**（L46-50）：靜態事件 `MidiEventReceived` / `OscEventReceived`，消費者 subscribe + dispatch 時 invoke
- **關鍵點**：不碰 MIDI control-surface 回饋（CompatibleMidiDevice）；僅服務圖形節點輸入（MidiInput/OscInput op）

---

## 2. simple_world 現況（吸收庫存）

### 2.1 已有基礎設施
- **platform/ 檔登記**（app/CMakeLists.txt:188-205）：每個 .mm 檔明確列 compile options + source list → platform 層幾乎滿額（audio_capture/playback/image_decode/metal_compile）
- **platform 接縫模式**（ARCHITECTURE.md §葉子接縫）：platform 只發 callback；app 註冊 DSP/邏輯。例 audio_capture.h:26 `BlockCallback = void(*)(void* user, const float* mono, int numFrames, float sampleRate)`
- **selftests 註冊**（selftests_decls.h / selftests_core.cpp）：REGISTER_SELFTESTS 一列一 golden；無檔 hook 設定（leaf 檔自帶轉發）
- **LiveSource 骨架**（source_registry.h）：SourceRegistry/LiveSource 已存在；graph_selftest.cpp 示範註冊 + 綁定 + 覆蓋。binding 層已三態（Constant/Connection/Automation/LiveSource）

### 2.2 缺的部分（0 行）
- OSC socket / UDP（沒網路程式碼）
- MIDI 設備列舉 / 事件監聽（沒 Core MIDI wrapper）
- loopback 測試框架
- osc/midi value → graph parameter 的線路

---

## 3. loopback harness 設計

### 3.1 架構拓撲

```
┌─────────────────────── loopback 自檢 ──────────────────────┐
│                                                               │
│  ┌──────────────────────── OSC 迴圈 ──────────────────────┐  │
│  │                                                          │  │
│  │  [Host Process]                                          │  │
│  │    UDP 127.0.0.1:9005 (Rx)        UDP 127.0.0.1:9005   │  │
│  │         ▲                                (Tx to self)     │  │
│  │         │←──────────────────────────────┐                │  │
│  │    OscReceiver                          │                │  │
│  │    (Rug.Osc / or C++ liblo)      [Test Sender]          │  │
│  │         │                                                 │  │
│  │    ParseMessage()                                         │  │
│  │    TryGetFloatFromMessagePart()                          │  │
│  │         │                                                 │  │
│  │         ▼                                                 │  │
│  │    Consumer (OscLoopbackDevice)                          │  │
│  │    [assert value == expected]                            │  │
│  │                                                           │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌──────────────────────── MIDI 迴圈 ──────────────────────┐   │
│  │                                                           │   │
│  │  [Virtual MIDI Port] ("SW Loopback")                     │   │
│  │    Rx ◄─────────── Test Sender sends MidiEvent ────────┤   │
│  │                                                           │   │
│  │  MidiConnectionManager.RegisterConsumer()               │   │
│  │  listens to every device (including virtual)            │   │
│  │                                                           │   │
│  │  Consumer (MidiLoopbackDevice)                          │   │
│  │  MessageReceivedHandler()                               │   │
│  │  [assert velocity/value == expected]                    │   │
│  │                                                           │   │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌───────────────────────── 驗證層 ─────────────────────────┐   │
│  │                                                            │   │
│  │  --selftest-io-osc-loopback:                             │   │
│  │    • send /test/value f:0.5 → Rx → assert value == 0.5  │   │
│  │    • send /test/int i:42 → coerce → assert == 42         │   │
│  │    • bundle + multiple → parse each                      │   │
│  │                                                            │   │
│  │  --selftest-io-midi-loopback:                            │   │
│  │    • send NoteOn velocity:64 → Rx → assert == 64/127     │   │
│  │    • send ControlChange value:100 → assert == 100/127    │   │
│  │    • multi-event sequence → all received in order        │   │
│  │                                                            │   │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 新檔案 & 職責分割

#### A. `platform/osc_loopback.h` + `platform/osc_loopback.cpp`
- **責任**：OSC socket + local receive loop（macOS UDP）
- **公開 API**：
  ```cpp
  class OscLoopbackDevice {
   public:
    OscLoopbackDevice();
    ~OscLoopbackDevice();
    
    // Start listening on localhost:port. Non-blocking; calls cb each message.
    bool startListening(int port, void (*cb)(void* user, const std::string& address, float value), void* user);
    void stopListening();
    
    // For testing: send ourselves a message (UDP to 127.0.0.1:port).
    bool sendTestMessage(const std::string& address, float value);
    
    bool isListening() const;
    int  listeningPort() const;
  };
  ```
- **實裝細節**：
  - macOS native UDP socket (POSIX `socket(AF_INET, SOCK_DGRAM)`, `bind`, `recvfrom`)
  - 或依賴現有 third_party OSC library（若 CMakeLists 已有）
  - parse OscMessage 用現有邏輯 `TryGetFloatFromMessagePart` 邏輯鏡像
- **自檢鑰匙**：內建 smoke 無堂，驗證在上層 platform selftest

#### B. `platform/midi_loopback.h` + `platform/midi_loopback.cpp`
- **責任**：虛擬 MIDI 埠掛鑰匙 + 事件接收（仰賴 macOS CoreMIDI）
- **公開 API**：
  ```cpp
  class MidiLoopbackDevice {
   public:
    MidiLoopbackDevice();
    ~MidiLoopbackDevice();
    
    // Create virtual MIDI port "SW Loopback" and listen.
    bool startListening(void (*cb)(void* user, int status, int velocity), void* user);
    void stopListening();
    
    // For testing: inject a MIDI event (NoteOn/ControlChange).
    bool sendTestMidiEvent(int status, int velocity);
    
    bool isListening() const;
  };
  ```
- **實裝細節**：
  - macOS CoreMIDI: `MIDISourceCreate` (create virtual input source) + `MIDIInputPortCreate` (listen)
  - event callback 格式：MIDI status byte (e.g., 0x90 for NoteOn ch0) + velocity (0..127)
  - 無 NAudio（Windows only）；改用 Objective-C++ 或純 C CoreMIDI wrapper
- **自檢鑰匙**：內建 smoke 無堂

#### C. `app/src/verify/io_loopback.h` + `app/src/verify/io_loopback.cpp`（選擇性，若想隔離 app 層）
- **責任**：驗證層 assertion + test 邏輯
- 可選；簡單實裝可直接在 selftests_io.cpp 裡

#### D. `app/src/selftests_io.cpp`（新）
- **責任**：`--selftest-io-osc-loopback` + `--selftest-io-midi-loopback` 註冊
- **簽名**：
  ```cpp
  int runOscLoopbackSelfTest(bool injectBug);
  int runMidiLoopbackSelfTest(bool injectBug);
  ```
- **流程 OSC**：
  1. 創建 OscLoopbackDevice
  2. startListening(9005, [capture](...){ assert received == sent; })
  3. sendTestMessage("/test/value", 0.75f)
  4. sendTestMessage("/test/int", 42.0f)  ← coerce from int
  5. sendTestMessage bundled multiple
  6. 清理 + return 0 on all pass
- **流程 MIDI**：
  1. 創建 MidiLoopbackDevice
  2. startListening([capture](...){ assert velocity == sent; })
  3. sendTestMidiEvent(0x90, 64)  ← NoteOn ch0 vel=64
  4. sendTestMidiEvent(0xB0, 100) ← CC ch0 val=100
  5. sequence test
  6. 清理 + return 0 on all pass

#### E. `CMakeLists.txt` 更新（最小 hook）
```cmake
# @ platform source list（L198-205 區間）
set_source_files_properties(src/platform/osc_loopback.cpp PROPERTIES COMPILE_OPTIONS -fPIC)
set_source_files_properties(src/platform/midi_loopback.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  # ^ midi 是 .mm (ObjC++，CoreMIDI)

# @ main target source list（L188+ 附近）
  src/platform/osc_loopback.cpp
  src/platform/midi_loopback.mm
  src/selftests_io.cpp
```

### 3.3 驗證數據合約（golden assertions）

#### OSC loopback 驗證表
| 送出訊號 | 接收訊息 | 解析法 | 預期值 | 測試名稱 |
|---------|--------|--------|--------|---------|
| `/test/val` float:0.75 | OscMessage addr="/test/val" args=[0.75f] | TryGetFloatFromMessagePart(args[0]) | 0.75f | osc_float_direct |
| `/test/int` int:42 | OscMessage addr="/test/int" args=[42] | TryGetFloatFromMessagePart(args[0]) → float(42) | 42.0f | osc_int_coerce |
| `/test/bool` bool:true | OscMessage addr="/test/bool" args=[true] | TryGetFloatFromMessagePart(args[0]) → float(1) | 1.0f | osc_bool_true |
| `/test/bool` bool:false | OscMessage addr="/test/bool" args=[false] | TryGetFloatFromMessagePart(args[0]) → float(0) | 0.0f | osc_bool_false |
| `/test/str` string:"3.5" | OscMessage addr="/test/str" args=["3.5"] | TryGetFloatFromMessagePart(args[0]) → parse "3.5" | 3.5f | osc_string_parse |
| `/test/str` string:"invalid" | OscMessage addr="/test/str" args=["invalid"] | TryGetFloatFromMessagePart(args[0]) → NaN | NaN (fail) | osc_string_invalid |
| `/test/mixed` float:0.5, int:10, bool:1 | OscMessage args=[0.5f, 10, true] | TryGetFloat(...args[i]) ×3 | [0.5, 10.0, 1.0] | osc_multiarg |
| `bundle [/test/a f:1.0, /test/b f:2.0]` | Two OscMessage per bundle | iterate bundle → forward each | both dispatched | osc_bundle |

#### MIDI loopback 驗證表
| 送出事件 | 接收事件 | 解析法 | 預期值 | 測試名稱 |
|---------|---------|--------|--------|---------|
| NoteOn ch=0 note=60 vel=64 | MidiInMessageEventArgs msg=NoteEvent | msg.Velocity | 64 | midi_noteon_vel |
| NoteOff ch=0 note=60 vel=32 | MidiInMessageEventArgs msg=NoteOffEvent | msg.Velocity | 32 | midi_noteoff_vel |
| ControlChange ch=0 ctrl=7 val=100 | MidiInMessageEventArgs msg=ControlChangeEvent | msg.ControlValue | 100 | midi_cc_value |
| Pitch Wheel ch=0 bend=+1000 | MidiInMessageEventArgs msg=PitchWheelChangeEvent | msg.Pitch | 1000 | midi_pitch_value |
| NoteOn ch=5 vel=127 | ...same device | velocity | 127 | midi_max_vel |
| (none) | (timeout 100ms) | — | (no call) | midi_no_spurious |

#### Refuter 牙
- **osc_float_inject**: 收到 0.75 改斷言期值 = 0.5 → 必 FAIL
- **midi_vel_inject**: 錯位送 vel=100 期值 = 50 → 必 FAIL
- **osc_late_msg**: 訊息到達慢（thread delay）→ timeout; 實測 localhost 靠後會打爆，不卡
- **midi_device_absent**: 起 loopback 前無權限（TCC）→ startListening() = false，合理 skip

---

## 4. 映射到 graph parameter（app 層 hook，L6 依賴項）

### 4.1 LiveSource 註冊點
一旦 loopback harness 綠（--selftest-io-* ✅），將 OscLoopbackDevice/MidiLoopbackDevice 包進 app 層 LiveSource register：

```cpp
// app/frame_cook.cpp (or new app/io_live_sources.cpp)
void registerIoLiveSources(SourceRegistry& reg, OscLoopbackDevice* osc, MidiLoopbackDevice* midi) {
  if (osc) {
    // Register OSC parameters
    LiveSource oscLive;
    oscLive.id = "osc:9005:/control/param";
    oscLive.value = [osc](void* self, const EvaluationContext& ctx) -> float {
      // osc internally tracks last-received value per address
      return osc->lastValue("/control/param");  // or similar API
    };
    oscLive.self = osc;
    reg.registerSource(oscLive);
  }
  
  if (midi) {
    // Register MIDI CC to parameter
    LiveSource midiLive;
    midiLive.id = "midi:cc:7";  // MIDI CC 7 (volume)
    midiLive.value = [midi](void* self, const EvaluationContext& ctx) -> float {
      return midi->lastCcValue(7) / 127.0f;  // normalize 0..1
    };
    midiLive.self = midi;
    reg.registerSource(midiLive);
  }
}
```

### 4.2 integration 順序
1. **S1 output-resolution seam** (cook-core 脊椎)：EvaluationContext 坑抓，確保 SourceRegistry 參數傳遞可靠 ✅（已定義，此 lane 無影響）
2. **L5 loopback harness** (本檔)：純 I/O 測試邏輯，零 graph 動作
3. **L5 參數綁定**（future refine）：app 層 hook OSC/MIDI → LiveSource register，綁定 node parameter；非本檔阻擋（此次測試 loopback + 簡單 assertion，不需 graph 互動）

---

## 5. 檔領域分割 & CMakeLists 鑰匙

### 檔域樹
```
app/src/
├── platform/
│   ├── osc_loopback.h          # ← 新
│   ├── osc_loopback.cpp        # ← 新
│   ├── midi_loopback.h         # ← 新
│   ├── midi_loopback.mm        # ← 新（ObjC++）
│   └── [existing audio/image/etc]
├── selftests_io.cpp            # ← 新
├── selftests_decls.h           # 加 2 行 fwd-decl
├── [existing selftests_core/list/etc]
└── [runtime, app, ui, verify/...]
```

### selftests_decls.h 加項
```cpp
// @ 檔頂或 platform 區（L27 附近）
int runOscLoopbackSelfTest(bool injectBug);    // platform/osc_loopback.cpp
int runMidiLoopbackSelfTest(bool injectBug);   // platform/midi_loopback.mm
```

### 新增 selftests_io.cpp 檔頭註解
```cpp
// app/src/selftests_io.cpp — L5 IO/硬體 loopback harness 驗證
// 機器驗半（虛擬 MIDI/localhost UDP）; 實體設備 = 柏為殘留 (L6+ future).
//
// Reached via main's --selftest-io-osc-loopback / --selftest-io-midi-loopback.
```

### CMakeLists.txt 更新（~3 處）
```cmake
# (1) @ L88-96 ARC/non-ARC 宣告
set_source_files_properties(src/platform/osc_loopback.cpp PROPERTIES COMPILE_OPTIONS -fPIC)
set_source_files_properties(src/platform/midi_loopback.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

# (2) @ L198-205 platform source list
  src/platform/osc_loopback.cpp
  src/platform/midi_loopback.mm

# (3) @ selftests source list（L188 附近，或單獨 glob）
  src/selftests_io.cpp
```

### 無編輯項（架構無 loop）
- graph.cpp evalParam：LiveSource 已三態，此 lane 不觸及參數綁定
- point_graph.cpp：平行 lane（L4），零 IO 交集
- selftests_core.cpp：無 I/O 節點，此行加 REGISTER_SELFTESTS → selftests_io.cpp

---

## 6. Blockers / 依賴確認

### 依賴項檢查
- ✅ **S1 output-resolution seam**：SourceRegistry 已存，binding 層完整 （graph_selftest.cpp 已證）。本 lane 不依賴 S1 運作；S1 完成後可綁定 OSC/MIDI → LiveSource（future L6 refinement）
- ✅ **audio_capture 既有基礎**：platform 層 leaf 架構已實證；osc_loopback/midi_loopback 鏡像 audio_capture（block callback + state）
- ✅ **CMakeLists 登記**：existing platform .mm 都有 compile options；新檔直接接軌
- ✅ **selftests 註冊**：selftests_core.cpp REGISTER_SELFTESTS 已模式；新 selftests_io.cpp 自帶轉發，zero 依賴
- ⚠️ **macOS 권한 (TCC)**：MIDI 使用 CoreMIDI（無 NAudio），無 user-prompt（virtual port 創建權限 app-sandbox 內）; UDP socket 開放端口需（bind 127.0.0.1 無提示）

### 協同吃地
- **L2 UI 分類**：無依賴（本 lane 測 I/O 邏輯，無 UI 觸及）
- **L4 節點開採**：平行獨立（L4 IO 節點 future 細化；本次 loopback 不接）
- **L6 audio export**：未接近（本 lane 純輸入驗；export 是 S1 之後）

---

## 7. 驗收定義 & 成功準則

### Harness 綠燈條件
1. **`--selftest-io-osc-loopback 0`** → PASS
   - 測試訊號送到 localhost 迴圈、接收、解析浮點全對 ✅
   - 5+ 個 golden case（float/int/bool/string/multi-arg/bundle）✅
   - refuter 牙 (injectBug=1) → FAIL ✅

2. **`--selftest-io-midi-loopback 0`** → PASS
   - 虛擬 MIDI NoteOn/NoteOff/CC 全型收到、velocity 解析 ✅
   - 多訊號序列 (order stable) ✅
   - refuter 牙 → FAIL ✅

3. **CMakeLists 構編通過** ✅

4. **無新 architecture 違律**
   - platform 只 callback，app 處理邏輯 ✅
   - selftests_decls + 新 cpp 無交集 ✅

### 手藝簽收（柏為）
1. 跑起 --selftest-io-osc-loopback / --selftest-io-midi-loopback → green
2. 連實體 MIDI 控制器 (future L6)，現場演出動作（真裝置 loopback 外的那半）

---

## 8. 解鎖與優先序

### L5 完成順序（機器驗部分）
1. **platform/osc_loopback.{h,cpp}** → 開發 + --selftest-io-osc-loopback ✅
2. **platform/midi_loopback.{h,mm}** → 開發 + --selftest-io-midi-loopback ✅
3. **CMakeLists** 補 3 處
4. **selftests_io.cpp** + selftests_decls.h 補 2 行

### 不阻擋並行
- **L1 Variation/Snapshot**：無交集
- **L3 檔案/專案**：無交集
- **L4 節點開採**：可並行；IO 節點細化在 L6
- **L6 audio export**：depends S1 (output-resolution)；本 lane 完全獨立

### 實體裝置（L6 refinement，非本次）
- 連 real MIDI controller → app LiveSource registry → graph parameter bind
- audio-in loopback（系統 loopback 虛擬裝置）→ audio_monitor 既有路徑
- serial port / video-in = future subsystems（超出 L5 scope）

---

## 9. 新檔概要清單

### Core Files (platform + selftest)
- **platform/osc_loopback.h**：UDP loopback socket API + parse float
- **platform/osc_loopback.cpp**：macOS POSIX socket impl + liblo / or homebrew OSC parse
- **platform/midi_loopback.h**：CoreMIDI virtual port API
- **platform/midi_loopback.mm**：ObjC++ CoreMIDI impl
- **selftests_io.cpp**：runOscLoopbackSelfTest / runMidiLoopbackSelfTest
- **selftests_decls.h**：+2 fwd-decl
- **CMakeLists.txt**：+3 小改

### Reference (TiXL ground-truth, read-only)
- external/tixl/Core/IO/OscConnectionManager.cs:73-76 (IOscConsumer interface)
- external/tixl/Core/IO/OscConnectionManager.cs:219-231 (TryGetFloatFromMessagePart)
- external/tixl/Core/IO/MidiInConnectionManager.cs:126-136 (IMidiConsumer interface)
- external/tixl/Core/IO/SimulatedIoBus.cs:27-51 (event dispatch model)

---

## 10. 風險 & 盲區

### R-1: macOS 限制
- UDP bind 127.0.0.1:port 需 sudo？→ 本地迴圈應無權限要求（tested loopback is safe）
- CoreMIDI virtual source 建立權限？→ app-sandbox 內可創（AUM 等已驗）
- ✅ **mitigation**：smoke test 裡嘗試 bind → fail graceful (skip test, no crash)

### R-2: Rug.Osc / 第三方選擇
- TiXL 用 `Rug.Osc` (C# NuGet)，sw 不便引 NuGet
- ✅ **option 1**: 自製簡單 OSC parse（地圖 ~100 行，only float/int/bool/string/double 分支 + bundle iterate）
- ✅ **option 2**: vendored liblo-cpp （POSIX socket 已有，liblo 輕量）
- **決議**：自製最輕，避免 third-party 風險

### R-3: 多線程競合
- OSC async Task（TiXL 用 ContinueWith）→ sw 迴圈 startup 同步 block 等 first message？
- MIDI callback in audio thread（NAudio 後臺線程）→ sw 同步 startup 等 event?
- ✅ **mitigation**: selftest 送訊息後 sleep(10ms) wait received flag，timeout 1 sec → FAIL（防 hang）

### R-4: NAudio → CoreMIDI 換血
- TiXL MidiInConnectionManager 深度倚 NAudio.MmException / MidiIn.Start()
- sw macOS 無 NAudio（Windows only）
- ✅ **swap**: 自製 ObjC++ wrapper（CoreMIDI API 簡單，~200 行）；MidiLoopbackDevice 為 exemplar

### R-5: virtual MIDI port naming
- macOS "IAC Driver" / "loopback" port 創建有系統權限微妙
- ✅ **mitigation**: StartListening() 容錯（權限 deny → return false），selftest graceful skip

---

## Critical Success Factors
1. **loopback harness 純機器驗**：零實體設備需求 → 持續集成可自動化 ✅
2. **平台葉子接縫**：app 層 hook via callback，platform 無 graph 見 ✅
3. **selftests 註冊模式一致**：selftests_decls + REGISTER_SELFTESTS，機械式新增 ✅
4. **refuter 牙**（injectBug）：每 golden 有對標「故意破壞 → FAIL」版本 ✅
5. **柏為手藝簽收**：機器綠後，實體設備接線 → 演出效果（L6 refinement scope） ✅

---

## Appendix: Code Sketch

### osc_loopback.h 輪廓
```cpp
namespace sw {

class OscLoopbackDevice {
 public:
  using ValueCallback = void(*)(void* user, const std::string& addr, float val);
  
  OscLoopbackDevice();
  ~OscLoopbackDevice();
  
  bool startListening(int port, ValueCallback cb, void* user);
  void stopListening();
  bool sendTestMessage(const std::string& address, float value);
  bool isListening() const;
  int listeningPort() const;
  
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sw
```

### selftests_io.cpp 輪廓
```cpp
int runOscLoopbackSelfTest(bool injectBug) {
  sw::OscLoopbackDevice osc;
  bool ok = true;
  
  // Test 1: float direct
  float received1 = -999.f;
  osc.startListening(9005, [](void* u, const std::string& a, float v) {
    if (a == "/test/val") *(float*)u = v;
  }, &received1);
  
  osc.sendTestMessage("/test/val", 0.75f);
  sleep(10);  // await async message
  
  if (injectBug) received1 = 0.5f;  // inject mismatch
  ok = ok && (fabsf(received1 - 0.75f) < 0.001f);
  
  // Test 2, 3, ... (int coerce, bool, string, multi-arg, bundle)
  // ...
  
  osc.stopListening();
  printf("[selftest-io-osc-loopback] ... -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

int runMidiLoopbackSelfTest(bool injectBug) {
  // Similar: MidiLoopbackDevice, NoteOn/CC events, velocity assert
  // ...
}
```

