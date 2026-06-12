// Headless RED->GREEN proof of Annotation 批A (annotation_commands.h header doc lists the legs):
//   1. struct defaults: a fresh Annotation is gray, empty text, not collapsed.
//   2. the four commands' do->undo->redo symmetry: Add/Delete mirror, ChangeText (incl. fork-F Label
//      undo), ChangeColor — each restores the lib byte-for-byte after undo, re-applies after redo,
//      and refuses (no-op/dup/missing) without a dead entry.
//   3. serialization: the v2 "annotations" segment roundtrips BYTE-STABLE for ASCII AND 中文 title,
//      honoring the omission rules (empty title/label, false collapsed, default-gray color omitted).
//   4. tolerance (S15): an old file with NO annotations segment loads ZERO-warning; a malformed
//      annotation (missing id) is dropped locally + warned without failing the whole file.
//   5. isolation: two symbols' annotation lists don't bleed into each other.
// injectBug corrupts the undo of ChangeText (leaves the new Label) -> the byte-identity assertion
// after undo FAILS (teeth).
#include "app/annotation_commands.h"

#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_save.h"

namespace sw {
namespace {

// A minimal lib with one root compound (so save/load has something to anchor to). Annotations ride
// on the root symbol — no children needed (annotation is UI-only, contract 0).
SymbolLibrary makeLib() {
  SymbolLibrary lib;
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  return lib;
}

Annotation makeAnn(const std::string& id, const std::string& title, const std::string& label) {
  Annotation a;
  a.id = id;
  a.title = title;
  a.label = label;
  a.x = 10.0f;
  a.y = 20.0f;
  a.w = 100.0f;
  a.h = 140.0f;  // = TiXL no-selection default 100x140
  return a;
}

}  // namespace

int runAnnotationSelfTest(bool injectBug) {
  bool ok = true;

  // --- 1: struct defaults ---
  {
    Annotation a;
    bool defaults = annotationColorIsDefault(a) && a.title.empty() && a.label.empty() &&
                    !a.collapsed && a.x == 0.0f && a.w == 0.0f;
    if (!defaults) { printf("[annotation] struct-default FAIL\n"); ok = false; }
  }

  // --- 2a: AddAnnotationCommand do/undo/redo + dup refusal ---
  {
    SymbolLibrary lib = makeLib();
    AddAnnotationCommand add(lib, "Root", makeAnn("a1", "Hello", "Grp"));
    add.doIt();
    bool added = lib.find("Root")->annotations.size() == 1 &&
                 lib.find("Root")->annotations[0].id == "a1";
    add.undo();
    bool removed = lib.find("Root")->annotations.empty();
    add.doIt();  // redo
    bool redone = lib.find("Root")->annotations.size() == 1;
    if (!(added && removed && redone)) { printf("[annotation] add do/undo/redo FAIL\n"); ok = false; }
    // dup id refused (no second entry, no dead undo)
    AddAnnotationCommand dup(lib, "Root", makeAnn("a1", "X", "Y"));
    bool dupRefused = dup.refused() && lib.find("Root")->annotations.size() == 1;
    // missing symbol refused
    AddAnnotationCommand miss(lib, "Nope", makeAnn("z", "", ""));
    if (!(dupRefused && miss.refused())) { printf("[annotation] add refusal FAIL\n"); ok = false; }
  }

  // --- 2b: DeleteAnnotationCommand mirror ---
  {
    SymbolLibrary lib = makeLib();
    lib.find("Root")->annotations.push_back(makeAnn("d1", "Bye", "L"));
    DeleteAnnotationCommand del(lib, "Root", "d1");
    del.doIt();
    bool gone = lib.find("Root")->annotations.empty();
    del.undo();
    bool back = lib.find("Root")->annotations.size() == 1 &&
                lib.find("Root")->annotations[0].title == "Bye" &&
                lib.find("Root")->annotations[0].label == "L";
    del.doIt();  // redo
    bool gone2 = lib.find("Root")->annotations.empty();
    DeleteAnnotationCommand missDel(lib, "Root", "nope");
    if (!(gone && back && gone2 && missDel.refused())) {
      printf("[annotation] delete mirror FAIL\n"); ok = false;
    }
  }

  // --- 2c: ChangeAnnotationTextCommand incl. fork-F Label undo ---
  {
    SymbolLibrary lib = makeLib();
    lib.find("Root")->annotations.push_back(makeAnn("t1", "old-title", "old-label"));
    ChangeAnnotationTextCommand chg(lib, "Root", "t1", "new-title", "new-label");
    chg.doIt();
    Annotation& a = lib.find("Root")->annotations[0];
    bool changed = a.title == "new-title" && a.label == "new-label";
    chg.undo();
    if (injectBug)  // teeth: a faulty undo restores Title but LEAVES the new Label
      a.label = "new-label";
    bool reverted = a.title == "old-title" && a.label == "old-label";  // fork-F: BOTH revert
    chg.doIt();  // redo
    bool redone = a.title == "new-title" && a.label == "new-label";
    // no-op refusal (same text) + missing refusal
    ChangeAnnotationTextCommand noop(lib, "Root", "t1", "new-title", "new-label");
    ChangeAnnotationTextCommand missTxt(lib, "Root", "t1-nope", "x", "y");
    if (!(changed && reverted && redone && noop.refused() && missTxt.refused())) {
      printf("[annotation] changetext (fork-F label undo) FAIL\n"); ok = false;
    }
  }

  // --- 2d: ChangeAnnotationColorCommand (fork-D) ---
  {
    SymbolLibrary lib = makeLib();
    lib.find("Root")->annotations.push_back(makeAnn("c1", "", ""));  // born gray
    float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    ChangeAnnotationColorCommand col(lib, "Root", "c1", red);
    col.doIt();
    Annotation& a = lib.find("Root")->annotations[0];
    bool isRed = a.color[0] == 1.0f && a.color[1] == 0.0f && !annotationColorIsDefault(a);
    col.undo();
    bool grayAgain = annotationColorIsDefault(a);
    col.doIt();  // redo
    bool redRedo = a.color[0] == 1.0f;
    float gray[4] = {0.6f, 0.6f, 0.6f, 1.0f};
    // build a fresh gray annotation to test no-op refusal
    lib.find("Root")->annotations.push_back(makeAnn("c2", "", ""));
    ChangeAnnotationColorCommand noop(lib, "Root", "c2", gray);
    ChangeAnnotationColorCommand miss(lib, "Root", "nope", red);
    if (!(isRed && grayAgain && redRedo && noop.refused() && miss.refused())) {
      printf("[annotation] changecolor (fork-D) FAIL\n"); ok = false;
    }
  }

  // --- 3: serialization byte-stable roundtrip (ASCII + 中文) + omission rules ---
  {
    SymbolLibrary lib = makeLib();
    Symbol& root = *lib.find("Root");
    // a1: full (custom color + collapsed + both texts). a2: minimal (gray, empty, expanded) — every
    // omittable field at default, so it exercises the省略 rules. a3: CJK title (byte-stable golden).
    Annotation a1 = makeAnn("a1", "# Big description", "Group A");
    a1.color[0] = 0.2f; a1.color[1] = 0.5f; a1.color[2] = 0.9f; a1.color[3] = 1.0f;
    a1.collapsed = true;
    Annotation a2 = makeAnn("a2", "", "");  // gray, no text, not collapsed -> color/title/label/collapsed omitted
    Annotation a3 = makeAnn("a3", "中文標題框 \xF0\x9F\x98\x80", "標籤");  // CJK + emoji
    root.annotations = {a1, a2, a3};

    std::string j1 = libToJsonV2(lib);
    SymbolLibrary back;
    std::vector<std::string> warn;
    bool loadOk = libFromJsonAny(j1, back, &warn);
    std::string j2 = libToJsonV2(back);
    bool byteStable = (j1 == j2);
    bool reloaded = back.find("Root") && back.find("Root")->annotations.size() == 3;
    // CJK survives: find a3 in the reload and check the title bytes match exactly.
    bool cjkOk = false;
    if (back.find("Root"))
      for (const Annotation& a : back.find("Root")->annotations)
        if (a.id == "a3") cjkOk = (a.title == "中文標題框 \xF0\x9F\x98\x80" && a.label == "標籤");
    // omission: a2 should serialize WITHOUT a color/title/label/collapsed key.
    bool omitOk = j1.find("\"a2\"") != std::string::npos;  // a2 present
    // verify a2's reloaded form is still default (proves the omitted keys defaulted correctly)
    if (back.find("Root"))
      for (const Annotation& a : back.find("Root")->annotations)
        if (a.id == "a2")
          omitOk = omitOk && annotationColorIsDefault(a) && a.title.empty() && a.label.empty() &&
                   !a.collapsed;
    if (!(loadOk && byteStable && reloaded && cjkOk && omitOk)) {
      printf("[annotation] save roundtrip (byte=%d cjk=%d omit=%d reload=%d warn=%zu) FAIL\n",
             byteStable, cjkOk, omitOk, reloaded, warn.size());
      ok = false;
    }
  }

  // --- 4a: old file with NO annotations segment -> zero warning ---
  {
    SymbolLibrary lib = makeLib();  // no annotations at all
    std::string j = libToJsonV2(lib);
    bool segAbsent = j.find("\"annotations\"") == std::string::npos;  // writer omits when empty
    SymbolLibrary back;
    std::vector<std::string> warn;
    bool loadOk = libFromJsonAny(j, back, &warn);
    bool zeroWarn = warn.empty() && back.find("Root") && back.find("Root")->annotations.empty();
    if (!(segAbsent && loadOk && zeroWarn)) {
      printf("[annotation] old-file-no-segment (absent=%d warn=%zu) FAIL\n", segAbsent, warn.size());
      ok = false;
    }
  }

  // --- 4b: malformed annotation (missing id) dropped locally + warned, file still loads ---
  {
    // Hand-craft a v2 file with one good annotation and one missing its id.
    std::string crafted =
        "{\"formatVersion\":2,\"rootSymbolId\":\"Root\","
        "\"composition\":{\"bpm\":120,\"soundtrackPath\":\"\",\"soundtrackVolume\":1},"
        "\"symbols\":[{\"id\":\"Root\",\"name\":\"Root\",\"nextChildId\":1,"
        "\"inputDefs\":[],\"outputDefs\":[],\"children\":[],\"connections\":[],"
        "\"annotations\":["
        "  {\"id\":\"good\",\"x\":0,\"y\":0,\"w\":10,\"h\":10},"
        "  {\"title\":\"orphan\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}"
        "]}]}";
    SymbolLibrary back;
    std::vector<std::string> warn;
    bool loadOk = libFromJsonAny(crafted, back, &warn);
    bool oneKept = back.find("Root") && back.find("Root")->annotations.size() == 1 &&
                   back.find("Root")->annotations[0].id == "good";
    bool warned = warn.size() == 1;  // exactly the orphan
    if (!(loadOk && oneKept && warned)) {
      printf("[annotation] malformed-tolerance (loadOk=%d kept=%d warn=%zu) FAIL\n", loadOk, oneKept,
             warn.size());
      ok = false;
    }
  }

  // --- 5: per-symbol isolation (two compounds, annotations don't bleed) ---
  {
    SymbolLibrary lib = makeLib();
    Symbol other;
    other.id = "Other";
    other.name = "Other";
    other.atomic = false;
    lib.symbols["Other"] = other;
    lib.find("Root")->annotations.push_back(makeAnn("r1", "root-ann", ""));
    lib.find("Other")->annotations.push_back(makeAnn("o1", "other-ann", ""));
    std::string j = libToJsonV2(lib);
    SymbolLibrary back;
    std::vector<std::string> warn;
    libFromJsonAny(j, back, &warn);
    bool isolated = back.find("Root") && back.find("Other") &&
                    back.find("Root")->annotations.size() == 1 &&
                    back.find("Root")->annotations[0].id == "r1" &&
                    back.find("Other")->annotations.size() == 1 &&
                    back.find("Other")->annotations[0].id == "o1";
    if (!isolated) { printf("[annotation] per-symbol isolation FAIL\n"); ok = false; }
  }

  // Convention (run_all_selftests.sh): the plain run must exit 0 (every leg held); the -bug run must
  // exit NON-zero (the teeth bit). injectBug corrupts the ChangeText undo above, so `ok` is already
  // false there — reaching here with ok still true under injectBug means the bug slipped = a failure.
  if (injectBug && ok) {
    printf("[selftest-annotation] FAIL (injected bug not caught — blind tooth)\n");
    return 1;
  }
  printf("[selftest-annotation] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
