// eye_selftest.mm — the headless RED->GREEN teeth for verify/eye (see eye.h).
// Split out of eye.mm (ARCHITECTURE rule 4: one file one duty — eye.mm is the live sink,
// this TU is the proof). Drives the REAL detail:: buffers + popup walk the live map uses,
// so the test exercises exactly what the app runs, not a copy. ObjC++ / MRC like eye.mm.
#include "verify/eye/eye.h"
#include "verify/eye/eye_internal.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "imgui.h"
#include "imgui_internal.h"  // GetCurrentWindow()->ID for the occlusion self-test owner

namespace sw::eye {

int runSelfTest(bool injectBug) {
  @autoreleasepool {
    const int W = 8, H = 8;
    const uint8_t R = 40, G = 80, B = 160;  // known truth
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (int i = 0; i < W * H; ++i) {
      px[i * 4 + 0] = injectBug ? 200 : R;  // wrong color when injecting a bug
      px[i * 4 + 1] = injectBug ? 10 : G;
      px[i * 4 + 2] = injectBug ? 10 : B;
      px[i * 4 + 3] = 255;
    }
    detail::ensureDir();
    std::string path = detail::outPath("selftest_eye.png");
    if (!detail::writePNG(path, px.data(), W, H)) {
      printf("[selftest-eye] FAIL: writePNG\n");
      return 1;
    }
    // Reload through a fresh decode and read RAW decoded bytes (CGImage's data
    // provider), NOT via NSColor — so no color management sits between the file
    // and the assertion. Proves the PNG round-trips byte-exact.
    NSData* d = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
    CGImageSourceRef src = CGImageSourceCreateWithData((CFDataRef)d, NULL);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
    if (!img) {
      printf("[selftest-eye] FAIL: decode\n");
      if (src) CFRelease(src);
      return 1;
    }
    CFDataRef pix = CGDataProviderCopyData(CGImageGetDataProvider(img));
    const UInt8* bytes = CFDataGetBytePtr(pix);
    size_t bpr = CGImageGetBytesPerRow(img);
    const UInt8* p = bytes + (H / 2) * bpr + (W / 2) * 4;
    int gr = p[0], gg = p[1], gb = p[2];
    CFRelease(pix);
    CGImageRelease(img);
    CFRelease(src);
    bool pass = std::abs(gr - R) <= 2 && std::abs(gg - G) <= 2 && std::abs(gb - B) <= 2;
    printf("[selftest-eye] center=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n", gr, gg, gb, R, G, B,
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
}

int runMapSelfTest(bool injectBug) {
  @autoreleasepool {
    // Headless ImGui frame: no platform/renderer backend, just DisplaySize + a built
    // font atlas (NewFrame asserts both). This exercises the SAME recordItem() the app
    // feeds from editor_ui — GetItemRectMin reads g.LastItemData, which only exists
    // inside an active frame, so a real frame is the honest way to test the map path.
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* tex = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);  // build the atlas so NewFrame passes
    io.Fonts->SetTexID((ImTextureID)1);

    auto& gItems = detail::items();
    auto& gNative = detail::nativeItems();

    // One toolbar pass, recorded exactly like drawToolbar(): unconditional recordItem()
    // after each widget, plus the Add-Node popup's rows when it is open.
    auto toolbarPass = [&](bool openPopup, bool closeInside, bool* popupWasOpen) {
      ImGui::Begin("Toolbar");
      ImGui::Button("New");      recordItem("New");
      ImGui::Button("Open");     recordItem("Open");
      ImGui::Button("Save");     recordItem("Save");
      ImGui::Button("Add Node"); recordItem("Add Node");
      if (openPopup) ImGui::OpenPopup("add_node_popup");
      if (ImGui::BeginPopup("add_node_popup")) {
        if (popupWasOpen) *popupWasOpen = true;
        ImGui::MenuItem("RadialPoints"); recordItem("menu:RadialPoints");
        ImGui::MenuItem("LinePoints");   recordItem("menu:LinePoints");
        if (closeInside) ImGui::CloseCurrentPopup();  // mirrors clicking a row -> popup dismissed
        ImGui::EndPopup();
      }
      ImGui::End();
    };

    // Frame 1 — fresh: open the popup. Toolbar buttons must be recorded.
    ImGui::NewFrame();
    beginWidgetFrame();
    toolbarPass(/*openPopup=*/true, /*closeInside=*/false, nullptr);
    const size_t c1 = gItems.size();
    ImGui::EndFrame();

    // Frame 2 — popup OPEN: its rows are recorded, then we dismiss it (the user's click).
    bool popupOpen2 = false;
    ImGui::NewFrame();
    beginWidgetFrame();
    toolbarPass(/*openPopup=*/false, /*closeInside=*/true, &popupOpen2);
    const size_t c2 = gItems.size();
    const bool menuRecorded2 = detail::itemsHavePrefix("menu:");
    ImGui::EndFrame();

    // Frame 3 — popup CLOSED: THE reported failing frame. The map must still be
    // populated, and must carry NO stale "menu:" rows from frame 2 (clear+refill).
    // injectBug models the regression by suppressing this post-popup record pass.
    ImGui::NewFrame();
    beginWidgetFrame();
    if (!injectBug) toolbarPass(/*openPopup=*/false, /*closeInside=*/false, nullptr);
    const size_t c3 = gItems.size();
    const bool staleMenu3 = detail::itemsHavePrefix("menu:");
    ImGui::EndFrame();

    // Frame 4 — BeginPopupContextItem path (批次9: the Inspector param right-click menu,
    // "popup rows must be findable in the map" live pain). The popup is keyed to the LAST
    // item's id; headless can't right-click, so OpenPopup on the SAME str_id (same window
    // scope -> same ImGuiID) stands in for it — the RECORD path is what's under test, not
    // the right-click mechanics. injectBug suppresses the row's record -> leg goes RED.
    ImGui::NewFrame();
    beginWidgetFrame();
    ImGui::Begin("Inspector");
    ImGui::Button("Center");
    recordItem("param:center");
    ImGui::OpenPopup("##anim_center");
    bool ctxPopupSeen = false;
    if (ImGui::BeginPopupContextItem("##anim_center")) {
      ctxPopupSeen = true;
      ImGui::MenuItem("Animate");
      if (!injectBug) recordItem("insp:Animate");
      ImGui::EndPopup();
    }
    ImGui::End();
    const bool inspRecorded = ctxPopupSeen && detail::itemsHavePrefix("insp:");
    ImGui::EndFrame();

    // Native NSMenu rows (批次9): registered once at startup, must SURVIVE the per-frame
    // clear (they're not imgui widgets) and never leak into the rect items.
    gNative.clear();
    if (!injectBug) recordNativeMenuItem("File", "Save", "s", false);
    beginWidgetFrame();
    const bool nativeOk = gNative.size() == 1 &&
                          gNative[0].label == "nsmenu:File:Save" &&
                          gNative[0].shortcut == "cmd+s";
    gNative.clear();  // don't leak test rows into a live map

    // --- Gap 1: an OPEN combo's dropdown rows must surface as popup_item rects, and must
    // be ABSENT when no popup is open. The combo popup is an internal "##Combo_.." window
    // the app never draws item-by-item, so only recordOpenPopupItems()'s OpenPopupStack
    // walk can see it. We open the combo by injecting a real click on its button (BeginCombo
    // toggles the popup on click), then assert popup_item rows appear. injectBug skips the
    // walk so the rows never materialize -> RED.
    const char* kComboItems = "Slot 0\0Slot 1\0Slot 2\0";  // 3 options (mirrors the crossfade picker)
    int comboSel = 0;
    ImVec2 comboMin, comboMax;
    auto comboPass = [&]() {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(ImVec2(400, 300));
      ImGui::Begin("combowin", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
      ImGui::SetNextItemWidth(120.0f);
      ImGui::Combo("Left", &comboSel, kComboItems);  // app records only the button rect (recordItem("Left"))
      comboMin = ImGui::GetItemRectMin();
      comboMax = ImGui::GetItemRectMax();
      ImGui::End();
    };
    // Frame A: lay the combo out closed so we learn its button rect (popup not yet open).
    ImGui::NewFrame();
    comboPass();
    ImGui::EndFrame();
    const float cbx = (comboMin.x + comboMax.x) * 0.5f;
    const float cby = (comboMin.y + comboMax.y) * 0.5f;
    // Closed-state map: NO popup is open, so the walk must add ZERO popup_item rows.
    beginWidgetFrame();
    detail::recordOpenPopupItems();
    const bool noPopupRowsWhenClosed = !detail::itemsHavePrefix("popup_item:");
    // Frame B: move+press on the combo button (settle hover, then down).
    io.AddMousePosEvent(cbx, cby);
    ImGui::NewFrame(); comboPass(); ImGui::EndFrame();
    io.AddMouseButtonEvent(0, true);
    ImGui::NewFrame(); comboPass(); ImGui::EndFrame();
    // Frame C: release -> BeginCombo opens the popup window on this/next frame.
    io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame(); comboPass(); ImGui::EndFrame();
    // Frame D: popup now open & laid out. Build the map; the OLD path (injectBug) does NOT
    // walk the open popup -> no rows = the reported gap; the NEW path walks it -> rows appear.
    ImGui::NewFrame(); comboPass();
    beginWidgetFrame();
    if (!injectBug) detail::recordOpenPopupItems();
    int comboRows = 0;
    for (const auto& it : gItems)
      if (it.label.rfind("popup_item:", 0) == 0) ++comboRows;
    ImGui::EndFrame();
    // The discriminator (RED->GREEN): the popup IS open here, so the contract is "rows must
    // be present". GREEN walks the OpenPopupStack -> comboRows>=1. RED (injectBug = the OLD
    // path that never walked open popups) -> comboRows==0 -> gap1Ok FALSE -> the leg bites.
    // closed_empty separately proves the rows are absent when NO popup is open (no false rows).
    const bool gap1Ok = noPopupRowsWhenClosed && (comboRows >= 1);
    beginWidgetFrame();  // leave the shared item buffer clean

    ImGui::DestroyContext(ctx);

    // The承重 invariant: the editor was drawn on every frame, so the map is never
    // empty — regardless of the prior popup interaction — and never leaks stale rows.
    // (OpenPopup makes BeginPopup true the SAME frame, so c1 already carries the menu
    // rows; the clear+refill proof is c2 > c3 — popup-open frame has the menu rows,
    // the post-popup frame has dropped them back to the toolbar.)
    const bool toolbarFresh = c1 >= 4;                 // at least the 4 toolbar buttons
    const bool popupRecorded = popupOpen2 && menuRecorded2 && c2 > c3;  // menu rows when open
    const bool postPopupOk  = c3 >= 4 && !staleMenu3;  // <- breaks under the reported bug
    const bool pass =
        toolbarFresh && popupRecorded && postPopupOk && inspRecorded && nativeOk && gap1Ok;
    printf("[selftest-map] fresh=%zu popup=%zu(menu=%d) post_popup=%zu(stale=%d) ctxitem=%d "
           "native=%d combo_popup=%d(closed_empty=%d) -> %s\n",
           c1, c2, (int)menuRecorded2, c3, (int)staleMenu3, (int)inspRecorded, (int)nativeOk,
           (int)gap1Ok, (int)noPopupRowsWhenClosed, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
}

int runOcclusionSelfTest(bool injectBug) {
  @autoreleasepool {
    // Headless ImGui frame (same setup as runMapSelfTest): DisplaySize + a built font atlas so
    // NewFrame passes. We open two overlapping windows and drive the REAL pointOccluded predicate
    // that writeWidgetMap stamps into map.json — testing the live code path, not a copy.
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 800.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* tex = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&tex, &tw, &th);
    io.Fonts->SetTexID((ImTextureID)1);

    auto& gItems = detail::items();

    // The widget we record lives in the LOWER window; its center is the point we hit-test.
    const ImVec2 lowPos(100, 100), lowSize(200, 200);
    const ImVec2 centerPt(lowPos.x + 80, lowPos.y + 40);  // a point well inside the lower window
    unsigned int lowOwner = 0;
    auto layoutLower = [&]() {
      ImGui::SetNextWindowPos(lowPos);
      ImGui::SetNextWindowSize(lowSize);
      ImGui::Begin("LowerPanel", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
      ImGui::Button("Target");
      lowOwner = (unsigned int)ImGui::GetCurrentWindow()->ID;
      ImGui::End();
    };
    // The COVER window — same rect, opened AFTER the lower one so it is frontmost in g.Windows.
    auto layoutUpper = [&]() {
      ImGui::SetNextWindowPos(lowPos);
      ImGui::SetNextWindowSize(lowSize);
      ImGui::Begin("UpperCover", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
      ImGui::Text("cover");
      ImGui::End();
    };

    // --- Phase 1: BOTH windows up (cover on top). The point is occluded. ---
    // Two frames so window order/focus settles before the hit-test.
    for (int f = 0; f < 2; ++f) { ImGui::NewFrame(); layoutLower(); layoutUpper(); ImGui::EndFrame(); }
    ImGui::NewFrame();
    layoutLower();
    layoutUpper();
    // injectBug models the pre-Part-C state: no owner was tracked, so ownerWindow is 0 and the
    // predicate short-circuits to "not occluded" — the covered widget LIES that it is reachable.
    unsigned int ownerArg = injectBug ? 0u : lowOwner;
    bool occludedWhenCovered = detail::pointOccluded(centerPt.x, centerPt.y, ownerArg);
    ImGui::EndFrame();

    // --- Phase 2: cover REMOVED (only the lower window). The point is reachable. ---
    for (int f = 0; f < 2; ++f) { ImGui::NewFrame(); layoutLower(); ImGui::EndFrame(); }
    ImGui::NewFrame();
    layoutLower();
    bool occludedWhenClear = detail::pointOccluded(centerPt.x, centerPt.y, lowOwner);
    ImGui::EndFrame();

    detail::items().clear();  // don't leak test rows into a live map
    ImGui::DestroyContext(ctx);
    (void)gItems;

    // GREEN contract: covered -> occluded true, clear -> occluded false. injectBug suppresses the
    // owner so coveredCase is FALSE -> the leg goes RED (a covered widget reported reachable).
    const bool pass = occludedWhenCovered && !occludedWhenClear;
    printf("[selftest-occlusion] covered=%d clear=%d (bug=%d) -> %s\n", (int)occludedWhenCovered,
           (int)occludedWhenClear, (int)injectBug, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }
}

}  // namespace sw::eye
