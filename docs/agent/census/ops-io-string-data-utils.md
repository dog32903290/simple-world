# Census: io / string / data / Utils (120 ops)

> Scope: `external/tixl/Operators/Lib/io/` (74 ops, including `_/` WIPs),
> `string/` (33 ops), `data/` (1 op), `Utils/` (5 utility files — see note).
> Obsolete dirs (`_obsolete/`, `obsolete/`) excluded.

---

## H2: io/ — Input / Output (74 ops)

### io/audio/ — 14 active ops (6 main + 8 in `_/`)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AudioPlayer | 播 WAV/MP3/AIFF 音訊，可觸發/暫停/尋位 | NEW-SEAM:audio-playback-op | BLOCKED:audio-playback-op | R2 | 需 per-op AudioEngine（非 soundtrack）；sw 只有 soundtrack playback；uses T3.Core.Audio.AudioEngine |
| SpatialAudioPlayer | 3D 空間音效播放，帶 ISpatialAudioPropertiesProvider | NEW-SEAM:audio-playback-op | BLOCKED:audio-playback-op | R3 | 同上 + ITransformable + 3D pan/attenuation；Command 輸出 |
| SpatialAudioPlayerGizmo | 空間音效 gizmo 視覺化 | NEW-SEAM:audio-playback-op | BLOCKED:audio-playback-op | R2 | 依賴 SpatialAudioPlayer context；Command 輸出 |
| AudioToneGenerator | 合成波形音源（正弦/方波等），即時輸出 | NEW-SEAM:audio-playback-op | BLOCKED:audio-playback-op | R2 | 使用 ManagedBass.Mix；需 per-op streaming 音訊引擎 |
| AudioReaction | 分析麥克風/線入頻譜，輸出 Level/WasHit/Range | audio-analysis | READY-LEAF | R1 | sw 已有 audio_monitor.h + spectrum_analyzer.h 完整對接；frame_cook 已 cook |
| AudioPlayer (PlayAudioClip) | 播放 timeline audio clip，鎖定至 soundtrack 時間 | transport | READY-LEAF | R1 | 已用 AudioEngine.UseSoundtrackClip；sw soundtrack seam 已建 |
| _(WIP)_ AudioFrequencies | 輸出 FFT buffer list（raw/normalized/bands） | audio-analysis | READY-LEAF | R1 | 直接讀 AudioAnalysis.FftGainBuffer；sw 已有對應資料 |
| _(WIP)_ AudioWaveform | 輸出左/右/低/中/高頻 waveform list | audio-analysis | READY-LEAF | R1 | 讀 WaveFormProcessing；sw audio_monitor 已有 |
| _(WIP)_ DataRecording | 記錄 MIDI/OSC 資料至 DataSet | NEW-SEAM:data-recording | BLOCKED:data-recording | R3 | 輸出 DataSet；需錄製 MIDI/OSC 並序列化；關聯 LoadDataClip |
| _(WIP)_ DetectBeatOffset | 分析音訊 beat offset 量測 | audio-analysis | READY-LEAF | R2 | 只用 T3.Core.Animation + Utils；transport+value-graph 可撐 |
| _(WIP)_ DetectBpm | 從音訊訊號自動偵測 BPM | audio-analysis | READY-LEAF | R2 | 同上；自洽計算 List<float> 輸出 |
| _(WIP)_ GetAllSpatialAudioPlayers | 取得所有 spatial audio player 狀態 | NEW-SEAM:audio-playback-op | BLOCKED:audio-playback-op | R2 | 需 SpatialAudio registry；private Slot（internal gizmo tool） |
| _(WIP)_ GetBeatTimingDetails | 反射讀 BeatTimingDetails 靜態欄位 | NEW-SEAM:beat-timing-details | BLOCKED:beat-timing-details | R2 | BeatTimingDetails 靜態 singleton 未在 sw transport 建立 |
| _(WIP)_ _SetAudioAnalysis | debug: 手動寫 AudioAnalysis 狀態（Command 輸出） | audio-analysis | READY-LEAF | R1 | 內部 debug op；audio-analysis seam 已建可撐 |

### io/data/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| LoadDataClip | 從 .json 檔案載入 MIDI/OSC 錄製資料 clip | NEW-SEAM:data-recording | BLOCKED:data-recording | R2 | TimeClipSlot<DataClip>；需 DataSet/DataClip 型別 + 解析基建 |
| SimulateIoData | 播放 LoadDataClip 並分發給 MIDI/OSC 下游 | NEW-SEAM:data-recording | BLOCKED:data-recording | R2 | Command 輸出；分發 DataClip → MidiInput/OscInput 下游 |

