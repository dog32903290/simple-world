# Lib.numbers Porting Research

Scope: all TiXL nodes whose namespace starts with `Lib.numbers`, researched for TiXL -> Vuo mapping. This is research only; no Vuo node code is included.

Source boundary used: `external/tixl-spec/TIXL_CLONE_SPEC_20260604`, `external/tixl`, `external/vuo`, and `docs/tixl-porting/README.md` Node Card Schema.

## Namespace Grade Summary

| Namespace | Nodes | A | B | C | D |
|---|---:|---:|---:|---:|---:|
| `Lib.numbers.anim` | 1 | 0 | 1 | 0 | 0 |
| `Lib.numbers.anim._obsolete` | 7 | 0 | 0 | 0 | 7 |
| `Lib.numbers.anim.animators` | 10 | 0 | 10 | 0 | 0 |
| `Lib.numbers.anim.time` | 13 | 0 | 9 | 0 | 4 |
| `Lib.numbers.anim.utils` | 2 | 0 | 0 | 0 | 2 |
| `Lib.numbers.anim.vj` | 4 | 0 | 1 | 0 | 3 |
| `Lib.numbers.bool.combine` | 4 | 4 | 0 | 0 | 0 |
| `Lib.numbers.bool.convert` | 2 | 2 | 0 | 0 | 0 |
| `Lib.numbers.bool.logic` | 9 | 3 | 6 | 0 | 0 |
| `Lib.numbers.bool.process` | 4 | 0 | 4 | 0 | 0 |
| `Lib.numbers.color` | 14 | 0 | 12 | 2 | 0 |
| `Lib.numbers.curve` | 2 | 0 | 1 | 1 | 0 |
| `Lib.numbers.data._obsolete` | 6 | 0 | 0 | 0 | 6 |
| `Lib.numbers.data.utils` | 7 | 5 | 0 | 2 | 0 |
| `Lib.numbers.float.adjust` | 8 | 8 | 0 | 0 | 0 |
| `Lib.numbers.float.basic` | 9 | 9 | 0 | 0 | 0 |
| `Lib.numbers.float.logic` | 7 | 5 | 2 | 0 | 0 |
| `Lib.numbers.float.process` | 14 | 4 | 10 | 0 | 0 |
| `Lib.numbers.float.random` | 3 | 0 | 3 | 0 | 0 |
| `Lib.numbers.float.trigonometry` | 3 | 3 | 0 | 0 | 0 |
| `Lib.numbers.floats.basic` | 4 | 0 | 4 | 0 | 0 |
| `Lib.numbers.floats.conversion` | 3 | 0 | 3 | 0 | 0 |
| `Lib.numbers.floats.io` | 1 | 0 | 0 | 1 | 0 |
| `Lib.numbers.floats.logic` | 2 | 0 | 2 | 0 | 0 |
| `Lib.numbers.floats.process` | 16 | 0 | 14 | 2 | 0 |
| `Lib.numbers.int.basic` | 10 | 9 | 1 | 0 | 0 |
| `Lib.numbers.int.logic` | 5 | 3 | 2 | 0 | 0 |
| `Lib.numbers.int.process` | 9 | 5 | 2 | 2 | 0 |
| `Lib.numbers.int2.basic` | 1 | 1 | 0 | 0 | 0 |
| `Lib.numbers.int2.process` | 5 | 5 | 0 | 0 | 0 |
| `Lib.numbers.ints` | 5 | 5 | 0 | 0 | 0 |
| `Lib.numbers.vec2` | 14 | 0 | 14 | 0 | 0 |
| `Lib.numbers.vec2.process` | 3 | 0 | 3 | 0 | 0 |
| `Lib.numbers.vec3` | 22 | 0 | 21 | 0 | 1 |
| `Lib.numbers.vec3.process` | 3 | 0 | 3 | 0 | 0 |
| `Lib.numbers.vec4` | 4 | 0 | 4 | 0 | 0 |
| **Total** | **236** | **71** | **132** | **10** | **23** |

Grade rule used here: A = pure scalar/int/bool value node; B = vector/list/color/stateful/time helper that is portable with care; C = image/texture/buffer/custom renderer/data model work; D = obsolete, TiXL app graph/control side effect, Ableton/app playback, or document-only dependency.

## Complete Node Cards
## Remap

- TiXL full path: `Lib.numbers.float.adjust.Remap`
- Namespace: `Lib.numbers.float.adjust`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/adjust/Remap.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/adjust/Remap.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/adjust/Remap.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: ApplyGainAndBias, Clamp, Fmod`
- Purpose: Map a scalar from input range to output range, with optional bias/gain shaping and Normal/Clamped/Modulo modes.
- Conversion: C# float maps to VuoReal; BiasAndGain Vector2 maps to VuoPoint2d; Mode needs an enum.
- Inputs:
  - BiasAndGain: Vector2, default=Unknown
  - Mode: int, default=Unknown, enum=['Normal', 'Clamped', 'Modulo']
  - RangeInMax: float, default=Unknown
  - RangeInMin: float, default=0.0
  - RangeOutMax: float, default=Unknown
  - RangeOutMin: float, default=Unknown
  - Value: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Computes normalized=(Value-RangeInMin)/(RangeInMax-RangeInMin), applies MathUtils.ApplyGainAndBias only for normalized inside 0..1, scales to output range, then clamps or fmods by mode. Division by zero behavior is not guarded in C#.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoPoint2d, VuoInteger
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.scale` partial
  - missing Vuo support: TiXL bias/gain curve and Modulo mode need a custom node wrapper.
- Porting grade:
  - A for scalar math, but custom because Vuo Scale lacks TiXL bias/gain and modulo semantics.
- First implementation recommendation: Implement as custom Vuo C node or composition around `vuo.math.scale` plus custom bias/gain.
- Verification fixture: Value 0.25, in 0..1, out 10..20, BiasAndGain 0.5/0.5, Normal => 12.5; reversed output range must clamp between sorted bounds in Clamped mode.
- Risks / unknowns: Unknown defaults from `.t3` need final verification before node code.

## Lerp

- TiXL full path: `Lib.numbers.float.process.Lerp`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/Lerp.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/Lerp.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/Lerp.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: Lerp`
- Purpose: Blend between scalar A and B by factor F, optionally clamping F to 0..1.
- Conversion: float -> VuoReal; bool Clamp -> VuoBoolean.
- Inputs:
  - A: float, default=0.0
  - B: float, default=1.0
  - Clamp: bool, default=Unknown
  - F: float, default=Unknown
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - If Clamp is true, F is clamped to 0..1; result uses MathUtils.Lerp(A, B, F).
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoBoolean
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.scale` partial
  - missing Vuo support: None for scalar form; vector/color variants need separate type choices.
- Porting grade:
  - A: pure value/control.
- First implementation recommendation: Use Vuo generic lerp if available or implement one scalar node; keep Clamp flag because Vuo Scale is not the same API.
- Verification fixture: A=10, B=20, F=1.5, Clamp=false => 25; Clamp=true => 20.
- Risks / unknowns: Unknown if Vuo built-in blend should replace custom node.

## Clamp

- TiXL full path: `Lib.numbers.float.adjust.Clamp`
- Namespace: `Lib.numbers.float.adjust`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/adjust/Clamp.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/adjust/Clamp.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/adjust/Clamp.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: Clamp`
- Purpose: Restrict a scalar to Min/Max.
- Conversion: float -> VuoReal.
- Inputs:
  - Max: float, default=Unknown
  - Min: float, default=0.0
  - Value: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Result is MathUtils.Clamp(Value, Min, Max). C# generic Clamp does not sort reversed Min/Max.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.limitToRange`
  - missing Vuo support: None.
- Porting grade:
  - A: direct pure value transform.
- First implementation recommendation: Use `vuo.math.limitToRange` Saturate or direct custom clamp matching TiXL reversed-range behavior.
- Verification fixture: Value 12, Min 0, Max 10 => 10; reversed Min/Max needs explicit test.
- Risks / unknowns: Reversed min/max semantics must be tested against C# generic MathUtils.Clamp.

## SmoothStep

- TiXL full path: `Lib.numbers.float.process.SmoothStep`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/SmoothStep.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/SmoothStep.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/SmoothStep.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: SmootherStep, Fade`
- Purpose: Return a smoothed 0..1 factor for Value between Min and Max.
- Conversion: float -> VuoReal.
- Inputs:
  - Max: float, default=Unknown
  - Min: float, default=Unknown
  - Value: float, default=1.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Despite the node name, C# calls MathUtils.SmootherStep, i.e. fade(t)=t^3*(t*(t*6-15)+10) after clamping t to 0..1.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: No direct Vuo node found in allowed source; custom formula needed.
- Porting grade:
  - A: pure scalar math, custom formula.
- First implementation recommendation: Implement exact SmootherStep formula, not classic smoothstep.
- Verification fixture: Min=0, Max=1, Value=0.5 => 0.5; Value below/above clamps to 0/1.
- Risks / unknowns: Name mismatch is the main semantic trap.

## Ease

- TiXL full path: `Lib.numbers.float.process.Ease`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/Ease.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/Ease.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/Ease.md`
  - related shader / helper source: `external/tixl/Core/Utils/EasingFunctions.cs: ApplyEasing, Interpolations, EaseDirection`
