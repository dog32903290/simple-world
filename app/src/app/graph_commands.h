// app/graph_commands — 改 SymbolLibrary 的具體命令（批次 3 N2, lib-native）。每個命令持有
// lib 參照 + 它編輯的 symbolId（= TiXL 的命令掛在 Symbol 定義上）——所以一個命令在 undo 時
// 不管畫布正看著哪一層，都改得到正確的定義。Zone: app. 依賴 runtime/compound_graph。
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "app/command.h"
#include "runtime/compound_graph.h"
#include "runtime/copy_paste.h"  // PastePlan (CopyPasteChildrenCommand carries a precomputed plan)

namespace sw {

// 加一個已建好（含 id/symbolId）的 child instance。undo 用 id 移除。
class AddChildCommand : public Command {
 public:
  AddChildCommand(SymbolLibrary& lib, std::string symbolId, SymbolChild child)
      : lib_(lib), symbolId_(std::move(symbolId)), child_(std::move(child)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Node"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  SymbolChild child_;
  bool did_ = false;  // doIt's cycle gate may refuse: undo must then be a true no-op
};

// 加一條已建好的 4-tuple 連線（append = multi-input 保序契約的尾端）。undo 移除同一條。
class AddWireCommand : public Command {
 public:
  AddWireCommand(SymbolLibrary& lib, std::string symbolId, SymbolConnection w)
      : lib_(lib), symbolId_(std::move(symbolId)), wire_(std::move(w)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Connection"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  SymbolConnection wire_;
};

// 刪 N 條連線（以 4-tuple 指名）。doIt 快照 (index, wire)，undo 按原 index 還原
// （S字段 multi-input 保序：還原後陣列順序 == 刪除前）。
class DeleteWiresCommand : public Command {
 public:
  DeleteWiresCommand(SymbolLibrary& lib, std::string symbolId, std::vector<SymbolConnection> wires)
      : lib_(lib), symbolId_(std::move(symbolId)), wires_(std::move(wires)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Connections"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::vector<SymbolConnection> wires_;                       // 要刪的連線（4-tuple 比對）
  std::vector<std::pair<size_t, SymbolConnection>> removed_;  // doIt 快照（原 index + 線）
};

// 刪 N 個 child + 它們的入射連線（同 symbol 內 srcChild/dstChild 命中即入射；boundary
// sentinel 0 不會被指名）。doIt 快照 children 與 (index, wire)，undo 全部還原。
class DeleteChildrenCommand : public Command {
 public:
  DeleteChildrenCommand(SymbolLibrary& lib, std::string symbolId, std::vector<int> ids)
      : lib_(lib), symbolId_(std::move(symbolId)), ids_(std::move(ids)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Nodes"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::vector<int> ids_;                                            // 要刪的 child id
  std::vector<std::pair<size_t, SymbolChild>> removedChildren_;     // doIt 快照（原 index + child）
  std::vector<std::pair<size_t, SymbolConnection>> removedWires_;   // doIt 快照（原 index + 線）
};

// 移動 N 個 child。記錄每個 child 的舊/新座標；undo 設回舊，doIt/redo 設新。
class MoveChildrenCommand : public Command {
 public:
  struct Move { int id; float oldX, oldY, newX, newY; };
  MoveChildrenCommand(SymbolLibrary& lib, std::string symbolId, std::vector<Move> moves)
      : lib_(lib), symbolId_(std::move(symbolId)), moves_(std::move(moves)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Move Nodes"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::vector<Move> moves_;
};

// 刪一個 compound 的 input/output definition（畫布上的 boundary 節點按 Delete）。這是改 Symbol
// 定義的契約變更，波及 lib 內所有引用此 compound 的實例：母圖指向該 slot 的線、實例 override、
// 內部 boundary 線全收屍（單一 runtime codepath removeInput/OutputDefFromLib）。照 TiXL 可 undo
// （RemoveInputsOrOutputsCommand.IsUndoable=true）——doIt 快照全部被刪物，undo 按原 index 還原。
// isInput 區分輸入/輸出 def（資料驅動：一個命令兩用，非兩個近乎相同的類）。
class DeleteInputOrOutputDefCommand : public Command {
 public:
  DeleteInputOrOutputDefCommand(SymbolLibrary& lib, std::string symbolId, std::string slotId,
                                bool isInput)
      : lib_(lib), symbolId_(std::move(symbolId)), slotId_(std::move(slotId)), isInput_(isInput) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return isInput_ ? "Delete Input" : "Delete Output"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string slotId_;
  bool isInput_;
  RemovedSlotDef removed_;  // doIt snapshot (def + scrubbed wires/overrides at original indices)
};

// 改某 child 某 input slot 的 instance override（Inspector 拉 slider）。undo 還原成
// 「之前有 override 就設回舊值、本來沒有就 erase」——定義層 default 永不被污染。
class SetOverrideCommand : public Command {
 public:
  SetOverrideCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                     bool hadOld, float oldV, float newV)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        hadOld_(hadOld), old_(oldV), new_(newV) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Set Value"; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  bool hadOld_;
  float old_, new_;
};

// 把某 child 某 input slot 重置回定義 default（Inspector 重置手勢：按參數名 / 右鍵 "Reset to
// default"）= TiXL ResetInputToDefault。doIt ERASE override（定義 default 重新透出，never a
// 0-residue），undo 還原成「之前有 override 就設回舊值、本來沒有就保持 erase」——SetOverrideCommand
// 的鏡像（它 doIt 設、undo 擦；這個 doIt 擦、undo 設）。沒 override 時 push 前用 refused() 擋掉。
class ResetOverrideCommand : public Command {
 public:
  ResetOverrideCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string slotId,
                       bool hadOld, float oldV)
      : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), slotId_(std::move(slotId)),
        hadOld_(hadOld), old_(oldV) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Reset Value"; }
  bool refused() const { return !hadOld_; }  // nothing to reset (already default) -> caller skips

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string slotId_;
  bool hadOld_;
  float old_;
};

// 貼上一批 child + 它們的內部連線（copy/paste 契約 4, 照 TiXL CopySymbolChildrenCommand）。
// 命令在 BUILD 時拿一個 PastePlan（planPaste 已配好新 id + oldToNew 重映射 + 重映射後的連線，
// = TiXL 在 ctor 就算好 _childrenToCopy/NewChildIds）。doIt 依序 append children -> append wires
// （-> bypass 套用，本model尚無 bypass 欄位，留接縫；見 .cpp）。undo 反序：移除連線、再移除 child
// （按 NEW id 比對，move-by-id 安全）。child 永不撞號（plan 從 monotonic floor 配），monotonic
// floor 隨之燒掉（undo 不降，照 AddChild 同律——freed id 不復活以免繼承 dead per-path state）。
class CopyPasteChildrenCommand : public Command {
 public:
  CopyPasteChildrenCommand(SymbolLibrary& lib, std::string symbolId, PastePlan plan)
      : lib_(lib), symbolId_(std::move(symbolId)), plan_(std::move(plan)) {}
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Paste"; }
  bool empty() const { return plan_.children.empty(); }  // nothing accepted -> caller skips push

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  PastePlan plan_;
};

// 改一個 compound 的 DEFINITION 名（= TiXL RenameSymbol / ChangeSymbolName）。改的是 Symbol.name，
// 波及全部：findSpec 的 title (graph_bridge specFromSymbol)、所有實例顯示名（無自訂名者）、Add 選單
// 標籤 — 全部走 refreshCompoundSpecs 在 libRevision 變動的下一幀自動同步，命令本身只動 name 欄。
// undoable（TiXL RenameSymbol 透過 undo stack）。空名/同名/找不到 symbol = no-op（refused()=true，
// 呼叫端不 push，照環檢前例）。CJK 放寬：TiXL 的 C# identifier 限制源自動態編譯，本 fork 資料驅動無此
// 約束，名字只做「非空」檢查 — CJK 取名是本單存在理由（FORK，報告具名）。
class RenameSymbolCommand : public Command {
 public:
  RenameSymbolCommand(SymbolLibrary& lib, std::string symbolId, std::string newName);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Rename Definition"; }
  bool refused() const { return refused_; }  // empty/same/missing -> caller skips push

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string newName_;
  std::string oldName_;
  bool refused_ = false;
};

// 改一個 child 的 INSTANCE 名（= TiXL ChangeSymbolChildNameCommand）。只動那一顆的顯示名；空名
// fallback 定義名（childReadableName）。改定義名不會動實例名，兩者正交。undoable。同名/找不到
// child = no-op（refused()）。空名是合法值（= 清除自訂名、回到 fallback），不算 refuse。CJK 同放寬。
class RenameChildCommand : public Command {
 public:
  RenameChildCommand(SymbolLibrary& lib, std::string symbolId, int childId, std::string newName);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Rename Node"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string newName_;
  std::string oldName_;
  bool refused_ = false;
};

// --- S2 (批次7) Child structural补欄 commands (child_state_commands.cpp), all undoable, all 照 TiXL ---

// Toggle a child's isBypassed (= TiXL right-click Bypass / SetBypassed, Symbol.Child.cs:263-310). REFUSES
// (refused()=true, caller skips push — no dead undo entry, 照環檢前例) when: missing symbol/child, the
// child's MAIN I/O type is not bypassable (childIsBypassable false = TiXL IsBypassable guard), the
// child's MAIN output is NOT wired (= TiXL .cs:287-300 isOutputConnected refusal — bypass of an
// unconnected output is meaningless), or no-op (already in the requested state). Stores only the bool;
// undo flips it back.
class SetBypassChildCommand : public Command {
 public:
  SetBypassChildCommand(SymbolLibrary& lib, std::string symbolId, int childId, bool bypass);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Bypass Node"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  bool new_ = false, old_ = false;
  bool refused_ = false;
};

// Toggle ONE output's isDisabled (= TiXL Symbol.Child.SetDisabled per-output, .cs:106-149). REFUSES on
// missing symbol/child, an outputSlot the referenced symbol does not define, or no-op. Sparse map:
// disabling sets disabledOutputs[slot]=true; enabling ERASES the key (so a never-disabled output leaves
// no residue, like the override model). undo restores the prior presence/value.
class SetOutputDisabledCommand : public Command {
 public:
  SetOutputDisabledCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                           std::string outputSlot, bool disabled);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Disable Output"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string outputSlot_;
  bool new_ = false, hadOld_ = false, old_ = false;
  bool refused_ = false;
};

// Set ONE output's DirtyFlagTrigger override (= TiXL EditNodeOutputDialog dropdown, .cs:35-52). REFUSES
// on missing symbol/child, an outputSlot the symbol does not define, or no-op. Sparse: a None value
// ERASES the key (= follow the def); Always/Animated set it. undo restores the prior presence/value.
class SetOutputTriggerCommand : public Command {
 public:
  SetOutputTriggerCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                          std::string outputSlot, TriggerOverride trigger);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Set Output Trigger"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  int childId_;
  std::string outputSlot_;
  TriggerOverride new_ = TriggerOverride::None;
  bool hadOld_ = false;
  TriggerOverride old_ = TriggerOverride::None;
  bool refused_ = false;
};

// S2 child-state golden (child_state_selftest.cpp): bypass passthrough + unwired/non-whitelist refusal,
// per-output isDisabled freeze (Command no-op) + thaw, triggerOverride Always LIVE flip + clear, the
// three fields' savev2 byte-stable roundtrip + S15 tolerance, and copy/paste bypass-after-wire. injectBug
// breaks one承重 leg -> FAIL (teeth).
int runChildStateSelfTest(bool injectBug);

// 修C compound-bypass golden (bypass_compound_selftest.cpp, 批次9): a bypassed COMPOUND child is
// rewired away by the resident builder (consumers of its main output adopt its main input's
// driver; zero resident footprint) — Points passthrough + zero inner cook, savev2 roundtrip,
// Command/Texture2D BOUNDARY compounds (the production entry for those types) incl. the
// viewProducerPath sideways step, nested bypassed-compound-in-compound, and the
// SetBypassChildCommand doIt/undo/redo projection through the rebuild path. injectBug emulates a
// flattener that ignores the compound flag -> the passthrough legs FAIL (teeth).
int runBypassCompoundSelfTest(bool injectBug);

// rename golden (rename_selftest.cpp): def-name change flows to spec title + all instances + undo;
// instance-name change is isolated + empty falls back to def name + undo; a CJK instance name
// roundtrips byte-identically through savev2 WITHOUT aborting the parser; a tampered name field is
// dropped locally (S15); empty/same/missing renames are refused (no undo entry). injectBug disables
// the writer's raw-UTF-8 path so the CJK roundtrip breaks -> FAIL (teeth).
int runRenameSelfTest(bool injectBug);

// S13 DeleteBoundaryDefs golden (def_removal_selftest.cpp): removing a compound's input/output def
// scrubs dangling wires + obsolete overrides lib-wide, is undoable, survives a v2 roundtrip, and
// tolerates a tampered file. injectBug skips the parent-wire scrub -> FAIL (teeth).
int runDefRemovalSelfTest(bool injectBug);

// copy/paste golden (copy_paste_selftest.cpp): extract a selection (only both-ends-internal wires
// survive, external cut), plan a paste (new ids + oldToNew remap, no id collision, multi-input
// order, full per-child state), apply through the undoable command (undo restores byte-identity),
// cross-symbol paste via clipboard JSON, and the cycle gate dropping a self-nesting paste.
// injectBug breaks the external-wire-cut so a dangling wire survives -> FAIL (teeth).
int runCopyPasteSelfTest(bool injectBug);

}  // namespace sw
