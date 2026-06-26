// ui/theme — see theme.h. Faithful port of TiXL's default theme + T3Style.Apply().
#include "ui/theme.h"

#include <cmath>
#include <cstdio>

namespace sw::ui::theme {
namespace {

// Each value below is the literal from external/tixl/Editor/Gui/Styling/UiColors.cs (line noted).
// FromString("#RRGGBB"/"#AARRGGBB") and new(int,...) are decoded to float (byte/255) — see Color.cs
// (FromString uses System.Drawing ARGB; new(int r,g,b,a) divides each by 255).
const DefaultTheme kDefault = {
    // Datatype base colors (UiColors.cs:97-103) ---------------------------------------------------
    /*colorForValues     */ ImVec4(0.525f, 0.550f, 0.554f, 1.000f),  // :97  ColorForValues
    /*colorForString     */ ImVec4(0.468f, 0.586f, 0.320f, 1.000f),  // :98  ColorForString
    /*colorForTextures   */ ImVec4(0.625f, 0.000f, 0.540f, 1.000f),  // :99  ColorForTextures
    /*colorForDX11       */ ImVec4(0.840f, 0.460f, 0.440f, 1.000f),  // :100 ColorForDX11
    /*colorForCommands   */ ImVec4(0.132f, 0.722f, 0.762f, 1.000f),  // :101 ColorForCommands
    /*colorForGpuData    */ ImVec4(0.720f, 0.200f, 0.180f, 1.000f),  // :102 ColorForGpuData
    /*colorForShaderGraph*/ ImVec4(0.820f, 0.260f, 0.700f, 1.000f),  // :103 ColorForShaderGraph

    // ImGui-style palette (UiColors.cs slots copied by T3Style.Apply) ------------------------------
    /*text                     */ ImVec4(0.75f, 0.75f, 0.75f, 1.0f),  // :20 Text  new(0.75f)
    /*textDisabled             */ ImVec4(0.2f, 0.2f, 0.2f, 1.0f),     // :22 TextDisabled new(0.2f)
    /*backgroundButton         */ ImVec4(0.3f, 0.3f, 0.3f, 0.5f),     // :37 BackgroundButton
    /*backgroundHover          */ ImVec4(0.32f, 0.32f, 0.32f, 0.8f),  // :39 BackgroundHover
    /*backgroundActive         */ ImVec4(69.0f / 255.0f, 146.0f / 255.0f, 1.0f, 1.0f),  // :42 #4592FF
    /*popupBorder              */ ImVec4(0.0f, 0.0f, 0.0f, 1.0f),     // :34 PopupBorder
    /*backgroundGaps           */ ImVec4(0.0f, 0.0f, 0.0f, 1.0f),     // :57 BackgroundGaps
    /*backgroundInputField     */ ImVec4(0.08f, 0.08f, 0.08f, 0.8f),  // :46 BackgroundInputField
    /*backgroundInputFieldHover*/ ImVec4(0.1f, 0.1f, 0.1f, 1.0f),     // :47 BackgroundInputFieldHover
    /*backgroundInputFieldActive*/ImVec4(0.0f, 0.0f, 0.0f, 1.0f),     // :48 BackgroundInputFieldActive
    /*scrollbarBackground      */ ImVec4(0.12f, 0.12f, 0.12f, 0.53f), // :59 ScrollbarBackground
    /*scrollbarHandle          */ ImVec4(0.31f, 0.31f, 0.31f, 0.33f), // :60 ScrollbarHandle
    /*windowResizeHandle       */ ImVec4(0.0f, 0.0f, 0.0f, 0.25f),    // :54 WindowResizeHandle
    /*windowBackground         */ ImVec4(0.23f, 0.23f, 0.23f, 0.5f),  // :26 WindowBackground (ChildBg)
    /*backgroundPopup          */ ImVec4(0.18f, 0.18f, 0.18f, 0.98f), // :33 BackgroundPopup
    /*checkMark                */ ImVec4(1.0f, 1.0f, 1.0f, 0.8f),     // :23 CheckMark
    /*backgroundTabActive      */ ImVec4(58.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 1.0f),   // :44 #3A3A3A
    /*backgroundTabInActive    */ ImVec4(40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 0.8f),   // :45 #CC282828 (A=0xCC=204/255=0.8)
    /*selection                */ ImVec4(1.0f, 1.0f, 1.0f, 1.0f),     // :78 Selection
};

// Color.Fade(f) = (r,g,b, clamp(a*f, 0, 1)) (Color.cs:527).
ImVec4 fade(ImVec4 c, float f) {
  float a = c.w * f;
  c.w = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
  return c;
}

}  // namespace

const DefaultTheme& defaultTheme() { return kDefault; }

// = T3Style.Apply() (T3Style.cs:24-94). Copies UiColors into style.Colors[...] + sets metrics.
// Only the slots TiXL actually overrides are set; ImGui's StyleColorsDark() fills the rest.
void apply() {
  const DefaultTheme& t = kDefault;
  ImGuiStyle& s = ImGui::GetStyle();
  ImVec4* c = s.Colors;

  // Text
  c[ImGuiCol_Text]                = t.text;
  c[ImGuiCol_TextDisabled]        = t.textDisabled;
  // Button
  c[ImGuiCol_Button]              = t.backgroundButton;
  c[ImGuiCol_ButtonHovered]       = t.backgroundHover;
  c[ImGuiCol_ButtonActive]        = t.backgroundActive;
  // Border
  c[ImGuiCol_Border]              = t.popupBorder;
  c[ImGuiCol_BorderShadow]        = t.backgroundGaps;
  // Frame
  c[ImGuiCol_FrameBg]             = t.backgroundInputField;
  c[ImGuiCol_FrameBgHovered]      = t.backgroundInputFieldHover;
  c[ImGuiCol_FrameBgActive]       = t.backgroundInputFieldActive;
  // ScrollBar
  c[ImGuiCol_ScrollbarBg]         = t.scrollbarBackground;
  c[ImGuiCol_ScrollbarGrab]       = t.scrollbarHandle;
  c[ImGuiCol_ResizeGrip]          = t.windowResizeHandle;
  c[ImGuiCol_ModalWindowDimBg]    = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);  // Color.Transparent (Color.cs:149)
  c[ImGuiCol_MenuBarBg]           = t.backgroundGaps;
  c[ImGuiCol_Separator]           = t.backgroundGaps;
  c[ImGuiCol_SeparatorHovered]    = t.backgroundActive;
  c[ImGuiCol_WindowBg]            = t.backgroundGaps;       // only shines through at window edges
  c[ImGuiCol_ChildBg]             = t.windowBackground;     // graph see-through strength
  c[ImGuiCol_PopupBg]             = t.backgroundPopup;
  c[ImGuiCol_CheckMark]           = t.checkMark;
  // Tab
  c[ImGuiCol_Tab]                 = t.backgroundTabInActive;
  c[ImGuiCol_TabHovered]          = t.backgroundActive;
  c[ImGuiCol_TabSelected]         = t.backgroundTabActive;
  c[ImGuiCol_TabDimmedSelected]   = t.backgroundTabActive;
  c[ImGuiCol_TabDimmed]           = t.backgroundTabInActive;
  // Title
  c[ImGuiCol_TitleBgActive]       = t.backgroundGaps;
  c[ImGuiCol_TitleBg]             = t.backgroundGaps;
  // Header (BackgroundActive faded — T3Style.cs:69-71)
  c[ImGuiCol_Header]              = fade(t.backgroundActive, 0.5f);
  c[ImGuiCol_HeaderHovered]       = t.backgroundActive;
  c[ImGuiCol_HeaderActive]        = fade(t.backgroundActive, 0.8f);
  c[ImGuiCol_DragDropTarget]      = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);  // Color.Transparent

  // Style metrics (T3Style.cs:79-93).
  s.WindowPadding         = ImVec2(0, 0);
  s.FramePadding          = ImVec2(7, 4);
  s.ItemSpacing           = ImVec2(1, 1.49f);
  s.ItemInnerSpacing      = ImVec2(3, 2);
  s.GrabMinSize           = 10;
  s.FrameBorderSize       = 0;
  s.WindowRounding        = 0;
  s.ChildRounding         = 0;
  s.ScrollbarRounding     = 2;
  s.FrameRounding         = 0.0f;
  s.DisplayWindowPadding  = ImVec2(0, 0);
  s.DisplaySafeAreaPadding = ImVec2(0, 0);
  s.ChildBorderSize       = 0;
  s.TabRounding           = 2;
  s.WindowBorderSize      = 0;
}