- Purpose: Animate from current result to a changed target value over Duration with easing direction/interpolation.
- Conversion: float -> VuoReal; enums -> VuoInteger-backed Vuo enum; bool -> VuoBoolean.
- Inputs:
  - Direction: int, default=2
  - Duration: float, default=0.25
  - Interpolation: int, default=6
  - UseAppRunTime: bool, default=False
  - Value: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Stateful: detects Value changes, stores start time, initial/target values, computes progress by local/app runtime, applies EasingFunctions.ApplyEasing, then lerps.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoBoolean, VuoInteger
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: Vuo needs stateful node instance data and a matching TiXL easing enum table.
- Porting grade:
  - B: high-value but stateful/time-dependent.
- First implementation recommendation: Custom stateful Vuo node; do not replace with pure easing math.
- Verification fixture: Hold Value=0 then switch to 1, Duration=1s; sample at 0,0.5,1s for each enum.
- Risks / unknowns: Motion blur pass skip and local/app runtime choice may not map 1:1.

## Damp

- TiXL full path: `Lib.numbers.float.process.Damp`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/Damp.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/Damp.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/Damp.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: DampFunctions.DampenFloat, SpringDampFloat, SpringDamp, Lerp`
- Purpose: Smooth a changing scalar using either linear interpolation or damped spring.
- Conversion: float -> VuoReal; bool -> VuoBoolean; Method enum -> VuoInteger/enum.
- Inputs:
  - Damping: float, default=Unknown
  - Method: int, default=Unknown
  - UseAppRunTime: bool, default=Unknown
  - Value: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Stateful; skips evaluations closer than 1 ms and motion-blur passes. First evaluation snaps to input. Method selects DampFunctions.LinearInterpolation or DampedSpring.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoInteger, VuoBoolean
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: Needs node state, velocity storage, and Playback.LastFrameDuration equivalent.
- Porting grade:
  - B: high-value stateful control node.
- First implementation recommendation: Custom Vuo stateful node; expose Method enum and preserve first-eval snap.
- Verification fixture: Feed step 0->1 with damping 0.5; compare frame-by-frame trace to TiXL.
- Risks / unknowns: Frame time and motion blur context are TiXL-specific.

## Spring

- TiXL full path: `Lib.numbers.float.process.Spring`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/Spring.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/Spring.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/Spring.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: Lerp`
- Purpose: Spring-like follower for scalar values.
- Conversion: float -> VuoReal; bool -> VuoBoolean.
- Inputs:
  - Strength: float, default=0.5
  - Tension: float, default=0.25
  - UseAppRunTime: bool, default=False
  - Value: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Stateful; every valid evaluation updates springedValue = Lerp(springedValue, (target - Result)*Strength, Tension), then adds it to Result.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoBoolean
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: Needs node instance state and TiXL local/app runtime skip behavior.
- Porting grade:
  - B: high-value stateful motion node.
- First implementation recommendation: Custom Vuo node; keep formula separate from DampFunctions.SpringDamp.
- Verification fixture: Step target to 1 with Tension/Strength presets; trace overshoot/settle.
- Risks / unknowns: Formula differs from Damp DampedSpring; do not merge them.

## Accumulator

- TiXL full path: `Lib.numbers.float.process.Accumulator`
- Namespace: `Lib.numbers.float.process`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/process/Accumulator.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/process/Accumulator.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/process/Accumulator.md`
  - related shader / helper source: `None`
- Purpose: Accumulate Increment per frame or per second, with Running, ResetTrigger, StartValue, and optional modulo.
- Conversion: float -> VuoReal; bool -> VuoBoolean; enum -> VuoInteger.
- Inputs:
  - Accumulate: int, default=Unknown, enum=['PerFrame', 'PerSeconds']
  - Increment: float, default=1.0
  - Modulo: float, default=Unknown
  - ResetTrigger: bool, default=Unknown
  - Running: bool, default=Unknown
  - StartValue: float, default=0.0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Stateful _v and _lastUpdateTime; reset writes StartValue; PerFrame adds Increment, PerSeconds adds Increment*dt; modulo applies when Modulo>0.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoInteger, VuoBoolean
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: Needs Vuo state and a composition time delta source.
- Porting grade:
  - B: high-value stateful utility.
- First implementation recommendation: Custom Vuo node with explicit event/time input or runner-provided delta.
- Verification fixture: Increment=2, PerFrame for 3 events => 6; PerSeconds with dt=0.5 for 3 events => 3.
- Risks / unknowns: TiXL uses context.Playback.SecondsFromBars(context.LocalFxTime); exact Vuo timeline mapping is open.

## Sin

- TiXL full path: `Lib.numbers.float.trigonometry.Sin`
- Namespace: `Lib.numbers.float.trigonometry`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/trigonometry/Sin.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/trigonometry/Sin.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/trigonometry/Sin.md`
  - related shader / helper source: `None`
- Purpose: Compute sine oscillator with period, phase, amplitude, and offset.
- Conversion: float -> VuoReal.
- Inputs:
  - Amplitude: float, default=Unknown
  - Input: float, default=Unknown
  - Offset: float, default=Unknown
  - Period: float, default=Unknown
  - Phase: float, default=Unknown
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Result = sin(Input/Period + Phase) * Amplitude + Offset. TiXL expects radians in the formula; Vuo math sin node expects degrees.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.sin` partial, degrees
  - missing Vuo support: Vuo built-in angle unit differs and lacks period/amplitude/offset wrapper.
- Porting grade:
  - A: pure scalar math.
- First implementation recommendation: Custom wrapper or composition around radians conversion; do not directly use `vuo.math.sin` without unit correction.
- Verification fixture: Input=pi/2, Period=1, Phase=0, Amp=2, Offset=1 => 3.
- Risks / unknowns: Period 0 is unguarded in C#.

## Cos

- TiXL full path: `Lib.numbers.float.trigonometry.Cos`
- Namespace: `Lib.numbers.float.trigonometry`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/trigonometry/Cos.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/trigonometry/Cos.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/trigonometry/Cos.md`
  - related shader / helper source: `None`
- Purpose: Compute cosine of scalar input.
- Conversion: float -> VuoReal.
- Inputs:
  - Input: float, default=Unknown
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Result = cos(Input). TiXL uses radians; Vuo `vuo.math.cos` expects degrees.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.cos` partial, degrees
  - missing Vuo support: Unit mismatch only.
- Porting grade:
  - A: pure scalar math.
- First implementation recommendation: Use custom radian cos or convert radians to degrees before Vuo built-in.
- Verification fixture: Input=0 => 1; Input=pi => -1.
- Risks / unknowns: Vuo built-in degree input is the risk.

## PerlinNoise

- TiXL full path: `Lib.numbers.float.random.PerlinNoise`
- Namespace: `Lib.numbers.float.random`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/random/PerlinNoise.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/random/PerlinNoise.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/random/PerlinNoise.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: PerlinNoise, Noise, Fade, ApplyGainAndBias`
- Purpose: Generate continuous scalar noise from local time or OverrideTime, with phase, seed, octaves, range, amplitude, and bias/gain.
- Conversion: float -> VuoReal; int -> VuoInteger; BiasAndGain Vector2 -> VuoPoint2d.
- Inputs:
  - Amplitude: float, default=Unknown
  - BiasAndGain: Vector2, default=Unknown
  - Frequency: float, default=Unknown
  - Octaves: int, default=Unknown
  - OverrideTime: float, default=0.0
  - Phase: float, default=Unknown
  - RangeMax: float, default=Unknown
  - RangeMin: float, default=Unknown
  - Seed: int, default=0
- Outputs:
  - Result: float, default=Unknown
- Runtime behavior:
  - Uses MathUtils.PerlinNoise(value, Frequency, Octaves, Seed), scales by 1.37, maps to 0..1, applies gain/bias, then amplitude and range.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoInteger, VuoPoint2d
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: 缺口
  - missing Vuo support: No equivalent Vuo Perlin node found; TiXL hash/noise algorithm must be copied semantically.
- Porting grade:
  - B: portable but helper-specific and time-dependent.
- First implementation recommendation: Custom deterministic noise node; accept explicit time input to avoid hidden context drift.
- Verification fixture: Seed/frequency/octaves fixed, sample OverrideTime 0,0.5,1; compare exact numbers from TiXL.
- Risks / unknowns: Expression has a precedence-looking line in vec noise variants; verify C# output before porting.

## Add / Sub / Multiply / Div / Pow

- TiXL full path: `Lib.numbers.float.basic.Add`
- Namespace: `Lib.numbers.float.basic`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/basic/Add.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/basic/Add.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/basic/Add.md`
  - related shader / helper source: `None`
  - sibling C# paths: `external/tixl/Operators/Lib/numbers/float/basic/Sub.cs`, `external/tixl/Operators/Lib/numbers/float/basic/Multiply.cs`, `external/tixl/Operators/Lib/numbers/float/basic/Div.cs`, `external/tixl/Operators/Lib/numbers/float/basic/Pow.cs`
  - sibling .t3 paths: `external/tixl/Operators/Lib/numbers/float/basic/Sub.t3`, `external/tixl/Operators/Lib/numbers/float/basic/Multiply.t3`, `external/tixl/Operators/Lib/numbers/float/basic/Div.t3`, `external/tixl/Operators/Lib/numbers/float/basic/Pow.t3`
  - sibling docs paths: `external/tixl/.help/docs/operators/lib/numbers/float/basic/Sub.md`, `external/tixl/.help/docs/operators/lib/numbers/float/basic/Multiply.md`, `external/tixl/.help/docs/operators/lib/numbers/float/basic/Div.md`, `external/tixl/.help/docs/operators/lib/numbers/float/basic/Pow.md`
- Purpose: Scalar arithmetic family: Add, Sub, Multiply, Div, Pow.
- Conversion: float -> VuoReal.
- Inputs:
  - Add/Sub: Input1/Input2 float.
  - Multiply/Div: A/B float.
  - Pow: Value/Exponent float.
- Outputs:
  - Result: float.
- Runtime behavior:
  - Add/Sub/Multiply are direct binary ops; Div returns NaN if B==0; Pow uses Math.Pow(Value, Exponent).
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal
  - Vuo output types: VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.add`, `subtract`, `multiply`, `divide.VuoReal`, `exponentiate`
  - missing Vuo support: Vuo Divide returns C division result; TiXL explicitly emits NaN on zero denominator.
