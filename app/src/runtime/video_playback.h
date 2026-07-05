// runtime/video_playback — the CLOSED-FORM timing core of the PlayVideo / PlayVideoClip operator
// nodes, factored out of any decoder so it can be verified without hardware or a real video file.
//
// ZONE: runtime (pure computation). The native decode (Windows MediaFoundation → macOS AVFoundation
// AVAssetReader) is a SEPARATE platform seam (deferred; see census io/video). What lives here is the
// deterministic per-frame math that decides, given the transport time and the engine's current
// position, WHERE to seek and WHETHER to play — the part that is a MV-tooling承重 (frame-accurate
// deterministic export) and is fully expressible as closed form.
//
// TiXL authority: external/tixl/Operators/Lib/io/video/PlayVideo.cs (PlaybackController) and
// PlayVideoClip.cs (Update). Every function below cites the .cs line it transcribes. The point of
// this file is that these numbers are hand-derivable from the .cs alone — no MediaEngine needed.
//
// runtime leaf: no hardware, no UI, no upward dep. Header-only (small, inlined, no allocations).
#pragma once

#include <algorithm>
#include <cmath>

namespace sw {
namespace video {

// ── Golden TEETH hook (--selftest-io-video-timing). Sticky module switch; the golden restores 0.
// 0 = production. Each mode corrupts ONE real term of the cook math so the corresponding closed-form
// assert goes RED (it is NOT a want-flip: the WANT stays pinned to the TiXL value, the CODE path is
// what changes). Mirrors setIoNodeBug / setAnimValueBug convention (io_node_cook.h:92).
//   1 = drop the 1/60 frame quantization (return the raw time → determinism assert diverges)
//   2 = flip the loop shortest-path wrap to a naive delta (loop boundary seek assert diverges)
//   3 = drop the clip play "advanced since last frame" guard (play-decision assert diverges)
inline int& videoPlaybackBugRef() { static int mode = 0; return mode; }
inline void setVideoPlaybackBug(int mode) { videoPlaybackBugRef() = mode; }
inline int  videoPlaybackBug() { return videoPlaybackBugRef(); }

// ── PlayVideo (untimeclipped player). PlayVideo.cs PlaybackController.HandleGettingFrames. ────────

// TiXL PlayVideo.cs:126 — snap the requested time onto the 1/60s frame grid BEFORE any seek math:
//   requestedTime = Math.Floor(requestedTime * 60) / 60
// Load-bearing for determinism: two wall times inside the same 1/60 window request the SAME frame,
// so a deterministic export never micro-seeks between them.
inline double quantizeRequestedTime(double requestedTimeSecs) {
  if (videoPlaybackBug() == 1) return requestedTimeSecs;  // TEETH: skip the frame snap
  return std::floor(requestedTimeSecs * 60.0) / 60.0;
}

// TiXL PlayVideo.cs:172 — the "precise at pause" look-ahead offset added to the requested time:
//   playbackOffset = precisePlayback ? 2/60f : 0
inline double precisePlaybackOffset(bool precisePlayback) {
  return precisePlayback ? (2.0 / 60.0) : 0.0;
}

// TiXL PlayVideo.cs:260-265 (local LoopOrClampTimeToVideoDuration) — map a time into the video's
// valid range: loop → time % duration (C# `%` = TRUNCATED remainder, keeps sign of the dividend, but
// requestedTime here is always ≥ 0 so it equals a floor-mod for our inputs); no-loop → clamp[0,dur].
// duration ≤ 0 is degenerate; we return the raw time (matches C#: n%0 would throw, clamp(0,≤0)=0 —
// callers never reach here with dur≤0 because HaveMetadata gates it, but we stay total).
inline double loopOrClampToDuration(double timeSecs, double durationSecs, bool loop) {
  if (durationSecs <= 0.0) return loop ? timeSecs : std::clamp(timeSecs, 0.0, 0.0);
  if (loop) return std::fmod(timeSecs, durationSecs);
  return std::clamp(timeSecs, 0.0, durationSecs);
}

// TiXL MathUtils.Fmod (MathUtils.cs:339) — EUCLIDEAN/positive modulo (NOT std::fmod's truncated one):
//   Fmod(v,mod) = v - mod*floor(v/mod). Used by the loop shortest-path delta below.
inline double euclidFmod(double v, double mod) {
  return v - mod * std::floor(v / mod);
}

// TiXL PlayVideo.cs:176-181 — the delta between where we should be and where the engine is. When
// looping, wrap it to the SHORTEST signed path so a seek near the loop boundary crosses the seam
// instead of rewinding the whole clip:
//   deltaRaw = clampedSeekTime - clampedVideoTime
//   loop ? Fmod(deltaRaw + dur/2, dur) - dur/2 : deltaRaw
inline double seekDelta(double clampedSeekTime, double clampedVideoTime,
                        double durationSecs, bool loop) {
  const double deltaRaw = clampedSeekTime - clampedVideoTime;
  if (!loop) return deltaRaw;
  if (videoPlaybackBug() == 2) return deltaRaw;  // TEETH: naive delta, no shortest-path wrap
  return euclidFmod(deltaRaw + durationSecs / 2.0, durationSecs) - durationSecs / 2.0;
}

// TiXL PlayVideo.cs:189-194 — the seek threshold: the caller's ResyncThreshold while playing forward,
// but a tight 0.5/60 while paused/scrubbing (so a stopped playhead stays frame-locked).
inline double seekThreshold(double resyncThreshold, bool isPlayingForward) {
  if (isPlayingForward) return resyncThreshold;
  return 0.5 / 60.0;  // thresholdWhenPaused (cs:192)
}

// TiXL PlayVideo.cs:196-197 — the actual seek decision. Do NOT seek if a non-looping clip already
// completed, if a seek is in flight, or if |delta| is within threshold:
//   notLoopingCompleted = !loop && hasCompleted
//   shouldSeek = !notLoopingCompleted && !isSeeking && !engineIsSeeking && |delta| > threshold
inline bool shouldSeek(double delta, double threshold, bool loop, bool hasCompleted,
                       bool isSeeking, bool engineIsSeeking) {
  const bool notLoopingCompleted = !loop && hasCompleted;
  return !notLoopingCompleted && !isSeeking && !engineIsSeeking &&
         std::abs(delta) > threshold;
}

// TiXL PlayVideo.cs:143-145 — end-of-playback: only a non-looping clip completes, once EITHER the
// engine's current time or the requested time passes (duration - 0.016):
//   durWithMargin = duration - 0.016
//   hasCompleted = !loop && (currentTime > durWithMargin || requestedTime > durWithMargin)
inline bool hasPlaybackCompleted(double currentTimeSecs, double requestedTimeSecs,
                                 double durationSecs, bool loop) {
  const double completionThreshold = 0.016;  // cs:143
  const double durWithMargin = durationSecs - completionThreshold;
  if (loop) return false;
  return currentTimeSecs > durWithMargin || requestedTimeSecs > durWithMargin;
}

// TiXL PlayVideo.cs:202-204 — while playing forward the seek target is nudged by an averageSeekTime
// look-ahead so the just-seeked frame lands on-time; while scrubbing there is no look-ahead:
//   lookAhead = isPlayingForward ? 0.05 : 0
//   seekTarget = clampedSeekTime + lookAhead
inline double seekTargetTime(double clampedSeekTime, bool isPlayingForward) {
  const double averageSeekTime = 0.05;  // cs:201
  return clampedSeekTime + (isPlayingForward ? averageSeekTime : 0.0);
}

// ── PlayVideoClip (timeline TimeClip player). PlayVideoClip.cs:82-119. ────────────────────────────
// The timeclip stretches a source range (in the video) across a timeline range (on the composition
// timeline). These structs mirror TiXL's TimeClip.TimeRange / SourceRange (both in BARS).

struct TimeRange {
  double start = 0.0;
  double end = 0.0;
};

// TiXL PlayVideoClip.cs:88-93 — the playback rate = how fast the source advances per timeline bar:
//   rate = (sourceRange.End - sourceRange.Start) / (timeRange.End - timeRange.Start)
// Degenerate timeRange (End==Start) → rate is undefined; TiXL skips the scaling entirely (leaves the
// rate at its default 1.0 and does not multiply). We return 1.0 for that case.
inline double clipPlaybackRate(const TimeRange& sourceRange, const TimeRange& timeRange) {
  const double timeSpan = timeRange.end - timeRange.start;
  if (timeSpan == 0.0) return 1.0;
  return (sourceRange.end - sourceRange.start) / timeSpan;
}

// TiXL PlayVideoClip.cs:87-95 — map a composition local time (bars) to the source time (bars) inside
// the video, then the caller converts to seconds via SecondsFromBars:
//   barsInSeconds = (localTime - timeRange.Start)          // misnamed in TiXL; still bars here
//   if (timeRange.End != timeRange.Start) barsInSeconds *= rate
//   barsInSeconds += sourceRange.Start
inline double clipSourceBars(double localTimeBars, const TimeRange& sourceRange,
                             const TimeRange& timeRange) {
  double b = localTimeBars - timeRange.start;
  if (timeRange.end != timeRange.start) {
    b *= clipPlaybackRate(sourceRange, timeRange);
  }
  b += sourceRange.start;
  return b;
}

// TiXL PlayVideoClip.cs:99-104 — the clamped [start,end] window of the source (in SECONDS), ordered
// (source range may run backwards) and clamped to the real video duration:
//   videoStart = clamp(min(sourceStartSecs, sourceEndSecs), 0, duration)
//   videoEnd   = clamp(max(sourceStartSecs, sourceEndSecs), 0, duration)
struct ClipWindow { double start = 0.0; double end = 0.0; };
inline ClipWindow clipVideoWindow(double sourceStartSecs, double sourceEndSecs,
                                  double durationSecs) {
  double lo = std::min(sourceStartSecs, sourceEndSecs);
  double hi = std::max(sourceStartSecs, sourceEndSecs);
  lo = std::clamp(lo, 0.0, durationSecs);
  hi = std::clamp(hi, 0.0, durationSecs);
  return {lo, hi};
}

// TiXL PlayVideoClip.cs:108-113 — clip seek decision. threshold is 0.01 while exporting else the
// caller's ResyncThreshold; seek if the path was reloaded OR (not already seeking and |delta|>thr):
//   clampedTime = clamp(shouldBeTimeSecs, window.start, window.end)
//   videoTime   = clamp(engineCurrentSecs, window.start, window.end)
//   delta       = clampedTime - videoTime
//   shouldSeek  = reloadedPath || (!engineIsSeeking && |delta| > threshold)
inline double clipSeekThreshold(double resyncThreshold, bool isExporting) {
  return isExporting ? 0.01 : resyncThreshold;
}
inline double clipClampedTime(double shouldBeTimeSecs, const ClipWindow& w) {
  return std::clamp(shouldBeTimeSecs, w.start, w.end);
}
inline bool clipShouldSeek(double delta, double threshold, bool reloadedPath,
                           bool engineIsSeeking) {
  return reloadedPath || (!engineIsSeeking && std::abs(delta) > threshold);
}

// TiXL PlayVideoClip.cs:117-118 — the play decision: only play when NOT exporting and either the path
// just reloaded, or the timeline is inside the clip (shouldBeTime == clampedTime, i.e. not clamped
// off either end) AND time advanced since last frame:
//   _play = !isExporting && (reloadedPath ||
//           (shouldBeTimeSecs == clampedTime && clampedTime - lastUpdateTime > 0))
inline bool clipShouldPlay(double shouldBeTimeSecs, double clampedTime, double lastUpdateTime,
                           bool isExporting, bool reloadedPath) {
  if (isExporting) return false;
  if (reloadedPath) return true;
  if (videoPlaybackBug() == 3) return shouldBeTimeSecs == clampedTime;  // TEETH: drop advance guard
  return shouldBeTimeSecs == clampedTime && (clampedTime - lastUpdateTime) > 0.0;
}

}  // namespace video
}  // namespace sw
