// app/animation_commands — S3 GUI 的動畫編輯命令層（undo/redo）。改的是 Symbol 定義上的
// Animator（runtime/curve_animator）——所以一個命令掛在 (symbolId, childId, slotId) 上，undo 時
// 不管畫布正看哪一層、resident 圖怎麼重建，都改得到正確的定義。Animate 手勢 = 在 Animator 建曲線；
// resident flattener（resident_eval_graph.cpp:149）讀 animator.isAnimated() 自動把該 input 的 driver
// 翻成 Automation——所以「①建曲線 ②driver 翻 Automation」是同一處（改 Animator）的兩面，不可能 drift
// （spec 既定）。命令本身只動 Animator + bump libRevision（透過 CommandStack），driver 投影下一幀自動跟上。
//
// = TiXL 對照（external/tixl，鎖 SHA 只讀）：
//   AddAnimationCommand          = UiModel/Commands/Animation/AddAnimationCommand.cs
//   RemoveAnimationCommand       = .../RemoveAnimationsCommand.cs（undo 前 clone 曲線，byte-faithful 還原）
//   AddKeyframeCommand           = .../AddKeyframeCommand.cs（undo: 原本有 key 還原舊 key、沒有就刪）
//   MoveKeyframeCommand          = .../ChangeKeyframesCommand.cs（記舊/新，undo 還原）
//   DeleteKeyframeCommand        = .../DeleteKeyframeCommand.cs
//   WriteKeyAtPlayheadCommand    = InputValueUi.DrawAnimatedParameter + FloatInputUi.ApplyValueToAnimation
//                                  （動已動畫參數 = 在當前播放頭寫/更新 key，P1 拍板）
// FORK：childId 是 int 實例 id（非 Guid）、inputId 是 string slotId、scalar Float = 單通道（index 0）。
// 範圍鎖死（第一刀）：dope-sheet 式；不做 TimeClip/Layer/loop range/變速/bezier 把手；key 內插固定
//   Linear（animateFloat 預設），內插 enum 之後再開。
// Zone: app. 依賴 runtime/compound_graph + curve_animator + curve。
#pragma once
#include <string>
#include <vector>

#include "app/command.h"
#include "runtime/compound_graph.h"
#include "runtime/curve.h"

namespace sw {

// Animate 手勢：在 (childId, slotId) 上建一條單通道 Float 曲線，首 key 落在 `time`、值 = 當前解析值
// （= TiXL AddAnimationCommand：建曲線 + 首 key 取當前 live 值；resident driver 隨之翻 Automation）。
// undo 移除曲線（driver 回 Constant、無殭屍）。redo 還原同一條曲線（_keepCurves，byte-faithful）。
// 已動畫的 input 重複 Animate = refused（呼叫端不 push）。
class AddAnimationCommand : public Command {
 public:
  AddAnimationCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                      double time, float currentValue);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Animation"; }
  bool refused() const { return refused_; }  // already animated / missing -> caller skips push

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  double time_;
  float value_;
  std::vector<Curve> kept_;  // doIt snapshots the curve(s) it created -> redo restores byte-identically
  bool refused_ = false;
};

// Remove Animation：移除 (childId, slotId) 的曲線（= TiXL RemoveAnimationsCommand）。doIt 前 clone
// 曲線陣列，undo 原樣裝回（byte-faithful）。resident driver 隨 isAnimated() 回 false 翻回 Constant。
// 未動畫 / 找不到 = refused。
class RemoveAnimationCommand : public Command {
 public:
  RemoveAnimationCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Remove Animation"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  std::vector<Curve> kept_;  // doIt clones the removed curves -> undo装回
  bool refused_ = false;
};

// 在 (childId, slotId) 的通道 `index` 加一個 key（dope-sheet 雙擊空白）。值 = 曲線在該時刻的取樣值
// （= TiXL InsertKeyframeToCurves：新 key 落在曲線上，不是 0）。undo 還原整條曲線快照——byte-faithful
// 不受 updateTangents 在單 key 時不重置 tangent 的不對稱影響（runtime curve 層既有語義，我不動它）。
class AddKeyframeCommand : public Command {
 public:
  AddKeyframeCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                     int index, double time)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        index_(index), time_(time) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Keyframe"; }
  bool refused() const { return refused_; }  // no curve / index OOR

 private:
  Curve* resolve();
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  int index_;
  double time_;
  Curve before_;          // doIt snapshots the WHOLE curve -> undo restores byte-faithful
  bool refused_ = false;
};