- Porting grade:
  - A: pure scalar math; Div needs exact zero behavior.
- First implementation recommendation: Map Add/Sub/Multiply/Pow to Vuo math nodes; wrap Div if NaN-on-zero must be guaranteed.
- Verification fixture: Div A=1,B=0 => NaN; Pow 4^0.5 => 2.
- Risks / unknowns: Spec pack reports `Add` ports oddly (`Input1` appears as output), so final node code should re-read C# slots before implementation.

## Compare

- TiXL full path: `Lib.numbers.float.logic.Compare`
- Namespace: `Lib.numbers.float.logic`
- Clone status: `doc_verified_no_csharp_match`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/float/logic/Compare.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/float/logic/Compare.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/float/logic/Compare.md`
  - related shader / helper source: `None`
- Purpose: Compare Value against TestValue using Smaller, Equal, Larger, NotEqual with precision for equality.
- Conversion: float -> VuoReal; bool result -> VuoBoolean; Mode -> enum.
- Inputs:
  - Mode: int, default=1, enum=['IsSmaller', 'IsEqual', 'IsLarger', 'IsNotEqual']
  - Precision: float, default=Unknown
  - TestValue: float, default=Unknown
  - Value: float, default=0.0
- Outputs:
  - IsTrue: bool, default=Unknown
- Runtime behavior:
  - Modes: IsSmaller uses <; IsEqual uses abs(v-test)<Precision; IsLarger uses >; IsNotEqual uses >= Precision.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoInteger
  - Vuo output types: VuoBoolean
  - direct built-in Vuo equivalent, if any: `vuo.math.areEqual`, `isLessThan`, `isGreaterThan` partial
  - missing Vuo support: Single Vuo built-in does not expose this exact four-mode + precision API.
- Porting grade:
  - A: pure scalar predicate.
- First implementation recommendation: Custom node or composition with `vuo.math.compareNumbers`/equal nodes; preserve strict equality threshold.
- Verification fixture: Value=1, Test=1.0005, Precision=.001 => equal true; notEqual false.
- Risks / unknowns: Enum ordering must match C#.

## Vec2 / Vec3 Common Conversions

- TiXL full path: `Lib.numbers.vec2.Vector2Components`
- Namespace: `Lib.numbers.vec2`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/vec2/Vector2Components.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/vec2/Vector2Components.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/vec2/Vector2Components.md`
  - related shader / helper source: `None`
  - sibling C# paths: `external/tixl/Operators/Lib/numbers/vec3/Vector3Components.cs`, `external/tixl/Operators/Lib/numbers/vec2/Vec2ToVec3.cs`, `external/tixl/Operators/Lib/numbers/vec2/Int2ToVector2.cs`, `external/tixl/Operators/Lib/numbers/vec4/RgbaToColor.cs`
  - sibling .t3 paths: `external/tixl/Operators/Lib/numbers/vec3/Vector3Components.t3`, `external/tixl/Operators/Lib/numbers/vec2/Vec2ToVec3.t3`, `external/tixl/Operators/Lib/numbers/vec2/Int2ToVector2.t3`, `external/tixl/Operators/Lib/numbers/vec4/RgbaToColor.t3`
  - sibling docs paths: `external/tixl/.help/docs/operators/lib/numbers/vec3/Vector3Components.md`, `external/tixl/.help/docs/operators/lib/numbers/vec2/Vec2ToVec3.md`, `external/tixl/.help/docs/operators/lib/numbers/vec2/Int2ToVector2.md`, `external/tixl/.help/docs/operators/lib/numbers/vec4/RgbaToColor.md`
- Purpose: Compose/decompose common TiXL vector shapes: Vector2Components, Vector3Components, Vec2ToVec3, Int2ToVector2, RgbaToColor.
- Conversion: Vector2 -> VuoPoint2d; Vector3 -> VuoPoint3d; Vector4/color -> VuoPoint4d or VuoColor depending semantic role; Int2 has no exact Vuo type, usually VuoPoint2d with integer-valued components or two VuoInteger ports.
- Inputs:
  - Value: Vector2, default=Unknown
- Outputs:
  - X: float, default=Unknown
  - Y: float, default=Unknown
- Runtime behavior:
  - C# components simply split X/Y/Z/W; Vec2ToVec3 copies XY plus Z; Int2ToVector2 casts Int2 to Vector2; RgbaToColor constructs Vector4(R,G,B,A).
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoInteger, VuoPoint2d, VuoPoint3d, VuoPoint4d, VuoColor
  - Vuo output types: VuoReal, VuoPoint2d, VuoPoint3d, VuoPoint4d, VuoColor
  - direct built-in Vuo equivalent, if any: `vuo.type.*`, `vuo.color.make.rgb`, `vuo.color.get.rgb`
  - missing Vuo support: No native Vuo Int2; choose Point2d or width/height integer pair per node.
- Porting grade:
  - B: common and portable, but type semantics matter.
- First implementation recommendation: Prefer Vuo `vuo.type.*` nodes where names match; custom only for Int2 policy and TiXL color-as-Vector4 compatibility.
- Verification fixture: Roundtrip Vec2(3,4)->components->Vec2, Vec2ToVec3 z=5 => (3,4,5).
- Risks / unknowns: Do not blindly map every Vector4 to VuoColor; some vec4 nodes are mathematical point4d.

## Vec2 / Vec3 Math