### io/dmx/ — 6 main + 2 helpers (8 ops, 2 obsolete 已排除)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| ArtnetInput | 接收 ArtNet DMX 封包，輸出 List<int> universe 資料 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | UDP 接收；macOS 需實作 ArtNet 解碼 |
| ArtnetOutput | 發送 ArtNet DMX，Command 輸出 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | UDP 發送；需 ArtNet 協定 |
| DMXOutput | 透過 USB DMX dongle 輸出 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | Windows USB DMX HID；macOS 需 dongle driver |
| PointsToDMXLights | Point/BufferWithViews → DMX List<int> 燈光轉換 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | 次要 seam: point-system（BufferWithViews 已有） |
| SacnInput | 接收 sACN/E1.31 封包，輸出 List<int> | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | 與 ArtnetInput 同屬 DMX 族 |
| SacnOutput | 發送 sACN/E1.31，Command 輸出 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | UDP multicast |
| _(helper)_ Video2DPointScanner | 攝影機掃描 2D point 位置（Texture2D in/out） | NEW-SEAM:video-input | BLOCKED:video-input | R3 | 需 camera/video-input seam + OpenCV；次要 seam: artnet-dmx |
| _(helper)_ VisualizeSpotLights | 用 BufferWithViews 視覺化 DMX 燈頭，Command 輸出 | NEW-SEAM:artnet-dmx | BLOCKED:artnet-dmx | R2 | 次要：point-system 已有 BufferWithViews |

### io/file/ — 3 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FilesInFolder | 列出資料夾下的檔案清單，輸出 List<string> | value-graph | READY-LEAF | R1 | 純 string/filesystem；value-graph 可撐 |
| ReadFile | 從路徑讀取文字檔，輸出 string | value-graph | READY-LEAF | R1 | Resource<string> pattern；sw 已有類似 asset 機制 |
| WriteToFile | 將 string 寫入檔案，輸出路徑 | value-graph | READY-LEAF | R1 | 副作用型 op；pure write；無特殊 seam |

### io/freed/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FreeDInput | 接收 FreeD 相機追蹤 UDP，輸出 Pos/Rot/BufferWithViews | NEW-SEAM:camera-tracking | BLOCKED:camera-tracking | R2 | 同族: PosiStageInput；輸出 BufferWithViews Point 已有，但 FreeD 解碼未建 |
| FreeDOutput | 發送 FreeD 封包（相機資料輸出） | NEW-SEAM:camera-tracking | BLOCKED:camera-tracking | R2 | UDP send；需 FreeD encode |

### io/http/ — 1 op

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| WebServer | 嵌入式 HTTP server，接收 GET 並回應 HTML | NEW-SEAM:network-io | BLOCKED:network-io | R2 | macOS 可用 Network.framework；輸出 IsRunning/Port |

### io/input/ — 4 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| KeyboardInput | 偵測鍵盤按鍵，輸出 bool | NEW-SEAM:keyboard-mouse | BLOCKED:keyboard-mouse | R1 | 需截 global key event；sw imgui 已接 key，但 op 層未橋接 |
| KeyboardInputAsInt | 偵測數字鍵，輸出 int | NEW-SEAM:keyboard-mouse | BLOCKED:keyboard-mouse | R1 | 同上 |
| MouseInput | 滑鼠位置/按鍵，輸出 bool/Vec2 | NEW-SEAM:keyboard-mouse | BLOCKED:keyboard-mouse | R1 | 需 imgui io 座標橋接；有 3D plane projection 邏輯 |
| Gamepad | 搖桿狀態，輸出 Dict<float> | NEW-SEAM:gamepad | BLOCKED:gamepad | R2 | Windows XInput；macOS 需 GameController.framework |

### io/json/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| GetAttributeFromJsonString | 解析 JSON string，輸出 string/columns/rowCount | value-graph | READY-LEAF | R1 | 純 JSON 解析；依賴 string value-graph |
| RequestUrl | HTTP GET 請求，輸出 string | NEW-SEAM:network-io | BLOCKED:network-io | R1 | async HTTP；macOS URLSession 可用；較輕量 |