int runThemeSelfTest(bool injectBug) {
  DefaultTheme t = defaultTheme();  // copy so the inject-bug leg can perturb a field
  // Inject-bug RED leg (run as --selftest-theme-bug): corrupt ONE field of the loaded table so the
  // value-golden below detects the drift and the function exits NON-zero (= the tooth can bite).
  if (injectBug) t.colorForGpuData.x += 0.1f;  // Points red base shifted off TiXL's 0.720

  int fails = 0;
  auto eq = [&](const char* name, ImVec4 got, ImVec4 want) {
    const float kEps = 0.0005f;  // tighter than a byte (1/255≈0.0039) so a real shift bites
    bool ok = std::fabs(got.x - want.x) < kEps && std::fabs(got.y - want.y) < kEps &&
              std::fabs(got.z - want.z) < kEps && std::fabs(got.w - want.w) < kEps;
    if (!ok) {
      ++fails;
      std::printf("[selftest-theme]   MISMATCH %-22s got(%.4f,%.4f,%.4f,%.4f) want(%.4f,%.4f,%.4f,%.4f)\n",
                  name, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
    }
    return ok;
  };

  // Closed-form value-golden: every field equals the TiXL constant it mirrors (UiColors.cs).
  eq("ColorForValues",      t.colorForValues,      ImVec4(0.525f, 0.550f, 0.554f, 1.0f));
  eq("ColorForString",      t.colorForString,      ImVec4(0.468f, 0.586f, 0.320f, 1.0f));
  eq("ColorForTextures",    t.colorForTextures,    ImVec4(0.625f, 0.0f,   0.540f, 1.0f));
  eq("ColorForDX11",        t.colorForDX11,        ImVec4(0.840f, 0.460f, 0.440f, 1.0f));
  eq("ColorForCommands",    t.colorForCommands,    ImVec4(0.132f, 0.722f, 0.762f, 1.0f));
  eq("ColorForGpuData",     t.colorForGpuData,     ImVec4(0.720f, 0.200f, 0.180f, 1.0f));
  eq("ColorForShaderGraph", t.colorForShaderGraph, ImVec4(0.820f, 0.260f, 0.700f, 1.0f));

  eq("Text",                t.text,                ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
  eq("TextDisabled",        t.textDisabled,        ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
  eq("BackgroundButton",    t.backgroundButton,    ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
  eq("BackgroundHover",     t.backgroundHover,     ImVec4(0.32f, 0.32f, 0.32f, 0.8f));
  // #4592FF = (0x45,0x92,0xFF)/255
  eq("BackgroundActive",    t.backgroundActive,    ImVec4(69.0f/255.0f, 146.0f/255.0f, 255.0f/255.0f, 1.0f));
  eq("PopupBorder",         t.popupBorder,         ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  eq("BackgroundGaps",      t.backgroundGaps,      ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  eq("BackgroundInputField",t.backgroundInputField,ImVec4(0.08f, 0.08f, 0.08f, 0.8f));
  eq("BgInputFieldHover",   t.backgroundInputFieldHover,  ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
  eq("BgInputFieldActive",  t.backgroundInputFieldActive, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  eq("ScrollbarBackground", t.scrollbarBackground, ImVec4(0.12f, 0.12f, 0.12f, 0.53f));
  eq("ScrollbarHandle",     t.scrollbarHandle,     ImVec4(0.31f, 0.31f, 0.31f, 0.33f));
  eq("WindowResizeHandle",  t.windowResizeHandle,  ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
  eq("WindowBackground",    t.windowBackground,    ImVec4(0.23f, 0.23f, 0.23f, 0.5f));
  eq("BackgroundPopup",     t.backgroundPopup,     ImVec4(0.18f, 0.18f, 0.18f, 0.98f));
  eq("CheckMark",           t.checkMark,           ImVec4(1.0f, 1.0f, 1.0f, 0.8f));
  // #3A3A3A = 58/255 ; #CC282828 = (0x28,0x28,0x28) alpha 0xCC=204/255=0.8
  eq("BackgroundTabActive", t.backgroundTabActive, ImVec4(58.0f/255.0f, 58.0f/255.0f, 58.0f/255.0f, 1.0f));
  eq("BackgroundTabInActive",t.backgroundTabInActive,ImVec4(40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f, 204.0f/255.0f));
  eq("Selection",           t.selection,           ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

  // PASS leg: every field must match (fails==0). RED leg (injectBug): the corrupted ColorForGpuData
  // makes its assertion fail → fails>0 → exit non-zero (the tooth bites). No special-casing needed.
  bool ok = (fails == 0);
  std::printf("[selftest-theme] fields=29 mismatches=%d injectBug=%d -> %s\n",
              fails, injectBug, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::ui::theme