// 移動一個 key 的時間/值（dope-sheet 拖 key）。= TiXL ChangeKeyframesCommand。`oldTime` 指名要動的 key；
// doIt 把它從 oldTime 移到 newTime + 設 newValue（撞號 = 覆蓋，AddOrUpdateV 同律）。undo 還原整條曲線。
class MoveKeyframeCommand : public Command {
 public:
  MoveKeyframeCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                      int index, double oldTime, double newTime, float newValue)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        index_(index), oldTime_(oldTime), newTime_(newTime), newValue_(newValue) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Move Keyframe"; }
  bool refused() const { return refused_; }

 private:
  Curve* resolve();
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  int index_;
  double oldTime_;
  double newTime_;
  float newValue_;
  Curve before_;
  bool refused_ = false;
};

// 刪一個 key（dope-sheet 選中 key 按 Delete）。= TiXL DeleteKeyframeCommand。undo 還原整條曲線。
class DeleteKeyframeCommand : public Command {
 public:
  DeleteKeyframeCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                        int index, double time)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        index_(index), time_(time) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Keyframe"; }
  bool refused() const { return refused_; }

 private:
  Curve* resolve();
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  int index_;
  double time_;
  Curve before_;
  bool refused_ = false;
};

// P1 拍板：動到「已動畫參數」的值 = 在當前播放頭寫/更新 key（= TiXL FloatInputUi.ApplyValueToAnimation
// -> Curve.UpdateCurveValues）。該時刻有 key 就更新值、沒有就在曲線上插一個。undo 還原整條曲線。
// Inspector 拉已動畫 slider 走這條（refused 若該 input 未動畫）。
class WriteKeyAtPlayheadCommand : public Command {
 public:
  WriteKeyAtPlayheadCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                            int index, double time, float value)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        index_(index), time_(time), value_(value) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Write Key"; }
  bool refused() const { return refused_; }

 private:
  Curve* resolve();
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  int index_;
  double time_;
  float value_;
  Curve before_;
  bool refused_ = false;
};

// P1 live-write 收尾命令：一次 slider 拖曳期間 Inspector 已把 key 直寫進曲線（每幀 sample(playhead)
// 自然累積，= TiXL FloatInputUi.DrawAnimatedValue 連續 ApplyValueToAnimation）；放手時 push 這個命令
// 把整條 CurveArray 的「拖之前/放手時」兩份快照交給 undo/redo —— undo 還原 before、redo 套 after，
// byte-faithful。和 WriteKeyAtPlayhead 的差別：那個是「一下寫一個 key」的單筆命令（drag 期間沒有 live
// 反饋）；這個包住整段 live-write，承重線是「拖曳中即時跟 + 放手才上 undo」（批次3 vec live-write 同款）。
class SetCurveSnapshotCommand : public Command {
 public:
  SetCurveSnapshotCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                          Animator::CurveArray before, Animator::CurveArray after);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Edit Animation"; }
  bool refused() const { return refused_; }  // before == after -> nothing changed

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  Animator::CurveArray before_;
  Animator::CurveArray after_;
  bool refused_ = false;
};

// Headless RED->GREEN proof of the S3 GUI command layer (--selftest-animgui):
//   ① Animate: curve生 + 首 key=當前值 + driver 翻 Automation（resident 投影驗）+ undo 全還原
//      （driver 回 Constant、曲線消失、無殭屍）
//   ② Remove Animation 反向 + undo 還原 byte-faithful
//   ③ add/move/delete key 命令各自 undo byte-faithful（libToJsonV2 比對）
//   ④ 動已動畫參數 = 播放頭寫 key（time=2.0 改值 -> key@2.0 出現/更新）
//   ⑤ savev2 全鏈：Animate 後存 -> 讀回 -> 曲線 + driver 都在
// injectBug 破壞一個期望（undo 留殭屍）-> 斷言 FAIL（teeth）。
int runAnimGuiSelfTest(bool injectBug);

}  // namespace sw