### io/midi/ — 10 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| MidiInput | 接收 MIDI CC/Note，輸出 float/List<float>/bool | NEW-SEAM:midi | BLOCKED:midi | R2 | MidiConnectionManager.IMidiConsumer；需 MIDI 裝置枚舉+接收 |
| MidiClip | 讀取 MIDI clip 資料，TimeClipSlot<Dict<float>> 輸出 | NEW-SEAM:midi | BLOCKED:midi | R2 | 需 MIDI 解析 + transport 整合 |
| MidiControlOutput | 發送 MIDI CC 訊息 | NEW-SEAM:midi | BLOCKED:midi | R2 | 需 MIDI 輸出裝置 |
| MidiNoteOutput | 發送 MIDI Note On/Off | NEW-SEAM:midi | BLOCKED:midi | R2 | 同上 |
| MidiOutput | 通用 MIDI 訊息輸出 | NEW-SEAM:midi | BLOCKED:midi | R2 | 同上 |
| MidiPitchbendOutput | 發送 MIDI Pitchbend | NEW-SEAM:midi | BLOCKED:midi | R2 | 同上 |
| MidiRecording | 錄製 MIDI 訊號（錄製 clip） | NEW-SEAM:midi | BLOCKED:midi | R3 | 需 midi + data-recording 兩 seam |
| MidiSysexOutput | 發送 MIDI SysEx 訊息 | NEW-SEAM:midi | BLOCKED:midi | R2 | 同上，byte[] 傳輸 |
| MidiTriggerOutput | MIDI 觸發輸出 | NEW-SEAM:midi | BLOCKED:midi | R2 | 同上 |
| LinkToMidiTime | 將播放頭鎖定至 MIDI clock | NEW-SEAM:midi | BLOCKED:midi | R2 | 需 MIDI clock 接收；transport 整合 |

### io/osc/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| OscInput | 接收 OSC 訊息，輸出 Dict<float>/List<float>/bool | NEW-SEAM:osc | BLOCKED:osc | R2 | OscConnectionManager.IOscConsumer；需 OSC UDP 解碼 |
| OscOutput | 發送 OSC 訊息（多型態：float/int/string） | NEW-SEAM:osc | BLOCKED:osc | R2 | 需 OSC encode + UDP send |

### io/posistage/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| PosiStageInput | 接收 PosiStageNet 追蹤器資料，輸出 BufferWithViews/Dict | NEW-SEAM:camera-tracking | BLOCKED:camera-tracking | R2 | UDP 多播；需 PosiStageNet 協定解碼 |
| PosiStageOutput | 發送 PosiStageNet 追蹤命令，Command 輸出 | NEW-SEAM:camera-tracking | BLOCKED:camera-tracking | R2 | UDP send；同族 |

### io/ptz/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| OnvifCamera | ONVIF IP 相機：PTZ 控制 + Texture2D 影像輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R3 | Texture2D 輸出 + 網路 PTZ；需 RTSP + ONVIF 協定 |
| ViscaCamera | Visca 相機 PTZ 控制 + Texture2D 影像輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R3 | 同上；Visca-over-IP |

### io/serial/ — 3 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| SerialInput | 串列埠接收，輸出 string/List<string>/bool | NEW-SEAM:serial | BLOCKED:serial | R2 | macOS 可用 /dev/tty.*；需 ISerialReceiver |
| SerialOutput | 串列埠發送 | NEW-SEAM:serial | BLOCKED:serial | R2 | 同上 |
| WLedSerialOutput | W-LED 裝置的串列輸出 | NEW-SEAM:serial | BLOCKED:serial | R2 | 同上 + W-LED 協定 |

### io/tcp/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| TcpClient | TCP 用戶端，收發 string，history buffer | NEW-SEAM:network-io | BLOCKED:network-io | R2 | macOS Network.framework 可撐 |
| TcpServer | TCP 伺服器，監聽+多連線，輸出 ConnectionCount/IsListening | NEW-SEAM:network-io | BLOCKED:network-io | R2 | 同上 |

### io/udp/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| UDPInput | UDP 接收，輸出 IsListening/LastSender/Port | NEW-SEAM:network-io | BLOCKED:network-io | R2 | macOS Network/GCD Socket 可用 |
| UDPOutput | UDP 發送 | NEW-SEAM:network-io | BLOCKED:network-io | R2 | 同上 |

