// app/command — CommandStack / MacroCommand 實作。
#include "app/command.h"

#include "app/document.h"  // bumpGraphRevision: every executed/undone command mutated the graph

namespace sw {

CommandStack g_commands;

void MacroCommand::doIt() {
  for (auto& c : children_) c->doIt();
}
void MacroCommand::undo() {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) (*it)->undo();
}

void CommandStack::push(std::unique_ptr<Command> cmd) {
  cmd->doIt();
  undo_.push_back(std::move(cmd));
  redo_.clear();
  doc::bumpGraphRevision();  // mirror contract (document.h): the command just mutated the graph
}
void CommandStack::undo() {
  if (undo_.empty()) return;
  std::unique_ptr<Command> c = std::move(undo_.back());
  undo_.pop_back();
  c->undo();
  redo_.push_back(std::move(c));
  doc::bumpGraphRevision();
}
void CommandStack::redo() {
  if (redo_.empty()) return;
  std::unique_ptr<Command> c = std::move(redo_.back());
  redo_.pop_back();
  c->doIt();
  undo_.push_back(std::move(c));
  doc::bumpGraphRevision();
}
void CommandStack::clear() {
  undo_.clear();
  redo_.clear();
}
const char* CommandStack::lastUndoName() const {
  return undo_.empty() ? "" : undo_.back()->name();
}

}  // namespace sw
