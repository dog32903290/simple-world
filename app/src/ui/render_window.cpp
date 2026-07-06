// ui/render_window — Render-to-file settings window (port of TiXL RenderWindow.cs). See render_window.h
// for the zone/seam contract. Reuses the Output resolution preset TABLE (output_window_resolution.h,
// 鐵律 7) and a data-driven codec table; drives an app::ExportSession one frame per editor frame.
#include "ui/render_window.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "app/document.h"                 // doc::g_lib (the frozen document to render)
#include "app/export_session.h"           // ExportSession / ExportSettings + (transitively) platform::VideoCodec
#include "app/frame_cook.h"               // transportBpm (Bars→seconds conversion needs the tempo)
#include "ui/output_window_resolution.h"  // kResPresets / kResPresetCount (reused resolution table)
#include "verify/eye/eye.h"

// Shell seams (defined in main.cpp, same shell-owned contract as previewTexture()): the live Metal
// context to build the export against, and the Fill-baseline window size for the aspect-ratio presets.
namespace sw {
bool exportMetalContext(MTL::Device*& dev, MTL::Library*& lib, MTL::CommandQueue*& queue);
bool outputWindowResolution(int& w, int& h);
}  // namespace sw

namespace sw::ui {

namespace {

// --- Codec table (鐵律 7: data-driven — add a codec = add a row) -------------------------------------
// The container/codec choices, mirroring TiXL's RenderMode (Video/ImageSequence) split but expressed as
// sw's three concrete VideoCodec sinks. `needsMovExt` = the output path is a .mov file (ProRes/H264);
// PNG sequence writes into a directory (no extension forced). ProRes4444 first = the MV alpha path (承重 #4).
struct CodecRow {
  const char* label;
  platform::VideoCodec codec;
  bool needsMovExt;
};
const CodecRow kCodecs[] = {
    {"ProRes 4444 (.mov, alpha)", platform::VideoCodec::ProRes4444, true},
    {"H.264 (.mov)", platform::VideoCodec::H264, true},
    {"PNG sequence (folder)", platform::VideoCodec::PngSequence, false},
};
const int kCodecCount = static_cast<int>(sizeof(kCodecs) / sizeof(kCodecs[0]));

// --- Time-reference vocabulary (TiXL RenderSettings.TimeReferences: Bars/Seconds/Frames) ------------
// The Start/End range is entered in ONE of these units; we convert to seconds → export frames at Start.
enum class TimeRef { Frames, Seconds, Bars };
const char* kTimeRefLabels[] = {"Frames", "Seconds", "Bars"};

// The window's persistent UI state (session-only — see the persistence FORK note at the bottom). Held in
// a file-scope struct so drawRenderWindow() is re-entrant across frames. Defaults = a 2-second 30fps
// 512² ProRes clip, matching ExportSettings' documented defaults (export_engine.h:34).
struct RenderUiState {
  bool visible = false;

  TimeRef timeRef = TimeRef::Frames;
  double rangeStart = 0.0;    // in `timeRef` units
  double rangeEnd = 59.0;     // in `timeRef` units (Frames: inclusive last frame → 60 frames at 0..59)
  double fps = 30.0;
  int codecIndex = 0;         // index into kCodecs
  int resIndex = 0;           // index into kResPresets (0 = Fill)
  char outputPath[512] = "/tmp/sw_render/out.mov";

