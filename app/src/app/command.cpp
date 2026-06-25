// app/command — CommandStack / MacroCommand 實作。
#include "app/command.h"

#include "app/document.h"  // bumpLibRevision: every executed/undone command mutated the lib

namespace sw {

CommandStack g_commands;

void MacroCommand::doIt() {
  for (auto& c : children_) c->doIt();
}
void MacroCommand::undo() {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo();
}

// The revision bump is the ONLY side channel here. The dynamic compound-spec table is
// deliberately NOT refreshed per-push: commands fire mid-frame while the inspector/canvas
// hold NodeSpec* into that table — swapping it here is a use-after-free (refuter N2 #1).
// frame_cook refreshes it at the frame boundary, keyed off the same revision.

void CommandStack::push(std::unique_ptr<Command> cmd) {
  cmd->doIt();
  undo_.push_back(std::move(cmd));
  redo_.clear();
  doc::bumpLibRevision();  // resident-projection contract (document.h)
}
void CommandStack::undo() {
  if (undo_.empty()) return;
  std::unique_ptr<Command> c = std::move(undo_.back());
  undo_.pop_back();
  c->undo();
  redo_.push_back(std::move(c));
  doc::bumpLibRevision();
}
void CommandStack::redo() {
  if (redo_.empty()) return;
  std::unique_ptr<Command> c = std::move(redo_.back());
  redo_.pop_back();
  c->doIt();
  undo_.push_back(std::move(c));
  doc::bumpLibRevision();
}
void CommandStack::clear() {
  undo_.clear();
  redo_.clear();
}
const char* CommandStack::lastUndoName() const {
  return undo_.empty() ? "" : undo_.back()->name();
}
// FORK (named): TiXL's UndoRedoStack exposes GetNextUndoTitle() but has NO GetNextRedoTitle()
// (its GraphContextMenu shows only an Undo title). This redo getter mirrors lastUndoName() so the
// context menu can show a parenthesized redo title too — a read-only sw parity extension, no state.
const char* CommandStack::lastRedoName() const {
  return redo_.empty() ? "" : redo_.back()->name();
}

}  // namespace sw
