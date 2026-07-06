// MidiClip — the FIRST device/file Dict<float> producer node (dataset seam → dict-currency rail). Wires
// the already-golden MidiClip core (parseSmf + accumulateMidiClip, midi_smf.{h,cpp}) onto the SAME
// producer rail BuildFloatDict rides (dict_op_registry.h): a leaf .cpp ending in a file-scope DictOp
// registrar feeding dictSpecSink() (Add menu / findSpec) + dictCookFns() (type → cook). This is the
// node the io/midi/MidiClip.cs op becomes in sw.
//
// TiXL authority: Operators/Lib/io/midi/MidiClip.cs. The op reads a .mid file (MidiClip.cs:237-238
// Filename InputSlot<string>), and as the timeline playhead sweeps folds every NoteOn/NoteOff/CC up to
// the current tick into Values (TimeClipSlot<Dict<float>>, MidiClip.cs:9-10). sw's MidiClip.Values IS the
// SwFloatDict this rail carries; the fold + key scheme live in accumulateMidiClip (midi_smf.h:63-72,
// already golden in midiclip_golden.cpp).
//
// TIME → TICKS (MidiClip.cs:53/144): TiXL takes context.LocalTime (BARS) - timeRange.Start, then
//   timeInTicks = (long)(bars * 4 * _deltaTicksPerQuarterNote)  (MidiClip.cs:144).
// sw's EvaluationContext.localFxTime IS TiXL's LocalFxTime = FxTimeInBars (eval_context.h:31-37), so this
// leaf computes timeInTicks the same way. There is no per-clip timeRange offset on the flat producer rail
// (the TimeClip placement is a resident-graph concern, not the Dict producer's — the golden drives the
// bars directly, matching how the core golden feeds timeInTicks); a wired timeline-offset is the deferred
// resident-clip leg. FORK fork-midiclip-timerange-offset-deferred: the timeRange.Start subtraction is a
// no-op on this producer rail (bars are already clip-local); the resident TimeClip wrapper adds it later.
//
// INPUTS (MidiClip.cs:237-241 [Input] order): Filename (String, .mid path — "Lib:..." asset key OR an
// absolute/cwd path, resolved like point_ops_loadsvgastexture2d.cpp:98-104); PrintLogMessages (bool,
// telemetry) is dropped (a log toggle has no value-path effect — same trim as the io node registry).
// OUTPUT: out = Dict<float> (MidiClip.Values). The ChannelNames(List<string>) + DeltaTicksPerQuarterNote
// (float) secondary outputs (MidiClip.cs:12-16) are DEFERRED (List currency + a second output rail);
// Values is the primary and the only one this rail carries. FORK fork-midiclip-secondary-outputs-deferred.
#include "runtime/dict_op_registry.h"  // DictOp / DictCookCtx / dictInjectBug
#include "runtime/eval_context.h"      // EvaluationContext (localFxTime = bars)
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/midi_smf.h"          // parseSmf / accumulateMidiClip / SmfFile

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// SW_ASSETS_DIR: the repo assets root (set by CMake target_compile_definitions, same as the image rail).
#ifndef SW_ASSETS_DIR
#define SW_ASSETS_DIR ""
#endif

namespace sw {

int runMidiClipSelfTest(bool injectBug);  // golden lives in dict_ops_midiclip_golden.cpp

namespace {

// Read an entire file into bytes (mirror the loadimage/loadsvg file read: binary, whole image). Returns
// false when the path is empty or the file cannot be opened.
bool readFileBytes(const std::string& path, std::vector<uint8_t>& out) {
  if (path.empty()) return false;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  std::streamsize n = f.tellg();
  if (n < 0) return false;
  f.seekg(0);
  out.resize((size_t)n);
  if (n > 0 && !f.read(reinterpret_cast<char*>(out.data()), n)) return false;
  return true;
}

// Resolve a MidiClip Filename to a filesystem path: "Lib:..." → SW_ASSETS_DIR-relative (strip prefix,
// join under the assets dir); otherwise verbatim (absolute / cwd-relative). Mirrors the loadsvg resolver
// (point_ops_loadsvgastexture2d.cpp:98-104) kept runtime-local (pure string, no platform include).
std::string resolveMidiPath(const std::string& key) {
  if (key.rfind("Lib:", 0) == 0) {
    std::string dir = SW_ASSETS_DIR;
    std::string rel = key.substr(4);
    if (dir.empty()) return rel;
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');
    return dir + rel;
  }
  return key;
}

// Cook: read Filename (inputStrings[0]) → the .mid bytes → parse → fold at the playhead tick into output.
void cookMidiClip(DictCookCtx& c) {
  if (!c.output) return;
  c.output->entries.clear();

  const std::string filename =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  std::vector<uint8_t> bytes;
  if (!readFileBytes(resolveMidiPath(filename), bytes)) return;  // no file → empty dict (TiXL null-file path)

  SmfFile file;
  if (!parseSmf(bytes, file)) return;  // malformed → empty dict (MidiClip.cs:85-88 try/catch bail)

  // bars → ticks (MidiClip.cs:144): timeInTicks = bars * 4 * deltaTicksPerQuarterNote. localFxTime IS
  // TiXL's LocalFxTime (bars). Clamp negative bars to 0 (before the clip start nothing has fired).
  const double bars = c.ctx ? (double)c.ctx->localFxTime : 0.0;
  const int64_t timeInTicks =
      (int64_t)((bars > 0.0 ? bars : 0.0) * 4.0 * (double)file.deltaTicksPerQuarterNote);

  accumulateMidiClip(file, timeInTicks, *c.output);
  // The value-transform injection seam (midiClipInjectBug, midi_smf.h:74-77) lives in accumulateMidiClip
  // itself, so the golden bites the REAL fold path — no extra corruption needed here (dictInjectBug's
  // drop-last would double-count; the fold's own seam is the faithful one for this producer).
}

}  // namespace

// Self-registration. File-scope static DictOp — independent leaf .cpp (dict producer rail).
//   Ports: "Filename" = String (.mid path, "Lib:" asset key or verbatim; resident-safe const);
//          "out"      = Dict<float> (MidiClip.Values — the folded channel→value map at the playhead).
static const DictOp _reg_midiclip{
    {"MidiClip", "MidiClip",
     {{"Filename", "Filename", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"out", "out", "Dict", false}},
     /*evaluate=*/nullptr},  // Dict output cannot ride NodeSpec::evaluate (returns a host map)
    cookMidiClip};

}  // namespace sw