  std::string lastMessage;    // status line under the controls
  app::ExportSession session; // the stepwise driver; active() while an export runs
  bool started = false;       // an export has been begun this session (drives the state.json hook)
  std::string stateJson = "null";
};
RenderUiState g_st;

// Convert a value in `timeRef` units to seconds. Bars uses the live transport BPM (TiXL SecondsFromBars:
// 4 beats/bar × 60/bpm). Frames uses fps (value == frame index → seconds = index/fps).
double toSeconds(double value, TimeRef ref, double fps, double bpm) {
  switch (ref) {
    case TimeRef::Frames:  return fps > 0.0 ? value / fps : value;
    case TimeRef::Seconds: return value;
    case TimeRef::Bars:    return bpm > 0.0 ? value * (240.0 / bpm) : value;  // 4×60/bpm seconds per bar
  }
  return value;
}

// Convert seconds back to `timeRef` units (used when switching the unit, so the displayed range keeps
// its wall-clock meaning — TiXL ConvertReferenceTime, RenderTiming.cs:54).
double fromSeconds(double sec, TimeRef ref, double fps, double bpm) {
  switch (ref) {
    case TimeRef::Frames:  return sec * fps;
    case TimeRef::Seconds: return sec;
    case TimeRef::Bars:    return bpm > 0.0 ? sec * (bpm / 240.0) : sec;
  }
  return sec;
}

// The export frame range [begin, end] inclusive, computed from the current UI range. begin = round(start
// seconds × fps); end = the LAST frame index so the clip covers [start, end] — for Frames units the
// entered end IS the inclusive last frame; for Seconds/Bars we take floor(endSec×fps) as the last frame.
void computeFrameRange(uint32_t& begin, uint32_t& end) {
  const double bpm = sw::framecook::transportBpm();
  const double startSec = toSeconds(g_st.rangeStart, g_st.timeRef, g_st.fps, bpm);
  const double endSec = toSeconds(g_st.rangeEnd, g_st.timeRef, g_st.fps, bpm);
  const long b = (long)std::lround(startSec * g_st.fps);
  long e;
  if (g_st.timeRef == TimeRef::Frames)
    e = (long)std::lround(g_st.rangeEnd);  // entered value is the inclusive last frame index
  else
    e = (long)std::floor(endSec * g_st.fps);
  begin = (uint32_t)(b < 0 ? 0 : b);
  end = (uint32_t)(e < (long)begin ? begin : e);
}

// Refresh the machine-readable export state (the state.json hook). Called every frame the window draws
// AND once more right after a Start/Cancel so a .scn sees the transition immediately.
void refreshStateJson() {
  if (!g_st.started) { g_st.stateJson = "null"; return; }
  const app::ExportSession& s = g_st.session;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"active\": %s, \"done\": %s, \"cancelled\": %s, \"framesDone\": %u, "
                "\"framesTotal\": %u}",
                s.active() ? "true" : "false", s.done() ? "true" : "false",
                s.cancelled() ? "true" : "false", s.framesDone(), s.framesTotal());
  g_st.stateJson = buf;
}

// Build the ExportSettings from the UI and begin() the session. Sets lastMessage on any failure.
void startExport() {
  MTL::Device* dev = nullptr; MTL::Library* lib = nullptr; MTL::CommandQueue* queue = nullptr;
  if (!sw::exportMetalContext(dev, lib, queue)) { g_st.lastMessage = "no render context"; return; }

  app::ExportSettings es;
  computeFrameRange(es.beginFrame, es.endFrame);
  es.fps = g_st.fps;
  es.codec = kCodecs[g_st.codecIndex].codec;
  es.outputPath = g_st.outputPath;

  // Resolution: reuse the Output preset table. Fixed-pixel presets → their dims verbatim; aspect-ratio
  // presets (Fill/1:1/16:9/4:3) resolve against the Fill baseline (the live Output window size), same as
  // the Output resolution selector. Degenerate baseline → fall back to the 512² default.
  const ResPreset& p = kResPresets[g_st.resIndex];
  int baseW = 512, baseH = 512;
  sw::outputWindowResolution(baseW, baseH);
  if (baseW <= 0 || baseH <= 0) { baseW = 512; baseH = 512; }
  if (!p.useAsAspectRatio) {
    es.width = (uint32_t)p.w; es.height = (uint32_t)p.h;
  } else if (p.w <= 0 || p.h <= 0) {       // Fill: baseline verbatim
    es.width = (uint32_t)baseW; es.height = (uint32_t)baseH;
  } else {                                  // aspect fit into the baseline (TiXL ComputeResolution)
    const float wa = (float)baseW / (float)baseH, ra = (float)p.w / (float)p.h;
    if (ra > wa) { es.width = (uint32_t)baseW; es.height = (uint32_t)(baseW / ra); }
    else { es.width = (uint32_t)(baseH * ra); es.height = (uint32_t)baseH; }
  }

  if (!g_st.session.begin(es, sw::doc::g_lib(), dev, lib, queue)) {
    g_st.lastMessage = "start failed: " + g_st.session.lastMessage();
    g_st.started = true;  // still surface the failure in state.json
    return;
  }
  g_st.started = true;
  g_st.lastMessage = "Rendering...";
}

}  // namespace

