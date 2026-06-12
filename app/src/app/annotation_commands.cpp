// app/annotation_commands — impl of the Annotation 批A commands (header doc lists each command's TiXL
// origin + fork). All locate by (symbolId, annotationId) (fork-C): a command never holds a raw
// Annotation* because save/load rebuilds the vector. A refused command is a true no-op on both Do and
// Undo (refused_ short-circuits) so the caller can drop it without a dead undo entry. Zone: app.
#include "app/annotation_commands.h"

#include <utility>

namespace sw {
namespace {

Symbol* sym(SymbolLibrary& lib, const std::string& id) { return lib.find(id); }

// Locate an annotation by id within a symbol's list (= our by-id discipline; nullptr if absent).
Annotation* annById(Symbol* s, const std::string& id) {
  if (!s) return nullptr;
  for (Annotation& a : s->annotations)
    if (a.id == id) return &a;
  return nullptr;
}

void copyRgba(float dst[4], const float src[4]) {
  for (int i = 0; i < 4; ++i) dst[i] = src[i];
}
bool rgbaEqual(const float a[4], const float b[4]) {
  for (int i = 0; i < 4; ++i)
    if (a[i] != b[i]) return false;
  return true;
}

}  // namespace

// ---- AddAnnotationCommand ----

AddAnnotationCommand::AddAnnotationCommand(SymbolLibrary& lib, std::string symbolId,
                                           Annotation annotation)
    : lib_(lib), symbolId_(std::move(symbolId)), annotation_(std::move(annotation)) {
  Symbol* s = sym(lib_, symbolId_);
  if (!s || annotation_.id.empty()) { refused_ = true; return; }
  if (annById(s, annotation_.id)) { refused_ = true; return; }  // dup id -> Undo would be ambiguous
}
void AddAnnotationCommand::doIt() {
  if (refused_) return;
  if (Symbol* s = sym(lib_, symbolId_)) s->annotations.push_back(annotation_);
}
void AddAnnotationCommand::undo() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto& v = s->annotations;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i].id == annotation_.id) { v.erase(v.begin() + i); return; }
}

// ---- DeleteAnnotationCommand (exact mirror of Add) ----

DeleteAnnotationCommand::DeleteAnnotationCommand(SymbolLibrary& lib, std::string symbolId,
                                                 std::string annotationId)
    : lib_(lib), symbolId_(std::move(symbolId)), annotationId_(std::move(annotationId)) {
  Annotation* a = annById(sym(lib_, symbolId_), annotationId_);
  if (!a) { refused_ = true; return; }
  original_ = *a;  // snapshot the whole struct so Undo restores byte-identical (= TiXL re-add)
}
void DeleteAnnotationCommand::doIt() {
  if (refused_) return;
  Symbol* s = sym(lib_, symbolId_);
  if (!s) return;
  auto& v = s->annotations;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i].id == annotationId_) { v.erase(v.begin() + i); return; }
}
void DeleteAnnotationCommand::undo() {
  if (refused_) return;
  if (Symbol* s = sym(lib_, symbolId_)) s->annotations.push_back(original_);
}

// ---- ChangeAnnotationTextCommand (Title + Label, fork-F补 Label undo) ----

ChangeAnnotationTextCommand::ChangeAnnotationTextCommand(SymbolLibrary& lib, std::string symbolId,
                                                         std::string annotationId,
                                                         std::string newTitle, std::string newLabel)
    : lib_(lib), symbolId_(std::move(symbolId)), annotationId_(std::move(annotationId)),
      newTitle_(std::move(newTitle)), newLabel_(std::move(newLabel)) {
  Annotation* a = annById(sym(lib_, symbolId_), annotationId_);
  if (!a) { refused_ = true; return; }
  oldTitle_ = a->title;
  oldLabel_ = a->label;
  if (oldTitle_ == newTitle_ && oldLabel_ == newLabel_) { refused_ = true; return; }  // no-op
}
void ChangeAnnotationTextCommand::doIt() {
  if (refused_) return;
  if (Annotation* a = annById(sym(lib_, symbolId_), annotationId_)) {
    a->title = newTitle_;
    a->label = newLabel_;
  }
}
void ChangeAnnotationTextCommand::undo() {
  if (refused_) return;
  if (Annotation* a = annById(sym(lib_, symbolId_), annotationId_)) {
    a->title = oldTitle_;
    a->label = oldLabel_;
  }
}

// ---- ChangeAnnotationColorCommand (fork-D, mirrors text) ----

ChangeAnnotationColorCommand::ChangeAnnotationColorCommand(SymbolLibrary& lib, std::string symbolId,
                                                           std::string annotationId,
                                                           const float newColor[4])
    : lib_(lib), symbolId_(std::move(symbolId)), annotationId_(std::move(annotationId)) {
  copyRgba(new_, newColor);
  Annotation* a = annById(sym(lib_, symbolId_), annotationId_);
  if (!a) { refused_ = true; return; }
  copyRgba(old_, a->color);
  if (rgbaEqual(old_, new_)) { refused_ = true; return; }  // no-op
}
void ChangeAnnotationColorCommand::doIt() {
  if (refused_) return;
  if (Annotation* a = annById(sym(lib_, symbolId_), annotationId_)) copyRgba(a->color, new_);
}
void ChangeAnnotationColorCommand::undo() {
  if (refused_) return;
  if (Annotation* a = annById(sym(lib_, symbolId_), annotationId_)) copyRgba(a->color, old_);
}

}  // namespace sw
