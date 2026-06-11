// app/graph_commands — 改 SymbolLibrary 的具體命令（批次 3 N2, lib-native）。每個命令持有
// lib 參照 + 它編輯的 symbolId（= TiXL 的命令掛在 Symbol 定義上）——所以一個命令在 undo 時
// 不管畫布正看著哪一層，都改得到正確的定義。Zone: app. 依賴 runtime/compound_graph。
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "app/command.h"
#include "runtime/compound_graph.h"

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

// S13 DeleteBoundaryDefs golden (def_removal_selftest.cpp): removing a compound's input/output def
// scrubs dangling wires + obsolete overrides lib-wide, is undoable, survives a v2 roundtrip, and
// tolerates a tampered file. injectBug skips the parent-wire scrub -> FAIL (teeth).
int runDefRemovalSelfTest(bool injectBug);

}  // namespace sw
