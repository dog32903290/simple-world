# simple_world 可用資源清單（不重造的輪子）

> 來源：2026-06-07 gemini deep research + 本地 `external/` 盤點。
> 這份是 **source material**，不是 dashboard。dashboard 看
> `2026-06-07-imgui-metal-pivot-master-progress.md`。
>
> **可信度標記**：`[web]` = 該 dispatch 有成功 web 搜尋確認存在；
> `[模型]` = 來自 Gemini 參數記憶、未經 web 二次確認，**用前自己再驗一次**。
> 標 `[USE]` / `[ADAPT]` / `[DON'T]` = 可直接用 / 要改 / 不要用。

## 0. 這份研究最重要的一句

我原本估「LLM 寫 C++ 最大風險 = build/link 工具鏈 + imgui-node-editor API 幻覺」。
研究把這兩個風險**直接消掉**：

1. **imgui 官方原生支援 metal-cpp** — `IMGUI_IMPL_METAL_CPP` macro，定義它就把
   backend 全部從 `id<MTLDevice>` 換成 `MTL::Device*`，**不用自己 bridge**。
   `[web]` https://github.com/ocornut/imgui/issues/4746
2. **build 接線已被別人解好** — 見下面的 starter。
3. **imgui-node-editor 是 backend-agnostic**（純 ImDrawList），接上 imgui 就能用，
   不需要專門的 Metal 整合。

→ **step 0 的殼幾乎不用從零寫，是 clone starter + 加 node-editor submodule。**

## 1. Step 0 殼起點（USE AS-IS）

- `[web][USE]` **`ikryukov/MetalCppImGui`** — metal-cpp + GLFW + Dear ImGui 純 C++ 最小 starter。
  https://github.com/ikryukov/MetalCppImGui ← **從這個 clone 起手。**
- `[web][USE]` **`LeeTeng2001/metal-cpp-cmake`** — metal-cpp 的 CMake 模板，省掉 CMakeLists 工。
  https://github.com/LeeTeng2001/metal-cpp-cmake
- `[web][ADAPT]` **`pthom/imgui_bundle`** — 把 imgui + imgui-node-editor + Metal backend 綁在一起。
  當「三者怎麼接」的參考；整包當依賴太重，抽接線 pattern 就好。
  https://github.com/pthom/imgui_bundle
- `[web][ADAPT]` **`Gellert5225/StellarEngine`** — metal-cpp + ImGui engine，看 renderer 整合參考。

## 2. 引擎輪子（ADAPT，核心資產可借的部分）

- `[web][ADAPT]` **Effekseer** — MIT，原生 Metal C++ VFX runtime，工業標準。
  借：Metal render path、粒子資料結構（curve math、sprite batching、instanced allocation）。
  不借：它的 Qt/Windows 編輯器。 https://github.com/effekseer/Effekseer
- `[web][ADAPT]` **Wicked Engine** — MIT，C++ engine 有 macOS/Metal 支援 + GPU 粒子 compute。
  看 `wiParticle.cpp` + `shaders/particleUpdateCS.hlsl`，**HLSL 邏輯可乾淨翻成 MSL**。
  只抽粒子子系統，別搬整包。 https://github.com/turanszkij/WickedEngine
- `[模型][ADAPT]` **`tcoppex/sparkle`**（2017，不再維護）— GPU Bitonic Sort + curl noise compute（GLSL）。
  數學 pattern 不過期，翻 MSL。 https://github.com/tcoppex/sparkle

## 3. 求值核心（ADAPT，免得自己重寫 dataflow）

- `[web][ADAPT]` **`kovacsv/VisualScriptEngine`** — MIT，純 C++ node/port/connection + dirty 傳播，
  **無 GUI**，ARCHICAD 生產線在用。最適合當執行層起點。活躍度 `[模型]`（2022 stable）待驗。
  https://github.com/kovacsv/VisualScriptEngine
- `[web][ADAPT 慎]` **`taskflow/taskflow`** — MIT，強在 topological sort + 平行執行；
  **沒有** value-based dirty 傳播 / incremental cache，那層要自己疊。現在還不需要平行就先別上。
  https://github.com/taskflow/taskflow
- `[web][DON'T]` **`skypjack/entt`** — ECS 不是 node graph，硬做要大量自訂碼，不如直接用 VisualScriptEngine。

## 4. 設計藍圖（read-only pattern，不是可抄的碼）

5 條可直接轉移的設計原則：

1. **push-dirty + pull-evaluate 混合**（TouchDesigner / Tooll3）：改參數只往下游推 dirty 不算；
   有 viewer/output 要資料時才往上游 pull。
2. **stateless node + `EvaluationContext`**（Tooll3）：node 不存渲染狀態；frame time / camera /
   render target 包成 const-ref context 往下傳。→ 安全多執行緒 + 好做 instancing。
3. **node 輸出 = GPU handle（`MTL::Texture*` / `MTL::Buffer*`），不是 CPU copy**（Notch）→ zero-copy。
4. **避開 viewer trap**：每個 node 都顯示 live thumbnail = 每 frame 全網求值。compute 重的圖會瞬間爆。
   從 day 1 就只 cook「連到當前輸出」的分支。
5. **PSO 不要 per-frame 重編**：在 node 連線時就 pre-compile pipeline state。

