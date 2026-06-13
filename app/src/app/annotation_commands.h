// app/annotation_commands — Annotation 批A 命令層: the three TiXL Commands/Annotations/ + one named
// fork (ChangeAnnotationColorCommand). New file (照 child_state_commands / rename_commands 前例 — not
// crammed into the already-full graph_commands.cpp, ARCHITECTURE rule 4). Each command holds lib& +
// the OWNING symbolId (= TiXL command keyed on symbolUi.Symbol.Id) so undo edits the right composition
// no matter which layer the canvas is viewing (= our id-everywhere cross-layer undo, contract 4).
// Zone: app, depends on runtime/compound_graph.
//
// fork-C (named): TiXL ChangeAnnotationTextCommand holds the Annotation OBJECT by reference
// (.cs constructor `_annotation = annotation`) — safe in TiXL because the edit-time object is never
// replaced. We save/load can rebuild a fresh vector<Annotation>, so we locate by (symbolId, annotationId)
// instead (= our id-everywhere discipline, same as every graph_command). Semantics identical (reversible).
#pragma once
#include <string>
#include <utility>

#include "app/command.h"
#include "runtime/annotation.h"
#include "runtime/compound_graph.h"

namespace sw {

// Add a fully-built annotation to symbol `symbolId`'s list (= TiXL AddAnnotationCommand). Do = push;
// Undo = remove by id (mirror, = TiXL Do/Undo). REFUSES (refused()=true, caller skips push — no dead
// undo entry, 照環檢/rename 前例) when the symbol is missing or an annotation with this id already
// exists (id is the identity; a dup would make Undo ambiguous).
class AddAnnotationCommand : public Command {
 public:
  AddAnnotationCommand(SymbolLibrary& lib, std::string symbolId, Annotation annotation);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Add Annotation"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  Annotation annotation_;
  bool refused_ = false;
};

// Delete annotation `annotationId` from `symbolId` (= TiXL DeleteAnnotationCommand, EXACT mirror of
// Add). Do = snapshot the original + remove; Undo = re-add the original object verbatim. REFUSES on a
// missing symbol or unknown annotation id (nothing to delete).
class DeleteAnnotationCommand : public Command {
 public:
  DeleteAnnotationCommand(SymbolLibrary& lib, std::string symbolId, std::string annotationId);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Delete Annotation"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string annotationId_;
  Annotation original_;  // doIt snapshot (whole struct — restores byte-identical on undo)
  bool refused_ = false;
};

// Change an annotation's text (= TiXL ChangeAnnotationTextCommand + fork-F). TiXL's command stores/
// restores ONLY Title (.cs `_originalText = annotation.Title`); the rename dialog writes Label DIRECTLY
// (AnnotationRenaming.cs:96 `annotation.Label = _labelBuffer`) OUTSIDE any command and the close-gate
// only tests `_titleBuffer != _originalTitle` (:113) — so in TiXL a Label change is NOT undoable (real
// asymmetry: undo restores Title but leaves the new Label). fork-F (拍板: 補): we store/restore BOTH
// title and label so undo fully reverts a rename. REFUSES on missing symbol/annotation or no-op (both
// fields unchanged).
class ChangeAnnotationTextCommand : public Command {
 public:
  ChangeAnnotationTextCommand(SymbolLibrary& lib, std::string symbolId, std::string annotationId,
                              std::string newTitle, std::string newLabel);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Change Annotation Text"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string annotationId_;
  std::string newTitle_, newLabel_;
  std::string oldTitle_, oldLabel_;
  bool refused_ = false;
};

// Change an annotation's color (fork-D, named: TiXL has NO dedicated color command under
// Commands/Annotations/ — only Add/Delete/ChangeText). 拍板: 採 — mirror the text command (store the
// original RGBA, Do sets new, Undo restores). REFUSES on missing symbol/annotation or no-op (same RGBA).
class ChangeAnnotationColorCommand : public Command {
 public:
  ChangeAnnotationColorCommand(SymbolLibrary& lib, std::string symbolId, std::string annotationId,
                               const float newColor[4]);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Change Annotation Color"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string annotationId_;
  float new_[4];
  float old_[4] = {0, 0, 0, 0};
  bool refused_ = false;
};

// Move and/or resize an annotation in one undoable step (= TiXL's ModifyCanvasElementsCommand path,
// AnnotationDragging.cs:62 / AnnotationResizing.cs:42 — TiXL has NO annotation-specific move/resize
// command; pos/size edits ride the generic "modify canvas elements" command shared with nodes). We
// have no such generic command, so 批B adds this small dedicated one (spec 契約3: "移動/縮放各自一個小
// command 帶 (oldPos/Size, newPos/Size)" — named fork "dedicated annotation move/resize"). One command
// carries BOTH pos and size so a drag-move (size unchanged) and a corner-resize (pos unchanged) share
// the same undo unit shape. REFUSES on missing symbol/annotation or a true no-op (pos+size unchanged).
// by-id located (fork-C), like the other annotation commands.
class MoveResizeAnnotationCommand : public Command {
 public:
  MoveResizeAnnotationCommand(SymbolLibrary& lib, std::string symbolId, std::string annotationId,
                              float newX, float newY, float newW, float newH);
  void doIt() override;
  void undo() override;
  const char* name() const override { return "Move/Resize Annotation"; }
  bool refused() const { return refused_; }

 private:
  SymbolLibrary& lib_;
  std::string symbolId_;
  std::string annotationId_;
  float newX_, newY_, newW_, newH_;
  float oldX_ = 0, oldY_ = 0, oldW_ = 0, oldH_ = 0;
  bool refused_ = false;
};

// Annotation 批A golden (annotation_selftest.cpp): the struct defaults (gray color, empty text, not
// collapsed); the four commands' do->undo->redo symmetry (add/delete mirror, text incl. fork-F Label
// undo, color); refusal on missing/dup/no-op; the savev2 "annotations" segment roundtrips BYTE-STABLE
// (ASCII + 中文 title, omission rules); an old file with NO annotations segment loads with ZERO warning;
// a malformed annotation (missing id) is dropped locally without failing the file; per-symbol isolation
// (two symbols' annotations don't bleed). injectBug breaks one 承重 leg -> FAIL (teeth).
int runAnnotationSelfTest(bool injectBug);

}  // namespace sw
