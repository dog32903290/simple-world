// app/rename_commands — rename 命令層（契約 rename, 批次 6）。兩個正交命令：
//   RenameSymbolCommand  改 Symbol DEFINITION 名（= TiXL RenameSymbol / ChangeSymbolName）— 波及
//                        全部實例 + Add 選單 + spec title，但只動 Symbol.name 一欄，傳播交給
//                        refreshCompoundSpecs（libRevision 變動的下一幀，frame_cook 觸發）。
//   RenameChildCommand   改某一顆 child 的 INSTANCE 名（= TiXL ChangeSymbolChildNameCommand）— 只動
//                        那顆，空名 fallback 定義名（childReadableName）。
// 兩者都 undoable（TiXL 兩條都進 undo stack）。命令持 lib& + symbolId（= TiXL 命令掛在定義上），undo
// 時不管畫布看哪層都改得到對的東西。Zone: app, 依賴 runtime/compound_graph。
//
// CJK 放寬（FORK）：TiXL 對 symbol 名跑 IsIdentifierValid（C# identifier，不准 CJK 起頭/空白），因為
// TiXL 的 symbol 名要拿去動態編譯成 C# 類別。本 fork 是資料驅動 — symbol 名只是 map key + 顯示字串，
// 不進編譯，所以那條限制不存在。命令只做最小檢查：「非空」（定義名）。CJK 能取名是本單存在理由。
#include "app/graph_commands.h"

#include <utility>

#include "runtime/compound_graph.h"

namespace sw {

// ---- RenameSymbolCommand (definition name) ----

RenameSymbolCommand::RenameSymbolCommand(SymbolLibrary& lib, std::string symbolId,
                                         std::string newName)
    : lib_(lib), symbolId_(std::move(symbolId)), newName_(std::move(newName)) {
  Symbol* s = lib_.find(symbolId_);
  // refuse: missing symbol, atomic (its name is the operator type — renaming would desync the
  // registry), empty new name, or no-op (same name). Refused commands MUST NOT touch the lib and
  // the caller skips pushing them (no dead undo entry — 照環檢三閘前例).
  if (!s || s->atomic || newName_.empty() || s->name == newName_) {
    refused_ = true;
    return;
  }
  oldName_ = s->name;
}

void RenameSymbolCommand::doIt() {
  if (refused_) return;
  if (Symbol* s = lib_.find(symbolId_)) s->name = newName_;
}

void RenameSymbolCommand::undo() {
  if (refused_) return;
  if (Symbol* s = lib_.find(symbolId_)) s->name = oldName_;
}

// ---- RenameChildCommand (instance name) ----

RenameChildCommand::RenameChildCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                                       std::string newName)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), newName_(std::move(newName)) {
  Symbol* s = lib_.find(symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  // refuse: missing symbol/child, or no-op (same name). An EMPTY new name is LEGAL — it clears the
  // custom name so the instance falls back to the def name (= TiXL ReadableName), so empty != refuse.
  if (!c || c->name == newName_) {
    refused_ = true;
    return;
  }
  oldName_ = c->name;
}

void RenameChildCommand::doIt() {
  if (refused_) return;
  Symbol* s = lib_.find(symbolId_);
  if (SymbolChild* c = s ? childById(*s, childId_) : nullptr) c->name = newName_;
}

void RenameChildCommand::undo() {
  if (refused_) return;
  Symbol* s = lib_.find(symbolId_);
  if (SymbolChild* c = s ? childById(*s, childId_) : nullptr) c->name = oldName_;
}

}  // namespace sw