bool& renderWindowVisible() { return g_st.visible; }

const char* renderExportStateJson() { return g_st.stateJson.c_str(); }

void drawRenderWindow() {
  // Pump the active export by ONE frame per editor frame (TiXL RenderProcess.Update, cs:189-232). Runs
  // even if the window is closed so an in-flight render still completes; the progress bar just isn't
  // visible. On the last frame the loop finalizes the file.
  if (g_st.session.active()) {
    if (!g_st.session.done()) {
      g_st.session.stepOneFrame();
    } else {
      g_st.session.finish();
      const app::ExportResult& r = g_st.session.result();
      g_st.lastMessage = r.ok ? "Render finished." : ("Render failed: " + r.message);
    }
  }
  refreshStateJson();

  if (!g_st.visible) return;

  // Docked by ui/layout_dock (default = tab in the lower-right stack). No manual SetNextWindowPos/
  // Size — DockBuilder owns first placement so resize re-flows it (柏為 drags it freely).
  if (!ImGui::Begin("Render", &g_st.visible)) { ImGui::End(); return; }

  const bool exporting = g_st.session.active();
  const double bpm = sw::framecook::transportBpm();
  ImGui::BeginDisabled(exporting);  // settings are read-only while a render runs (TiXL locks them)

  // --- Time unit (Frames / Seconds / Bars) — TiXL RenderSettings.TimeReference "Scale" row ---
  ImGui::TextUnformatted("Range unit");
  for (int i = 0; i < 3; ++i) {
    if (i) ImGui::SameLine();
    const bool sel = ((int)g_st.timeRef == i);
    if (ImGui::RadioButton(kTimeRefLabels[i], sel) && !sel) {
      // Keep the wall-clock meaning of the range when switching units (TiXL ConvertReferenceTime).
      const double sSec = toSeconds(g_st.rangeStart, g_st.timeRef, g_st.fps, bpm);
      const double eSec = toSeconds(g_st.rangeEnd, g_st.timeRef, g_st.fps, bpm);
      g_st.timeRef = (TimeRef)i;
      g_st.rangeStart = fromSeconds(sSec, g_st.timeRef, g_st.fps, bpm);
      g_st.rangeEnd = fromSeconds(eSec, g_st.timeRef, g_st.fps, bpm);
    }
    sw::eye::recordItem((std::string("render_unit:") + kTimeRefLabels[i]).c_str());
  }

  // --- Start / End (in the chosen unit) + FPS — TiXL DrawTimeSetup ---
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputDouble("Start", &g_st.rangeStart, 0.0, 0.0, "%.3f");
  sw::eye::recordItem("render_start");
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputDouble("End", &g_st.rangeEnd, 0.0, 0.0, "%.3f");
  sw::eye::recordItem("render_end");
  ImGui::SetNextItemWidth(140.0f);
  if (ImGui::InputDouble("FPS", &g_st.fps, 0.0, 0.0, "%.2f") && g_st.fps < 1.0) g_st.fps = 1.0;
  sw::eye::recordItem("render_fps");

  // --- Resolution (REUSE the Output preset table, 鐵律 7) — TiXL Resolution row ---
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("Resolution", kResPresets[g_st.resIndex].title)) {
    for (int i = 0; i < kResPresetCount; ++i) {
      const bool sel = (i == g_st.resIndex);
      if (ImGui::Selectable(kResPresets[i].title, sel)) g_st.resIndex = i;
      sw::eye::recordItem((std::string("render_res:") + kResPresets[i].title).c_str());
    }
    ImGui::EndCombo();
  }
  sw::eye::recordItem("render_resolution");

  // --- Codec (data-driven table) — TiXL RenderMode + FileFormat ---
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("Codec", kCodecs[g_st.codecIndex].label)) {
    for (int i = 0; i < kCodecCount; ++i) {
      const bool sel = (i == g_st.codecIndex);
      if (ImGui::Selectable(kCodecs[i].label, sel)) g_st.codecIndex = i;
      sw::eye::recordItem((std::string("render_codec:") + std::to_string(i)).c_str());
    }
    ImGui::EndCombo();
  }
  sw::eye::recordItem("render_codec");

  // --- Output path (text field; NAMED FORK of TiXL's native folder picker, see bottom note) ---
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##render_path", g_st.outputPath, sizeof(g_st.outputPath));
  sw::eye::recordItem("render_path");
  ImGui::TextDisabled("Output: %s file/folder", kCodecs[g_st.codecIndex].needsMovExt ? ".mov" : "PNG");

  ImGui::EndDisabled();  // end settings-locked region

  // --- Summary (TiXL DrawRenderSummary): resolution/fps + frame count ---
  {
    uint32_t b = 0, e = 0;
    computeFrameRange(b, e);
    const uint32_t frames = e - b + 1;
    ImGui::Separator();
    ImGui::TextDisabled("%u frames @ %.2f fps  (frames %u..%u)", frames, g_st.fps, b, e);
  }

  // --- Start / Cancel + progress (TiXL DrawRenderingControls) ---
  if (exporting) {
    const float progress = g_st.session.framesTotal() > 0
                               ? (float)g_st.session.framesDone() / (float)g_st.session.framesTotal()
                               : 0.0f;
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
    const uint32_t remaining = g_st.session.framesTotal() - g_st.session.framesDone();
    ImGui::Text("%u frames remaining", remaining);
    if (ImGui::Button("Cancel Render", ImVec2(-1.0f, 0.0f))) {
      g_st.session.abort();
      g_st.lastMessage = "Render cancelled.";
      refreshStateJson();
    }
    sw::eye::recordItem("render_cancel");
  } else {
    if (ImGui::Button("Start Render", ImVec2(-1.0f, 0.0f))) {
      startExport();
      refreshStateJson();
    }
    sw::eye::recordItem("render_start_button");
  }

  if (!g_st.lastMessage.empty()) ImGui::TextWrapped("%s", g_st.lastMessage.c_str());

  ImGui::End();
}