### io/video/ — 8 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| PlayVideo | 播放影片（MediaFoundation），每幀更新 Texture2D | NEW-SEAM:video-input | BLOCKED:video-input | R3 | Windows MediaFoundation；macOS 需 AVFoundation 替換；Texture2D 輸出 |
| PlayVideoClip | 播放 VideoClip timeline clip，Texture2D 輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R3 | 同上；TimeClipSlot 整合 |
| VideoClip | timeline video clip container，TimeClipSlot<Command> 輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R2 | 容器 op；依賴 video-input 解碼基建 |
| VideoDeviceInput | DirectShow 相機即時輸入，Texture2D 輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R3 | Windows DirectShow + OpenCV；macOS 需 AVCaptureSession |
| VideoStreamInput | RTSP/網路 stream 接收（OpenCV VideoCapture），Texture2D 輸出 | NEW-SEAM:video-input | BLOCKED:video-input | R3 | OpenCV VideoCapture RTSP；macOS 可用 |
| SwiftCamDevice | SwiftImaging 科學相機 SDK，BGRA8→Texture2D | NEW-SEAM:video-input | BLOCKED:video-input | R3 | 廠商 SDK；高度 Windows 特定；macOS 可能無對應 |
| CameraCalibrator | 棋盤格相機校正（OpenCV），Texture2D in/out | NEW-SEAM:video-input | BLOCKED:video-input | R3 | OpenCV 棋盤格偵測；macOS OpenCV 可用 |
| PlayAudioClip | 播放 timeline audio clip，鎖定至 soundtrack 時間 | transport | READY-LEAF | R1 | 已用 AudioEngine.UseSoundtrackClip；sw soundtrack seam 已建 |

### io/websocket/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| WebSocketClient | WebSocket 用戶端，收發 string，history | NEW-SEAM:network-io | BLOCKED:network-io | R2 | macOS URLSessionWebSocketTask 可撐 |
| WebSocketServer | WebSocket 伺服器，多連線，輸出 IsListening/ConnectionCount | NEW-SEAM:network-io | BLOCKED:network-io | R2 | 同上 |

### io/ 摘要

- **總 op 數：74**（main 59 + `_/` WIP 8 + helpers 7）
- **READY-LEAF：7**（AudioReaction、PlayAudioClip、AudioFrequencies、AudioWaveform、DetectBeatOffset、DetectBpm、_SetAudioAnalysis）+ FilesInFolder、ReadFile、WriteToFile、GetAttributeFromJsonString = **11 READY-LEAF**
- **主要 NEW-SEAM 分佈：**
  - `NEW-SEAM:video-input` — 9 顆（PlayVideo/PlayVideoClip/VideoClip/VideoDeviceInput/VideoStreamInput/SwiftCamDevice/CameraCalibrator/OnvifCamera/ViscaCamera）
  - `NEW-SEAM:midi` — 10 顆
  - `NEW-SEAM:network-io` — 9 顆（TcpClient/TcpServer/UDPInput/UDPOutput/WebSocketClient/WebSocketServer/WebServer/RequestUrl + 1）
  - `NEW-SEAM:artnet-dmx` — 8 顆
  - `NEW-SEAM:audio-playback-op` — 5 顆（AudioPlayer/SpatialAudioPlayer/SpatialAudioPlayerGizmo/AudioToneGenerator/GetAllSpatialAudioPlayers）
  - `NEW-SEAM:camera-tracking` — 4 顆（FreeDInput/FreeDOutput/PosiStageInput/PosiStageOutput）
  - `NEW-SEAM:serial` — 3 顆
  - `NEW-SEAM:osc` — 2 顆
  - `NEW-SEAM:keyboard-mouse` — 3 顆
  - `NEW-SEAM:gamepad` — 1 顆
  - `NEW-SEAM:data-recording` — 4 顆（LoadDataClip/SimulateIoData/DataRecording/MidiRecording）
  - `NEW-SEAM:beat-timing-details` — 1 顆

---

## H2: string/ — 字串運算 (33 ops)

> 大宗 TRIVIAL：純 string/int/float value-graph，無 shader、無 buffer、無 IO。

### string/combine/ — 4 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| BlendStrings | 多字串輸入 blend/lerp 成一個 string | value-graph | TRIVIAL | R1 | MultiInputSlot<string> |
| CombineStrings | 串接多個 string | value-graph | TRIVIAL | R1 | — |
| FloatListToString | float list → 格式化 string | value-graph | TRIVIAL | R1 | Format template |
| StringRepeat | 重複一段 string N 次 | value-graph | TRIVIAL | R1 | — |

### string/convert/ — 3 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FloatToString | float → string（格式化） | value-graph | TRIVIAL | R1 | — |
| IntToString | int → string | value-graph | TRIVIAL | R1 | — |
| Vec3ToString | Vector3 → string | value-graph | TRIVIAL | R1 | Format template |

