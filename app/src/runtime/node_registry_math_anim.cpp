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
       nullptr,
       "io.audio"}
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
       nullptr,
       "numbers.anim.animators"}
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
       nullptr,
       "numbers.anim.animators"}
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
       nullptr,
       "numbers.anim.animators"}
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
       nullptr,
       "io.audio._"}
};

      // TiXL RequestedResolution (Lib/render/utils/RequestedResolution.cs) — value-output-rail Phase 1.
      // Emits the cook-context resolution (= TiXL EvaluationContext.RequestedResolution). NO inputs
      // (RequestedResolution.t3 Inputs == []). CONTEXT-reading, so no pure evaluate() (evaluate cannot
      // see ctx.RequestedResolution) — evaluate==nullptr, and cookValueOutputNodes (resident_value_output_cook)
      // writes the outputs onto extOut[] once per frame from ctx.requestedWidth/Height (the frame-level
      // window seed PointGraph::cook plants, point_graph.cpp:122). Mirror of DetectBpm/AudioReaction's
      // no-evaluate extOut readback, but cooked from ctx fields not from audio.
      // OUTPUT PORTS FIRST → output-port index == extOut index (the readback contract):
      //   [0] Size.Width  [1] Size.Height  [2] Width  [3] Height  [4] AspectRatio.
      // FORK fork-vec-output-as-n-scalar-ports: TiXL's Size is ONE Slot<Int2>; here it is 2 Float ports
      // (Size.Width / Size.Height). Faithful in VALUE, forked in wire-cardinality (extends the input-side
      // scalar-pack fork). Size.Width/Height duplicate Width/Height (the Int2 packing of the same pair).
      // No inputs → no .t3 default rows.
static const MathOp _reg_RequestedResolution{
      {"RequestedResolution", "RequestedResolution",
       {{"Size.Width",  "Size",        "Float", false},
        {"Size.Height", "Size.Height", "Float", false},
        {"Width",       "Width",       "Float", false},
        {"Height",      "Height",      "Float", false},
        {"AspectRatio", "AspectRatio", "Float", false}},
       nullptr,
       "render.utils"}
};

      // TiXL TransformMatrix (Lib/render/_/TransformMatrix.cs) — value-output-rail Phase 3 (MATRIX value).
      // Builds an SRT matrix (Scale·Rotation·Translation about Pivot, + shear, then HLSL-row Transpose) and
      // emits the 4 transposed ROWS (Row1..Row4) as a 4-element Vector4[] = TiXL Slot<Vector4[]> Result.
      // A 4×4 matrix is LITERALLY a 4-element Vector4 list, so the value rides the EXISTING extColorOut vec4
      // channel (resident_eval_graph.h) — NO new rail. PURE value op (no points), but CONTEXT-free + the
      // matrix is a LIST (not one float) → cannot ride NodeSpec::evaluate (returns ONE float). evaluate==null;
      // cookMatrixOutputNodes (resident_matrix_output_cook.cpp) resolves the SRT Float inputs and writes the
      // 4 rows onto extColorOut[outPortIdx] once per frame (mirror of cookColorListNodes / cookValueOutputNodes).
      // FORK fork-matrix-as-4-vec4-on-extColorOut: TiXL wires ONE Slot<Vector4[]>; sw emits the 4 float4 rows
      // onto the ColorList channel. Faithful in VALUE (byte-identical rows), forked in downstream wire-type.
      // FORK fork-vec3-as-3-floats / fork-vec4-as-4-floats: each Vector3/Vector4 input decomposes into N
      // consecutive Float ports (the established scalar-pack fork). Bool/enum carried on the float rail.
      // Defaults (TransformMatrix.t3): Scale=(1,1,1), UniformScale=1, RotationMode=0, Rotation_Quaternion=
      // (0,0,0,1), Translation/Pivot/Shear/Rotation_PitchYawRoll=0, Invert=false.
      // DEFERRED FORK fork-transformmatrix-resultinverted-matrix-outputs: TiXL's ResultInverted (Vector4[])
      // and Matrix (Slot<Matrix4x4>) outputs are omitted — only the primary Result row-list ships (named).
