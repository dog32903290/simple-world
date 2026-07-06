// runtime/playback_fft — PlaybackFFT node public seam. See playback_fft.cpp for the full contract.
//
// PlaybackFFT (TiXL Operators/Lib/numbers/floats/io/PlaybackFFT.cs) is a FloatList PRODUCER: it copies the
// live spectrum analysis (one of five arrays selected by InputBand) onto the host FloatList rail. It is a
// self-registering FloatListOp leaf (no exported cook fn) — the only public surface is the DATA SEAM the
// app drives each frame.
#pragma once
#include "runtime/spectrum_analyzer.h"  // SpectrumSnapshot (app→runtime, allowed)

namespace sw {

// Publish the current spectrum snapshot for PlaybackFFT to read. The APP calls this once per frame with the
// live analysis (audio_monitor::spectrum()) — the FloatList-rail analog of frame_cook handing cookAudioReaction
// the same `spec`. PlaybackFFT is pull-driven, so it reads whatever was last published at cook time. A plain
// value copy (trivially copyable). Never called by production before the first publish → the default all-zero
// snapshot yields zero-filled lists (silent), never a crash.
void setCurrentSpectrum(const SpectrumSnapshot& s);

// Isolated proof (--selftest-playbackfft): feed a known snapshot, cook PlaybackFFT for each InputBand mode,
// assert the FloatList output equals the matching snapshot array element-for-element (closed-form: the mode→
// array mapping + length is hand-derived from PlaybackFFT.cs:22-41). injectBug corrupts the REAL cook output
// so the test must FAIL. Returns 0 on PASS, 1 on FAIL.
int runPlaybackFFTSelfTest(bool injectBug);

}  // namespace sw
