// runtime/audio_reaction — the stateful cook for the AudioReaction node, ported faithfully
// from TiXL Operators/Lib/io/audio/AudioReaction.cs. Given the live spectrum + the node's
// params + a per-node state block, it produces the node's three outputs (Level/WasHit/
// HitCount). Kept out of the generic stateless value-node `evaluate()` because it needs (a)
// the full spectrum (too big for the GPU-shared EvaluationContext) and (b) per-node memory
// across frames. main cooks each AudioReaction node once per frame and caches the result;
// evalFloat reads that cache for the node's output pins.
//
// runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include "runtime/spectrum_analyzer.h"  // SpectrumSnapshot

namespace sw {

// The 10 AudioReaction inputs (TiXL InputSlots), gathered from Node::params each frame.
struct AudioReactionParams {
  float amplitude = 1.0f;
  int   inputBand = 2;          // InputModes: 0 RawFft,1 NormalizedFft,2 FrequencyBands,3 Peaks,4 Attacks
  float windowCenter = 0.0f;    // 0..1 across the bins
  float windowWidth = 1.0f;     // 0..1
  float windowEdge = 1.0f;      // 0.0001..1
  float threshold = 0.5f;
  float minTimeBetweenHits = 0.1f;
  int   output = 3;             // OutputModes: 0 Pulse,1 TimeSinceHit,2 Count,3 Level,4 AccumulatedLevel
  float bias = 1.0f;
  bool  reset = false;
};

// Per-node memory across frames (TiXL private fields). One per AudioReaction instance.
struct AudioReactionState {
  int    hitCount = 0;
  bool   isHitActive = false;
  double lastHitTime = -1.0;
  double accumulatedLevel = 0.0;
  bool   prevReset = false;
  float  sum = 0.0f;            // last windowed Sum (for the node's spectrum overlay)
};

// The node's three outputs (TiXL Level/WasHit/HitCount).
struct AudioReactionOut {
  float level = 0.0f;
  bool  wasHit = false;
  int   hitCount = 0;
};

// Cook one frame. `time` is the app's seconds clock (TiXL LocalFxTime). Mutates `state`.
// Faithful port of AudioReaction.Update(): pick bins by inputBand, window-weighted Sum,
// threshold + min-time hit detection, then the selected Output mode shapes Level.
AudioReactionOut cookAudioReaction(const SpectrumSnapshot& spec, const AudioReactionParams& p,
                                   double time, AudioReactionState& state);

// Isolated proof: Output=Level tracks a windowed band; a rising band past Threshold fires
// WasHit and bumps HitCount; min-time debounces a second immediate hit; Reset zeroes Count.
// injectBug flips the verdict so the test must FAIL.
int runAudioReactionSelfTest(bool injectBug);

}  // namespace sw