- TiXL full path: `Lib.numbers.vec2.RemapVec2`
- Namespace: `Lib.numbers.vec2`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/vec2/RemapVec2.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/vec2/RemapVec2.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/vec2/RemapVec2.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: Remap(Vector2/Vector3), Clamp(Vector2/Vector3), Lerp(Vector2/Vector3)`
  - sibling C# paths: `external/tixl/Operators/Lib/numbers/vec2/AddVec2.cs`, `external/tixl/Operators/Lib/numbers/vec2/ScaleVector2.cs`, `external/tixl/Operators/Lib/numbers/vec2/DivideVector2.cs`, `external/tixl/Operators/Lib/numbers/vec2/DotVec2.cs`, `external/tixl/Operators/Lib/numbers/vec3/AddVec3.cs`, `external/tixl/Operators/Lib/numbers/vec3/SubVec3.cs`, `external/tixl/Operators/Lib/numbers/vec3/ScaleVector3.cs`, `external/tixl/Operators/Lib/numbers/vec3/DotVec3.cs`, `external/tixl/Operators/Lib/numbers/vec3/CrossVec3.cs`, `external/tixl/Operators/Lib/numbers/vec3/LerpVec3.cs`, `external/tixl/Operators/Lib/numbers/vec3/Magnitude.cs`, `external/tixl/Operators/Lib/numbers/vec3/NormalizeVector3.cs`, `external/tixl/Operators/Lib/numbers/vec3/Vec3Distance.cs`, `external/tixl/Operators/Lib/numbers/vec3/RotateVector3.cs`
  - sibling .t3 paths: same filenames as above with `.t3`.
  - sibling docs paths: matching `.help/docs/operators/lib/numbers/vec2/*.md` and `.help/docs/operators/lib/numbers/vec3/*.md`; Unknown for any missing doc record.
- Purpose: Vector math set: Add/Sub/Scale/Divide/Dot/Cross/Lerp/Remap/Magnitude/Normalize/Distance/Rotate.
- Conversion: Vector2 -> VuoPoint2d; Vector3 -> VuoPoint3d; scalar factors -> VuoReal.
- Inputs:
  - Mode: int, default=Unknown, enum=['Normal', 'Clamped', 'Modulo']
  - RangeInMax: Vector2, default=Unknown
  - RangeInMin: Vector2, default=Unknown
  - RangeOutMax: Vector2, default=Unknown
  - RangeOutMin: Vector2, default=Unknown
  - Value: Vector2, default=Unknown
- Outputs:
  - Result: Vector2, default=Unknown
- Runtime behavior:
  - Most nodes are component-wise wrappers over System.Numerics operations or MathUtils vector helpers; RemapVec2 supports Normal/Clamped/Modulo like scalar remap but without bias/gain.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoPoint2d, VuoPoint3d, VuoReal, VuoInteger
  - Vuo output types: VuoPoint2d, VuoPoint3d, VuoReal
  - direct built-in Vuo equivalent, if any: `vuo.math.add/subtract/multiply/scale`, `vuo.type.*` partial
  - missing Vuo support: Vuo has generic point math for many ops but not every TiXL mode/rotation/matrix shape.
- Porting grade:
  - B: high-value vector surface; doable with type care.
- First implementation recommendation: Batch map direct ops to Vuo generic math; keep custom nodes for RemapVec2, RotateVector3, matrix-related nodes if Vuo transform APIs diverge.
- Verification fixture: RemapVec2 (0.5,0.5) 0..1 to 10..20 => (15,15); modulo mode with negative input needs explicit test.
- Risks / unknowns: Vector clamp with reversed ranges may differ from scalar Remap clamped mode.

## SampleGradient

- TiXL full path: `Lib.numbers.color.SampleGradient`
- Namespace: `Lib.numbers.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/color/SampleGradient.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/color/SampleGradient.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/color/SampleGradient.md`
  - related shader / helper source: `external/tixl/Core/DataTypes/Gradient.cs: Sample, SampleSpline; external/tixl/Core/Utils/MathUtils.cs: SmootherStep`
- Purpose: Sample a TiXL Gradient at SamplePos and optionally override its interpolation mode.
- Conversion: Gradient has no direct Vuo type; sampled Color can map Vector4 -> VuoColor; OutGradient is a gap.
- Inputs:
  - Gradient: Gradient, default=Unknown
  - Interpolation: int, default=Unknown
  - OverrideInterpolation: bool, default=Unknown
  - SamplePos: float, default=0.0
- Outputs:
  - Color: Vector4, default=Unknown
  - OutGradient: Gradient, default=Unknown
- Runtime behavior:
  - If gradient is null, update returns. If OverrideInterpolation, mutates gradient.Interpolation. Color=gradient.Sample(t); OutGradient passes the possibly modified gradient through.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoReal, VuoBoolean, VuoInteger, 缺口(Gradient)
  - Vuo output types: VuoColor for Color, 缺口(Gradient) for OutGradient
  - direct built-in Vuo equivalent, if any: `vuo.color.palette.make.gradient` partial list palette, no Gradient object
  - missing Vuo support: Native TiXL Gradient data type and interpolation modes Hold/Linear/Smooth/OkLab/Spline.
- Porting grade:
  - B high-value but custom-type gap; C if required as editable Vuo palette type.
- First implementation recommendation: First port only Color sampling from a VuoList_VuoColor plus positions/interpolation enum; postpone OutGradient passthrough.
- Verification fixture: Two-step magenta/black gradient, t=0,0.5,1 across interpolation modes.
- Risks / unknowns: This node mutates input Gradient interpolation; Vuo value semantics may need cloning.

## BlendColors

- TiXL full path: `Lib.numbers.color.BlendColors`
- Namespace: `Lib.numbers.color`
- Clone status: `doc_and_csharp_verified`
- Source evidence:
  - C#: `external/tixl/Operators/Lib/numbers/color/BlendColors.cs`
  - .t3 defaults: `external/tixl/Operators/Lib/numbers/color/BlendColors.t3`
  - docs: `external/tixl/.help/docs/operators/lib/numbers/color/BlendColors.md`
  - related shader / helper source: `external/tixl/Core/Utils/MathUtils.cs: Lerp(Vector4)`
- Purpose: Blend two Vector4 RGBA colors by mode Mix, Multiply, Add, Blend.
- Conversion: Vector4 color maps to VuoColor when semantic color; Factor -> VuoReal; Mode -> enum.
- Inputs:
  - ColorA: Vector4, default=Unknown
  - ColorB: Vector4, default=Unknown
  - Factor: float, default=Unknown
  - Mode: int, default=Unknown, enum=['Mix', 'Multiply', 'Add', 'Blend']
- Outputs:
  - Color: Vector4, default=Unknown
- Runtime behavior:
  - Mix: c1*(1-m)+c2*m. Multiply: c1 * Lerp(Vector4.One,c2,m). Add: c1+c2*m. Blend: alpha composite using c2.W and combined alpha c1.W+c2.W-c1.W*c2.W; Factor is ignored in Blend mode.
- Observed graph usage:
  - common incoming nodes: Unknown; spec pack has frequency edges only, not port-law proof.
  - common outgoing nodes: Unknown; use `.t3` adjacency only as usage evidence.
- Vuo mapping:
  - Vuo input types: VuoColor, VuoReal, VuoInteger
  - Vuo output types: VuoColor
  - direct built-in Vuo equivalent, if any: `vuo.color.blend` partial
  - missing Vuo support: Vuo Blend Colors has richer blend modes but TiXL exact four-mode formulas and Factor behavior need matching.
- Porting grade:
  - B: high-value color node with mode semantics.
- First implementation recommendation: Implement exact TiXL formulas over VuoColor RGBA; do not substitute Vuo blend blindly.
- Verification fixture: ColorA red alpha 1, ColorB blue alpha .5, Factor .25; test all modes, especially Blend ignoring Factor.
- Risks / unknowns: HDR/out-of-range Vector4 values are not clamped in C#.

## Compact Coverage Rows

These rows cover every node, including nodes already expanded above. `ports` are summarized from C# slots when available, otherwise docs/spec ports. `Unknown` means the allowed evidence did not establish the behavior.

### Lib.numbers.anim

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim.AdsrEnvelope` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Duration:float, Envelope:Vector4, Gate:bool, Max:float, Min:float, Mode:int; out IsActive:bool, Result:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.anim._obsolete

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim._obsolete.Counter` | Unknown | in AllowSpeedFactor:int, Blending:float, Increment:float, Modulo:float, Phase:float, Rate:float, SmoothBlending:bool, StartValue:float, ...; out Result:float, WasStep:bool | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete._AnimValueOld` | Unknown | in AllowSpeedFactor:int, Amplitude:float, Bias:float, Offset:float, OverrideTime:float, Phase:float, Rate:float, Ratio:float, ...; out Result:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete._Jitter` | Unknown | in Blending:float, Jump:bool, JumpDistance:float, MaxRange:float, Rate:float, Reset:bool, Seed:int, SmoothBlending:bool, ...; out Result:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete._Jitter2d` | Unknown | in Blending:float, Jump:bool, JumpDistance:float, MaxRange:float, Position:Vector2, Rate:float, Reset:bool, Seed:int, ...; out NewPosition:Vector2 | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete._Time_old` | Unknown | in Mode:int, SpeedFactor:float; out TimeInBars:float, TimeInSecs:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete.__ObsoletePulsate` | Unknown | in BeatTime:float, Frequency:float, Intensity:float, Speed:float; out - | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim._obsolete.__ObsoletePulse` | Unknown | in Amplitude:float, Decay:float, Trigger:bool; out - | D | obsolete/app graph/playback side effect or TiXL-only dependency |

### Lib.numbers.anim.animators

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim.animators.AnimBoolean` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in AllowSpeedFactor:int, Phase:float, Rate:float; out TriggerOutput:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.AnimFloatList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in AllowSpeedFactor:int, Amplitude:float, Bias:float, Offset:float, OffsetCycle:float, OffsetNumber:int, OverrideTime:float, Phase:float, ...; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.AnimInt` | Animates an integer value. | in AllowSpeedFactor:Int32, Modulo:Int32, OverrideTime:Single, Phase:Single, Rate:Single; out Result:int, WasHit:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.AnimValue` | Generates a repetitive LFO-like signal synced to the current BPM rate. It supports various shapes, modes and forms. Y... | in AllowSpeedFactor:Int32, Amplitude:Single, Bias:Single, Offset:Single, OverrideTime:Single, Phase:Single, Rate:Single, Ratio:Single, ...; out Result:float, WasHit:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.AnimVec2` | Generates a repetitive LFO-like signal synced to the current BPM rate. It supports various shapes, modes, and forms. ... | in AllowSpeedFactor:int, AmplitudeFactor:float, Amplitudes:Vector2, Bias:float, Offsets:Vector2, OverrideTime:float, Phases:Vector2, RateFactor:float, ...; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.AnimVec3` | Generates a repetitive LFO-like signal synced to the current BPM rate. It supports various shapes, modes and forms. Y... | in AllowSpeedFactor:int, AmplitudeFactor:float, Amplitudes:Vector3, Bias:float, Offsets:Vector3, OverrideTime:float, Phases:Vector3, RateFactor:float, ...; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.OscillateVec2` | A helper that combines 2 sin waves into a vector Similar Operators [OscillateVec3] [AnimValue] [AnimVec2] [AnimVec3] | in Amplitude:Vector2, AmplitudeScale:float, Offset:Vector2, OverrideTime:float, Period:Vector2, Phase:Vector2, SpeedFactor:float; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.OscillateVec3` | A helper that combines 3 sin waves into a vector Similar Operators [OscillateVec2] [AnimValue] [AnimVec2] [AnimVec3] | in Amplitude:Vector3, AmplitudeScale:float, Offset:Vector3, OverrideTime:float, Period:Vector3, Phase:Vector3, SpeedFactor:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.SequenceAnim` | Creates a visual sequencer that can be controlled by adding a string of numbers | in Bias:float, Direction:int, Interpolation:int, MaxValue:float, MinValue:float, OutputMode:int, OverrideTime:float, Phase:float, ...; out Result:float, WasStep:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.animators.TriggerAnim` | Generate interactive animation values that can be triggered with a boolean value. It offers a variety of shapes and m... | in Amplitude:float, AnimMode:int, Base:float, Bias:float, Delay:float, Duration:float, Shape:int, TimeMode:int, ...; out HasCompleted:bool, Result:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.anim.time

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim.time.AbletonLinkSync` | An experimental implementation of the Ableton Link synchronization. This operator uses the library provided by Ableto... | in AutoConnect:bool, OutputType:int, PauseIfDisconnected:bool, TriggerReconnect:bool, TriggerStartPlaying:bool, TriggerStopPlaying:bool; out IsConnected:bool, Result:float, Tempo:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.time.ClipTime` | Returns the current time in Bars. It is never affected by 'Idle Motion'. This means that when the time is paused, thi... | in -; out Time:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.ConvertTime` | Converts time between seconds and bars. | in Mode:int, Time:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.DateTimeInSecs` | Returns the seconds since 1970. | in Freeze:bool; out Result:int | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.GetFrameSpeedFactor` | This is set when rendering updates not at 60fps. This can happen for... - high framerate displays -> e.g. 2 for 120Hz... | in -; out FrameSpeedFactor:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.HasTimeChanged` | Triggers a bool if time was scrubbed back in the timeline by the user or by looping. Also known as: DidTimeChange. | in Mode:int, Threshold:float, WhichTime:int; out DeltaTime:float, HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.LastFrameDuration` | Measures how long it took to render the last frame in seconds Info: Since Vertical Synchronization is activated by de... | in -; out Duration:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.RunTime` | Returns the application runtime in seconds. This is rarely useful. Consider using [Time]. | in -; out TimeInSeconds:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.SetPlaybackSpeed` | An advanced option that allows you to slow down the playback (including audio). This might be useful in some situatio... | in SpeedFactor:float, SubGraph:Command, TriggerUpdate:bool; out Commands:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.time.SetPlaybackTime` | An advanced playback control that will move the current playhead to the defined time. This can be useful in complex V... | in Enabled:bool, ShowLogMessages:bool, SubGraph:Command, TimeInBars:float, TriggerMode:int; out Commands:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.time.SetTime` | Overrides the animation time of a sub-command graph. Useful combination [PlayVideo] -> [Layer2d] -> [SetCommandTime] | in NewTime:float, OffsetMode:int, SubTree:Command; out Result:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.time.StopWatch` | A stopwatch that can stop, measure, and keep time and thus measure durations. | in DurationIn:int, PauseWithPlayback:bool, ResetTrigger:bool; out Delta:float, LastDuration:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.time.Time` | A cleaned up version of Time adds mode to clarify if time is returned in bars or seconds | in Mode:int, SpeedFactor:float, Units:int; out Timefloat:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.anim.utils

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim.utils.FindKeyframes` | Experimental operator to access keyframe times of another operator instance. This can be useful to build interactive ... | in AnimatedOp:List/float multi, CurveIndex:int, IndexOrTime:float, Mode:int, OpIndex:int, WrapIndex:bool; out KeyframeCount:int, Time:float, Value:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.utils.SetKeyframes` | Allows to record keyframes to a connected animated operator | in AnimatedOp:List/float multi, CurveIndex:int, OpIndex:int, TriggerClear:bool, TriggerSet:bool, Value:float; out CurrentValue:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |

### Lib.numbers.anim.vj

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.anim.vj.ForwardBeatTaps` | A helper op that forwards beat tapping to TiXL interfaces. This can be useful for tapping BPM rate with a MIDI contro... | in SlideSyncTimeOffset:float, SubTree:Command, TriggerBeatTap:bool, TriggerResync:bool; out Result:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.vj.GetBpm` | Returns the current BPM rate | in -; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.anim.vj.SetBpm` | DANGER: Overriding the BPM rate will interfere with your playback and animation speed. Only use this operator if you ... | in BpmRate:float, SubGraph:Command, TriggerUpdate:bool; out Commands:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.anim.vj.SetSpeedFactors` | Speed factors are multiplied to most animation operators like [AnimValue] and [Counter]. This is an extremely powerfu... | in ApplyAs:int, Commands:List/Command multi, SpeedFactorA:float, SpeedFactorB:float, Texture:Texture2D; out Output:Texture2D, OutputCommand:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |

### Lib.numbers.bool.combine

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.bool.combine.All` | Returns true if all of the connected booleans are true. | in Input:List/bool multi; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.combine.And` | Returns True if both inputs are true. Consider using [All] if you want to test more than two booleans for true. | in A:bool, B:bool; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.combine.Any` | Returns true if any of its inputs are true. | in Input:List/bool multi; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.combine.Or` | Returns true if one of its inputs is true. Consider using [Any] if you want to test more than two booleans. | in A:bool, B:bool; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.bool.convert

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.bool.convert.BoolToFloat` | Allows the conversion of a Boolean value into a float value with predefined values Also see [Boolean] / [Value] | in BoolValue:bool, ForFalse:float, ForTrue:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.convert.BoolToInt` | Returns different integer values depending on if a boolean is true or false. With its default values, it converts a b... | in BoolValue:bool, ResultForFalse:int, ResultForTrue:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.bool.logic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.bool.logic.FlipBool` | Toggles (flips) a boolean between true/false every time it is triggered. This is similar to [FlipFlop]. | in DefaultValue:bool, ResetTrigger:bool, Trigger:bool; out Result:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.FlipFlop` | Holds the "activated" state of a boolean. Also check [DelayTrigger]. | in DefaultValue:bool, ResetTrigger:bool, Trigger:bool; out Result:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.HasBooleanChanged` | Returns true if the connected input changed, either from False to True or vice versa. This can be useful to detect th... | in Mode:int, Value:bool; out HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.Not` | Creates and/or inverts a single Boolean value Similar to [Boolean] (but inverted) | in BoolValue:bool; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.logic.PickBool` | Picks a bool from the connected inputs. | in BoolValues:List/bool multi, Index:int; out Selected:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.bool.logic.ToggleBoolean` | When triggered toggles from true to false and back. | in TriggerReset:bool, TriggerToggle:bool; out Result:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.Trigger` | Returns a boolean that gets reset to false on the next frame. | in BoolValue:bool, ColorInGraph:Vector4, OnlyOnDown:bool; out Result:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.WasTrigger` | Returns true if one of the trigger variables has been set. | in CustomVariableName:string, Trigger:int; out WasTriggered:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.logic.Xor` | Combines two boolean values with a not operation. | in A:bool, B:bool; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.bool.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.bool.process.CacheBoolean` | Prevents multiple updates by forwarding a boolean value that was computed earlier. | in Value:bool; out Result:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.process.DelayBoolean` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in FrameCount:int, Trigger:bool; out DelayedTrigger:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.process.DelayTriggerChange` | Delays the change of a boolean flag. This can be useful for implementing interactions where a value needs to stay tru... | in DelayDuration:float, Mode:int, TimeMode:int, Trigger:bool; out DelayedTrigger:bool, RemainingTime:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.bool.process.KeepBoolean` | Keeps the state of flag until it is reset. | in Freeze:bool, Mode:int, Value:bool; out Result:bool, TimeSinceFreeze:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.color

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.color.BlendColors` | Blends two colors with the defined blend mode | in ColorA:Vector4, ColorB:Vector4, Factor:float, Mode:int; out Color:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.BlendGradients` | Blends two color gradients with the defined blendmode | in BlendMode:int, GradientA:Gradient, GradientB:Gradient, MixFactor:float; out Result:Gradient | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.BuildGradient` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Colors:List<Vector4>, Interpolation:int, Positions:List<float>; out OutGradient:Gradient | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.CombineColorLists` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in InputLists:List/List<Vector4> multi; out Selected:List<Vector4> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.DefineGradient` | Defines a gradient from a set of colors and positions. This can be useful for animating gradients. Positions outside ... | in Color1:Vector4, Color1Pos:float, Color2:Vector4, Color2Pos:float, Color3:Vector4, Color3Pos:float, Color4:Vector4, Color4Pos:float, ...; out OutGradient:Gradient | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.GradientsToTexture` | Generates a texture from a gradient | in Direction:int, Gradients:List/Gradient multi, Resolution:int; out GradientsTexture:Texture2D | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.color.HSBToColor` | Creates an RGBA color from HSB settings (same as HSB in the Color Picker) | in Alpha:float, Brightness:float, Hue:float, Saturation:float; out Color:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.HSLToColor` | Creates an RGBA color from HSL settings | in Alpha:float, Hue:float, Lightness:float, Saturation:float; out Color:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.KeepColors` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in AddColorToList:bool, Color:Vector4, MaxLength:int, Reset:bool; out Result:List<Vector4> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.OKLChToColor` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Alpha:float, Brightness:float, Hue:float, IntensityBoost:float, Saturation:float, UseGamma:bool; out Color:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.PickColorFromImage` | Gets the color of a certain position in the texture | in AlwaysUpdate:bool, InputImage:Texture2D, Position:Vector2; out Output:Vector4 | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.color.PickColorFromList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Index:int, Input:List<Vector4>; out Selected:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.PickGradient` | Picks one of the connected gradients | in Gradients:List/Gradient multi, Index:int; out Selected:Gradient | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.color.SampleGradient` | Defines a gradient that can then be sampled into a color or used further by [GradientsToTexture] or other operators. ... | in Gradient:Gradient, Interpolation:int, OverrideInterpolation:bool, SamplePos:float; out Color:Vector4, OutGradient:Gradient | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.curve

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.curve.CurvesToTexture` | Tip: Hold Shift for snapping. Press F to extend the view to the selection. | in Curves:List/Curve multi, Direction:int, ExportGrayScale:bool, SampleSize:int; out CurveTexture:Texture2D | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.curve.SampleCurve` | Creates a window in the 'graph' in which a curve editor with a single curve is created. This curve can be edited (in ... | in Curve:Curve, U:float; out CurveOutput:Curve, Result:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.data._obsolete

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.data._obsolete.ExportPointList` | Unknown | in FilePath:string, FilterDoubleNaN:bool, InputList:StructuredList, TriggerSave:bool; out Result:StructuredList | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.data._obsolete.GetIteratedFloat` | Unknown | in FieldName:string; out Result:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.data._obsolete.GetIteratedVec3` | Unknown | in FieldName:string; out Result:Vector3 | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.data._obsolete.GetIteration` | Unknown | in -; out Index:int, Progress:float | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.data._obsolete.IterateList` | Unknown | in IterateCommands:List/Command multi, List:StructuredList, SetupCommands:List/Command multi; out Result:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |
| `Lib.numbers.data._obsolete.Iterator` | Unknown | in List:StructuredList, SubTree:List/Command multi; out Result:Command | D | obsolete/app graph/playback side effect or TiXL-only dependency |

### Lib.numbers.data.utils

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.data.utils.GetListItemAttribute` | Fetches a float from a list item attribute via Reflection. This is slow. | in DataList:StructuredList, FieldIndex:int, ItemIndex:int, OrFieldName:string; out Result:float | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.data.utils.GetPointDataFromList` | Returns Position, Rotation and W from a point. This can be useful if combined with [PointsToCPU]. | in DataList:StructuredList, ItemIndex:int; out Orientation:Vector4, Position:Vector3, W:float | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.data.utils.JoinLists` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Lists:List/StructuredList multi; out Length:int, Result:StructuredList | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.data.utils.SelectBoolFromFloatDict` | A small helper that converts a float value from a dict to a boolean. | in DictionaryInput:Dict<float>, Select:string; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.data.utils.SelectFloatFromDict` | Parses a dictionary and converts a single value to a float. | in DictionaryInput:Dict<float>, Select:string; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.data.utils.SelectVec2FromDict` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in DictionaryInput:Dict<float>, SelectX:string; out Result:Vector2 | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.data.utils.SelectVec3FromDict` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in DictionaryInput:Dict<float>; out Result:Vector3 | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.float.adjust

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.adjust.Abs` | The absolute value of a number. | in Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Ceil` | Rounds to the next highest integer value. Also see [Round] & [Floor] | in Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Clamp` | Clamps an input float between two values. Can be used to find minimum or maximum values. AKA: Min, Max Tips: Also con... | in Max:float, Min:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Floor` | Rounds to the next lower integer value. Also see [Round] & [Ceil] | in Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.InvertFloat` | Negates a float value. This is the same as multiplying by -1. | in A:float, Invert:bool; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Remap` | Remaps a value range to a new value range. Optionally applies bias, clamping, or modulo. | in BiasAndGain:Vector2, Mode:int, RangeInMax:float, RangeInMin:float, RangeOutMax:float, RangeOutMin:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Round` | Advanced version of rounding. See https://www.desmos.com/calculator/rojg8taxot for more additional details. Also see ... | in RoundRatio:float, StepsPerUnit:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.adjust.Sigmoid` | Returns a sigmoid function 1/(1+e^x-1). It's very similar to [SmoothStep] but never reaches the -1 and 1 boundaries. | in Stretch:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.float.basic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.basic.Add` | Adds two float values and outputs the result as a float If more values are to be added, there is also [Sum] | in Input2:float; out Input1:float, Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Div` | Divides two float values and outputs the quotient / result as float Also see: [Multiply] [Sum] etc. | in A:float, B:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Log` | Calculates the result of a logarithmic function In C#: Math.Log(x, y) | in Base:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Modulo` | Modulo for floats. | in ModuloValue:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Multiply` | Multiplies two floats. Also see [Div] [Sum] etc. | in A:float, B:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Pow` | Calculates the result of an exponential function In C#: Math.Pow(x, y) | in Exponent:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Sqrt` | Computes the square root of a float value. TIP: You can use the [Pow] operator to compute roots for other bases by us... | in Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Sub` | Subtracts two float values. | in Input1:float, Input2:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.basic.Sum` | Allows connecting any amount of floats whose sum is output Also see [Add] | in InputValues:List/float multi; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.float.logic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.logic.Compare` | Compares two float values, outputting a boolean. AKA: greater, less, equal Tips: - If you want to output the min or m... | in Mode:int, Precision:float, TestValue:float, Value:float; out IsTrue:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.logic.HasValueChanged` | Tests whether the incoming value has changed by a defined threshold and outputs this as a Boolean Similar to [HasValu... | in MinTimeBetweenHits:float, Mode:int, PreventContinuedChanges:bool, Threshold:float, Value:float; out Delta:float, DeltaOnHit:float, HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.logic.HasValueIncreased` | Tests whether the incoming value has increased by a defined threshold and outputs this as a Boolean Operator with mor... | in Threshold:float, Value:float; out HasIncreased:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.logic.IsGreater` | Tests whether the incoming value is higher than the threshold and outputs the result as a Boolean Operator with more ... | in Threshold:float, Value:float; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.logic.PickFloat` | Picks a value from the connected float inputs. | in FloatValues:List/float multi, Index:int; out Selected:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.logic.TryParse` | Tries to parse a string to a float number; if failing, the default value is used instead. | in Default:float, String:string; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.logic.ValueToRate` | Converts a value to a rate. Can also be used with [MockStrings] to select between various other groups of numbers suc... | in Rates:string, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.float.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.process.Accumulator` | Accumulates a value with the incoming rate. Note that the increment is a rate that gets multiplied with the current f... | in Accumulate:int, Increment:float, Modulo:float, ResetTrigger:bool, Running:bool, StartValue:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.BlendValues` | Blends a number of incoming values with a float value. | in F:float, Values:List/float multi; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.process.Damp` | Damps (i.e. smoothens or filters) an incoming float value. See also: [DampVec2], [DampVec3] and [DampFloatList]. | in Damping:float, Method:int, UseAppRunTime:bool, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.DampAngle` | Damps (i.e. smoothens or filters) an incoming float value. Avoids flips when jumping from 359 to 0 degrees. Other dam... | in Damping:float, Method:int, UseAppRunTime:bool, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.DetectPulse` | Detects if rapid changes within an input signal exceed a threshold. This can be useful for detecting beats in OSC or ... | in Damping:float, MinTimeBetweenHits:float, Threshold:float, Value:float; out DebugValue:float, HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.Ease` | Note: The input value should evolve by steps (e.g., sudden changes) to fully utilize the easing effect. This operator... | in Direction:int, Duration:float, Interpolation:int, UseAppRunTime:bool, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.EaseKeys` | This operator creates smooth, ease-in-out transitions by overriding the easing of keyframes. Customize the transition... | in Direction:int, Interpolation:int, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.FreezeValue` | Keeps the current input value as long as Keep is true. This can be useful for building interactive features like stop... | in Freeze:bool, Mode:int, Value:float; out DeltaSinceFreeze:float, Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.HasValueDecreased` | Tests whether the incoming value has decreased by a defined threshold and outputs this as a Boolean Operator with mor... | in Threshold:float, Value:float; out HasDecreased:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.Lerp` | Blends between two values (lerp). Also see [BlendValues] to blend between more values and [Remap] to blend between tw... | in A:float, B:float, Clamp:bool, F:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.process.PeakLevel` | Analyzes the incoming value changes and outputs information about the peaks Output: 1. attack level (float): Attack l... | in MinTimeBetweenPeaks:float, Threshold:float, Value:float; out AttackLevel:float, FoundPeak:bool, MovingSum:float, TimeSincePeak:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.process.RemapValues` | Takes in a list of Vector2 and picks the y component of the pair where X is closest to LookupValue. | in InputAndOutputPairs:List/Vector2 multi, InputValue:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.process.SmoothStep` | Smoothes an incoming animated value For an alternative with more options see [Damp] | in Max:float, Min:float, Value:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.process.Spring` | Adds a bounce effect to the incoming value. Also known as jump, bounce, wobble, rubber, flex A similar op: [Damp] [Sp... | in Strength:float, Tension:float, UseAppRunTime:bool, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.float.random

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.random.FloatHash` | Generates a psydorandom integer from a float value | in Seed:float, UniqueForChild:bool; out Result:int | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.random.PerlinNoise` | Generates a continuously and randomly changing value in the predefined range based on Perlin Noise / Gradient Noise. ... | in Amplitude:float, BiasAndGain:Vector2, Frequency:float, Octaves:int, OverrideTime:float, Phase:float, RangeMax:float, RangeMin:float, ...; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.float.random.Random` | Generates a static random value based on a seed in the predefined range. | in Max:float, Min:float, Seed:int, UniqueForChild:bool; out Result:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.float.trigonometry

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.float.trigonometry.Atan2` | Returns the arctangent of a vector. | in Vector:Vector2; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.trigonometry.Cos` | Creates a cosine wave also known as cosinusoidal wave or cosinusoid. Useful combinations: [Time] as an input Alternat... | in Input:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.float.trigonometry.Sin` | Creates a sine wave also known as sinusoidal wave or sinusoid. Useful combinations: [Time] and an input Alternative: ... | in Amplitude:float, Input:float, Offset:float, Period:float, Phase:float; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.floats.basic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.floats.basic.ColorsToList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Colors:List/Vector4 multi; out Result:List<Vector4> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.basic.FloatListLength` | Needs a [FloatsToList] Operator connected. The amount of [Value]s connected to this Operator is counted and sent out ... | in Input:List<float>; out Length:int | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.basic.FloatsToList` | Combines connected float values into a List. | in Input:List/float multi; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.basic.SetFloatListValue` | Manipulate a value in a float list | in FloatList:List<float>, Index:int, Mode:int, TriggerSet:bool, Value:float; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.floats.conversion

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.floats.conversion.ComposeVec3FromList` | Composes a Vector3 by selecting floats from a list. This can be useful for converting data from OSC messages into pos... | in IndexForX:int, IndexForY:int, IndexForZ:int, Input:List<float>, InputRange:Vector2, OutputRange:Vector2, SpringDamping:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.conversion.FloatListToIntList` | Converts a list of floats to a list of integers. Each float in the input list is converted to an integer. The fractio... | in FloatList:List<float>; out Result:List<int> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.conversion.IntListToFloatList` | Converts a list of integers to a list of floats. Each integer in the input list is cast to its floating-point equival... | in IntList:List<int>; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.floats.io

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.floats.io.PlaybackFFT` | Returns the bass audio FFT buffer during soundtrack playback. Check [AudioFrequencies] if you're using an external au... | in InputBand:int; out Result:List<float> | C | needs image/buffer/renderer or TiXL data model design |

### Lib.numbers.floats.logic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.floats.logic.PickFloatFromList` | Picks a float value from the connected float list. Also see [PickFloat]. | in Index:int, Input:List<float>; out Selected:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.logic.PickFloatList` | Picks a FloatList from the connected list inputs. | in Index:int, Input:List/List<float> multi; out Selected:List<float> | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.floats.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.floats.process.AmplifyValues` | A helper that smooths or amplifies a value list over time. This can be used to extract beats in an audio spectrum. | in Input:List<float>, MixAboveAverage:float, MixAverage:float, MixCurrent:float, Smoothing:float; out Output:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.AnalyzeFloatList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Input:List<float>; out AllValid:bool, AverageMean:float, Max:float, Min:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.ColorListToInts` | Converts a color list into to a list of integer values. This can beu useful for sending them with [ArtNetOutput] | in ColorLists:List/List<Vector4> multi, OutputMode:int; out Result:List<int> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.CombineFloatLists` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in InputLists:List/List<float> multi; out Selected:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.CompareFloatLists` | Compares two float lists and returns the difference as normalized value. | in ListA:List<float>, ListB:List<float>, Threshold:float; out Difference:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.DampFloatList` | Damps (i.e., smoothens or filters) every value in an incoming float list. Other damping functions: [Damp], [DampAngle... | in Damping:float, Method:int, UseAppRunTime:bool, Values:List<float>; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.DampPeakDecay` | Compares the current input value to the current damped value. If the input value is great, it is immediately used and... | in Decay:float, Value:float; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.DefineIqGradient` | A simple procedural method to define a gradient established by Inigo Quelez. It's fun for playing around but takes so... | in A_Brightness:Vector4, B_Contrast:Vector4, C_Frequency:Vector4, D_Phase:Vector4, Interpolation:int, NumOfSteps:int, Phase:float; out Gradient:Gradient | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.DeltaSinceLastFrame` | Returns the change of the value during the last frame (or a given smoothing average). Please note that this time is n... | in Threshold:float, Value:float; out Change:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.KeepFloatValues` | Builds a list of float values (e.g., a cycle buffer) but collects float values at the beginning of the list. | in AddValueToList:bool, BufferLength:int, DefaultValue:float, Reset:bool, Value:float; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.MergeFloatLists` | Merges multiple lists of floats into a single list. This operator combines several float lists into one. It's particu... | in Enabled:bool, InputLists:List/List<float> multi, MergeMode:int, StartIndices:List<int>; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.RemapFloatList` | Remaps a list of float values from an input range to an output range. This operator is useful for scaling or normaliz... | in BiasAndGain:Vector2, FloatList:List<float>, Mode:int, RangeInMax:float, RangeInMin:float, RangeOutMax:float, RangeOutMin:float; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.SmoothValues` | Smooths a list of values by applying a smoothing window. | in Input:List<float>, WindowSize:int; out Result:List<float> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.SumRange` | Takes a <FloatList> and sums up the values of the given index range limits. This can be useful for beat detection on ... | in Input:List<float>, LowerLimit:int, UpperLimit:int; out Selected:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.floats.process.ValuesToTexture` | Converts a list of values into pixels of a texture in real time. See the example of how this can be used to create au... | in Direction:int, Gain:float, Offset:float, Pow:float, Values:List/List<float> multi; out ValuesTexture:Texture2D | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.floats.process.ValuesToTexture2` | An upgrade version to convert a list of float value lists to a texture. | in Clamp:bool, Direction:int, GainAndBias:Vector2, InputRange:Vector2, OutputRange:Vector2, Values:List/List<float> multi; out ValuesTexture:Texture2D | C | needs image/buffer/renderer or TiXL data model design |

### Lib.numbers.int.basic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.int.basic.AddInts` | Adds two integers Same as [IntAdd] | in Input2:int; out Input1:int, Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.IntAdd` | Adds two integers Same as [AddInts] | in Value1:int, Value2:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.IntDiv` | Divides Integer A (Numerator) by Integer B (Denominator) | in Denominator:int, Numerator:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.IntToFloat` | Converts an integer to a Float | in IntValue:int; out Result:float | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.ModInt` | Modulo for integers. | in Mod:int, Value:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.MultiplyInt` | Multiplies two Integers | in A:int, B:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.MultiplyInts` | Multiplies all connected integer values. This can be useful to calculate the capacity or volume defined by an arbitra... | in InputValues:List/int multi; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.RandomChoiceIndex` | Returns a random index for a given seed that can be used to pick from a list or scene. The index will remain in the g... | in Mod:int, Value:int; out Result:int | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.int.basic.SubInts` | Subtracts int B from int A. | in Input1:int, Input2:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.basic.SumInts` | Adds the connected integer values | in InputValues:List/int multi; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.int.logic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.int.logic.CompareInt` | Compares two integers and gives out the result as a Boolean and/or an Integer | in Mode:int, ResultForFalse:int, ResultForTrue:int, TestValue:int, Value:int; out IsTrue:bool, ResultValue:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.logic.CountInt` | A helper operator that counts evaluations as an integer. | in DefaultValue:int, Delta:int, Modulo:int, OnlyCountChanges:bool, TriggerDecrement:bool, TriggerIncrement:bool, TriggerReset:bool; out Result:int | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.int.logic.HasIntChanged` | Checks if the change of the incoming integer exceeded the given threshold. | in ReturnTrueIf:int, Value:int; out HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.int.logic.IsIntEven` | Checks whether the incoming or defined Integer is Odd or Even and gives out a bool accordingly Can be combined with [... | in Value:int; out Result:bool | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.logic.PickInt` | Picks a value from the connected Int inputs. | in Index:int, InputValues:List/int multi; out Selected:int | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.int.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.int.process.ClampInt` | Bounds the defined or incoming integer to the predefined upper and lower limit | in Max:int, Min:int, Value:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.process.FloatToInt` | Converts a float value to an integer. This can be useful for using values for indices or random seeds. Note: positive... | in FloatValue:float; out Integer:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.process.GetAPrime` | Computes a prime number Useful with [IntToString] [IntToFloat] | in Index:int; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.process.IntListToBuffer` | Converts a list of integer values to a structured GPU buffer. | in -; out Result:BufferWithViews | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.int.process.IntsToBuffer` | Combines / converts any connected integer into a buffer | in Params:List/int multi; out Result:Buffer | C | needs image/buffer/renderer or TiXL data model design |
| `Lib.numbers.int.process.KeepInts` | Keeps integer values in a list | in AddValueToList:bool, BufferLength:int, Reset:bool, Value:int; out Result:List<int> | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.int.process.MaxInt` | Outputs the maximum integer of the connected inputs. To find a maximum float value, use [Clamp]. | in Ints:List/int multi; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.process.MinInt` | Outputs the minimum integer of the connected inputs. To find a minimum float value, use [Clamp]. | in Ints:List/int multi; out Result:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int.process.TryParseInt` | Tries to parse a string to an integer; if failing, the default value is used instead. | in Default:int, String:string; out Result:int | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.int2.basic

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.int2.basic.AddInt2` | Adds two int2 | in Input1:Int2, Input2:Int2; out Result:Int2 | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.int2.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.int2.process.Int2Components` | Converts an int2 to 4 single integers For the opposite see [Int2] | in Resolution:Int2; out AspectRatio:float, Height:int, Length:int, Width:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int2.process.MakeResolution` | Creates an int2 from two single ints Technically the same as [int2] with different naming | in Height:Int32, Width:Int32; out Size:T3.Core.DataTypes.Vector.Int2 | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int2.process.MaxInt2` | Returns the larger of two integer values (also as an int2) Example: If connected to 4 and 7 it will return 7 | in Sizes:List/Int2 multi; out MaxSize:Int2 | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int2.process.ScaleResolution` | Non-uniformly scales/multiplies an int2 which is used for defining resolutions For a similar operator with fewer sett... | in ClampToValidTextureSize:bool, Resolution:Int2; out Size:Int2 | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.int2.process.ScaleSize` | Uniformly scales/multiplies an int2 which is used for defining resolutions For a similar operator with more settings ... | in InputSize:Int2, Scale:float, Stretch:Vector2; out Result:Int2 | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.ints

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.ints.IntListLength` | Returns the number of elements in the connected int list. | in Input:List<int>; out Length:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.ints.IntsToList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Input:List/int multi; out Result:List<int> | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.ints.MergeIntLists` | Merges multiple lists of integers into a single list. This operator combines several integer lists into one. It's par... | in Enabled:bool, InputLists:List/List<int> multi, MergeMode:int, StartIndices:List<int>; out Result:List<int> | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.ints.PickIntFromList` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Index:int, Input:List<int>; out Selected:int | A | pure scalar/int/bool value transform with direct Vuo type shape |
| `Lib.numbers.ints.SetIntListValue` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Index:int, IntList:List<int>, Mode:int, TriggerSet:bool, Value:int; out Result:List<int> | A | pure scalar/int/bool value transform with direct Vuo type shape |

### Lib.numbers.vec2

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.vec2.AddVec2` | Adds two Vec2 variables together | in Input1:Vector2, Input2:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.DampVec2` | Smooths the incoming Vector2. | in Damping:float, Method:int, Value:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.DivideVector2` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in A:Vector2, B:Vector2, UniformScale:float; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.DotVec2` | Applies a dot product to two Vec2's | in Input1:Vector2, Input2:Vector2; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.GridPosition` | Converts an index to a grid raster position and sizes that can be used to draw a [Layer2d]. This can be helpful for p... | in A:Vector2, Index:int, RasterSize:Int2; out Position:Vector2, Size:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.HasVec2Changed` | Checks if the change of the incoming vector exceeded the given threshold. | in MinTimeBetweenHits:float, PreventContinuedChanges:bool, Threshold:float, Value:Vector2; out Delta:Vector2, HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.Int2ToVector2` | Converts an [Int2] into a [Vector2] To do the opposite two [FloatToInt] can be used. | in Int2:Int2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.PadVec2Range` | Pad (aka extend) a value range (x=min and y=max). This can be useful for adding "space" for value outputs. | in A:Vector2, ClampMinExtend:float, GuaranteedRange:Vector2, UniformScale:float; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.PerlinNoise2` | Generates a continuously and randomly changing value in the predefined range based on Perlin Noise / Gradient Noise. ... | in Amplitude:float, AmplitudeXY:Vector2, BiasAndGain:Vector2, Frequency:float, Octaves:int, Offset:Vector2, OverrideTime:float, Phase:float, ...; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.PickVector2` | Selects one Vector2 of many, like a switch. | in Index:int, Input:List/Vector2 multi; out Selected:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.RemapVec2` | Remaps an incoming Vec2 from one value range to another. This can be useful for conforming vectors to a range or for ... | in Mode:int, RangeInMax:Vector2, RangeInMin:Vector2, RangeOutMax:Vector2, RangeOutMin:Vector2, Value:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.ScaleVector2` | Multiplies two incoming [Vector2]s with each other | in A:Vector2, B:Vector2, UniformScale:float; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.Vec2ToVec3` | Converts a vec2 and a single to a vec3 | in XY:Vector2, Z:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.Vector2Components` | Converts a Vector2 into two individual values Opposite of [Vector2] | in Value:Vector2; out X:float, Y:float | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.vec2.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.vec2.process.EaseVec2` | [Ease] [SpringVec2] [DampVec2] | in Direction:int, Duration:float, Interpolation:int, UseAppRunTime:bool, Value:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.process.EaseVec2Keys` | [EaseKeys] [EaseVec3Keys] | in Direction:int, Interpolation:int, Value:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec2.process.SpringVec2` | This is the vector 2 version of [Spring] Example: [SpringVec3Example] Similar op: [SpringVec3] | in Strength:float, Tension:float, UseAppRunTime:bool, Value:Vector2; out Result:Vector2 | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.vec3

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.vec3.AddVec3` | Adds two Vec3 variables together | in Input2:Vector3; out Input1:Vector3, Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.BlendVector3` | Blends (lerps) two vectors. Also see [LerpVec3] | in F:float, Vectors:List/Vector3 multi; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.CrossVec3` | Applies a cross product to two Vec3's | in Input1:Vector3, Input2:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.DampVec3` | Damps (i.e. smoothens or filters) an incoming float value. Other damping functions: [Damp], [DampAngle], [DampVec2] a... | in Damping:float, Method:int, Value:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.DotVec3` | Applies a dot product to two Vec3's | in Input1:Vector3, Input2:Vector3; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.EulerToAxisAngle` | Converts 3 Euler angles (heading, pitch, yaw) into an axis and angle representation. | in Rotation:Vector3; out Angle:float, Axis:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.HasVec3Changed` | Checks if the change of the incoming vector exceeded the given threshold. | in MinTimeBetweenHits:float, Mode:int, PreventContinuedChanges:bool, Threshold:float, Value:Vector3; out Delta:Vector3, DeltaOnHit:Vector3, HasChanged:bool | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.LerpVec3` | Blends between two values (Lerp). Also see: - [BlendVector3] to blend between more Vector3s. | in A:Vector3, B:Vector3, Clamp:bool, F:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.Magnitude` | Returns the length of a Vector3. | in Input:Vector3; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.MulMatrix` | Multiplies two 4x4 matrices | in MatrixA:Matrix4x4, MatrixB:Matrix4x4; out Result:Matrix4x4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.NormalizeVector3` | Normalizes a Vec3 | in A:Vector3, Factor:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.PerlinNoise3` | Generates a continuously and randomly changing value in the predefined range based on Perlin Noise/Gradient Noise. Us... | in Amplitude:float, AmplitudeXYZ:Vector3, BiasAndGain:Vector2, Frequency:float, Octaves:int, Offset:Vector3, OverrideTime:float, Phase:float, ...; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.PickVector3` | Selects one Vector3 from many, like a switch. | in Index:int, Input:List/Vector3 multi; out Selected:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.RotateVector3` | Rotates a vector around an axis. | in Angle:float, Axis:Vector3, Scale:float, VectorA:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.RoundVec3` | Rounds the incoming Vec3 value by the given precision and method. This can be useful for snapping positions onto a grid. | in Mode:int, Precision:Vector3, Value:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.ScaleVector3` | Defines a value that is multiplied onto the incoming Vec3 | in A:Vector3, B:Vector3, ScaleUniform:float; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.SubVec3` | Subtracts two vectors | in Input1:Vector3, Input2:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.TransformVec3` | Transform Vector3 with the given transform matrix. | in A:Vector3, Matrix:Matrix4x4; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.Vec2Magnitude` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in Input:Vector2; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.Vec3Distance` | Computes the distance between two vectors. (I.e., subtract two positions) | in Input1:Vector3, Input2:Vector3; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.Vector3Components` | Converts a Vector3 into three individual values Opposite of [Vector3] | in Value:Vector3; out X:float, Y:float, Z:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.Vector3Gizmo` | Creates a 3D Gizmo in the Output View (if gizmos are toggled on) and sends out the position data as a [Vector3]. Usef... | in Position:Vector3, ShowGizmo:bool; out Result:Vector3 | D | obsolete/app graph/playback side effect or TiXL-only dependency |

### Lib.numbers.vec3.process

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.vec3.process.EaseVec3` | [Ease] [SpringVec3] [DampVec3] | in Direction:int, Duration:float, Interpolation:int, UseAppRunTime:bool, Value:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.process.EaseVec3Keys` | [EaseKeys] [EaseVec2Keys] | in Direction:int, Interpolation:int, Value:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec3.process.SpringVec3` | This is the vector 3 version of [Spring] Similar op: [SpringVec2] [DampVec3] | in Strength:float, Tension:float, UseAppRunTime:bool, Value:Vector3; out Result:Vector3 | B | portable but needs state/list/vector/color/custom-type care |

### Lib.numbers.vec4

| full_path | purpose | ports | grade | reason |
|---|---|---|---|---|
| `Lib.numbers.vec4.DotVec4` | Applies a dot product to two Vec4's | in Input1:Vector4, Input2:Vector4; out Result:float | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec4.PickColor` | Selects and switches between any of the connected colors Useful combinations: [AColor], [SampleGradient] | in Index:int, Input:List/Vector4 multi; out Selected:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec4.RgbaToColor` | Creates a Color from 4 values (RGBA) Similar to [AColor] and [PickColor] and the same as [Vector4] Alternative with m... | in A:float, B:float, G:float, R:float; out Result:Vector4 | B | portable but needs state/list/vector/color/custom-type care |
| `Lib.numbers.vec4.Vector4Components` | Converts a Vector4 (or RGBA Color) into three individual values. Opposite of [RgbaToColor] | in Value:Vector4; out W:float, X:float, Y:float, Z:float | B | portable but needs state/list/vector/color/custom-type care |

## Largest Risks

- TiXL `Gradient`, `Curve`, `Int2`, `Command`, `Texture2D`, `Buffer`, and playback-time context do not have one-to-one Vuo mappings in the allowed evidence.
- `SmoothStep` is actually `MathUtils.SmootherStep`; `Sin`/`Cos` use radians while Vuo math trig nodes use degrees.
- Stateful nodes (`Damp`, `Spring`, `Ease`, `Accumulator`, change/trigger/keep/delay nodes) need Vuo instance state and explicit event/time fixtures; static formula tests are insufficient.
