// runtime/copy_paste — copy/paste of a SELECTION of children inside the compound model
// (契約 4 第三刀, 照 TiXL CopySymbolChildrenCommand.cs). Runtime leaf: pure CPU data on the
// SymbolLibrary model, no Metal, no UI, no upward deps (ARCHITECTURE.md runtime leaf). The
// undoable COMMAND that drives this lives in app/graph_commands (CopyPasteChildrenCommand);
// the Cmd+C/Cmd+V + context-menu GUI lives in ui. This file owns only the data semantics.
//
// TiXL语义对照 (CopySymbolChildrenCommand.cs):
//   • per child: new id + OldToNewChildIds remap            (.cs:90-98)
//   • connections: ONLY both-ends-in-selection survive,
//     external connections are cut                          (.cs:100-109 — the double `where`)
//   • multi-input order: source order preserved (TiXL reverses for insert-at-front; we APPEND so
//     no reverse — named FORK in planPaste, same no-reverse append招 as combine.cpp)  (.cs:110)
//   • per-child full state carried over                      (.cs:223-243)
//   • bypass deferred until after wires exist                (.cs:245-251, 269-276)
//   • paste position = targetPos + (childPos - upperLeftCorner) (.cs:77-95,215)
//   • cross-symbol paste via clipboard JSON; transient symbol
//     never enters the registry                              (.cs:312-317)
#pragma once
#include <map>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolChild, SymbolConnection
#include "runtime/curve_animator.h"  // Animator::CurveArray (copy/paste carries animation curves)

namespace sw {

// A self-contained snapshot of a selection, portable across symbols (and, serialized, across
// processes via the OS clipboard). Children carry their FULL per-child state (whatever our
// model holds: symbolId + overrides + position); wires are ONLY the both-ends-internal ones
// (TiXL .cs:100-109). Positions are stored RELATIVE to the selection's upper-left corner so
// paste can re-anchor at an arbitrary target point (.cs:77-95).
struct ClipboardChild {
  int id = 0;                              // ORIGINAL child id (the remap source key)
  std::string symbolId;                    // which Symbol this instantiates
  std::map<std::string, float> overrides;  // per-instance overrides (= override + IsDefault, see .cpp FORK)
  float relX = 0.0f, relY = 0.0f;          // position relative to selection upper-left
  // The animation curves bound to THIS child on the source symbol's Animator, keyed by inputId
  // (= TiXL CopySymbolChildrenCommand.cs:196-199 CopyAnimationsTo: the curves travel WITH the copied
  // child so a paste reproduces the animation). Empty when the child is unanimated.
  std::map<std::string, Animator::CurveArray> curves;
  // S2 (批次7) child structural state carried with the copy (= TiXL CopySymbolChildrenCommand.cs:223-243
  // full per-child state). isBypassed is applied AFTER the wires exist (the deferred bypass seam,
  // .cs:245-251,269-276); the per-output maps copy verbatim with the child.
  bool isBypassed = false;
  std::map<std::string, bool> disabledOutputs;
  std::map<std::string, TriggerOverride> triggerOverrides;
};

struct ClipboardData {
  std::vector<ClipboardChild> children;
  // Wires whose BOTH endpoints are in `children` (boundary-sentinel wires are external by
  // definition and never captured). srcChild/dstChild reference ORIGINAL ids; paste remaps
  // them. Order is the source symbol's connection order (multi-input order preserved on write).
  std::vector<SymbolConnection> wires;
};

// Build a clipboard from a selection of child ids inside `src`. Ignores ids not present and the
// boundary sentinel (0). Only both-ends-in-selection wires are captured (external wires cut —
// TiXL .cs:100-109). Positions stored relative to the selection's upper-left corner.
ClipboardData extractClipboard(const Symbol& src, const std::vector<int>& childIds);

// Serialize / parse clipboard JSON (cross-symbol + cross-process via imgui SetClipboardText /
// GetClipboardText). Self-describing tag {clipboardVersion:1,...}. parse returns false on any
// non-matching / malformed text so a paste of unrelated clipboard content is a clean no-op.
// NOTE: symbol names are NOT carried (we paste by symbolId reference, like TiXL's child refs);
// CJK only appears here inside symbolId strings if a compound id were non-ASCII — our compound
// ids are generated ASCII, so the crude_json non-ASCII assert (known trap) cannot fire on this
// path. If that ever changes, escape on write (named in the report).
std::string clipboardToJson(const ClipboardData& clip);
bool clipboardFromJson(const std::string& json, ClipboardData& out);

// One child the paste will create: the NEW id + the fully-formed SymbolChild (remapped, with
// absolute position). The command appends these and removes them by id on undo.
struct PastedChild {
  SymbolChild child;  // .id is the NEW id; ready to push into the target symbol
  // S2 deferred bypass (= TiXL .cs:245-251,269-276): the COPIED isBypassed is NOT baked into `child`
  // above — SetBypassed no-ops on an unwired fresh instance, so the bypass seam in
  // CopyPasteChildrenCommand::doIt applies it AFTER the wires are laid (and only when the pasted child
  // is actually bypassable + its main output got a wire). This field carries the intent to that seam.
  bool wantBypass = false;
};

// The deterministic result of planning a paste: the new children (with fresh ids), the wires
// remapped onto those new ids (multi-input order via reverse — TiXL .cs:110), the new ids to
// bump the target's monotonic floor by, and the oldToNew map (mirrors TiXL OldToNewChildIds).
// A PLAN, not an apply: the command (graph_commands) owns apply/undo so the undo is exact.
struct PastePlan {
  std::vector<PastedChild> children;
  std::vector<SymbolConnection> wires;     // remapped to new ids, in source (multi-input) order
  std::map<int, int> oldToNew;             // original child id -> new child id
  // Animation curves to install on the TARGET symbol's Animator, keyed by NEW childId -> inputId ->
  // curves (remapped through oldToNew). The command installs these in doIt and removes the same
  // (newChildId,inputId) entries in undo (= TiXL CopyAnimationsTo before child instantiation; our
  // command applies them alongside the children so undo cleans up without leaving殭屍 curves).
  std::map<int, std::map<std::string, Animator::CurveArray>> curves;
};

// Plan pasting `clip` into Symbol `targetId` of `lib`, anchoring the selection's upper-left at
// (pasteX, pasteY). New ids are allocated from the target's monotonic floor (nextFreeChildId),
// strictly increasing, never colliding (TiXL: Guid.NewGuid()). Children whose symbolId would
// CLOSE A CYCLE if instantiated in the target are DROPPED from the plan (and their wires with
// them) — reuses addChildWouldCycle, the SSOT gate (a compound pasted into its own ancestor =
// a cycle). Returns an empty plan if the target symbol is absent or every child was dropped.
//
// 曲线 / S3 (CLOSED): the curves bound to each copied child (carried on the clipboard) are remapped
// through oldToNew into plan.curves, keyed by NEW childId (= TiXL CopyAnimationsTo .cs:196-199,
// keyed by OldToNewChildIds). The command installs them on the target Animator alongside the
// children; undo removes them. Curves for a child dropped by the cycle gate are dropped with it.
PastePlan planPaste(const SymbolLibrary& lib, const std::string& targetId,
                    const ClipboardData& clip, float pasteX, float pasteY);

}  // namespace sw