來源：`[web]` https://derivative.ca/wiki/index.php?title=Cooking （TD，較舊）、
`[web]` https://github.com/still-scene/t3 （Tooll3 EvaluationContext/DirtyFlag，較舊但碼仍活）、
Houdini APEX/Copernicus（SideFX HIVE talks）、Notch Blocks。

本地對應：`external/tixl`（= Tooll3 的同源 C# 碼）可直接讀這個 pattern；
`external/tixl-spec` 935 節點報告書當設計藍圖。

## 5. node editor 選型 verdict

`[web]` **用 `thedmd/imgui-node-editor`（develop branch）** — 2026 仍是最佳 C++ 選擇。
理由對這個專案：原生 zoom/pan（VFX 網路長很快必要）、link flow animation（讓藝術家看資料流向）、
group node（可把 Emit/Update/Render 階段分群）。API 較囉嗦但 LLM 扛，柏為不碰。
- 替代 `[web]` `Nelarius/imnodes`（MIT，極簡、無原生 zoom）— 想「最快出第一張畫面」可先用，
  兩者都是 ImDrawList-based，之後換 thedmd 不難。
- `[web][DON'T]` `rokups/ImNodes`（2021 archived）。

## 6. LLM 寫 C++/Metal 的陷阱 + 工作慣例 checklist（對 Claude 最實用）

**最陰險（會 silent 壞畫面、不 crash、難 debug）：**
- ⚠️ **float3 對齊不一致**：MSL `float3` = **16 bytes**（同 float4），C++ `{float x,y,z}` = **12 bytes**。
  LLM 會生 12-byte struct 丟給 shader → 畫面 garbled 但不報錯。
  **慣例：任何要丟 shader 的 struct，host 端一律用 `simd::float3`**（或 MSL 端用 `packed_float3` 並手動 unpack）。

**會 silent 漏記憶體直到 OOM：**
- ⚠️ **每個 render frame 開頭要 `NS::AutoreleasePool`**，結尾 release。LLM 常漏，因為 ObjC ARC 範例裡看不到。
  ```cpp
  void renderFrame() {
      auto pool = NS::AutoreleasePool::alloc()->init();
      // ... Metal commands ...
      pool->release();
  }
  ```

**指標所有權（LLM 最常搞錯）：**
- `NS::TransferPtr(p)` — p 來自 `alloc/new/copy/CreateSystemDefaultDevice` 時用（取得所有權、不 retain）。
- `NS::RetainPtr(p)` — p 來自 getter（`currentDrawable()`、`commandBuffer()`）且要存超過 local scope 時用。
- LLM 常把這兩個用反 → 反了就 double-retain 漏記憶體 / over-release crash。**每個指標包裝當下就 review。**

**其他：**
- `MTL::Buffer` `commit()` 後沒同步就寫 → Apple Silicon unified memory data race → 畫面 artifact/crash。
  semaphore-gate 或 triple-buffer。
- 互動工具（非遊戲）：triple buffering 加 16-33ms 延遲，考慮 double buffering 換手感。`[模型]`
- 通用 C++：dangling `string_view`、range-for 裡 `push_back`、use-after-`move`。

**開發期環境（day 1 就開）：**
```
clang++ -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer   # 每個 .cpp 編譯閘
export MTL_DEBUG_LAYER=1
export MTL_SHADER_VALIDATION=1
export MTL_DEBUG_LAYER_ERROR_MODE=assert
# Xcode 裝好後：Instruments > Metal System Trace / GPU debugger 抓 binding mismatch
```

## 7. 不要碰 / 已否決

- `[DON'T]` **Vuo runtime** — LGPL 連結對閉源專案法律複雜（`[模型]` 未法務驗證），且方向已否決。
  只 read-only 看 event-driven push model 概念。
- `[DON'T]` 克隆 TiXL C# runtime（DX11/WinForms 焊死，pivot 已判）。
- `external/` 內：`hand-midi-controller`（無關）、`vuo-downloads` 1.8G（歷史包袱，可清）。

## 8. 待驗清單（用前再確認）

- `suzukiplan/imgui-metal-cpp` — Gemini 自己標不確定，**別依賴**。
- Apple 官方 sample code 確切 URL（developer.apple.com/metal/sample-code）可能已重組 — 用前查。
- VisualScriptEngine / imgui-node-editor 活躍度、star 數 — `[模型]`，未即時抓。
- TiXL v4.2.0-alpha / .NET 10 / Linux port 狀態 — `[模型]`，未獨立驗證。
- Vuo LGPL + Mac App Store 法律邊界 — 未驗（但我們不用 Vuo runtime，無所謂）。

## 9. 研究員建議的下一步（已採納進 master 的 Active Lane）

1. 從 `ikryukov/MetalCppImGui` + `LeeTeng2001/metal-cpp-cmake` 起 build 骨架。
2. git submodule 拉 `thedmd/imgui-node-editor`（develop）。
3. 寫第一個 `EvaluationContext` struct（frame time、render resolution、active `MTL::CommandBuffer*`）再寫任何 node。
4. launch 環境 day 1 開 `MTL_DEBUG_LAYER=1` + `MTL_SHADER_VALIDATION=1`。
5. `simd::float3` 規則當不可妥協慣例。

完整 transcript：`~/.claude/agent_logs/20260607_031824_gemini-researcher.md`