### string/datetime/ — 6 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| CountDown | 倒數至目標時間，輸出 string | value-graph | TRIVIAL | R1 | — |
| DateTimeToFloat | DateTime → float（mapping） | value-graph | TRIVIAL | R1 | — |
| DateTimeToString | DateTime → 格式化 string | value-graph | TRIVIAL | R1 | — |
| NowAsDateTime | 當前系統時間輸出 DateTime | value-graph | TRIVIAL | R1 | System.DateTime |
| StringToDateTime | string → DateTime 解析 | value-graph | TRIVIAL | R1 | — |
| TimeToString | float（時間秒數） → string | value-graph | TRIVIAL | R1 | — |

### string/list/ — 6 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| JoinStringList | List<string> → 合併 string | value-graph | TRIVIAL | R1 | — |
| KeepStrings | 過濾 List<string>，輸出子集 | value-graph | TRIVIAL | R1 | — |
| PickStringFromList | 從 List<string> 選一個 | value-graph | TRIVIAL | R1 | — |
| SplitString | string → List<string>（分隔符） | value-graph | TRIVIAL | R1 | — |
| StringLength | string → int（長度） | value-graph | TRIVIAL | R1 | — |
| ZipStringList | 兩個 List<string> 交叉合併 | value-graph | TRIVIAL | R1 | — |

### string/logic/ — 4 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| FilePathParts | 路徑 → 目錄/檔名/副檔名分解 | value-graph | TRIVIAL | R1 | — |
| HasStringChanged | 偵測 string 是否改變，輸出 bool | value-graph | TRIVIAL | R1 | — |
| PickString | MultiInput<string> 選一個 | value-graph | TRIVIAL | R1 | — |
| PickStringPart | string 拆分後選第 N 片段 | value-graph | TRIVIAL | R1 | — |

### string/random/ — 3 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| AnimRandomString | 按 Rate 隨機切換 string 片段 | value-graph | TRIVIAL | R1 | transport 時間驅動 |
| BuildRandomString | 隨機組合 string | value-graph | TRIVIAL | R1 | — |
| MockStrings | 從預設類別輸出假 string（測試用） | value-graph | TRIVIAL | R1 | — |

### string/search/ — 3 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| IndexOf | 在 string 中搜尋子字串，輸出 int | value-graph | TRIVIAL | R1 | — |
| SearchAndReplace | string 搜尋取代 | value-graph | TRIVIAL | R1 | — |
| SubString | 取 string 子字串 | value-graph | TRIVIAL | R1 | — |

### string/transform/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| ChangeCase | 大小寫轉換 | value-graph | TRIVIAL | R1 | — |
| WrapString | 按寬度折行 string | value-graph | TRIVIAL | R1 | — |

### string/buffers/ — 2 ops

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| StringBuilderToString | StringBuilder → string 轉換 | value-graph | TRIVIAL | R1 | 依賴 System.Text.StringBuilder |
| StringInsert | 在指定位置插入子字串 | value-graph | TRIVIAL | R1 | — |

### string/ 摘要

- **總 op 數：33**
- **全部 TRIVIAL（33/33）**，全踩 value-graph，零 shader，零 buffer
- 唯一注意：`AnimRandomString` 有 transport 時間驅動，但 transport seam 已建，仍 TRIVIAL

---

## H2: data/ — 雜項資料 (1 op)

| op | 一句功能 | 主要 seam | 狀態 | 風險 | 備註 |
|----|---------|----------|------|------|------|
| PickObject | 從 MultiInput<object> 選第 N 個輸出 | value-graph | TRIVIAL | R1 | 泛型 object；最通用的選擇器 |

### data/ 摘要
- **總 op 數：1**（`data/object/PickObject`）
- TRIVIAL × 1

---

## H2: Utils/ — 工具類別 (5 files)

> 注意：Utils/ 下的 .cs 是輔助類別（Font loader、GpuQuery、ShareDefinition、SvgLoader、BmFontDescription），**不是直接可端口的 op**，是供其他 op 使用的 helper 基建。