// ---------------------------------------------------------------------------------------------------
// NAMED FORKS from TiXL RenderWindow.cs (每一條分岔 + 理由):
//   1. SETTINGS PERSISTENCE — session-only (a file-scope RenderUiState), NOT persisted. TiXL stores
//      RenderSettings per-composition in symbolUi.RenderSettings (.t3ui). sw has no per-composition
//      UI-settings layer (symbolUi persistence is unbuilt), and the sibling Output resolution selector
//      is ALSO deliberately session-only ("a view setting, not graph state" — output_window_resolution
//      .h). Matching that precedent keeps this window from growing a whole new persistence pipeline.
//      Documented follow-up (same status as window-layout in user_settings.h).
//   2. OUTPUT PATH — a plain text field, not TiXL's AddFilePicker (native folder browser). NFD lives in
//      the app zone (soundtrack.cpp); wiring a picker through a new app seam is out of scope for
//      "put a UI face on the engine", and the eye/hand harness can drive a text field but not an NFD
//      modal. The export CLI already takes a text path, so this is fully functional.
//   3. FLOATING window, not TiXL's dockable Window. sw's layout system is a separate lane; a floating
//      tool window (toggled from the toolbar, like Assets/Vary/Theme) is the current idiom. Re-homing
//      into a dock is that lane's job.
//   4. RESOLUTION as absolute-pixel presets (reusing kResPresets), not TiXL's ResolutionFactor (a scale
//      of the MainOutputTexture). sw's ExportSettings takes absolute width/height, and reusing the
//      Output preset table (鐵律 7) is the DRY win. Aspect-ratio presets resolve against the live
//      Output window size, exactly as the Output resolution selector does.
// ---------------------------------------------------------------------------------------------------

}  // namespace sw::ui
