// runtime/node_registry_math_anim — self-registering MATH NodeSpec leaf:
// animator / audio value sources (AudioReaction, AnimValue, AnimInt, AnimBoolean).
//
// Split from the 980-line node_registry_math.cpp (ratchet debt, ARCHITECTURE rule 4 + rule 7).
// Every spec below is moved VERBATIM from the old mathSpecs() manifest — name / ports / widgets /
// defaults / evaluate binding unchanged. Adding a math op here = drop a MathOp registrar; the
// central manifest is never touched again (mirror of the point-modify / image-filter / value-op
// self-registration sinks). Stateless ops carry their pure evaluate fn; stateful ops carry
// nullptr (cooked by frame_cook's stateful-value seam, dispatched by type name).
#include "runtime/graph.h"            // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink
#include "runtime/value_eval_ops.h"    // evalAdd, evalSine, evalClamp, … (pure value-node fns)

namespace sw {
namespace {

      // TiXL AudioReaction (full parity): 3 outputs + 10 params. STATEFUL — cooked in main
      // from the live spectrum (runtime/audio_reaction) because it needs the whole spectrum
      // (too big for ctx) + per-node memory; so it has no pure evaluate() and evalFloat reads
      // its outputs from Node::outCache. Params are pinless (Inspector knobs, no canvas pins).
static const MathOp _reg_AudioReaction{
      {"AudioReaction", "AudioReaction",
       {{"Level", "Level", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"HitCount", "HitCount", "Float", false},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"InputBand", "InputBand", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
         {"RawFft", "NormalizedFft", "FrequencyBands", "Peaks", "Attacks"}, true},
        {"WindowCenter", "WindowCenter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowWidth", "WindowWidth", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowEdge", "WindowEdge", "Float", true, 1.0f, 0.0001f, 1.0f, Widget::Slider, {}, true},
        {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.1f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Output", "Output", "Float", true, 3.0f, 0.0f, 4.0f, Widget::Enum,
         {"Pulse", "TimeSinceHit", "Count", "Level", "AccumulatedLevel"}, true},
        {"Bias", "Bias", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr}
};

      // AnimValue — the Anim* family foundation: Result = AnimMath.CalcValueForNormalizedTime(shape,
      // time*rateFactor*rate+phase, 0, bias, ratio)*amplitude + offset (PURE, via anim_math.h);
      // WasHit = (int)priorNormalizedTime != (int)normalizedTime (the cross-frame integer tooth — the
      // ONLY consumer of state). TiXL numbers/anim/animators/AnimValue.cs (stateful; outputs FIRST;
      // evaluate==nullptr — cooked by frame_cook's stateful-value seam). WasHit is Bool→Float 0/1
      // (Cut 32). Inputs in TiXL Input decl order. .t3 DEFAULTS (AnimValue.t3, re-read & confirmed):
      // Rate=1, Shape=1(Ramps — the .t3 selector value, not the C# field default Endless=0), Phase=0,
      // Amplitude=1, Ratio=1, Offset=0, Bias=0.5, AllowSpeedFactor=1(FactorA), OverrideTime=0.
      // FORKS (see step fn): SINGLE-CLOCK time (OverrideTime when nonzero, else the seam clock — the
      // resolver can't see HasInputConnections); WasHit double-eval guard dropped (once-per-frame cook);
      // SpeedFactor read from the context-var YELLOW seam (FloatVariables["SpeedFactorA/B"], default 1).
      // Shape/AllowSpeedFactor are compile-time Widget::Enum selectors (Cut 71-72 precedent).
static const MathOp _reg_AnimValue{
      {"AnimValue", "AnimValue",
       {{"Result", "Result", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"OverrideTime", "OverrideTime", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"Shape", "Shape", "Float", true, 1.0f, 0.0f, 12.0f, Widget::Enum,
         {"Endless", "Ramps", "Saws", "KickSaws", "Square", "ZigZag", "Wave", "Sin",
          "PerlinNoise", "PerlinNoiseSigned", "Random", "RandomSigned", "Steps"}},
        {"Rate", "Rate", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Ratio", "Ratio", "Float", true, 1.0f, 0.0001f, 10.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Offset", "Offset", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Bias", "Bias", "Float", true, 0.5f, 0.0001f, 1.0f},
        {"AllowSpeedFactor", "AllowSpeedFactor", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "FactorA", "FactorB"}}},
       nullptr}
};

      // AnimInt — the integer sibling of AnimValue: Result = (int)(time*rateFactor*rate+phase),
      // optionally wrapped with a POSITIVE modulo (Modulo!=0 → result.Mod(Modulo), MathUtils.cs:273);
      // WasHit = the same cross-frame integer tooth. TiXL numbers/anim/animators/AnimInt.cs (stateful;
      // outputs FIRST; evaluate==nullptr — cooked by frame_cook's stateful-value seam). Result is
      // int→Float; WasHit is Bool→Float 0/1 (Cut 32). Inputs in TiXL Input decl order. .t3 DEFAULTS
      // (AnimInt.t3, re-read & confirmed): Modulo=0, Rate=1, Phase=0, AllowSpeedFactor=1(FactorA),
      // OverrideTime=0. Modulo is a TiXL int carried on the float rail (step fn does std::lround).
      // FORKS (see step fn): SINGLE-CLOCK time (OverrideTime when nonzero, else seam clock); the
      // _lastUpdateFrame frame-dedup guard dropped (once-per-frame cook); SpeedFactor read from the
      // context-var YELLOW seam. AllowSpeedFactor is a compile-time Widget::Enum selector.
static const MathOp _reg_AnimInt{
      {"AnimInt", "AnimInt",
       {{"Result", "Result", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"Modulo", "Modulo", "Float", true, 0.0f, 0.0f, 100.0f},
        {"OverrideTime", "OverrideTime", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"Rate", "Rate", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"AllowSpeedFactor", "AllowSpeedFactor", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "FactorA", "FactorB"}}},
       nullptr}
};

      // AnimBoolean — the INVERSE of AnimValue/AnimInt: NO Result output; its ONLY output
      // TriggerOutput IS the cross-frame integer tooth = (int)priorNormalizedTime != (int)nt (Bool→
      // Float 0/1). TiXL numbers/anim/animators/AnimBoolean.cs (stateful; evaluate==nullptr; reads
      // ONLY context.LocalFxTime — NO OverrideTime). Inputs in TiXL Input decl order. .t3 DEFAULTS
      // (AnimBoolean.t3, re-read & confirmed): Rate=1, ★AllowSpeedFactor=0 (None — DIFFERENT from
      // AnimValue/AnimInt's 1/FactorA), Phase=0. NO Modulo/OverrideTime/Ratio. FORKS (see step fn):
      // no single-clock fork (no OverrideTime → always the seam clock, exact); _lastUpdateFrame guard
      // dropped; SpeedFactor via context-var seam. AllowSpeedFactor is a Widget::Enum selector.
static const MathOp _reg_AnimBoolean{
      {"AnimBoolean", "AnimBoolean",
       {{"TriggerOutput", "TriggerOutput", "Float", false},
        {"Rate", "Rate", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"AllowSpeedFactor", "AllowSpeedFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "FactorA", "FactorB"}}},
       nullptr}
};

      // TiXL DetectBpm (the OLDER io/audio/_/DetectBpm.cs operator — NOT the editor BpmDetection class).
      // STATEFUL — cooked in main from the live FFT (runtime/detect_bpm) because it needs the whole
      // spectrum (too big for ctx) + per-node memory across frames; so it has no pure evaluate() and
      // evalResidentFloat reads its DetectedBpm output from extOut[0] (the no-evaluate readback path).
      // Mirror of AudioReaction. ONE float output (DetectedBpm) FIRST → extOut[0]. Params are pinless
      // (Inspector knobs). .t3 DEFAULTS (DetectBpm.t3, confirmed): LowerLimit=2/UpperLimit=199 (INTEGER
      // bin borders, NOT a normalized range — the operator's contract), BufferDurationSec=15,
      // LowestBpm=120, HighestBpm=180, LockItFactor=0. LowerLimit/UpperLimit are integer-valued knobs
      // carried on the float rail (the cook does (int)(v+0.5)). DEFERRED FORK: DetectBpm.cs's second
      // output `Measurements` (Slot<List<float>>, the per-bpm energy curve) is a FloatList output the
      // NodeSpec can't yet express — omitted; named, not invented (fork-detectbpm-measurements-floatlist).
static const MathOp _reg_DetectBpm{
      {"DetectBpm", "DetectBpm",
       {{"DetectedBpm", "DetectedBpm", "Float", false},
        {"LowerLimit", "LowerLimit", "Float", true, 2.0f, 0.0f, 1024.0f, Widget::Slider, {}, true},
        {"UpperLimit", "UpperLimit", "Float", true, 199.0f, 0.0f, 1024.0f, Widget::Slider, {}, true},
        {"BufferDurationSec", "BufferDurationSec", "Float", true, 15.0f, 1.0f, 60.0f, Widget::Slider, {}, true},
        {"LowestBpm", "LowestBpm", "Float", true, 120.0f, 50.0f, 200.0f, Widget::Slider, {}, true},
        {"HighestBpm", "HighestBpm", "Float", true, 180.0f, 50.0f, 200.0f, Widget::Slider, {}, true},
        {"LockItFactor", "LockItFactor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true}},
       nullptr}
};

}  // namespace
}  // namespace sw