| file | 功能 | 是否為獨立 op | 注意 |
|------|------|-------------|------|
| BmFont.cs | BMFont XML 資料結構定義 | 否（類別庫） | 供文字渲染 op 使用 |
| BmFontDescription.cs | BMFont 從檔案解析 | 否（utility） | 需 font-rendering seam |
| GpuQuery.cs | GPU 查詢計時器 | 否（utility） | D3D11 相依；macOS Metal 需替換 |
| ShareDefinition.cs | IShareResources 介面實作 | 否（utility） | — |
| SvgLoader.cs | SVG 檔案載入（用 Svg.NET） | 否（utility） | 供 SVG 相關 op；macOS 需 Svg.NET 替代 |

### Utils/ 摘要
- **無獨立 op**（皆為 helper 類別）
- `BmFont`/`BmFontDescription` 是文字渲染 op（如 BmFont op）的基礎；若要 port BmFont op 需這兩個類別
- `SvgLoader` 依賴 Svg.NET（Windows library）；macOS 需確認替代方案
- `GpuQuery` 是 D3D11 timestamp query；Metal 替換需 MTLCounterSampleBuffer

---

## 整體摘要

| 類別 | 總 ops | READY-LEAF | TRIVIAL | BLOCKED | 新 NEW-SEAM |
|------|--------|-----------|---------|---------|------------|
| io/ | 74 | 11 | 0 | 63 | 11 個（見下） |
| string/ | 33 | 0 | 33 | 0 | 0 |
| data/ | 1 | 0 | 1 | 0 | 0 |
| Utils/ | (5 helpers, 非 op) | — | — | — | — |
| **合計** | **108** | **11** | **34** | **63** | **11** |

### io/ 所有 NEW-SEAM（優先排序建議）

| NEW-SEAM | 擋住 op 數 | 解鎖說明 |
|----------|-----------|---------|
| `NEW-SEAM:midi` | 10 | MidiInput/Output 全族；macOS CoreMIDI 框架可用 |
| `NEW-SEAM:video-input` | 9 | 影片/相機輸入；macOS AVFoundation/AVCaptureSession |
| `NEW-SEAM:network-io` | 9 | TCP/UDP/WebSocket/HTTP；macOS Network.framework 可用 |
| `NEW-SEAM:artnet-dmx` | 8 | ArtNet/sACN 燈控；UDP 可用後，協定解碼為主工作 |
| `NEW-SEAM:audio-playback-op` | 5 | per-op AudioEngine；macOS AVAudioEngine 擴展 |
| `NEW-SEAM:data-recording` | 4 | DataSet/DataClip 型別 + MIDI/OSC 錄製基建 |
| `NEW-SEAM:camera-tracking` | 4 | FreeD/PosiStageNet；UDP 已有後，協定解碼為主 |
| `NEW-SEAM:keyboard-mouse` | 3 | imgui io → op slot 橋接 |
| `NEW-SEAM:serial` | 3 | macOS /dev/tty.* serial port |
| `NEW-SEAM:osc` | 2 | OSC UDP 解碼；macOS 可用 |
| `NEW-SEAM:gamepad` | 1 | macOS GameController.framework |
| `NEW-SEAM:beat-timing-details` | 1 | BeatTimingDetails 靜態 singleton（transport 延伸） |

### 意外/盲區
1. **network-io 與 artnet-dmx/osc/midi 共用 UDP 基礎**：若先建一個通用 UDP 原語，artnet/osc/camera-tracking 工作量會縮減。不確定 sw 是否有計畫 UDP 共用層。
2. **audio-playback-op vs soundtrack**：sw 的 `audio_playback.mm` 明確是 soundtrack-only（header 說 "composition's backing audio"）。AudioPlayer 需要獨立的 per-op 音源池（TiXL AudioEngine.UpdateStereoOperatorPlayback），是完全不同的 seam。
3. **video-input Windows vs macOS gap**：PlayVideo/VideoDeviceInput 深度綁定 SharpDX.MediaFoundation + DirectShow。macOS 替換工作量高（R3）。SwiftCamDevice 是廠商 SDK，可能根本無 macOS 版本。
4. **CameraCalibrator 使用 OpenCV**：TiXL 版綁 OpenCvSharp（Windows DLL wrapper）。macOS 有 OpenCV，但 binding 不同；工作量中等。
5. **io/audio/_/ WIP ops**：TiXL 自己也標為 WIP（放在 `_/`）。DataRecording/GetBeatTimingDetails/GetAllSpatialAudioPlayers 可能是 TiXL 未完成功能，port 優先度應低。
6. **SoundTrackLevels/_LegacyAudioReaction** 在 `_obsolete/`：已排除，但確認 sw 的 AudioReaction 對應的是主目錄版本（非 legacy）。