static const MathOp _reg_TransformMatrix{
      {"TransformMatrix", "TransformMatrix",
       {{"Result", "Result", "ColorList", false},  // the 4-row matrix (extColorOut channel)
        {"Translation.x", "Translation",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RotationMode", "RotationMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"PitchYawRoll", "Quaternion"}, true},
        {"Rotation_PitchYawRoll.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 3},
        {"Rotation_PitchYawRoll.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 1},
        {"Rotation_PitchYawRoll.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 1},
        {"Rotation_Quaternion.x", "RotationQuat",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"Rotation_Quaternion.y", "RotationQuat.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Rotation_Quaternion.z", "RotationQuat.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Rotation_Quaternion.w", "RotationQuat.w", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Scale.x", "Scale",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"Shear.x", "Shear",   "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 3},
        {"Shear.y", "Shear.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 1},
        {"Shear.z", "Shear.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, false, 1},
        {"Pivot.x", "Pivot",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1}},
        // fork-transformmatrix-invert: TiXL Invert(bool)→inverse(matrix). No 4x4 inverse on this rail
        // yet → port DROPPED rather than exposed-and-silently-ignored (avoid the silent-wrong-toggle).
       nullptr,
       "render._"}
};

      // TiXL PointToMatrix (Lib/point/helper/PointToMatrix.cs) — value-output-rail Phase 4 (MATRIX value).
      // Builds the objectToParentObject of points[0] (Position/Orientation/Scale → SRT, pivot=0, transposed)
      // and emits the 4 ROWS as a Vector4[] = Slot<Vector4[]> Matrix. SAME matrix-as-4-vec4-on-extColorOut
      // convention as TransformMatrix. NOW WIRED (Phase 4 lifted defer-pointtomatrix-needs-point-into-frame-
      // pass): its input is a POINT BUFFER (StructuredList<Point>); cookPointValueOutputNodes (resident_point_
      // value_output_cook.cpp) — the SEPARATE pass that runs AFTER pg.cookResident (when the point buffers
      // exist) — reads point[0] from the cooked Shared buffer host-side (PointAccessor, zero blit) and writes
      // the 4 rows onto extColorOut (the IDENTICAL math+channel as cookMatrixOutputNodes's TransformMatrix
      // path). evaluate==nullptr (the matrix is a LIST, not one float — can't ride NodeSpec::evaluate).
      // CamPointBuffer = a Points input (canvas pin, no Float decomposition). Empty/unwired → identity.
static const MathOp _reg_PointToMatrix{
      {"PointToMatrix", "PointToMatrix",
       {{"Matrix", "Matrix", "ColorList", false},     // the 4-row matrix (extColorOut channel)
        {"CamPointBuffer", "CamPointBuffer", "Points", true}},
       nullptr,
       "point.helper"},  // the point buffer (point-into-frame emit); category = TiXL Symbol.Namespace
};

      // TiXL GetPointDataFromList (Lib/numbers/data/utils/GetPointDataFromList.cs) — value-output-rail Phase 4.
      // Reads ONE point from a StructuredList<Point>: point[ItemIndex.Mod(N)] → Position(Vec3) / W(=point.F1,
      // float) / Orientation(Vec4). NO pure evaluate (it reads a point buffer the resident graph carries no
      // access to from a value pull) — evaluate==nullptr; cookPointValueOutputNodes (the SAME point-into-frame
      // pass as PointToMatrix, run AFTER pg.cookResident) indexes the cooked Shared buffer host-side and fans
      // the outputs onto extOut[0..7]. FORK fork-getpointdata-vec-as-scalar-ports: TiXL's Position(Vec3)/
      // W(float)/Orientation(Vec4) are 3 typed Slots; sw fans them onto the SCALAR extOut[] rail (8 slots =
      // 3+1+4 exactly) — the EXACT scalar-pack fork RequestedResolution uses. Faithful in VALUE, forked in
      // wire-cardinality. OUTPUT PORTS FIRST → output-port index == extOut index: [0..2]=Position.x/y/z,
      // [3]=W, [4..7]=Orientation.x/y/z/w. .t3 defaults (GetPointDataFromList.t3): ItemIndex=0, DataList=null.
      // index.Mod(N) = EUCLIDEAN modulo (MathUtils.cs:273-284, always non-negative). Empty/unwired → extOut 0.
static const MathOp _reg_GetPointDataFromList{
      {"GetPointDataFromList", "GetPointDataFromList",
       {{"Position.x", "Position",   "Float", false},
        {"Position.y", "Position.y", "Float", false},
        {"Position.z", "Position.z", "Float", false},
        {"W",          "W",          "Float", false},
        {"Orientation.x", "Orientation",   "Float", false},
        {"Orientation.y", "Orientation.y", "Float", false},
        {"Orientation.z", "Orientation.z", "Float", false},
        {"Orientation.w", "Orientation.w", "Float", false},
        {"DataList", "DataList", "Points", true},
        {"ItemIndex", "ItemIndex", "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider}},
       nullptr,
       "numbers.data.utils"}
};

      // TiXL SetBpm (Lib/numbers/anim/vj/SetBpm.cs) — the [SetBpm] VJ transport-BPM writer. On a
      // TriggerUpdate RISING edge (SetBpm.cs:22 MathUtils.WasTriggered — edge, not level) it hands a
      // clamped BpmRate to the triggered-pull BpmProvider singleton; frame_cook pulls it onto
      // g_transport.bpm (mirroring PlaybackUtils.cs:74-78). STATEFUL — per-instance edge memory
      // (s[0]=prevTrigger), evaluate==nullptr; cooked by frame_cook's stateful-value seam (step fn in
      // runtime/stateful_value_ops_setbpm.cpp). Output (Command in TiXL, no value) → out[0] echoes the
      // clamped rate this cook would write (golden probe; the real product is the singleton mutation).
      // Ports = TiXL InputSlot decl order MINUS the Command SubGraph (no Command sub-tree in the value
      // rail — NAMED FORK, same drop as SetFloatVar/SetIntVar). .t3 defaults (SetBpm.t3): BpmRate=120,
      // TriggerUpdate=false. BpmRate slider range = the operator's own clamp window [54,240] (cs:24).
static const MathOp _reg_SetBpm{
      {"SetBpm", "SetBpm",
       {{"Result", "Result", "Float", false},
        {"BpmRate", "BpmRate", "Float", true, 120.0f, 54.0f, 240.0f},
        {"TriggerUpdate", "TriggerUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.anim.vj"}
};

      // TiXL SetPlaybackTime (Lib/numbers/anim/time/SetPlaybackTime.cs) — the transport-PLAYHEAD writer.
      // On a rising Enabled edge (OnceEnabledGetsTrue) OR every frame while Enabled (Continuously) it
      // arms the PlaybackProvider with TimeInBars; frame_cook pulls it onto g_transport.position (scrub).
      // STATEFUL — per-instance edge memory (s[0]=prevEnabled), evaluate==nullptr; cooked by frame_cook's
      // stateful-value seam (step fn in runtime/stateful_value_ops_setplayback.cpp). Output (Command in
      // TiXL, no value) → out[0] echoes the bars this cook would write (golden probe). Ports = TiXL
      // InputSlot decl order MINUS the Command SubGraph and the telemetry-only ShowLogMessages (NAMED
      // FORK, same Command-drop as SetBpm). .t3 defaults (SetPlaybackTime.t3): TimeInBars=0,
      // TriggerMode=0 (OnceEnabledGetsTrue), Enabled=false.
static const MathOp _reg_SetPlaybackTime{
      {"SetPlaybackTime", "SetPlaybackTime",
       {{"Result", "Result", "Float", false},
        {"TimeInBars", "TimeInBars", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
        {"TriggerMode", "TriggerMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"OnceEnabledGetsTrue", "Continuously"}},
        {"Enabled", "Enabled", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.anim.time"}
};

      // TiXL SetPlaybackSpeed (Lib/numbers/anim/time/SetPlaybackSpeed.cs) — the transport-SPEED writer.
      // LEVEL-gated (TiXL's WasTriggered is COMMENTED OUT, cs:24) — every frame TriggerUpdate is true it
      // snap-adjusts SpeedFactor (near-1→1, small-positive→0.0001) and arms the PlaybackProvider;
      // frame_cook pulls it onto g_transport.rate (setRate gate). STATEFUL in the cook sense (no state
      // used; evaluate==nullptr; product = the provider arm). Output → out[0] echoes the snap-adjusted
      // speed (golden probe). Ports = decl order MINUS the Command SubGraph. .t3 defaults
      // (SetPlaybackSpeed.t3): SpeedFactor=1.0, TriggerUpdate=false.
static const MathOp _reg_SetPlaybackSpeed{
      {"SetPlaybackSpeed", "SetPlaybackSpeed",
       {{"Result", "Result", "Float", false},
        {"SpeedFactor", "SpeedFactor", "Float", true, 1.0f, -16.0f, 16.0f, Widget::Slider},
        {"TriggerUpdate", "TriggerUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.anim.time"}
};

}  // namespace
}  // namespace sw
