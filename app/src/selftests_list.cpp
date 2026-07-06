// app/src/selftests_list.cpp — area manifest leaf for the --selftest router: float/color list + gradient + string rail + point list
//
// Shell-tier (app/src/ root, like selftests.cpp): may name selftest fns from any zone via
// selftests_decls.h. Self-registers its rows into selftestRegistry() during pre-main dynamic init;
// selftests.cpp reads that sink. Adding a selftest to this area = add ONE row below — selftests.cpp
// is never touched. ORDER_BASE is the global index of the first row (keeps --selftest-list identical
// to the pre-split kTable order; see selftest_registry.h). Rows kept verbatim from the old kTable.
#include "runtime/selftest_registry.h"
#include "selftests_decls.h"

namespace sw {
REGISTER_SELFTESTS(/*orderBase=*/72,
    {"floatlist", runFloatListSelfTest},
    {"colorstolist", runColorsToListSelfTest},
    {"colorlist", runColorListSelfTest},
    {"combinecolorlists", runCombineColorListsSelfTest},
    {"readpointcolors", runReadPointColorsSelfTest},
    {"keepcolors", runKeepColorsSelfTest},
    {"stringrail", runStringRailSelfTest},
    {"listrouting", runListRoutingSelfTest},
    {"listroutingwave1", runListRoutingWave1SelfTest},
    {"pointlist", runPointListSelfTest},
    {"pointstocpu", runPointsToCpuSelfTest},
    {"gradient", runGradientSelfTest},
    {"readpixel", runReadPixelSelfTest},
    {"pickgradient", runPickGradientSelfTest},
    {"blendgradients", runBlendGradientsSelfTest},
);
// New stateful-string-rail rows go in their OWN block with a high orderBase so they APPEND at the end of
// --selftest-list deterministically (the 72-block is contiguous 72..83 with no room to insert mid-block;
// the registry sorts by `order`, so 300 lands after every existing row without renumbering anything).
REGISTER_SELFTESTS(/*orderBase=*/300,
    {"hasstringchanged", runHasStringChangedSelfTest},  // per-node cross-frame STRING state (HasStringChanged)
    {"stringctxvar", runStringCtxVarSelfTest},          // String ctx-var seam (sub-seam C): Set/GetStringVar on typed stringVars + writer-first 2-pass + per-frame clear
    {"matrixctxvar", runMatrixCtxVarSelfTest},          // Matrix ctx-var seam (sub-seam D): Set/GetMatrixVar on typed matrixVars (4-row on extColorOut) + writer-first 2-pass
);
// Wave-2 FloatList→FloatList producers (list fan-out). Own high-orderBase block so it appends at the end
// of --selftest-list deterministically (the registry sorts by `order`).
REGISTER_SELFTESTS(/*orderBase=*/310,
    {"floatlistproducers", runFloatListProducersSelfTest},  // Combine/IntsToList/SetFloat/SetInt/Remap (chain-through-evalFloat)
    {"smoothvalues", runSmoothValuesSelfTest},              // SmoothValues forward-window box average (STATELESS FloatList→FloatList, chain-through-evalFloat)
    {"animfloatlist", runAnimFloatListSelfTest},            // AnimFloatList animator PRODUCER (AnimMath shapes → List<float> on LocalFxTime; flat + production-resident chain-through)
    {"floatlistconversion", runFloatListConversionSelfTest},  // FloatListToIntList (trunc-toward-zero) + IntListToFloatList (widening), chain-through-evalFloat
    {"colorlisttoints", runColorListToIntsSelfTest},       // ColorListToInts COLORLIST→FLOATLIST BRIDGE (vec4-list → int-list per-channel 0..255 truncate+clamp; RGBA/ARGB/RGB/R/A modes), chain-through-evalFloat
    {"analyzefloatlist", runAnalyzeFloatListSelfTest},     // AnalyzeFloatList MULTI-OUTPUT host-scalar (Min/Max/AverageMean/AllValid off widened outCache[0..3]; the op that forced the 3→8 widen)
    {"amplifyvalues", runAmplifyValuesSelfTest},            // AmplifyValues cross-frame STATE (damp toward input over frames; flat + R-2 production-resident)
    {"dampfloatlist", runDampFloatListSelfTest},            // DampFloatList cross-frame STATE (per-index damp + dt-gate; flat + production-resident)
    {"keepfloatvalues", runKeepFloatValuesSelfTest},        // KeepFloatValues cross-frame STATE (front-insert ring accumulator; flat + production-resident)
    {"keepints", runKeepIntsSelfTest},                      // KeepInts INT twin cross-frame STATE (front-insert ring accumulator pad-0; flat + production-resident)
    {"playbackfft", runPlaybackFFTSelfTest},                // PlaybackFFT FloatList producer (spectrum snapshot → InputBand-selected array on the host list rail; 5 modes closed-form)
    {"spatialaudio", runSpatialAudioSelfTest},              // SpatialAudioPlayer gain spine (linear distance attenuation + effective volume + Euler→forward/up; BASS 3D field deferred-hw-verify)
);
// PointList host-rail LEAF ops (SampleCpuPoints / JoinLists). Own high-orderBase block so they append at
// the end of --selftest-list deterministically (the registry sorts by `order`; the 72-block is full).
REGISTER_SELFTESTS(/*orderBase=*/320,
    {"samplecpupoints", runSampleCpuPointsSelfTest},        // SampleCpuPoints: 2-key host list -> 1 Bezier+quaternion resampled point (pure CPU)
    {"joinlists", runJoinListsSelfTest},                    // JoinLists (Result-only): N host lists -> ONE concat in wire order (Length deferred)
);
// Atom-layer leaves (numbers/string): String->Float/Int parse host-scalars + FloatList aggregators. Own
// high-orderBase block so they append at the end of --selftest-list deterministically.
REGISTER_SELFTESTS(/*orderBase=*/330,
    {"tryparse", runTryParseSelfTest},                      // TryParse/TryParseInt: String -> Float/Int (parse-or-Default), flat + resident bridge
    {"stringtodatetime", runStringToDateTimeSelfTest},      // StringToDateTime: String -> DateTime route-B epoch (parse-or-0), flat + resident bridge
    {"stringtodatetime", runStringToDateTimeSelfTest},      // StringToDateTime: String -> DateTime route-B epoch (parse-or-0), flat + resident bridge
    {"mergelists", runMergeListsSelfTest},                  // MergeFloatLists/MergeIntLists (Append/Htp/Average) + PickFloatList, chain-through-evalFloat
    {"valuetorate", runValueToRateSelfTest},                // ValueToRate: String rate table + Value -> picked rate, flat + resident bridge
);
// Dict-currency seam: Dict<float> host rail + the 4 Select*FromDict consumers. Own high-orderBase block so
// it appends at the end of --selftest-list deterministically (the registry sorts by `order`).
REGISTER_SELFTESTS(/*orderBase=*/340,
    {"selectfromdict", runSelectFromDictSelfTest},          // Dict<float> currency + SelectFloat/Vec2/Vec3/BoolFromDict (flat + resident bridge)
);
// SwDataSet currency seam: the recorded-session value type + its 3 cores (LoadDataClip parse / MidiRecording
// ingest / SimulateIoData replay window). Own high-orderBase block so it appends at the end deterministically.
REGISTER_SELFTESTS(/*orderBase=*/800,
    {"dataset", runDataSetSelfTest},                        // SwDataSet: parse .data JSON / record MIDI / simulate (last,current] window + TimeRangeMapping
    {"midiclip", runMidiClipSelfTest},                      // MidiClip: minimal SMF reader (header+track+VLQ+events) → Dict<float> /channel<n>/<name>
    {"midiclipnode", runMidiClipNodeSelfTest},              // MidiClip NODE cook: Filename→.mid→cookFlatDict, bars→ticks via EvaluationContext.localFxTime
);
// pbr-lighting island value currencies (data-layer): SetPointLight/SetFog/SetMaterial value slices proven
// against the TiXL .cs. Context stacks + PBR-shader consumer are the BLOCKED render-pass seam. Own high-
// orderBase block so it appends at the end of --selftest-list deterministically.
REGISTER_SELFTESTS(/*orderBase=*/350,
    {"pointlight", runPointLightSelfTest},                  // SwPointLight currency vs SetPointLight.cs:17-21 (-bug swaps Intensity↔Range)
    {"fog", runFogSelfTest},                                // SwFogParameters currency vs SetFog.cs:20-25 + ambient-default P2 guard (-bug swaps Distance↔Bias)
    {"pbrmaterial", runPbrMaterialSelfTest},                // SwPbrParameters slice vs SetMaterial.cs:34-38 + fresh-default anchor (-bug swaps Roughness↔Specular)
);
}  // namespace sw
