// app/child_state_commands — S2 (批次7) 命令層: Child 結構补欄的三個 undoable 命令
// （isBypassed / per-output isDisabled / per-output triggerOverride），全照 TiXL Symbol.Child.cs。
// 開新檔（照 rename_commands 前例，不塞進已滿的 graph_commands.cpp）。命令持 lib& + symbolId
// （= TiXL 命令掛在定義上）；refused 不上 undo stack（照環檢/rename 前例）。Zone: app,
// 依賴 runtime/compound_graph。
//
// 三個命令都是 SPARSE 寫入：disable/trigger 用 map 表示「非預設輸出」，回到預設 = erase key，
// 所以從未動過的輸出不留殘渣（= override 模型）。bypass 是 child 一個 bool。
#include "app/graph_commands.h"

#include <utility>

#include "runtime/compound_graph.h"

namespace sw {
namespace {

Symbol* sym(SymbolLibrary& lib, const std::string& id) { return lib.find(id); }

// Is this child's MAIN output (outputDefs[0]) wired as the SOURCE of any connection in `parent`?
// = TiXL SetBypassed's isOutputConnected scan (.cs:288-297): bypass of an unconnected main output
// is meaningless (nothing reads the passthrough) so SetBypassed refuses it. We reproduce the gate.
bool mainOutputWired(const SymbolLibrary& lib, const Symbol& parent, const SymbolChild& c) {
  const Symbol* def = lib.find(c.symbolId);
  if (!def || def->outputDefs.empty()) return false;
  const std::string mainOut = def->outputDefs[0].id;
  for (const SymbolConnection& w : parent.connections)
    if (w.srcChild == c.id && w.srcSlot == mainOut) return true;
  return false;
}

// Does the child's referenced symbol define `slotId` as an OUTPUT? (the per-output commands only act
// on real output slots; a stale id is refused, not silently stored — no zombie per-output state.)
bool hasOutputDef(const SymbolLibrary& lib, const SymbolChild& c, const std::string& slotId) {
  const Symbol* def = lib.find(c.symbolId);
  if (!def) return false;
  for (const SlotDef& d : def->outputDefs)
    if (d.id == slotId) return true;
  return false;
}

// The child's MAIN output slot id ("" when unresolvable) — the slot bypass redirects.
std::string mainOutputSlot(const SymbolLibrary& lib, const SymbolChild& c) {
  const Symbol* def = lib.find(c.symbolId);
  return (def && !def->outputDefs.empty()) ? def->outputDefs[0].id : std::string();
}

// Is the child's main output currently disabled (frozen)?
bool mainOutputDisabled(const SymbolLibrary& lib, const SymbolChild& c) {
  auto it = c.disabledOutputs.find(mainOutputSlot(lib, c));
  return it != c.disabledOutputs.end() && it->second;
}

}  // namespace

// ---- SetBypassChildCommand ----

SetBypassChildCommand::SetBypassChildCommand(SymbolLibrary& lib, std::string symbolId, int childId,
                                             bool bypass)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId), new_(bypass) {
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) { refused_ = true; return; }
  old_ = c->isBypassed;
  if (new_ == old_) { refused_ = true; return; }  // no-op
  // TiXL guards: a non-bypassable type (childIsBypassable) is silently ignored by SetBypassed; and
  // ENABLING bypass of an unconnected main output is refused (.cs:299-300). DISABLING (new_==false)
  // is always allowed — you can always un-bypass (TiXL hits RestoreUpdateAction regardless of wiring).
  if (new_) {
    if (!childIsBypassable(lib_, *c)) { refused_ = true; return; }
    if (!mainOutputWired(lib_, *s, *c)) { refused_ = true; return; }
    // Mutual exclusion with disable (= TiXL Slot.cs:50-53: the SECOND op is refused — bypass and
    // freeze both replace the update action; stacking them makes one a dead knob).
    if (mainOutputDisabled(lib_, *c)) { refused_ = true; return; }
  }
}
void SetBypassChildCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (SymbolChild* c = s ? childById(*s, childId_) : nullptr) c->isBypassed = new_;
}
void SetBypassChildCommand::undo() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (SymbolChild* c = s ? childById(*s, childId_) : nullptr) c->isBypassed = old_;
}

// ---- SetOutputDisabledCommand ----

SetOutputDisabledCommand::SetOutputDisabledCommand(SymbolLibrary& lib, std::string symbolId,
                                                   int childId, std::string outputSlot, bool disabled)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId),
      outputSlot_(std::move(outputSlot)), new_(disabled) {
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c || !hasOutputDef(lib_, *c, outputSlot_)) { refused_ = true; return; }
  auto it = c->disabledOutputs.find(outputSlot_);
  hadOld_ = it != c->disabledOutputs.end();
  old_ = hadOld_ && it->second;
  // Effective current state (absent key == enabled == false). No-op if unchanged.
  if ((hadOld_ ? it->second : false) == new_) { refused_ = true; return; }
  // Mutual exclusion with bypass on the MAIN output (= TiXL Slot.cs:50-53, second op refused):
  // disabling the redirected slot of a bypassed child would be a knob with no visible effect.
  if (new_ && c->isBypassed && outputSlot_ == mainOutputSlot(lib_, *c)) { refused_ = true; return; }
}
void SetOutputDisabledCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) return;
  if (new_) c->disabledOutputs[outputSlot_] = true;
  else c->disabledOutputs.erase(outputSlot_);  // back to default -> no residue
}
void SetOutputDisabledCommand::undo() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) return;
  if (hadOld_) c->disabledOutputs[outputSlot_] = old_;
  else c->disabledOutputs.erase(outputSlot_);
}

// ---- SetOutputTriggerCommand ----

SetOutputTriggerCommand::SetOutputTriggerCommand(SymbolLibrary& lib, std::string symbolId,
                                                 int childId, std::string outputSlot,
                                                 TriggerOverride trigger)
    : lib_(lib), symbolId_(std::move(symbolId)), childId_(childId),
      outputSlot_(std::move(outputSlot)), new_(trigger) {
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c || !hasOutputDef(lib_, *c, outputSlot_)) { refused_ = true; return; }
  auto it = c->triggerOverrides.find(outputSlot_);
  hadOld_ = it != c->triggerOverrides.end();
  old_ = hadOld_ ? it->second : TriggerOverride::None;
  if (old_ == new_) { refused_ = true; return; }  // no-op (absent key == None)
}
void SetOutputTriggerCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) return;
  if (new_ == TriggerOverride::None) c->triggerOverrides.erase(outputSlot_);  // back to default
  else c->triggerOverrides[outputSlot_] = new_;
}
void SetOutputTriggerCommand::undo() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  SymbolChild* c = s ? childById(*s, childId_) : nullptr;
  if (!c) return;
  if (hadOld_) c->triggerOverrides[outputSlot_] = old_;
  else c->triggerOverrides.erase(outputSlot_);
}

}  // namespace sw
