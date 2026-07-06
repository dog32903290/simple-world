#include "app/export_engine.h"

#include "app/export_session.h"  // ExportSession — THE export loop; runExport is a blocking pump over it

namespace sw::app {

// The blocking (CLI/batch) export entry — since the driver convergence this is a THIN PUMP over
// ExportSession, the single export loop. Everything per-run lives in the session: validation, the
// PointGraph + DeterministicCookState + VideoWriter trio, the Command-view background guard
// (out-window-persistence -> executor clear color), and PRE-ROLL (ExportSettings::preroll). Keeping
// runExport as a session client makes the blocking and stepwise (Render window) drivers structurally
// incapable of diverging — the fixer-2 parity red (runExport had pre-roll + background wiring, the
// stepwise path didn't -> byte divergence) cannot recur, because there is only one loop to fix.
//
// Progress/cancel semantics are unchanged from the pre-convergence loop:
//   • pre-roll frames report (framesPrerolled, 0) — framesTotal==0 is the pre-roll sentinel (header
//     doc); pre-roll is not cancellable (the callback's return value is ignored during warm-up).
//   • real frames report (framesDone, framesTotal) AFTER each encoded frame; returning false cancels
//     (the session finalizes what was written; the result reads ok=false / "cancelled").
ExportResult runExport(const ExportSettings& s, const SymbolLibrary& lib, MTL::Device* dev,
                       MTL::Library* shaderLib, MTL::CommandQueue* queue, const ProgressFn& onProgress) {
  ExportSession sess;
  if (!sess.begin(s, lib, dev, shaderLib, queue, /*onPrerollProgress=*/onProgress)) {
    ExportResult r;
    r.message = sess.lastMessage();  // same strings the pre-convergence validation produced
    return r;
  }

  while (!sess.done()) {
    if (!sess.stepOneFrame()) return sess.result();  // cook/readback/encode failed; session self-aborted
    if (onProgress && !onProgress(sess.framesDone(), sess.framesTotal())) {
      sess.abort();  // caller cancelled -> finalize what was written, verdict ok=false/"cancelled"
      return sess.result();
    }
  }

  sess.finish();
  return sess.result();
}

}  // namespace sw::app
