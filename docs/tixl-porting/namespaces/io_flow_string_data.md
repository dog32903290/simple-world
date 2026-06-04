# TiXL IO / Flow / String / Data Porting Research

Scope: all nodes whose namespace starts with `Lib.io`, `Lib.flow`, `Lib.string`, or `Lib.data`. Source pack: `external/tixl-spec/TIXL_CLONE_SPEC_20260604`; TiXL source: `external/tixl`; Vuo source: `external/vuo`. This is research only; no Vuo node code.

## Namespace Grade Summary

| namespace | nodes | A | B | C | D |
|---|---:|---:|---:|---:|---:|
| `Lib.data.object` | 1 | 0 | 1 | 0 | 0 |
| `Lib.flow` | 11 | 2 | 0 | 0 | 9 |
| `Lib.flow.context` | 18 | 0 | 0 | 0 | 18 |
| `Lib.flow.skillQuest` | 4 | 0 | 0 | 0 | 4 |
| `Lib.io.audio` | 5 | 0 | 0 | 1 | 4 |
| `Lib.io.audio._` | 4 | 0 | 0 | 2 | 2 |
| `Lib.io.audio._obsolete` | 2 | 0 | 0 | 1 | 1 |
| `Lib.io.data` | 2 | 0 | 0 | 2 | 0 |
| `Lib.io.dmx` | 6 | 0 | 0 | 0 | 6 |
| `Lib.io.dmx.helpers` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.dmx.obsolete` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.file` | 3 | 0 | 3 | 0 | 0 |
| `Lib.io.freed` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.http` | 1 | 0 | 0 | 0 | 1 |
| `Lib.io.input` | 4 | 0 | 0 | 0 | 4 |
| `Lib.io.json` | 2 | 0 | 2 | 0 | 0 |
| `Lib.io.midi` | 10 | 0 | 0 | 10 | 0 |
| `Lib.io.osc` | 2 | 0 | 0 | 2 | 0 |
| `Lib.io.posistage` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.ptz` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.serial` | 3 | 0 | 0 | 0 | 3 |
| `Lib.io.tcp` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.udp` | 2 | 0 | 0 | 0 | 2 |
| `Lib.io.video` | 13 | 0 | 0 | 3 | 10 |
| `Lib.io.video.mediapipe` | 7 | 0 | 0 | 0 | 7 |
| `Lib.io.websocket` | 2 | 0 | 0 | 0 | 2 |
| `Lib.string.buffers.convert` | 1 | 1 | 0 | 0 | 0 |
| `Lib.string.buffers.transform` | 1 | 1 | 0 | 0 | 0 |
| `Lib.string.combine` | 4 | 3 | 1 | 0 | 0 |
| `Lib.string.convert` | 3 | 3 | 0 | 0 | 0 |
| `Lib.string.datetime` | 6 | 6 | 0 | 0 | 0 |
| `Lib.string.list` | 6 | 0 | 6 | 0 | 0 |
| `Lib.string.logic` | 4 | 2 | 2 | 0 | 0 |
| `Lib.string.random` | 3 | 3 | 0 | 0 | 0 |
| `Lib.string.search` | 3 | 3 | 0 | 0 | 0 |
| `Lib.string.transform` | 2 | 2 | 0 | 0 | 0 |
| **Total** | **147** | **26** | **15** | **21** | **85** |

## Vuo Node Set Triage

- Vuo already has relevant node sets for audio, MIDI, OSC, video, serial, file/data/url, text, and list work: `external/vuo/node/vuo.audio`, `vuo.midi`, `vuo.osc`, `vuo.video`, `vuo.serial`, `vuo.file`, `vuo.data`, `vuo.url`, `vuo.text`, `vuo.list`.
- No direct generic TiXL-style HTTP server, TCP, UDP, or WebSocket node set was found in the checked `external/vuo/node` tree. Treat those as custom network/runtime lifecycle work.
- TiXL context variables and `Command` scene flow are app-state/runtime concepts. Vuo has event/data flow, trigger ports, event doors/walls, published ports, Hold/Share/Record style memory nodes, and layer/image/scene composition, but no direct `EvaluationContext` dictionary or TiXL `Command` object equivalent.
- Device/permission/realtime nodes: audio devices, MIDI, OSC/network, serial, keyboard/mouse/gamepad, video camera/capture/NDI/Spout/streaming, HTTP/TCP/UDP/WebSocket, DMX/Art-Net/sACN/PTZ/FreeD/PosiStage/MediaPipe.

## Compact Node Rows

| full_path | purpose | I/O summary | source evidence | grade | reason |
|---|---|---|---|---|---|
| `Lib.data.object.PickObject` | Pick on of the connected objects. This can be useful for switching between camera references. | in: Index:int, Input:object; out: Selected:object | C# `Operators/Lib/data/object/PickObject.cs`; .t3 `Operators/Lib/data/object/PickObject.t3`; docs `.help/docs/operators/lib/data/object/PickObject.md` | B | Generic object routing; Vuo generic data shape must be chosen. |
| `Lib.flow.BlendScenes` | Uses a float index to alpha blend between draw scenes. | in: BlendFraction:float, Scenes:Command; out: Output:Command | C# `Operators/Lib/flow/BlendScenes.cs`; .t3 `Operators/Lib/flow/BlendScenes.t3`; docs `.help/docs/operators/lib/flow/BlendScenes.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.ExecRepeatedly` | Controls how frequently the sub graph is being executed. This can be useful for increasing or slowing down simulation speeds. | in: Command:Command, RepeatCount:int, SkipFrameCount:int; out: Output:Command | C# `Operators/Lib/flow/ExecRepeatedly.cs`; .t3 `Operators/Lib/flow/ExecRepeatedly.t3`; docs `.help/docs/operators/lib/flow/ExecRepeatedly.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.Execute` | Performs a sequence of drawing operations and then restores the graphics context. You can use it for grouping or activating and deactivating parts of your graphs. Since [Execute] itself does nothing, you can also use it to name things by inserting it into the graph and setting the instance name by pressing Enter. An alternative to [Execute] is [Group], which can also have a translation and multiple input connections. | in: Command:Command, IsEnabled:bool; out: Output:Command | C# `Operators/Lib/flow/Execute.cs`; .t3 `Operators/Lib/flow/Execute.t3`; docs `.help/docs/operators/lib/flow/Execute.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.ExecuteOnce` | Executes the subgraph only once. This can be useful for initializing buffers or resetting states. This is obsolete. Please consider using [Execute] or [RenderTarget] with [Once] connected to IsEnabled. | in: Command:Command, Trigger:bool; out: Output:Command, OutputTrigger:bool | C# `Operators/Lib/flow/ExecuteOnce.cs`; .t3 `Operators/Lib/flow/ExecuteOnce.t3`; docs `.help/docs/operators/lib/flow/ExecuteOnce.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.LoadSoundtrack` | This operator has no function other than to point out that, unlike meshes, images and other assets, audio files are integrated via the "Playback Settings" menu. This menu can be opened by clicking on the gear icon in the control bar of the timeline. This gives the option to integrate a project soundtrack (for example as an mp3 file) as well as live audio devices. | in: Command:Command, IsEnabled:bool; out: Output:Command | C# `Operators/Lib/flow/LoadSoundtrack.cs`; .t3 `Operators/Lib/flow/LoadSoundtrack.t3`; docs `.help/docs/operators/lib/flow/LoadSoundtrack.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.LogMessage` | Prints a message to the console log. This can be useful for debugging updates, caching, and timing in complex graph networks. | in: LogLevel:int, Message:string, OnlyOnChanges:bool, SubGraph:Command; out: Output:Command | C# `Operators/Lib/flow/LogMessage.cs`; .t3 `Operators/Lib/flow/LogMessage.t3`; docs `.help/docs/operators/lib/flow/LogMessage.md` | A | Control/debug value behavior can map to Vuo event/data nodes with focused contract. |
| `Lib.flow.Loop` | Executes the sub graph multiple times. This can be very useful for iterating a drawing function. You can use [GetFloatVar] and [GetIntVar] to access the iterator variables. Please note that this operation creates draw calls for each iteration and thus is not meant for loop counts exceeding 1000 or more. Please check the example. | in: Command:Command, Count:int, IndexVariable:string, ProgressVariable:string; out: Output:Command | C# `Operators/Lib/flow/Loop.cs`; .t3 `Operators/Lib/flow/Loop.t3`; docs `.help/docs/operators/lib/flow/Loop.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.Once` | Briefly indicates whether the incoming Boolean value has changed. | in: Trigger:bool; out: OutputTrigger:bool | C# `Operators/Lib/flow/Once.cs`; .t3 `Operators/Lib/flow/Once.t3`; docs `.help/docs/operators/lib/flow/Once.md` | A | Control/debug value behavior can map to Vuo event/data nodes with focused contract. |
| `Lib.flow.ResetSubtreeTrigger` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: Command:Command, Trigger:bool; out: Output:Command | C# `Operators/Lib/flow/ResetSubtreeTrigger.cs`; .t3 `Operators/Lib/flow/ResetSubtreeTrigger.t3`; docs `.help/docs/operators/lib/flow/ResetSubtreeTrigger.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.Switch` | Switches between connected graphs. Can be used to "cut" between scenes. The index starts with 0 for the first input and will wrap on values exceeding the count of connected inputs. Tip: - use index -1 to activate none (i.e. disable all) - use index -2 to activate all The naming of this operator diverges from the Pick... convention like [PickColor], [PickImage] etc because it also allows executing "nothing" or all connected inputs. This is a consequence of command being the only "type" that doesn't return data but only executes connected commands. For a more visual approach see [TimeClip]. | in: Commands:Command, Index:int, OptimizeInvalidation:bool; out: Count:int, Output:Command | C# `Operators/Lib/flow/Switch.cs`; .t3 `Operators/Lib/flow/Switch.t3`; docs `.help/docs/operators/lib/flow/Switch.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.TimeClip` | Creates a time clip bar within the DopeView of the Timeline, similar to how video editing apps show clips. TimeClips can be moved by drag and drop and arranged next to and on top of each other (classic NLE non-linear editing). When the time marker is running over/playing the time clip it is activated and rendered. It can be helpful to give the time clip a suitable name in the name field (which is "Untitled Instance" by default). Also see [Switch] for a way to cut between scenes without the bars in the Timeline. To load an image sequence into the DopeView of the Timeline [ImageSequenceClip] can be used. [MidiClip] can be used to load linear Midi information into the scene. | in: Command:Command; out: Output:Command | C# `Operators/Lib/flow/TimeClip.cs`; .t3 `Operators/Lib/flow/TimeClip.t3`; docs `.help/docs/operators/lib/flow/TimeClip.md` | D | Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code. |
| `Lib.flow.context.ExecuteRawBufferUpdate` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: Buffer:Buffer, UpdateCommands:Command; out: Output:Buffer | C# `Operators/Lib/flow/context/ExecuteRawBufferUpdate.cs`; .t3 `Operators/Lib/flow/context/ExecuteRawBufferUpdate.t3`; docs `.help/docs/operators/lib/flow/context/ExecuteRawBufferUpdate.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetBoolVar` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: FallbackDefault:bool, VariableName:string; out: Result:bool | C# `Operators/Lib/flow/context/GetBoolVar.cs`; .t3 `Operators/Lib/flow/context/GetBoolVar.t3`; docs `.help/docs/operators/lib/flow/context/GetBoolVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetFloatVar` | Fetches a float variable from the context. Use [SetFloatVar] to set the variable further up the graph. Also [GetIntVar] Important: If you're using this together with [Loop], don't forget to use 'f' as the normalized progress variable. | in: FallbackDefault:float, VariableName:string; out: Result:float | C# `Operators/Lib/flow/context/GetFloatVar.cs`; .t3 `Operators/Lib/flow/context/GetFloatVar.t3`; docs `.help/docs/operators/lib/flow/context/GetFloatVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetForegroundColor` | Gets the current foreground color from the context. This can then be used to apply it to drawing operators, which can be affected by the primary drawing color. | in: none; out: Result:Vector4 | C# `Operators/Lib/flow/context/GetForegroundColor.cs`; .t3 `Operators/Lib/flow/context/GetForegroundColor.t3`; docs `.help/docs/operators/lib/flow/context/GetForegroundColor.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetIntVar` | Reads a variable from the context. Also see [GetFloatVar] | in: FallbackValue:int, LogUpdates:int, VariableName:string; out: Result:int | C# `Operators/Lib/flow/context/GetIntVar.cs`; .t3 `Operators/Lib/flow/context/GetIntVar.t3`; docs `.help/docs/operators/lib/flow/context/GetIntVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetMatrixVar` | Retrieves a matrix as an array for Vector4 from the evaluation context. This is used internally for passing transform matrices between render passes for things like rendering shadow maps. | in: none; out: VariableName:string | C# `Operators/Lib/flow/context/GetMatrixVar.cs`; .t3 `Operators/Lib/flow/context/GetMatrixVar.t3`; docs `.help/docs/operators/lib/flow/context/GetMatrixVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetObjectVar` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: FallbackDefault:object, VariableName:string; out: Result:Object | C# `Operators/Lib/flow/context/GetObjectVar.cs`; .t3 `Operators/Lib/flow/context/GetObjectVar.t3`; docs `.help/docs/operators/lib/flow/context/GetObjectVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetPosition` | Gets the current position, rotation, and scale in the world so it can be applied to other transforms. | in: PositionOffset:Vector3; out: Position:Vector3, Scale:Vector3, Space:int, UpdateCommand:Command | C# `Operators/Lib/flow/context/GetPosition.cs`; .t3 `Operators/Lib/flow/context/GetPosition.t3`; docs `.help/docs/operators/lib/flow/context/GetPosition.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetStringVar` | Fetches a float variable from the context. Use [SetStringVar] to set the variable further up the graph. Important: If you're using this together with [Loop], don't forget to use 's' as the normalized progress variable. Example: [SetContextVariableExample] | in: FallbackDefault:string, VariableName:string; out: Result:string | C# `Operators/Lib/flow/context/GetStringVar.cs`; .t3 `Operators/Lib/flow/context/GetStringVar.t3`; docs `.help/docs/operators/lib/flow/context/GetStringVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.GetVec3Var` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: FallbackDefault:Vector3, VariableName:string; out: Result:Vector3 | C# `Operators/Lib/flow/context/GetVec3Var.cs`; .t3 `Operators/Lib/flow/context/GetVec3Var.t3`; docs `.help/docs/operators/lib/flow/context/GetVec3Var.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetBoolVar` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: BoolValue:bool, SubGraph:Command, VariableName:string; out: Result:Command | C# `Operators/Lib/flow/context/SetBoolVar.cs`; .t3 `Operators/Lib/flow/context/SetBoolVar.t3`; docs `.help/docs/operators/lib/flow/context/SetBoolVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetFloatVar` | Writes a float value to the context's float variable dictionary. Use [ContextFloat] to read this value further down the graph. | in: ClearAfterExecution:bool, FloatValue:float, SubGraph:Command, VariableName:string; out: Output:Command | C# `Operators/Lib/flow/context/SetFloatVar.cs`; .t3 `Operators/Lib/flow/context/SetFloatVar.t3`; docs `.help/docs/operators/lib/flow/context/SetFloatVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetIntVar` | Sets or overwrites an int variable that can be retrieved by [GetIntVar] further down (left in) the graph. | in: ClearAfterExecution:bool, LogLevel:int, SubGraph:Command, Value:int, VariableName:string; out: Output:Command | C# `Operators/Lib/flow/context/SetIntVar.cs`; .t3 `Operators/Lib/flow/context/SetIntVar.t3`; docs `.help/docs/operators/lib/flow/context/SetIntVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetMatrixVar` | Stores a matrix as an array of vector4 on the evaluation context. This is used internally by passing transform matrices between render passes for things like rendering shadow maps. | in: ClearAfterExecution:bool, SubGraph:Command, VariableName:string; out: Output:Command | C# `Operators/Lib/flow/context/SetMatrixVar.cs`; .t3 `Operators/Lib/flow/context/SetMatrixVar.t3`; docs `.help/docs/operators/lib/flow/context/SetMatrixVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetObjectVar` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: ClearAfterExecution:bool, ObjectValue:Object, SubGraph:Command, VariableName:string; out: Output:Command | C# `Operators/Lib/flow/context/SetObjectVar.cs`; .t3 `Operators/Lib/flow/context/SetObjectVar.t3`; docs `.help/docs/operators/lib/flow/context/SetObjectVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetRequestedResolutionCmd` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: Resolution:Int2, ScaleResolution:float, StretchResolution:Vector2, Texture:Command; out: Result:Command | C# `Operators/Lib/flow/context/SetRequestedResolutionCmd.cs`; .t3 `Operators/Lib/flow/context/SetRequestedResolutionCmd.t3`; docs `.help/docs/operators/lib/flow/context/SetRequestedResolutionCmd.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetStringVar` | Writes a string value to the context's float variable dictionary. Use [GetStringVar] to read this value further down the graph. Example: [SetContextVariableExample] | in: ClearAfterExecution:bool, StringValue:string, SubGraph:Command, VariableName:string; out: Output:Command | C# `Operators/Lib/flow/context/SetStringVar.cs`; .t3 `Operators/Lib/flow/context/SetStringVar.t3`; docs `.help/docs/operators/lib/flow/context/SetStringVar.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.context.SetVec3Var` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: SubGraph:Command, VariableName:string, Vec3Value:Vector3; out: Result:Command | C# `Operators/Lib/flow/context/SetVec3Var.cs`; .t3 `Operators/Lib/flow/context/SetVec3Var.t3`; docs `.help/docs/operators/lib/flow/context/SetVec3Var.md` | D | TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary. |
| `Lib.flow.skillQuest.DrawQuiz` | Stacking | in: DifferenceRange:Vector2, DoNotChange:Command, YourSolution:Command; out: Output:Texture2D | C# `Operators/Lib/flow/skillQuest/DrawQuiz.cs`; .t3 `Operators/Lib/flow/skillQuest/DrawQuiz.t3`; docs `.help/docs/operators/lib/flow/skillquest/DrawQuiz.md` | D | Training/quiz/test harness node; not first-pass port target. |
| `Lib.flow.skillQuest.ImageQuiz` | Stacking | in: DifferenceRange:Vector2, DoNotChange:Texture2D, YourSolution:Texture2D; out: Output:Texture2D | C# `Operators/Lib/flow/skillQuest/ImageQuiz.cs`; .t3 `Operators/Lib/flow/skillQuest/ImageQuiz.t3`; docs `.help/docs/operators/lib/flow/skillquest/ImageQuiz.md` | D | Training/quiz/test harness node; not first-pass port target. |
| `Lib.flow.skillQuest.ValueQuiz` | Computes a visual comparison between a "solution" and a "goal" value animation. Connect your solution to the upper input. | in: DifferenceRange:Vector2, DisplayValueRange:Vector2, Goal:float, Yours:float; out: Output:Texture2D | C# `Operators/Lib/flow/skillQuest/ValueQuiz.cs`; .t3 `Operators/Lib/flow/skillQuest/ValueQuiz.t3`; docs `.help/docs/operators/lib/flow/skillquest/ValueQuiz.md` | D | Training/quiz/test harness node; not first-pass port target. |
| `Lib.flow.skillQuest._ReadBackImageDifference` | Unknown. | in: Differences:BufferWithViews; out: Result:int | C# `Operators/Lib/flow/skillQuest/_ReadBackImageDifference.cs`; .t3 `Operators/Lib/flow/skillQuest/_ReadBackImageDifference.t3`; docs `None` | D | Training/quiz/test harness node; not first-pass port target. |
| `Lib.io.audio.AudioPlayer` | An audio player using BASS audio library. Features include: - [Playback Control]: Independent play, pause, and stop functionality with seek support - [Stereo Panning]: Position audio in the stereo field from left to right - [ADSR Envelope]: Optional amplitude envelope for dynamic volume shaping - [Speed Control]: Playback speed multiplier (affects pitch) | in: AudioFile:string, Duration:float, Envelope:Vector4, Mute:bool, Panning:float, PauseAudio:bool, PlayAudio:bool, Seek:float, Speed:float, StopAudio:bool, TriggerMode:int, UseEnvelope:bool, Volume:float; out: GetLevel:float, IsPlaying:bool, Result:Command | C# `Operators/Lib/io/audio/AudioPlayer.cs`; .t3 `Operators/Lib/io/audio/AudioPlayer.t3`; docs `.help/docs/operators/lib/io/audio/AudioPlayer.md` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio.AudioReaction` | Creates audio reaction from audio inputs. | in: Amplitude:float, Bias:float, InputBand:int, MinTimeBetweenHits:float, Output:int, Reset:bool, Threshold:float, WindowCenter:float, WindowEdge:float, WindowWidth:float; out: HitCount:int, Level:float, WasHit:bool | C# `Operators/Lib/io/audio/AudioReaction.cs`; .t3 `Operators/Lib/io/audio/AudioReaction.t3`; docs `.help/docs/operators/lib/io/audio/AudioReaction.md` | C | Vuo has audio analysis, but TiXL analysis buffers/timing differ. |
| `Lib.io.audio.AudioToneGenerator` | Generates test tone direct to the operator mixer for audio testing and debugging. | in: Duration:float, Envelope:Vector4, Frequency:float, Mute:bool, Trigger:bool, TriggerMode:int, Volume:float, WaveformType:int; out: GetLevel:float, IsPlaying:bool, Result:Command | C# `Operators/Lib/io/audio/AudioToneGenerator.cs`; .t3 `Operators/Lib/io/audio/AudioToneGenerator.t3`; docs `.help/docs/operators/lib/io/audio/AudioToneGenerator.md` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio.SpatialAudioPlayer` | A 3D positional audio player using BASS audio library's 3D sound system. It simulates real-world audio by positioning sound sources in 3D space relative to a listener (camera). Features include: - [Distance Attenuation]: Sound volume decreases with distance between MinDistance (full volume) and MaxDistance (silent) - [Directional Sound Cones]: Define inner/outer cone angles to create directional audio sources (e.g., speakers, spotlights) - [Listener Orientation]: Audio panning based on listener position and rotation for realistic spatial perception - [3D Mode]: Normal (absolute positioning), Relative (source attached to listener for footsteps/breathing), or Off (mono playback) | in: Audio3DMode:int, AudioFile:string, InnerConeAngle:float, ListenerPosition:Vector3, ListenerRotation:Vector3, MaxDistance:float, MinDistance:float, Mute:bool, OuterConeAngle:float, OuterConeVolume:float, PauseAudio:bool, PlayAudio:bool, Seek:float, SourcePosition:Vector3, SourceRotation:Vector3, Speed:float, StopAudio:bool, Visibility:GizmoVisibility, Volume:float; out: GetLevel:float, IsPaused:bool, IsPlaying:bool, Result:Command | C# `Operators/Lib/io/audio/SpatialAudioPlayer.cs`; .t3 `Operators/Lib/io/audio/SpatialAudioPlayer.t3`; docs `.help/docs/operators/lib/io/audio/SpatialAudioPlayer.md` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio.SpatialAudioPlayerGizmo` | Debug visualization gizmo for spatial audio. Shows source/listener locators with direction indicators, min/max distance spheres, and directional cones | in: Color:Vector4, ConeLength:float, ContextKey:string, InnerConeAngle:float, ListenerPosition:Vector3, ListenerRotation:Vector3, MaxDistance:float, MinDistance:float, OuterConeAngle:float, SourcePosition:Vector3, SourceRotation:Vector3, Visibility:GizmoVisibility; out: Output:Command | C# `Operators/Lib/io/audio/SpatialAudioPlayerGizmo.cs`; .t3 `Operators/Lib/io/audio/SpatialAudioPlayerGizmo.t3`; docs `.help/docs/operators/lib/io/audio/SpatialAudioPlayerGizmo.md` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio._.AudioFrequencies` | Unknown. | in: Mode:int; out: FftBuffer:List<float> | C# `Operators/Lib/io/audio/_/AudioFrequencies.cs`; .t3 `Operators/Lib/io/audio/_/AudioFrequencies.t3`; docs `None` | C | Vuo has audio analysis, but TiXL analysis buffers/timing differ. |
| `Lib.io.audio._.AudioWaveform` | Unknown. | in: none; out: High:List<float>, Left:List<float>, Low:List<float>, Mid:List<float>, Right:List<float> | C# `Operators/Lib/io/audio/_/AudioWaveform.cs`; .t3 `Operators/Lib/io/audio/_/AudioWaveform.t3`; docs `None` | C | Vuo has audio analysis, but TiXL analysis buffers/timing differ. |
| `Lib.io.audio._.DataRecording` | Unknown. | in: ActiveDataSetId:string, ResetTrigger:bool; out: DataSet:DataSet | C# `Operators/Lib/io/audio/_/DataRecording.cs`; .t3 `Operators/Lib/io/audio/_/DataRecording.t3`; docs `None` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio._._SetAudioAnalysis` | Unknown. | in: AccumulatedHiHat:float, BeatCount:int, BeatSum:float, HiHatCount:int, TimeSinceBeat:float, TimeSinceHiHat:float; out: Output:Command | C# `Operators/Lib/io/audio/_/_SetAudioAnalysis.cs`; .t3 `Operators/Lib/io/audio/_/_SetAudioAnalysis.t3`; docs `None` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio._obsolete.SoundTrackLevels` | Unknown. | in: BeatTimeMode:int, FilePath:string, FlashDecay:float, MinTimeBetweenPeaks:float, Smooth:float, Threshold:float; out: BeatIndex:float, BeatTime:float, Level:float, Loudness:float | C# `Operators/Lib/io/audio/_obsolete/SoundTrackLevels.cs`; .t3 `Operators/Lib/io/audio/_obsolete/SoundTrackLevels.t3`; docs `None` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.audio._obsolete._LegacyAudioReaction` | Unknown. | in: Amplitude:float, Band:int, Decay:float, Mode:int, UseModulo:int; out: Level:float, PeakCount:int, PeakDetected:bool | C# `Operators/Lib/io/audio/_obsolete/_LegacyAudioReaction.cs`; .t3 `Operators/Lib/io/audio/_obsolete/_LegacyAudioReaction.t3`; docs `None` | C | Vuo has audio analysis, but TiXL analysis buffers/timing differ. |
| `Lib.io.data.LoadDataClip` | Unknown. | in: none; out: FilePath:string | C# `Operators/Lib/io/data/LoadDataClip.cs`; .t3 `Operators/Lib/io/data/LoadDataClip.t3`; docs `None` | C | Mixed IO/control behavior; needs Vuo-specific event/device design. |
| `Lib.io.data.SimulateIoData` | Unknown. | in: Enabled:bool; out: Execute:Command | C# `Operators/Lib/io/data/SimulateIoData.cs`; .t3 `Operators/Lib/io/data/SimulateIoData.t3`; docs `None` | C | Mixed IO/control behavior; needs Vuo-specific event/device design. |
| `Lib.io.dmx.ArtnetInput` | Receives DMX data over the network using the Art-Net protocol. This operator listens for Art-Net packets on the local network, allowing you to receive DMX data from lighting consoles, media servers, or other Art-Net-enabled devices. It can listen to multiple DMX universes simultaneously. Tips: - Select the correct LocalIpAddress for your network interface. - StartUniverse and NumUniverses define the range of DMX universes to listen to. - The output is a list of lists. The outer list corresponds to the universes, and each inner list contains the 512 channel values for that universe. AKA: artnet, dmx, lighting, sacn | in: Active:bool, LocalIpAddress:string, NumUniverses:int, PrintToLog:bool, StartUniverse:int, Timeout:float; out: Result:List<int> | C# `Operators/Lib/io/dmx/ArtnetInput.cs`; .t3 `Operators/Lib/io/dmx/ArtnetInput.t3`; docs `.help/docs/operators/lib/io/dmx/ArtnetInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.ArtnetOutput` | Sends DMX data over the network using the Art-Net protocol. This operator broadcasts or unicasts DMX data to Art-Net-enabled devices like lighting fixtures, nodes, or media servers. It can send multiple DMX universes simultaneously and uses a dedicated background thread for high-performance transmission. Features: - Multi-input support: Connect multiple lists to send different universes - Automatic universe expansion: Lists larger than 512 channels span multiple universes - Built-in ArtPoll discovery to find Art-Net nodes on your network - Frame rate limiting to prevent network flooding - Optional ArtSync for synchronized updates Tips: - For broadcast, leave TargetIpAddress empty. For unicast, specify the destination IP. - Use UniverseChannels to define the starting universe for each input (auto-expands as needed) - Enable SendTrigger to start continuous transmission; disable to stop. AKA: artnet, dmx, lighting, sacn | in: EnableArtNet4:bool, InputsValues:List<int>, LocalIpAddress:string, MaxFps:int, PrintArtnetPoll:bool, PrintToLog:bool, Reconnect:bool, SendSync:bool, SendTrigger:bool, SendUnicast:bool, TargetIpAddress:string, UniverseChannels:List<int>; out: Result:Command | C# `Operators/Lib/io/dmx/ArtnetOutput.cs`; .t3 `Operators/Lib/io/dmx/ArtnetOutput.t3`; docs `.help/docs/operators/lib/io/dmx/ArtnetOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.DmxOutput` | Sends DMX data to a connected DMX interface. This operator sends a list of float values as DMX channels to a specified DMX universe via a connected DMX interface (e.g., FTDI-based USB-DMX interfaces). The input DmxUniverse should be a list of floats, where each float represents a DMX channel value (0-1). Tips: - Ensure the correct PortName for your DMX interface is selected. - The DmxUniverse input expects a list of up to 512 float values. Use operators like PointsToDMXLights to generate this data. - MaxFps can be used to limit the update rate and reduce CPU usage. AKA: dmx, light, lighting, ftdi | in: Connect:bool, DmxUniverse:List<int>, MaxFps:int, PortName:string; out: IsConnected:bool, Result:Command | C# `Operators/Lib/io/dmx/DMXOutput.cs`; .t3 `Operators/Lib/io/dmx/DMXOutput.t3`; docs `.help/docs/operators/lib/io/dmx/DmxOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.PointsToDmxLights` | Converts 3D points into DMX channel values for lighting fixtures. Supports position, rotation (pan/tilt), color, and custom features. | in: AlphaChannel:int, BlueChannel:int, CustomVariableChannels:List<int>, CustomVariableValues:List<int>, DebugToLog:bool, EffectedPoints:BufferWithViews, F1Channel:int, F2Channel:int, FillUniverse:bool, FitInUniverse:bool, FixtureChannelSize:int, ForwardVector:int, GetColor:bool, GetF1:bool, GetF1ByPixel:bool, GetF2:bool, GetF2ByPixel:bool, GetPosition:bool, GetRotation:bool, GreenChannel:int, InvertPan:bool, InvertPositionDirection:bool, InvertTilt:bool, InvertX:bool, InvertY:bool, InvertZ:bool, Is16BitColor:bool, PanAxis:int, PanChannel:int, PanFineChannel:int, PanOffset:float, PanRange:Vector2, PositionChannel:int, PositionDistanceRange:Vector2, PositionFineChannel:int, PositionMeasureAxis:int, RedChannel:int, ReferencePoints:BufferWithViews, RgbToCmy:bool, RotationOrder:int, ShortestPathPanTilt:bool, TestModeSelect:int, TiltAxis:int, TiltChannel:int, TiltFineChannel:int, TiltOffset:float, TiltRange:Vector2, VisPanAxis:int, VisTiltAxis:int, WhiteChannel:int; out: Result:List<int>, VisualizeLights:BufferWithViews | C# `Operators/Lib/io/dmx/PointsToDMXLights.cs`; .t3 `Operators/Lib/io/dmx/PointsToDMXLights.t3`; docs `.help/docs/operators/lib/io/dmx/PointsToDmxLights.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.SacnInput` | Receives DMX data over the network using the sACN (Streaming ACN) protocol. This operator listens for sACN packets on the local network, allowing you to receive DMX data from lighting consoles, media servers, or other sACN-enabled devices. It can listen to multiple DMX universes simultaneously. Tips: - Select the correct LocalIpAddress for your network interface. - StartUniverse and NumUniverses define the range of DMX universes to listen to. - The output is a list of lists. The outer list corresponds to the universes, and each inner list contains the 512 channel values for that universe. AKA: sacn, streaming acn, dmx, artnet, lighting | in: Active:bool, LocalIpAddress:string, NumUniverses:int, Result:List<int>, StartUniverse:int, Timeout:float; out: none | C# `Operators/Lib/io/dmx/SacnInput.cs`; .t3 `Operators/Lib/io/dmx/SacnInput.t3`; docs `.help/docs/operators/lib/io/dmx/SacnInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.SacnOutput` | Sends DMX data over the network using the sACN (Streaming ACN / E1.31) protocol. This operator multicasts or unicasts DMX data to sACN-enabled devices like lighting fixtures, nodes, or media servers. It can send multiple DMX universes simultaneously and uses a dedicated background thread for high-performance transmission. Features: - Multi-input support: Connect multiple lists to send different universes - Automatic universe expansion: Lists larger than 512 channels span multiple universes - Built-in source discovery to find sACN devices on your network - Configurable priority for HTP (Highest Takes Precedence) merging - Frame rate limiting to prevent network flooding - Optional sACN Synchronization packets Tips: - For multicast, leave TargetIpAddress empty. For unicast, specify the destination IP. - Use UniverseChannels to define the starting universe for each input (auto-expands as needed) - Enable SendTrigger to start continuous transmission; disable to stop. - Higher Priority values (0-200) override lower priority sources on sACN receivers. AKA: sacn, streaming acn, dmx, artnet, lighting, e1.31 | in: DiscoverSources:bool, EnableSync:bool, InputsValues:List<int>, LocalIpAddress:string, MaxFps:int, Priority:int, Reconnect:bool, SendTrigger:bool, SendUnicast:bool, SourceName:string, SyncUniverse:int, TargetIpAddress:string, UniverseChannels:List<int>; out: Result:Command | C# `Operators/Lib/io/dmx/SacnOutput.cs`; .t3 `Operators/Lib/io/dmx/SacnOutput.t3`; docs `.help/docs/operators/lib/io/dmx/SacnOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.helpers.Video2DPointScanner` | Scans a video input to detect the 2D positions of bright pixels, often used for projector-camera calibration or tracking LEDs. This operator sequentially tests pixels by outputting a bright value and checking if the result is visible in the video feed. This process allows it to map the output pixel coordinates to the camera's view. The resulting 2D point map can be saved and later used with a [CameraCalibrator] to create a 3D spatial mapping. Tips: - Connect a camera feed to VideoIn. - Set PixelCount to the number of lights or pixels you want to scan. - Use ScanTrigger to start the process. - The DebugTexture output is useful for visualizing the scanning process. AKA: projector calibration, led tracking, blob detection, camera mapping, structured light | in: ApplyCorrection:bool, CalibrationPath:string, DebugMode:bool, FilePath:string, Load:bool, LoadCalibration:bool, PixelBrightness:float, PixelCount:int, ResetScan:bool, Save:bool, ScanIntervallum:float, ScanTrigger:bool, TestFullMode:bool, TestPixelMode:bool, Threshold:float, VideoIn:Texture2D; out: DebugTexture:Texture2D, PixelOutput:BufferWithViews, ScannedPoints2D:BufferWithViews | C# `Operators/Lib/io/dmx/helpers/Video2DPointScanner.cs`; .t3 `Operators/Lib/io/dmx/helpers/Video2DPointScanner.t3`; docs `.help/docs/operators/lib/io/dmx/helpers/Video2DPointScanner.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.helpers.VisualizeSpotLights` | Visualizes the position and orientation of spotlights. | in: Color:Vector4, GPoints:BufferWithViews, ReferencePoints:BufferWithViews, ShowBody:bool, Visibility:T3.Core.Operator.GizmoVisibility, VisualizeAxis:bool; out: Output:Command | C# `Operators/Lib/io/dmx/helpers/VisualizeSpotLights.cs`; .t3 `Operators/Lib/io/dmx/helpers/VisualizeSpotLights.t3`; docs `.help/docs/operators/lib/io/dmx/helpers/VisualizeSpotLights.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.obsolete.ArtnetPixelOutput` | Converts a list of 3D points into Art-Net DMX data for controlling RGB pixel arrays or similar fixtures. This operator takes a buffer of points, extracts their color information (RGB from Point.Color.X/Y/Z), and assigns them to DMX universes based on the Point.F2 attribute. It then sends this data as Art-Net packets over the network. Tips: - Ensure the IpAddress is set to the correct Art-Net node or broadcast address. - The ArtNetSendRate controls how frequently DMX data is transmitted. - Use the Reconnect trigger if the network connection is lost or the IP address changes. AKA: artnet, pixel, rgb, dmx, obsolete | in: ArtNetSendRate:float, IpAddress:string, Points:BufferWithViews, Reconnect:bool; out: Result:Command | C# `Operators/Lib/io/dmx/obsolete/ArtnetPixelOutput.cs`; .t3 `Operators/Lib/io/dmx/obsolete/ArtnetPixelOutput.t3`; docs `.help/docs/operators/lib/io/dmx/obsolete/ArtnetPixelOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.dmx.obsolete.PointsToRGBList` | Converts a buffer of 3D points into a flat list of RGB float values (0-255). This operator extracts the color (RGB) from each point in the input buffer and flattens it into a single list of floats, where each point contributes 3 float values (Red, Green, Blue). This can be useful for sending color data to systems that expect a linear list of values. AKA: rgb, color, points, list, obsolete | in: Points:BufferWithViews; out: Result:List<float> | C# `Operators/Lib/io/dmx/obsolete/PointsToRGBList.cs`; .t3 `Operators/Lib/io/dmx/obsolete/PointsToRGBList.t3`; docs `.help/docs/operators/lib/io/dmx/obsolete/PointsToRGBList.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.file.FilesInFolder` | Scans a folder for all existing files and creates a list that can be used with [PickFromStringList] It also counts files in a folder and outputs the amount as an integer. See the example [FilesInFolderExample] and [FadingSlideShow] for what might be a useful combination Other interesting related ops: [ReadFile] [RequestUrl] [PickStringPart] [WriteToFile] [GetAttributeFromJsonString] | in: Filter:string, Folder:string, TriggerUpdate:bool; out: Files:List<string>, NumberOfFiles:int | C# `Operators/Lib/io/file/FilesInFolder.cs`; .t3 `Operators/Lib/io/file/FilesInFolder.t3`; docs `.help/docs/operators/lib/io/file/FilesInFolder.md` | B | Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract. |
| `Lib.io.file.ReadFile` | Reads a file on the local disk and outputs the content as a string Opposite of: [WriteToFile] Useful combination: [PickStringPart] Also see: [RequestUrl] which adds online capabilities and [GetAttributeFromJsonString] | in: FilePath:string, TriggerUpdate:bool; out: Result:string | C# `Operators/Lib/io/file/ReadFile.cs`; .t3 `Operators/Lib/io/file/ReadFile.t3`; docs `.help/docs/operators/lib/io/file/ReadFile.md` | B | Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract. |
| `Lib.io.file.WriteToFile` | Writes the incoming string into a predefined file on the local disk Info: This operator is unable to create files. The file has to exist in order to write something into it. Depending on the user and operating system, Tooll might need admin privileges. Also see: [ReadFile] [RequestUrl] [FilesInFolder] | in: Content:string, Filepath:string; out: OutFilepath:string, Result:string | C# `Operators/Lib/io/file/WriteToFile.cs`; .t3 `Operators/Lib/io/file/WriteToFile.t3`; docs `.help/docs/operators/lib/io/file/WriteToFile.md` | B | Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract. |
| `Lib.io.freed.FreeDInput` | Receives camera tracking data over UDP using the FreeD protocol. This is essential for virtual production, augmented reality, and broadcasting, allowing real-time integration with systems like Unreal Engine, Vizrt, or Aximmetry. Also see: [FreeDOutput] | in: CameraId:int, Listen:bool, LocalIpAddress:string, Port:int, PrintToLog:bool; out: CameraDataAsDict:Dict<float>, CameraPos:Vector3, CameraRot:Vector3, Focus:float, IsListening:bool, User:float, Zoom:float | C# `Operators/Lib/io/freed/FreeDInput.cs`; .t3 `Operators/Lib/io/freed/FreeDInput.t3`; docs `.help/docs/operators/lib/io/freed/FreeDInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.freed.FreeDOutput` | Sends camera tracking data over UDP using the FreeD protocol. This is essential for virtual production, augmented reality, and broadcasting, allowing real-time integration with systems like Unreal Engine, Vizrt, or Aximmetry. Also see: [FreeDInput] | in: CameraId:int, Connect:bool, Focus:int, LocalIpAddress:string, Position:Vector3, PrintToLog:bool, Rotation:Vector3, SendOnChange:bool, SendTrigger:bool, TargetIpAddress:string, TargetPort:int, User:int, Zoom:int; out: Command:Command, IsConnected:bool | C# `Operators/Lib/io/freed/FreeDOutput.cs`; .t3 `Operators/Lib/io/freed/FreeDOutput.t3`; docs `.help/docs/operators/lib/io/freed/FreeDOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.http.WebServer` | Starts a simple HTTP web server to serve content over the network. This operator can be used to create simple web interfaces, provide data to other applications via a basic REST API, or serve files. The content served to connecting clients is determined by the HtmlContent input. Tips: - Use the Listen toggle to start and stop the server. - If Port is set to 0, the server will automatically pick a free port. - The server's URL will be http://[LocalIpAddress]:[Port]/ AKA: http, server, web, rest, api | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/http/WebServer.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.input.Gamepad` | # Gamepad input using XInput. It supports up to four gamepads. Use in combination with these operators: ThumbSticks: [SelectVec2FromDict] Buttons: [SelectBoolFromFloatDict] Triggers: [SelectFloatFromDict] Check the example: [GamePadExample] | in: Index:int; out: IsConnected:bool, State:Dict<float> | C# `Operators/Lib/io/input/Gamepad.cs`; .t3 `Operators/Lib/io/input/Gamepad.t3`; docs `.help/docs/operators/lib/io/input/Gamepad.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.input.KeyboardInput` | Allows the real-time manipulation of any value using a key on the keyboard. Useful combinations: [BoolToFloat] [BoolToInt] [FlipFlop] [HasBooleanChanged] [TriggerAnim] Also see: [MouseInput] [MidiInput] | in: Key:int, Mode:int; out: Result:bool | C# `Operators/Lib/io/input/KeyboardInput.cs`; .t3 `Operators/Lib/io/input/KeyboardInput.t3`; docs `.help/docs/operators/lib/io/input/KeyboardInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.input.KeyboardInputAsInt` | Returns integer values from 0 to 9 for pressed numeric keys. | in: Mode:int, Zone:int; out: PressedNumber:int | C# `Operators/Lib/io/input/KeyboardInputAsInt.cs`; .t3 `Operators/Lib/io/input/KeyboardInputAsInt.t3`; docs `.help/docs/operators/lib/io/input/KeyboardInputAsInt.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.input.MouseInput` | Allows the real-time manipulation of any values using the mouse / trackpad If the mouse pointer is moved over the output window, the values of the X and Y axes of the mouse movements are evaluated and can control any functions. The left mouse button can also be evaluated as a boolean. Useful combinations: [Vec2ToVec3] [Vector2Components] [BoolToFloat] [Transform] [TransformMesh] Also see: [KeyboardInput] [MidiInput] | in: DoUpdate:bool, OutputMode:int, Scale:float; out: IsLeftButtonDown:bool, Position:Vector2, Position3d:Vector3 | C# `Operators/Lib/io/input/MouseInput.cs`; .t3 `Operators/Lib/io/input/MouseInput.t3`; docs `.help/docs/operators/lib/io/input/MouseInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.json.GetAttributeFromJsonString` | Loads a JSON string from a URL and outputs the defined part as a string. Simpler alternative [RequestUrl] Also see [LoadImageFromUrl] [WriteToFile] [ReadFile] [PickStringPart] [FilesInFolder] | in: ColumnName:string, JsonString:string, RowIndex:int; out: Columns:List<string>, Result:string, RowCount:int | C# `Operators/Lib/io/json/GetAttributeFromJsonString.cs`; .t3 `Operators/Lib/io/json/GetAttributeFromJsonString.t3`; docs `.help/docs/operators/lib/io/json/GetAttributeFromJsonString.md` | B | Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract. |
| `Lib.io.json.RequestUrl` | Loads a URL and outputs the source text as a string. Hint: The string contains what you see when you open the URL with a browser and select 'View Page Source' Useful combinations: [PickStringPart] Also see [LoadImageFromUrl] [GetAttributeFromJsonString] | in: TriggerRequest:bool, Url:string; out: Result:string | C# `Operators/Lib/io/json/RequestUrl.cs`; .t3 `Operators/Lib/io/json/RequestUrl.t3`; docs `.help/docs/operators/lib/io/json/RequestUrl.md` | B | Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract. |
| `Lib.io.midi.LinkToMidiTime` | This helper uses MIDI time clock events to drive the playback time. This can be useful for live performances when you want to sync visuals to a clock provided by a DAW like Ableton or Traktor. Tip: When connecting the SyncTrigger parameter to a [MidiInput] and [HasValueIncreased], be sure to disable damping on the MidiInput. | in: ResyncTrigger:bool, SubGraph:Command; out: Commands:Command | C# `Operators/Lib/io/midi/LinkToMidiTime.cs`; .t3 `Operators/Lib/io/midi/LinkToMidiTime.t3`; docs `.help/docs/operators/lib/io/midi/LinkToMidiTime.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiClip` | Creates a time clip bar within the DopeView of the Timeline, similar to how video editing apps show clips. TimeClips can be moved by drag and drop and arranged next to and on top of each other (classic NLE non-linear editing). When the time marker is running over / playing the time clip it is activated. To load an image sequence into the DopeView of the Timeline [ImageSequenceClip] can be used. [TimeClip] can be used to activate full node trees. | in: Filename:string, PrintLogMessages:bool; out: ChannelNames:List<string>, DeltaTicksPerQuarterNote:float, Values:Dict<float> | C# `Operators/Lib/io/midi/MidiClip.cs`; .t3 `Operators/Lib/io/midi/MidiClip.t3`; docs `.help/docs/operators/lib/io/midi/MidiClip.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiControlOutput` | Send 7 bit ControlChange or Channel Pressure events to the selected device. The value is given either as an integer value between 0 and 127 or as a normalized float value between 0.0 and 1.0. Values can either be sent continuously (one value per frame) or only when the trigger input is sent a (temporary) boolean "true" value. Sending continuously can lead to flooding of the receiving device, so if possible, use the trigger option. You can for instance use an [AnimValue]s "WasHit" output as the trigger and set it to a frequency that is sufficiently smooth for your purpose to reduce the load. This node can be used in combination with [MidiNoteOutput] [MidiPitchbendOutput] and [MidiTriggerOutput] to create generative music or control hardware. See the [MidiNoteOutputExample] AKA: aftertouch, pressure, cc, controller | in: CCorPressure:int, ChannelNumber:int, ControllerNumber:int, Device:string, SendMode:int, TriggerSend:bool, UseValueFloat:bool, Value:int, ValueFloat:float; out: Result:Command | C# `Operators/Lib/io/midi/MidiControlOutput.cs`; .t3 `Operators/Lib/io/midi/MidiControlOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiControlOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiInput` | Provides input from connected MIDI devices. By default, the MIDI range from 0 to 127 is mapped to an output range from 0 to 1, which can be adjusted with the parameters. For smoothing the output, damping is enabled by default. Reduce the damping parameter to improve latency. If you want to react to a range for controls (i.e., to the range of keys on a keyboard), you can use the ControlRange parameter. If this is something other than [0, 0], we map its range to the OutputRange (e.g., you could map two octaves of your keyboard to output values from 0 to 1). To support controllers that have controllers and note buttons with overlapping control IDs (like the APC Mini), we distinguish between different event types. Useful combinations: [FloatToInt] [BooltoFloat] [BoolToInt] [FlipFlop] [HasBooleanChanged] [TriggerAnim] Also see: [MouseInput] [KeyboardInput] | in: Channel:int, Control:int, ControlRange:Int2, Damping:float, DefaultOutputValue:float, Device:string, EventType:int, LastKnownControllerValue:float, OutputRange:Vector2, PrintLogMessages:bool, ResetToDefaultTrigger:bool, TeachTrigger:bool; out: Range:List<float>, Result:float, WasHit:bool | C# `Operators/Lib/io/midi/MidiInput.cs`; .t3 `Operators/Lib/io/midi/MidiInput.t3`; docs `.help/docs/operators/lib/io/midi/MidiInput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiNoteOutput` | Send MIDI notes to the selected device on the given channel. The velocity is provided either as an integer between 0 and 127 or as a normalized float value between 0.0 and 1.0 When NoteWhileTriggered is selected, a note is played while the TriggerSend input is boolean "true". When it turns "false" a NoteOff command is sent to the receiving device. When NoteFixedDuration is selected, a note starts when the TriggerSend input is boolean "true" and ends after the time in DurationInSecs has passed or if another new note is triggered (to prevent overlapping or hanging notes). This node can be used in combination with [MidiControlOutput], [MidiPitchbendOutput] and [MidiTriggerOutput] to create generative music or control hardware. | in: ChannelNumber:int, Device:string, DurationInSecs:float, NoteNumber:int, SendMode:int, TriggerSend:bool, UseVelocityFloat:bool, Velocity:int, VelocityFloat:float; out: Result:Command | C# `Operators/Lib/io/midi/MidiNoteOutput.cs`; .t3 `Operators/Lib/io/midi/MidiNoteOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiNoteOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiOutput` | Send notes or controller change events to the selected device. This operator is experimental. The velocity is given as a normalized float value between 0 and 1. | in: ChannelNumber:int, Device:string, DurationInSecs:float, NoteOrController:int, SendMode:int, TriggerSend:bool, Velocity:float, Velocity127:int; out: Result:Command | C# `Operators/Lib/io/midi/MidiOutput.cs`; .t3 `Operators/Lib/io/midi/MidiOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiPitchbendOutput` | Send 14-bit pitchbend events to the selected Device on the given Channel. The value can be provided either as an integer value between -8192 and 8191 or as a normalized float value between -1.0 and 1.0. Values can either be sent continuously (one value per frame) or only when the trigger input is sent a (temporary) boolean "true" value. Sending continuously can lead to flooding of the receiving device, so if possible, use the trigger option. You can, for instance, use an [AnimValue]'s "WasHit" output and set it to a frequency that is sufficiently smooth for your purpose to reduce the load. This node can be used in combination with [MidiNoteOutput], [MidiControlOutput], and [MidiTriggerOutput] to create generative music or control hardware. See the [MidiNoteOutputExample] AKA: pitch, pitchbend, transpose, detune | in: ChannelNumber:int, Device:string, Pitch:int, PitchFloat:float, SendMode:int, TriggerSend:bool, UsePitchFloat:bool; out: Result:Command | C# `Operators/Lib/io/midi/MidiPitchbendOutput.cs`; .t3 `Operators/Lib/io/midi/MidiPitchbendOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiPitchbendOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiRecording` | An experimental implementation to record MIDI input signals. | in: ResetTrigger:bool; out: DataSet:DataSet | C# `Operators/Lib/io/midi/MidiRecording.cs`; .t3 `Operators/Lib/io/midi/MidiRecording.t3`; docs `.help/docs/operators/lib/io/midi/MidiRecording.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiSysexOutput` | Allow sending midi sysex string to a selected midi device. Example: This is useful in the case of the APC40 to change device modes. | in: Device:string, SysexString:string, TriggerSend:bool; out: Result:Command | C# `Operators/Lib/io/midi/MidiSysexOutput.cs`; .t3 `Operators/Lib/io/midi/MidiSysexOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiSysexOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.midi.MidiTriggerOutput` | Send ProgramChange or Sequencer commands to the selected Device on the chosen Channel. The ProgramChangeNumber is given as an integer between 0 and 127. All parameters are sent when a trigger is received in the respective input. It should only be a momentary, one frame long "true" value to not flood the receiving gear. Be aware that not many applications actually react to these triggers! Hardware is more likely to have them implemented. This node can be used in combination with [MidiNoteOutput] [MidiControlOutput] and [MidiPitchbendOutput] to create generative music or control hardware. AKA: programchange, start, stop, continue, tempo, bpm | in: ChannelNumber:int, Device:string, ProgramChangeNumber:int, TriggerContinue:bool, TriggerProgramChange:bool, TriggerStart:bool, TriggerStop:bool, TriggerTempoEvent:bool; out: Result:Command | C# `Operators/Lib/io/midi/MidiTriggerOutput.cs`; .t3 `Operators/Lib/io/midi/MidiTriggerOutput.t3`; docs `.help/docs/operators/lib/io/midi/MidiTriggerOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.osc.OscInput` | Receives OSC input from a connected server. In most scenarios, it is useful to record some input messages and then pick the relevant addresses by using a [SelectFloatFromDict] or [SelectVec2FromDict] operator. You might want to adjust the default OSC port in the settings window to see a visual indication of incoming messages in the timeline bar. | in: Address:string, FilterKeys:string, GroupKeysAsPaths:string, IsListening:bool, Port:int, PrintLogMessages:bool, SearchFilterKey:string, SearchPattern:string, UseKeyValuePairs:bool; out: Contents:Dict<float>, Values:List<float>, WasTrigger:bool | C# `Operators/Lib/io/osc/OscInput.cs`; .t3 `Operators/Lib/io/osc/OscInput.t3`; docs `.help/docs/operators/lib/io/osc/OscInput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.osc.OscOutput` | Send out OSC signals to a receiver. | in: Address:string, IntValues:int, IntValuesList:System.Collections.Generic.List<int>, IpAddress:string, OnlySendChanges:bool, Port:int, Reconnect:bool, SendTrigger:bool, Strings:string, Values:float, ValuesList:System.Collections.Generic.List<float>; out: Result:Command | C# `Operators/Lib/io/osc/OscOutput.cs`; .t3 `Operators/Lib/io/osc/OscOutput.t3`; docs `.help/docs/operators/lib/io/osc/OscOutput.md` | C | Vuo has direct node set; realtime event/device mapping must be designed. |
| `Lib.io.posistage.PosiStageInput` | Receives real-time 3D tracking data using the PosiStageNet protocol. This operator listens for PosiStageNet (PSN) packets, which are commonly used in live events and virtual production to stream tracking data for cameras, objects, and performers. Tips: - Ensure your tracking system is sending PSN data to the specified MulticastIpAddress and Port. - The output TrackersAsBuffer provides the raw tracking data as a structured buffer, which is efficient for use with other operators that process point data. - TrackersAsDict provides the data as a dictionary, which can be easier for inspecting and extracting specific tracker information. AKA: posistage, tracking, stage, psn, motion capture | in: Listen:bool, LocalIpAddress:string, MulticastIpAddress:string, Port:int, PrintToLog:bool; out: IsListening:bool, TrackersAsDict:Dict<float> | C# `Operators/Lib/io/posistage/PosiStageInput.cs`; .t3 `Operators/Lib/io/posistage/PosiStageInput.t3`; docs `.help/docs/operators/lib/io/posistage/PosiStageInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.posistage.PosiStageOutput` | Sends real-time 3D tracking data using the PosiStageNet (PSN) protocol. This operator takes a list of points and sends them as PosiStageNet tracker data over the network. This allows T3 to act as a source of tracking information for other applications in a virtual production or live event workflow. Tips: - The TrackerData input expects a structured buffer of points, where each point represents a tracker with position and orientation. - Use SendOnChange or SendTrigger to control when data is sent. - The Names input can be used to assign names to the trackers being sent. AKA: posistage, tracking, stage, psn, motion capture, sender | in: Connect:bool, LocalIpAddress:string, Names:string, PrintToLog:bool, SendOnChange:bool, SendTrigger:bool, ServerName:string, TargetIpAddress:string, TargetPort:int, TrackerData:BufferWithViews; out: Command:Command, IsConnected:bool | C# `Operators/Lib/io/posistage/PosiStageOutput.cs`; .t3 `Operators/Lib/io/posistage/PosiStageOutput.t3`; docs `.help/docs/operators/lib/io/posistage/PosiStageOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.ptz.OnvifCamera` | Connects to an ONVIF-compliant IP camera to retrieve video texture and control PTZ. This operator handles the discovery, connection, and control of network cameras supporting the ONVIF standard. It uses FFmpeg to capture the video stream (RTSP) and SOAP requests for PTZ control. The video stream is output as a Texture2D. PTZ controls are normalized (Pan/Tilt -1..1, Zoom 0..1). AKA: IPCamera, RTSP, NetworkCamera, SecurityCamera, PTZ Tips: * Use the 'Discover' toggle to find cameras on your local network. * Ensure the 'Local IP Address' is set to the network adapter connected to the cameras. * PTZ movements are damped/smoothed. * If the stream URL is not automatically resolved, you can enter a direct RTSP URL in the Address field. | in: Address:string, Connect:bool, Discover:bool, LocalIpAddress:string, Move:bool, Pan:float, Password:string, PrintToLog:bool, Tilt:float, Username:string, Zoom:float; out: CurrentPtz:Vector3, IsConnected:bool, Texture:Texture2D | C# `Operators/Lib/io/ptz/OnvifCamera.cs`; .t3 `Operators/Lib/io/ptz/OnvifCamera.t3`; docs `.help/docs/operators/lib/io/ptz/OnvifCamera.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.ptz.ViscaCamera` | Controls a PTZ camera using the VISCA over IP protocol. This operator sends UDP packets to control Pan, Tilt, and Zoom. It supports absolute positioning. AKA: PTZ, Sony VISCA, Network Camera Control Tips: * Ensure the camera is configured to accept VISCA over IP (often port 52381). * Pan and Tilt inputs are normalized (-1..1). Adjust 'Pan Range' and 'Tilt Range' to match your specific camera model's max steps (e.g., Sony SRG series often use 0x1800 for Pan). * Zoom is normalized (0..1). | in: Address:string, Connect:bool, LocalIpAddress:string, Move:bool, Pan:float, PanRange:int, Port:int, PrintToLog:bool, TextureInput:Texture2D, Tilt:float, TiltRange:int, Zoom:float; out: CurrentPtz:Vector3, IsConnected:bool, TextureOutput:Texture2D | C# `Operators/Lib/io/ptz/ViscaCamera.cs`; .t3 `Operators/Lib/io/ptz/ViscaCamera.t3`; docs `.help/docs/operators/lib/io/ptz/ViscaCamera.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.serial.SerialInput` | Receives data from a serial port. This operator listens for incoming data on a specified serial port, allowing communication with external hardware devices such as microcontrollers (e.g., Arduino) or other serial-enabled equipment. It can parse incoming data as individual lines or as a continuous string. Tips: - Ensure the BaudRate and PortName match the settings of the sending device. - Use ReceivedLines to process multiple incoming messages, especially when the sending device terminates messages with a line ending. - WasTrigger can be used to trigger other operators when new data arrives. AKA: arduino, rs232, uart, com, serial reader | in: BaudRate:int, Connect:bool, ListLength:int, PortName:string; out: IsConnected:bool, ReceivedLines:List<string>, ReceivedString:string, WasTrigger:bool | C# `Operators/Lib/io/serial/SerialInput.cs`; .t3 `Operators/Lib/io/serial/SerialInput.t3`; docs `.help/docs/operators/lib/io/serial/SerialInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.serial.SerialOutput` | Sends data to a serial port. This operator can be used to send data to hardware devices like Arduino, DMX interfaces, or other custom electronics using the serial protocol. Multiple values can be combined into a single message using the MessageParts input. Tips: - Use SendOnChange to automatically send data when MessageParts changes. - Use SendTrigger for manual control over when messages are sent. - AddLineEnding is often required for text-based communication protocols. AKA: arduino, rs232, uart, com | in: AddLineEnding:bool, BaudRate:int, Connect:bool, MessageParts:string, PortName:string, SendOnChange:bool, SendTrigger:bool, Separator:string; out: IsConnected:bool, Result:Command | C# `Operators/Lib/io/serial/SerialOutput.cs`; .t3 `Operators/Lib/io/serial/SerialOutput.t3`; docs `.help/docs/operators/lib/io/serial/SerialOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.serial.WLedSerialOutput` | Unknown. | in: BaudRate:int, Brightness:float, ColorOrder:int, Colors:List<Vector4>, LedCount:int, LogMessages:bool, MapMode:int, PortName:string, Reconnect:bool, Sending:bool; out: IsConnected:bool, Result:Command | C# `Operators/Lib/io/serial/WLedSerialOutput.cs`; .t3 `Operators/Lib/io/serial/WLedSerialOutput.t3`; docs `None` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.tcp.TcpClient` | Connects to a TCP server to send and receive data. This operator establishes a client connection to a specified TCP server. It can send messages constructed from various parts and receives data from the server, which can be processed as a continuous string or as a list of lines. Tips: - Use SendOnChange to automatically send data whenever the MessageParts input changes. - For manual control over sending, use the SendTrigger input. - Check the IsConnected output to verify the connection status before attempting to send data. AKA: tcp, network, socket, client | in: Connect:bool, Host:string, ListLength:int, LocalIpAddress:string, MessageParts:string, Port:int, PrintToLog:bool, SendOnChange:bool, SendTrigger:bool, Separator:string; out: IsConnected:bool, ReceivedLines:List<string>, ReceivedString:string, WasTrigger:bool | C# `Operators/Lib/io/tcp/TcpClient.cs`; .t3 `Operators/Lib/io/tcp/TcpClient.t3`; docs `.help/docs/operators/lib/io/tcp/TcpClient.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.tcp.TcpServer` | Creates a TCP server to listen for incoming connections and receive data. This operator starts a TCP server on a specified IP address and port, allowing multiple clients to connect simultaneously. It receives data from all connected clients and can broadcast messages to them. Tips: - Use Listen to start and stop the server. - ConnectionCount shows how many clients are currently connected. - Messages sent via the Message input are broadcast to all connected clients. AKA: tcp, network, socket, server | in: Listen:bool, LocalIpAddress:string, Message:string, Port:int, PrintToLog:bool, SendOnChange:bool, SendTrigger:bool; out: ConnectionCount:int, IsListening:bool, Result:Command | C# `Operators/Lib/io/tcp/TcpServer.cs`; .t3 `Operators/Lib/io/tcp/TcpServer.t3`; docs `.help/docs/operators/lib/io/tcp/TcpServer.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.udp.UdpInput` | Receives data from the network using the UDP (User Datagram Protocol). This operator listens for UDP packets on a specified port, allowing communication with other applications or devices that send UDP data. UDP is a connectionless protocol, meaning data is sent without establishing a persistent connection, which is fast but less reliable than TCP. Tips: - Ensure the Port and LocalIpAddress match the sender's destination settings. - WasTrigger becomes true for one frame whenever a new message is received, which can be used to trigger other actions. - Unlike TCP, UDP does not guarantee message delivery or order. AKA: udp, network, socket, datagram | in: Listen:bool, ListLength:int, LocalIpAddress:string, Port:int, PrintToLog:bool; out: IsListening:bool, LastSenderAddress:string, LastSenderPort:int, ReceivedLines:List<string>, ReceivedString:string, WasTrigger:bool | C# `Operators/Lib/io/udp/UDPInput.cs`; .t3 `Operators/Lib/io/udp/UDPInput.t3`; docs `.help/docs/operators/lib/io/udp/UdpInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.udp.UdpOutput` | Sends data to the network using the UDP (User Datagram Protocol). This operator sends UDP packets to a specified IP address and port. UDP is a connectionless protocol, which is fast but does not guarantee the delivery or order of messages. Messages can be constructed from multiple parts. Tips: - Use SendOnChange to automatically send data when MessageParts changes. - Use SendTrigger for manual control over sending messages. - To broadcast to all devices on the local network, use a broadcast IP address like '255.255.255.255'. AKA: udp, network, socket, datagram | in: Connect:bool, LocalIpAddress:string, MessageParts:string, PrintToLog:bool, SendOnChange:bool, SendTrigger:bool, Separator:string, TargetIpAddress:string, TargetPort:int; out: IsConnected:bool, Result:Command | C# `Operators/Lib/io/udp/UDPOutput.cs`; .t3 `Operators/Lib/io/udp/UDPOutput.t3`; docs `.help/docs/operators/lib/io/udp/UdpOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.CameraCalibrator` | Calibrates a camera to determine its intrinsic parameters (like focal length and lens distortion) and to remove distortion from the video feed. This operator analyzes images of a known chessboard pattern to calculate the camera's properties. This process is essential for many computer vision tasks, such as 3D reconstruction, augmented reality, and spatial mapping. To use: 1. Set the ChessboardSize to match your physical chessboard pattern. 2. Show the chessboard to the camera. 3. Trigger CaptureImage multiple times from different angles and distances. 4. Trigger Calibrate to compute the camera parameters. 5. Switch Mode to 'LensCorrected' to see the corrected image on the TextureOut output. 6. Save the calibration to a file for later use. AKA: lens calibration, camera intrinsics, undistort, computer vision, chessboard | in: Alpha:float, BorderInSquares:int, Calibrate:bool, CaptureImage:bool, ChessboardSize:Int2, DisplayResolution:Int2, FilePath:string, Load:bool, Mode:int, Reset:bool, Save:bool, SquareInMm:float, TextureIn:Texture2D, UndistortMethod:int; out: CheckerboardTexture:Texture2D, IsCalibrated:bool, StatusMessage:string, TextureOut:Texture2D | C# `Operators/Lib/io/video/CameraCalibrator.cs`; .t3 `Operators/Lib/io/video/CameraCalibrator.t3`; docs `.help/docs/operators/lib/io/video/CameraCalibrator.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.NdiInput` | NDI live video input | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/NdiInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.NdiOutput` | NDI live video output. R8G8B8A8_UNorm texture format or R8G8B8A8_Typeless is required for output. You can adjust the render format by using a ConvertFormat op or [RenderTarget] like in this example: [TorusMesh]->[DrawMesh]->[RenderTarget]->[SpoutOutput]. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/NdiOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.PlayAudioClip` | This is a workaround to playback audio clips without syncing. | in: IsLooping:bool, IsPlaying:bool, Path:string, TimeInSecs:float, Volume:float; out: Result:Command | C# `Operators/Lib/io/video/PlayAudioClip.cs`; .t3 `Operators/Lib/io/video/PlayAudioClip.t3`; docs `.help/docs/operators/lib/io/video/PlayAudioClip.md` | D | TiXL/BASS audio engine or spatial mixer dependency. |
| `Lib.io.video.PlayVideo` | Uses Windows Media Foundation to play a video file. To ensure seek precision while editing, it enforces seeking if timeline playback is paused. If timeline playback is running, it will only seek if the video playback drift exceeds the resync threshold. If this threshold is too small, playback will stutter. If it's excessively large, syncing might be off. [SetCommandTime] can be used to control / offset the video playback and speed: [PlayVideo] -> [Layer2D] -> [SetCommandTime] Important: Media Foundation returns textures in BGRA format. This might not work for some draw operators like [Layer2d]. Please insert a [ConvertFormat] operator to convert the format. Note: If this is flickering, you can try to deactivate Idle Motion from the timeline control bar. | in: IsPreciseAtPlayback:bool, Loop:bool, OverrideTimeInSecs:float, Path:string, ResyncThreshold:float, Volume:float; out: Duration:float, HasCompleted:bool, UpdateCount:int | C# `Operators/Lib/io/video/PlayVideo.cs`; .t3 `Operators/Lib/io/video/PlayVideo.t3`; docs `.help/docs/operators/lib/io/video/PlayVideo.md` | C | Mixed IO/control behavior; needs Vuo-specific event/device design. |
| `Lib.io.video.PlayVideoClip` | Implementation to load and play video files with similar options like a [TimeClip] Also see [PlayVideo] and [PlayVideoExample] for an operator with more options | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/PlayVideoClip.md` | C | Mixed IO/control behavior; needs Vuo-specific event/device design. |
| `Lib.io.video.ScreenCapture` | Loads and renders the content of the entire screen, similar to screen capturing software like OBS (Open Broadcaster Software). Useful combinations: Can be rendered to a file/image sequence within TiXL's 'Render To File' window to make a recording of TiXL's UI. Can be combined with [ScreenCloseUp] to make it look like the monitor is being filmed with a real camera. Can also be used to add 2D effects on the captured image like [Pixelate], [SortPixel], etc. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/ScreenCapture.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.SpoutInput` | Spout live video input | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/SpoutInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.SpoutOutput` | Spout live video output. We recommend using the R8G8B8A8_UNorm texture format for output. You can adjust the render format by using a [RenderTarget] like in this example: [TorusMesh]->[DrawMesh]->[RenderTarget]->[SpoutOutput]. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/SpoutOutput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.SwiftCamDevice` | Unknown. | in: Active:bool, AnalogGainFactor:float, AutoExposure:bool, DeviceName:string, Exposure:float, HardReset:bool, LogMessages:bool, Reconnect:bool, ResolutionIndex:int, RoiAlignment:Vector2, RoiResolution:Int2; out: Resolution:Int2, Texture:Texture2D, UpdateCount:int | C# `Operators/Lib/io/video/SwiftCamDevice.cs`; .t3 `Operators/Lib/io/video/SwiftCamDevice.t3`; docs `None` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.VideoClip` | *No description yet. Edit this operator's description in the TiXL editor to populate this page.* | in: FilePath:string, Loop:bool, ResynchThreshold:float, ShowTimeCode:bool; out: Output2:Command | C# `Operators/Lib/io/video/VideoClip.cs`; .t3 `Operators/Lib/io/video/VideoClip.t3`; docs `.help/docs/operators/lib/io/video/VideoClip.md` | C | Mixed IO/control behavior; needs Vuo-specific event/device design. |
| `Lib.io.video.VideoDeviceInput` | Captures live video from a connected device like a webcam, capture card, or NDI source. This operator provides access to video devices available on your system. You can select the device, resolution, and frame rate. It also allows for basic image manipulation like flipping and repositioning. Tips: - To save system resources, enable DeactivateWhenNotShowing. This will pause the capture when the operator is not being rendered. - Use OpenSettings to access the device-specific configuration dialog for more advanced options like exposure and color balance. - The Status output provides useful information for troubleshooting connection issues. AKA: webcam, camera, capture card, ndi, directshow, video input | in: Active:bool, ApplyRotationData:float, CustomFps:int, CustomResolution:Int2, DeactivateWhenNotShowing:bool, FlipHorizontally:bool, FlipVertically:bool, InputDeviceName:string, OpenSettings:bool, Reconnect:bool, Reposition:Vector2, ResolutionFpsType:int, Scale:Vector2; out: Resolution:Int2, Texture:Texture2D, UpdateCount:int | C# `Operators/Lib/io/video/VideoDeviceInput.cs`; .t3 `Operators/Lib/io/video/VideoDeviceInput.t3`; docs `.help/docs/operators/lib/io/video/VideoDeviceInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.VideoStreamInput` | Receives a video stream from a network source. This operator connects to a network video stream (e.g., RTSP, HTTP, RTMP) and provides the video as a texture. It is useful for bringing in live feeds from IP cameras, streaming servers, or other network video sources. Tips: - Use the Connect toggle to start and stop receiving the stream. - If the stream is interrupted, use the Reconnect trigger to try and establish the connection again. - The Status output provides valuable information for debugging connection issues. AKA: video stream, rtsp, rtmp, http, ip camera, network video | in: Connect:bool, Reconnect:bool; out: Resolution:Int2, Url:string | C# `Operators/Lib/io/video/VideoStreamInput.cs`; .t3 `Operators/Lib/io/video/VideoStreamInput.t3`; docs `.help/docs/operators/lib/io/video/VideoStreamInput.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.FaceDetection` | Detects faces in an image using MediaPipe Face Detection. This operator uses MediaPipe Face Detection task to detect faces in input image. It outputs detected faces as a buffer of points (bounding box corners and keypoints) and a dictionary of face data. Inputs: - InputTexture: The image to process. - EnableDetection: Enables or disables face detection. - ConfidenceThreshold: Minimum confidence score for a face to be detected. - MaxFaces: Maximum number of faces to detect. - ShowDetections: Whether to include bounding box points in the output buffer. - ShowKeypoints: Whether to include keypoints (eyes, nose, mouth, ears) in the output buffer. - DetectionSize: Size of points in the output buffer. - DetectionColor: Color of bounding box points. - KeypointColor: Color of keypoint points. - UseCombinedAIOutput: Whether to generate unified AI texture outputs. - Debug: Enables debug logging and debug texture output. - CorrectAspectRatio: Whether to correct aspect ratio for non-square inputs. - ZScale: Scale factor for Z coordinate of points. Outputs: - OutputTexture: The input texture (passed through). - DetectionsBuffer: A structured buffer containing points for detected faces. - FaceData: A dictionary containing detailed data for each detected face (bounding box coordinates, keypoint positions, confidence scores). - FaceCount: The number of faces detected. - UpdateCount: Increments each time a new frame is processed. - DebugTexture: A debug visualization texture showing detected faces with bounding boxes and keypoints (only when Debug is enabled). | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/FaceDetection.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.FaceLandmarkDetection` | Detects face landmarks in an image using MediaPipe Face Mesh. This operator uses the MediaPipe Face Mesh task to detect faces and their landmarks (468 or 478 points per face) in the input image. It outputs the detected landmarks as a buffer of points and a dictionary of face data. Inputs: - InputTexture: The image to process. - EnableDetection: Enables or disables face detection. - MaxFaces: Maximum number of faces to detect. - ShowLandmarks: Whether to include landmark points in the output buffer. - LandmarkSize: Size of the landmark points in the output buffer. - LandmarkColor: Color of the landmark points. - LandmarkScale: Scale factor for the landmark positions (useful for 3D visualization). - NormalizeLandmarks: Whether to normalize landmark coordinates to [0, 1] range. - MinFaceDetectionConfidence: Minimum confidence score for face detection to be considered successful. - MinFacePresenceConfidence: Minimum confidence score for face presence. - MinTrackingConfidence: Minimum confidence score for face tracking. - Debug: Enables debug logging. Outputs: - OutputTexture: The input texture (passed through). - PointBuffer: A structured buffer containing points for detected face landmarks. - FaceData: A dictionary containing detailed data for each detected face (landmark positions, blendshapes, transformation matrices). - FaceCount: The number of faces detected. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/FaceLandmarkDetection.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.GestureRecognition` | Recognizes hand gestures in an image using MediaPipe Gesture Recognition. This operator uses MediaPipe Gesture Recognition task to detect hands and recognize gestures (e.g., "Thumb_Up", "Victory", "Closed_Fist") in the input image. It outputs the recognized gesture name and confidence score. Inputs: - InputTexture: The image to process. - Enabled: Enables or disables gesture recognition. - MaxHands: Maximum number of hands to detect. - MinHandDetectionConfidence: Minimum confidence score for hand detection to be considered successful. - MinHandPresenceConfidence: Minimum confidence score for hand presence to be considered successful. - UseCombinedAIOutput: Whether to generate unified AI texture outputs. - Debug: Enables debug visualization with debug texture output. - CategoryAllowlist: Comma-separated list of category names to allow (e.g. "Thumb_Up"). - CategoryDenylist: Comma-separated list of category names to deny (e.g. "Thumb_Up"). - CorrectAspectRatio: Whether to correct aspect ratio for non-square inputs. - ZScale: Scale factor for Z coordinate of points. Outputs: - OutputTexture: The input texture (passed through). - DebugTexture: Debug visualization texture showing hand landmarks and connections when Debug is enabled. - RecognizedGestures: A list of recognized gesture names (e.g., "Thumb_Up"). - PrimaryGesture: The name of the primary recognized gesture. - Confidence: The confidence score of the recognized gesture. - HandCount: The number of hands detected. - UpdateCount: Increments each time a new frame is processed. - Handedness: A list of handedness (Left/Right) for detected hands. - WorldLandmarksBuffer: A structured buffer containing points for detected hand landmarks in world coordinates. - PointBuffer: A structured buffer containing points for visualization. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/GestureRecognition.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.HandLandmarkDetection` | Detects hand landmarks in an image using MediaPipe Hand Landmark Detection. This operator uses MediaPipe Hand Landmark Detection task to detect hands and their landmarks (21 points per hand) in the input image. It outputs detected landmarks as a buffer of points and a dictionary of hand data. Inputs: - InputTexture: The image to process. - Enabled: Enables or disables hand detection. - MaxHands: Maximum number of hands to detect. - MinHandDetectionConfidence: Minimum confidence score for hand detection to be considered successful. - MinHandPresenceConfidence: Minimum confidence score for hand presence to be considered successful. - MinTrackingConfidence: Minimum confidence score for hand tracking. - Debug: Enables debug visualization with debug texture output. - CorrectAspectRatio: Whether to correct aspect ratio for non-square inputs. - ZScale: Scale factor for Z coordinate of points. Outputs: - OutputTexture: The input texture (passed through). - DebugTexture: Debug visualization texture showing hand landmarks and connections when Debug is enabled. - HandCount: The number of hands detected. - UpdateCount: Increments each time a new frame is processed. - WorldLandmarksBuffer: A structured buffer containing world landmarks. - PointBuffer: A structured buffer containing points for detected hand landmarks. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/HandLandmarkDetection.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.ImageSegmentation` | Segments an image using MediaPipe Image Segmentation. This operator uses MediaPipe Image Segmentation task to partition input image into different regions (e.g., background, person, hair, clothing). It outputs segmentation masks and data that can be used for compositing or analysis. Inputs: - InputTexture: The image to process. - Enabled: Enables or disables image segmentation. - Model: Selects the segmentation model to use (e.g., SelfieSegmenter, DeepLabV3). - SelectedCategories: Comma-separated list of category indices to include in the mask. - Debug: Enables debug logging and debug texture output. - CategoryAllowlist: Comma-separated list of category names to allow (e.g. "person, cup"). Outputs: - OutputTexture: The input texture (passed through). - MaskTexture: A texture where each pixel value corresponds to a category index. - UpdateCount: Increments each time a new frame is processed. - Confidence: The confidence score of the segmentation. - CategoryMask: A texture where each pixel value corresponds to a category index. - ConfidenceMask: A texture containing confidence scores for each pixel. - DebugTexture: Debug visualization texture. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/ImageSegmentation.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.ObjectDetection` | Detects objects in an image using MediaPipe Object Detection. This operator uses MediaPipe Object Detection task to detect objects in the input image. It outputs detected objects as a buffer of points (bounding box corners) and a dictionary of object data. Inputs: - InputTexture: The image to process. - Enabled: Enables or disables object detection. - MaxObjects: Maximum number of objects to detect. - MinDetectionConfidence: Minimum confidence score for an object to be detected. - Model: Selects the object detection model to use. - Debug: Enables debug visualization with debug texture output. - CategoryAllowlist: Comma-separated list of category names to allow (e.g. "person, cup"). - CategoryDenylist: Comma-separated list of category names to deny (e.g. "person, cup"). - CorrectAspectRatio: Whether to correct aspect ratio for non-square inputs. - ZScale: Scale factor for Z coordinate of points. Outputs: - OutputTexture: The input texture (passed through). - DebugTexture: Debug visualization texture showing bounding boxes and labels when Debug is enabled. - ObjectCount: The number of objects detected. - UpdateCount: Increments each time a new frame is processed. - PointBuffer: A structured buffer containing points for detected objects (bounding box corners). - ObjectData: A dictionary containing detailed data for each detected object (bounding box coordinates, category name, confidence score). - ActiveCategories: A list of currently active detection categories. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/ObjectDetection.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.video.mediapipe.PoseLandmarkDetection` | Detects pose landmarks in an image using MediaPipe Pose Landmark Detection. This operator uses MediaPipe Pose Landmark Detection task to detect human poses and their landmarks (33 points per pose) in the input image. It outputs detected landmarks as a buffer of points and a dictionary of pose data. Inputs: - InputTexture: The image to process. - Enabled: Enables or disables pose detection. - MaxPoses: Maximum number of poses to detect. - MinPoseDetectionConfidence: Minimum confidence score for pose detection to be considered successful. - MinPosePresenceConfidence: Minimum confidence score for pose presence to be considered successful. - MinTrackingConfidence: Minimum confidence score for pose tracking. - Debug: Enables debug visualization with debug texture output. - CorrectAspectRatio: Whether to correct aspect ratio for non-square inputs. - ZScale: Scale factor for Z coordinate of points. Outputs: - OutputTexture: The input texture (passed through). - DebugTexture: Debug visualization texture showing pose landmarks and connections when Debug is enabled. - PoseCount: The number of poses detected. - UpdateCount: Increments each time a new frame is processed. - WorldLandmarksBuffer: A structured buffer containing world landmarks. - PointBuffer: A structured buffer containing points for detected pose landmarks. - SegmentationMask: A texture containing the segmentation mask. | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/video/mediapipe/PoseLandmarkDetection.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.websocket.WebSocketClient` | Connects to a WebSocket server to enable real-time, two-way communication. This operator establishes a client connection to a WebSocket server, allowing you to send and receive messages. It's useful for interacting with web-based applications, APIs, or other systems that use the WebSocket protocol. Tips: - The Url should start with 'ws://' or 'wss://' (for secure connections). - Use SendOnChange or SendTrigger to control when messages are sent. - Received messages can be accessed as a raw string, a list of lines, or parsed into a dictionary or list of parts, depending on the ParsingMode. AKA: websocket, socket, web, real-time, client | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/websocket/WebSocketClient.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.io.websocket.WebSocketServer` | Creates a WebSocket server to enable real-time, two-way communication with web clients. This operator is ideal for building interactive web-based UIs, remote controls, or dashboards that can send data to and receive data from T3. It can broadcast messages to all connected clients and parse incoming messages from them. Tips: - Use Listen to start and stop the server. - The server address will be ws://[LocalIpAddress]:[Port]/[Path]. - Messages sent from T3 via the Message input are broadcast to all connected clients. - Incoming messages from clients are available on the LastReceived... outputs. AKA: websocket, server, network, web, socket, real-time | in: none; out: none | C# `None`; .t3 `None`; docs `.help/docs/operators/lib/io/websocket/WebSocketServer.md` | D | Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation. |
| `Lib.string.buffers.convert.StringBuilderToString` | Converts the 'Builder' output of a [StringBuilder] or [BuildRandomString] to a string | in: InputBuffer:StringBuilder; out: String:string | C# `Operators/Lib/string/buffers/convert/StringBuilderToString.cs`; .t3 `Operators/Lib/string/buffers/convert/StringBuilderToString.t3`; docs `.help/docs/operators/lib/string/buffers/convert/StringBuilderToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.buffers.transform.StringInsert` | Can replace and move characters to any position within a string | in: Insertion:string, Original:string, Position:int, UseModuloPosition:bool; out: Result:string | C# `Operators/Lib/string/buffers/transform/StringInsert.cs`; .t3 `Operators/Lib/string/buffers/transform/StringInsert.t3`; docs `.help/docs/operators/lib/string/buffers/transform/StringInsert.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.combine.BlendStrings` | Blends two strings with a scrambling animation similar to an airport flight display board. | in: Blend:float, BlendSpread:float, Characters:string, InputStrings:string, InputTextA:string, InputTextB:string, MaxLength:int, Scramble:float, ScrambleSeed:int; out: Result:string | C# `Operators/Lib/string/combine/BlendStrings.cs`; .t3 `Operators/Lib/string/combine/BlendStrings.t3`; docs `.help/docs/operators/lib/string/combine/BlendStrings.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.combine.CombineStrings` | Combines any number with strings with the provided optional separator. To add line breaks use "\n". Alternative titles: Concat, Join | in: Input:string, Separator:string; out: Result:string | C# `Operators/Lib/string/combine/CombineStrings.cs`; .t3 `Operators/Lib/string/combine/CombineStrings.t3`; docs `.help/docs/operators/lib/string/combine/CombineStrings.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.combine.FloatListToString` | Converts a list of float numbers to String | in: Format:string, Separator:string, Value:List<float>; out: Output:string | C# `Operators/Lib/string/combine/FloatListToString.cs`; .t3 `Operators/Lib/string/combine/FloatListToString.t3`; docs `.help/docs/operators/lib/string/combine/FloatListToString.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.combine.StringRepeat` | Repeats a string multiple times. | in: Count:int, Fragment:string; out: Result:string | C# `Operators/Lib/string/combine/StringRepeat.cs`; .t3 `Operators/Lib/string/combine/StringRepeat.t3`; docs `.help/docs/operators/lib/string/combine/StringRepeat.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.convert.FloatToString` | Converts a float value into a string. You can use the .net string format descriptor for various effects, like... Here are some examples for the value 1.2345 {0:P1} -> 123.45% {0:000.000} -> 001.234 {0:0} -> 1 {0:G3} -> 1.23 need {0:1.2} cookies  -> need 1.2 cookies | in: Format:string, Value:float; out: Output:string | C# `Operators/Lib/string/convert/FloatToString.cs`; .t3 `Operators/Lib/string/convert/FloatToString.t3`; docs `.help/docs/operators/lib/string/convert/FloatToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.convert.IntToString` | Converts an [IntValue] to a string. Tip: You can override the string formatting to something like... "{0:0} times" add prefixes or suffixes. | in: Format:string, Value:int; out: Output:string | C# `Operators/Lib/string/convert/IntToString.cs`; .t3 `Operators/Lib/string/convert/IntToString.t3`; docs `.help/docs/operators/lib/string/convert/IntToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.convert.Vec3ToString` | Converts a Vector3 into a string. Formats examples: ({0:F2}, {1:F2}, {2:F2}) Result: (0.00, 1.00, 2.00) X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2} Result: X:    1.50 Y:    2.50 Z:    3.50 | in: Format:string, Vector:Vector3; out: Output:string | C# `Operators/Lib/string/convert/Vec3ToString.cs`; .t3 `Operators/Lib/string/convert/Vec3ToString.t3`; docs `.help/docs/operators/lib/string/convert/Vec3ToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.CountDown` | Prints a formatted string counting down a duration or time to/since a fixed date. | in: Format:string, LaunchTime:string; out: Output:string | C# `Operators/Lib/string/datetime/CountDown.cs`; .t3 `Operators/Lib/string/datetime/CountDown.t3`; docs `.help/docs/operators/lib/string/datetime/CountDown.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.DateTimeToFloat` | Converts a DateTime into different float values like normalized time of day (between 0 ... 1), day of the year, etc. | in: HourOffset:float, OutputMapping:int, Value:DateTime; out: Output:float | C# `Operators/Lib/string/datetime/DateTimeToFloat.cs`; .t3 `Operators/Lib/string/datetime/DateTimeToFloat.t3`; docs `.help/docs/operators/lib/string/datetime/DateTimeToFloat.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.DateTimeToString` | Uses [NowAsDateTime] output to convert it into a string | in: Format:string, Value:DateTime; out: Output:string | C# `Operators/Lib/string/datetime/DateTimeToString.cs`; .t3 `Operators/Lib/string/datetime/DateTimeToString.t3`; docs `.help/docs/operators/lib/string/datetime/DateTimeToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.NowAsDateTime` | Returns the current system time. | in: none; out: Output:DateTime | C# `Operators/Lib/string/datetime/NowAsDateTime.cs`; .t3 `Operators/Lib/string/datetime/NowAsDateTime.t3`; docs `.help/docs/operators/lib/string/datetime/NowAsDateTime.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.StringToDateTime` | Tries to parse the incoming string as a DateTime. | in: DateString:string; out: Output:DateTime | C# `Operators/Lib/string/datetime/StringToDateTime.cs`; .t3 `Operators/Lib/string/datetime/StringToDateTime.t3`; docs `.help/docs/operators/lib/string/datetime/StringToDateTime.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.datetime.TimeToString` | Converts [Time] float output and similar to a string | in: Input:float; out: Result:string | C# `Operators/Lib/string/datetime/TimeToString.cs`; .t3 `Operators/Lib/string/datetime/TimeToString.t3`; docs `.help/docs/operators/lib/string/datetime/TimeToString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.list.JoinStringList` | Joins the members of a string with a separator. | in: Input:List<string>, Separator:string; out: Result:string | C# `Operators/Lib/string/list/JoinStringList.cs`; .t3 `Operators/Lib/string/list/JoinStringList.t3`; docs `.help/docs/operators/lib/string/list/JoinStringList.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.list.KeepStrings` | Collects the input string to a list. | in: ClearTrigger:bool, Index:int, InsertMode:int, InsertTrigger:bool, MaxCount:int, NewString:string, OnlyOnChanges:bool; out: Count:int, InsertTimes:List<float>, Strings:List<string> | C# `Operators/Lib/string/list/KeepStrings.cs`; .t3 `Operators/Lib/string/list/KeepStrings.t3`; docs `.help/docs/operators/lib/string/list/KeepStrings.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.list.PickStringFromList` | Can switch between different strings if the 'Fragments' output of a [SplitString] operator is used as an input | in: Index:int, Input:List<string>; out: Count:int, Selected:string | C# `Operators/Lib/string/list/PickStringFromList.cs`; .t3 `Operators/Lib/string/list/PickStringFromList.t3`; docs `.help/docs/operators/lib/string/list/PickStringFromList.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.list.SplitString` | Splits a string by the provided separation character. Returns a List<string> that can be parsed with [PickFromStringList]. | in: Split:string, String:string; out: Count:int, Fragments:List<string> | C# `Operators/Lib/string/list/SplitString.cs`; .t3 `Operators/Lib/string/list/SplitString.t3`; docs `.help/docs/operators/lib/string/list/SplitString.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.list.StringLength` | Counts the characters in a string and outputs the amount as a float | in: InputString:string; out: Length:int | C# `Operators/Lib/string/list/StringLength.cs`; .t3 `Operators/Lib/string/list/StringLength.t3`; docs `.help/docs/operators/lib/string/list/StringLength.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.list.ZipStringList` | Zip two lists of strings, if the list sizes are differents, additional values of the longest are ignored | in: StringsOne:List<string>, StringsTwo:List<string>; out: Output:List<string> | C# `Operators/Lib/string/list/ZipStringList.cs`; .t3 `Operators/Lib/string/list/ZipStringList.t3`; docs `.help/docs/operators/lib/string/list/ZipStringList.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.logic.FilePathParts` | Extracts directory, filename and extension from a filepath and checks if filepath exists. | in: FilePath:string; out: Directory:string, Extension:string, FileExists:bool, FilenameWithoutExtension:string | C# `Operators/Lib/string/logic/FilePathParts.cs`; .t3 `Operators/Lib/string/logic/FilePathParts.t3`; docs `.help/docs/operators/lib/string/logic/FilePathParts.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.logic.HasStringChanged` | Returns true if the connected string attribute has changed. | in: Value:string; out: HasChanged:bool | C# `Operators/Lib/string/logic/HasStringChanged.cs`; .t3 `Operators/Lib/string/logic/HasStringChanged.t3`; docs `.help/docs/operators/lib/string/logic/HasStringChanged.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.logic.PickString` | Picks a string of multiple connected. | in: Index:int, Input:string; out: Selected:string | C# `Operators/Lib/string/logic/PickString.cs`; .t3 `Operators/Lib/string/logic/PickString.t3`; docs `.help/docs/operators/lib/string/logic/PickString.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.logic.PickStringPart` | Gets lines, words, or characters from the input string. Good for typewriter effects, etc. Useful combinations: [ReadFile] [RequestUrl] [GetAttributeFromJsonString] | in: FragmentCount:int, FragmentStart:int, InputText:string, SplitInto:int; out: Fragments:string, TotalCount:int | C# `Operators/Lib/string/logic/PickStringPart.cs`; .t3 `Operators/Lib/string/logic/PickStringPart.t3`; docs `.help/docs/operators/lib/string/logic/PickStringPart.md` | B | Portable text/list operation; Vuo type/list edge cases need contract. |
| `Lib.string.random.AnimRandomString` | Returns a random word per beat. | in: Category:int, Rate:float; out: Fragments:string | C# `Operators/Lib/string/random/AnimRandomString.cs`; .t3 `Operators/Lib/string/random/AnimRandomString.t3`; docs `.help/docs/operators/lib/string/random/AnimRandomString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.random.BuildRandomString` | Produces a wide selection of text writer effects that can be linked to AudioReactions. Interesting setups are [MockStrings]->[GetStringPart]->[ScrambleString] | in: Clear:bool, Insert:bool, InsertString:string, JumpToRandomPos:bool, MaxLength:int, OverrideBuilder:StringBuilder, OverwriteOffset:int, Scramble:bool, ScrambleRatio:float, ScrambleSeed:int, Separator:string, WrapLineColumn:int, WrapLines:int, WriteMode:int; out: Builder:StringBuilder, Result:string | C# `Operators/Lib/string/random/BuildRandomString.cs`; .t3 `Operators/Lib/string/random/BuildRandomString.t3`; docs `.help/docs/operators/lib/string/random/BuildRandomString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.random.MockStrings` | A small selection of strings of different categories. | in: Category:int; out: Result:string | C# `Operators/Lib/string/random/MockStrings.cs`; .t3 `Operators/Lib/string/random/MockStrings.t3`; docs `.help/docs/operators/lib/string/random/MockStrings.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.search.IndexOf` | Searches the original string for a search pattern and outputs the position at which the pattern was found | in: OriginalString:string, SearchPattern:string; out: Index:int | C# `Operators/Lib/string/search/IndexOf.cs`; .t3 `Operators/Lib/string/search/IndexOf.t3`; docs `.help/docs/operators/lib/string/search/IndexOf.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.search.SearchAndReplace` | Looks for characters or strings within another string and returns a new string with the matches replaced. | in: OriginalString:string, Replace:string, SearchPattern:string, UseRegex:bool; out: Result:string | C# `Operators/Lib/string/search/SearchAndReplace.cs`; .t3 `Operators/Lib/string/search/SearchAndReplace.t3`; docs `.help/docs/operators/lib/string/search/SearchAndReplace.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.search.SubString` | Returns a substring of the connect string. This can be useful for trimming a string to a maximum length. | in: InputText:string, Length:int, Start:int; out: Result:string | C# `Operators/Lib/string/search/SubString.cs`; .t3 `Operators/Lib/string/search/SubString.t3`; docs `.help/docs/operators/lib/string/search/SubString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.transform.ChangeCase` | Changes a case of string to upper or lower case. | in: InputText:string, Mode:int; out: Result:string | C# `Operators/Lib/string/transform/ChangeCase.cs`; .t3 `Operators/Lib/string/transform/ChangeCase.t3`; docs `.help/docs/operators/lib/string/transform/ChangeCase.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |
| `Lib.string.transform.WrapString` | Wraps a string at a column width | in: InputText:string, Mode:int, WrapColumn:int; out: Result:string | C# `Operators/Lib/string/transform/WrapString.cs`; .t3 `Operators/Lib/string/transform/WrapString.t3`; docs `.help/docs/operators/lib/string/transform/WrapString.md` | A | Pure text/value transform or predicate; good Vuo C node/subcomposition candidate. |

## High-Value Node Cards

## AudioReaction

- TiXL full path: `Lib.io.audio.AudioReaction`
- Namespace: `Lib.io.audio`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/audio/AudioReaction.cs`
  - .t3 defaults: `Operators/Lib/io/audio/AudioReaction.t3`
  - docs: `.help/docs/operators/lib/io/audio/AudioReaction.md`
  - related shader / helper source: `T3.Core.Audio.AudioAnalysis`, `AudioAnalysisContext`, `MathUtils`; skips `__MotionBlurPass` and uses `EvaluationContext.LocalFxTime`.
- Purpose: Creates audio reaction from audio inputs.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Amplitude: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Bias: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - InputBand: int, default=4, enum=['RawFft', 'NormalizedFft', 'FrequencyBands', 'FrequencyBandsPeaks', 'FrequencyBandsAttacks'], semantic role Unknown from slot name/docs unless stated.
  - MinTimeBetweenHits: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Output: int, default=Unknown, enum=['Pulse', 'TimeSinceHit', 'Count', 'Level', 'AccumulatedLevel'], semantic role Unknown from slot name/docs unless stated.
  - Reset: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Threshold: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - WindowCenter: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - WindowEdge: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - WindowWidth: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - HitCount: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Level: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - WasHit: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Reads TiXL global audio analysis buffers, windows selected FFT/band data, threshold-detects hits with minimum hit spacing, tracks hit count/accumulated level, and skips motion-blur passes.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.audio` (receive/send/play/list/analyze). TiXL audio analysis/player uses `T3.Core.Audio` and for players BASS/AudioEngine semantics; realtime/device permission risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has audio analysis, but TiXL analysis buffers/timing differ.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## AudioWaveform

- TiXL full path: `Lib.io.audio._.AudioWaveform`
- Namespace: `Lib.io.audio._`
- Clone status: source_verified_interface_only
- Source evidence:
  - C#: `Operators/Lib/io/audio/_/AudioWaveform.cs`
  - .t3 defaults: `Operators/Lib/io/audio/_/AudioWaveform.t3`
  - docs: `None`
  - related shader / helper source: `T3.Core.Audio.AudioAnalysis` waveform buffers; exact buffer lifetime Unknown.
- Purpose: Unknown.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - High: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Left: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Low: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Mid: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Right: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Unknown beyond source/docs evidence.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.audio` (receive/send/play/list/analyze). TiXL audio analysis/player uses `T3.Core.Audio` and for players BASS/AudioEngine semantics; realtime/device permission risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has audio analysis, but TiXL analysis buffers/timing differ.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## AudioFrequencies

- TiXL full path: `Lib.io.audio._.AudioFrequencies`
- Namespace: `Lib.io.audio._`
- Clone status: source_verified_interface_only
- Source evidence:
  - C#: `Operators/Lib/io/audio/_/AudioFrequencies.cs`
  - .t3 defaults: `Operators/Lib/io/audio/_/AudioFrequencies.t3`
  - docs: `None`
  - related shader / helper source: `T3.Core.Audio.AudioAnalysis` FFT/frequency buffers; exact mode enum needs source audit.
- Purpose: Unknown.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Mode: int, default=Unknown, enum=['RawFft', 'NormalizedFft', 'FrequencyBands', 'FrequencyBandsPeaks', 'FrequencyBandsAttacks'], semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - FftBuffer: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Unknown beyond source/docs evidence.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.audio` (receive/send/play/list/analyze). TiXL audio analysis/player uses `T3.Core.Audio` and for players BASS/AudioEngine semantics; realtime/device permission risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has audio analysis, but TiXL analysis buffers/timing differ.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## AudioPlayer

- TiXL full path: `Lib.io.audio.AudioPlayer`
- Namespace: `Lib.io.audio`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/io/audio/AudioPlayer.cs`
  - .t3 defaults: `Operators/Lib/io/audio/AudioPlayer.t3`
  - docs: `.help/docs/operators/lib/io/audio/AudioPlayer.md`
  - related shader / helper source: `T3.Core.Audio.AudioEngine`, `AudioPlayerUtils`, `AdsrCalculator`, BASS-backed stereo operator stream.
- Purpose: An audio player using BASS audio library. Features include: - [Playback Control]: Independent play, pause, and stop functionality with seek support - [Stereo Panning]: Position audio in the stereo field from left to right - [ADSR Envelope]: Optional amplitude envelope for dynamic volume shaping - [Speed Control]: Playback speed multiplier (affects pitch)
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - AudioFile: string, default='', semantic role Unknown from slot name/docs unless stated.
  - Duration: float, default=1.0, semantic role Unknown from slot name/docs unless stated.
  - Envelope: Vector4, default={'X': 0.1, 'Y': 0.1, 'Z': 1.0, 'W': 0.1}, semantic role Unknown from slot name/docs unless stated.
  - Mute: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - Panning: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - PauseAudio: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - PlayAudio: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - Seek: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - Speed: float, default=1.0, semantic role Unknown from slot name/docs unless stated.
  - StopAudio: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - TriggerMode: int, default=0, semantic role Unknown from slot name/docs unless stated.
  - UseEnvelope: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - Volume: float, default=1.0, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - GetLevel: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - IsPlaying: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Updates a per-operator stereo audio stream via TiXL AudioEngine, handles play/stop/pause, seek, speed, pan, mute, volume, optional ADSR envelope, and reports playing state/level.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.audio` (receive/send/play/list/analyze). TiXL audio analysis/player uses `T3.Core.Audio` and for players BASS/AudioEngine semantics; realtime/device permission risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL/BASS audio engine or spatial mixer dependency.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## AudioToneGenerator

- TiXL full path: `Lib.io.audio.AudioToneGenerator`
- Namespace: `Lib.io.audio`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/io/audio/AudioToneGenerator.cs`
  - .t3 defaults: `Operators/Lib/io/audio/AudioToneGenerator.t3`
  - docs: `.help/docs/operators/lib/io/audio/AudioToneGenerator.md`
  - related shader / helper source: `ManagedBass`, `ManagedBass.Mix`, `AudioEngine`, procedural tone stream, `AdsrCalculator`.
- Purpose: Generates test tone direct to the operator mixer for audio testing and debugging.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Duration: float, default=1.0, semantic role Unknown from slot name/docs unless stated.
  - Envelope: Vector4, default={'X': 1.0, 'Y': 1.0, 'Z': 1.0, 'W': 1.0}, semantic role Unknown from slot name/docs unless stated.
  - Frequency: float, default=440.0, semantic role Unknown from slot name/docs unless stated.
  - Mute: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - Trigger: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - TriggerMode: int, default=0, semantic role Unknown from slot name/docs unless stated.
  - Volume: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - WaveformType: int, default=2, enum=['Sine = 0', 'Square = 1', 'Sawtooth = 2', 'Triangle = 3', 'WhiteNoise = 4', 'PinkNoise = 5'], semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - GetLevel: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - IsPlaying: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Creates/reuses a procedural tone stream, applies waveform/frequency/volume/mute/ADSR trigger or gate behavior, and reports active level.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.audio` (receive/send/play/list/analyze). TiXL audio analysis/player uses `T3.Core.Audio` and for players BASS/AudioEngine semantics; realtime/device permission risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL/BASS audio engine or spatial mixer dependency.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## MidiInput

- TiXL full path: `Lib.io.midi.MidiInput`
- Namespace: `Lib.io.midi`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/midi/MidiInput.cs`
  - .t3 defaults: `Operators/Lib/io/midi/MidiInput.t3`
  - docs: `.help/docs/operators/lib/io/midi/MidiInput.md`
  - related shader / helper source: `NAudio.Midi`, `Operators.Utils.MidiConnectionManager`, `T3.Core.IO.SimulatedIoBus`, `T3.Core.Animation`, `T3.Core.Stats`.
- Purpose: Provides input from connected MIDI devices. By default, the MIDI range from 0 to 127 is mapped to an output range from 0 to 1, which can be adjusted with the parameters. For smoothing the output, damping is enabled by default. Reduce the damping parameter to improve latency. If you want to react to a range for controls (i.e., to the range of keys on a keyboard), you can use the ControlRange parameter. If this is something other than [0, 0], we map its range to the OutputRange (e.g., you could map two octaves of your keyboard to output values from 0 to 1). To support controllers that have controllers and note buttons with overlapping control IDs (like the APC Mini), we distinguish between different event types. Useful combinations: [FloatToInt] [BooltoFloat] [BoolToInt] [FlipFlop] [HasBooleanChanged] [TriggerAnim] Also see: [MouseInput] [KeyboardInput]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Channel: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Control: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ControlRange: Int2, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Damping: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - DefaultOutputValue: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Device: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - EventType: int, default=Unknown, enum=['All', 'Notes', 'ControllerChanges', 'MidiTime', 'MidiEvent'], semantic role Unknown from slot name/docs unless stated.
  - LastKnownControllerValue: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - OutputRange: Vector2, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - PrintLogMessages: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ResetToDefaultTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - TeachTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Range: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - WasHit: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime MIDI device/event behavior; exact note/controller/sysex packing is in the C# node/helper and should be fixture-tested against Vuo MIDI messages.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.midi` (receive/send/filter/make/get/list). Needs TiXL channel/controller conventions and event timing mapped to Vuo trigger events.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## MidiOutput

- TiXL full path: `Lib.io.midi.MidiOutput`
- Namespace: `Lib.io.midi`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/midi/MidiOutput.cs`
  - .t3 defaults: `Operators/Lib/io/midi/MidiOutput.t3`
  - docs: `.help/docs/operators/lib/io/midi/MidiOutput.md`
  - related shader / helper source: `NAudio.Midi`, `Operators.Utils.MidiConnectionManager`, `T3.Core.Animation`, `T3.Core.Utils`.
- Purpose: Send notes or controller change events to the selected device. This operator is experimental. The velocity is given as a normalized float value between 0 and 1.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ChannelNumber: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Device: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - DurationInSecs: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - NoteOrController: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendMode: int, default=Unknown, enum=['Notes_FixedDuration', 'Note_WhileTriggered', 'ControllerChange', 'StartSequence', 'StopSequence', 'ContinueSequence', 'TempoEvent'], semantic role Unknown from slot name/docs unless stated.
  - TriggerSend: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Velocity: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Velocity127: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime MIDI device/event behavior; exact note/controller/sysex packing is in the C# node/helper and should be fixture-tested against Vuo MIDI messages.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.midi` (receive/send/filter/make/get/list). Needs TiXL channel/controller conventions and event timing mapped to Vuo trigger events.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## MidiNoteOutput

- TiXL full path: `Lib.io.midi.MidiNoteOutput`
- Namespace: `Lib.io.midi`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/midi/MidiNoteOutput.cs`
  - .t3 defaults: `Operators/Lib/io/midi/MidiNoteOutput.t3`
  - docs: `.help/docs/operators/lib/io/midi/MidiNoteOutput.md`
  - related shader / helper source: `NAudio.Midi`, `Operators.Utils.MidiConnectionManager`, `T3.Core.Animation`, `T3.Core.Utils`.
- Purpose: Send MIDI notes to the selected device on the given channel. The velocity is provided either as an integer between 0 and 127 or as a normalized float value between 0.0 and 1.0 When NoteWhileTriggered is selected, a note is played while the TriggerSend input is boolean "true". When it turns "false" a NoteOff command is sent to the receiving device. When NoteFixedDuration is selected, a note starts when the TriggerSend input is boolean "true" and ends after the time in DurationInSecs has passed or if another new note is triggered (to prevent overlapping or hanging notes). This node can be used in combination with [MidiControlOutput], [MidiPitchbendOutput] and [MidiTriggerOutput] to create generative music or control hardware.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ChannelNumber: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Device: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - DurationInSecs: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - NoteNumber: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendMode: int, default=Unknown, enum=['Note_FixedDuration', 'Note_WhileTriggered'], semantic role Unknown from slot name/docs unless stated.
  - TriggerSend: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - UseVelocityFloat: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Velocity: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - VelocityFloat: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime MIDI device/event behavior; exact note/controller/sysex packing is in the C# node/helper and should be fixture-tested against Vuo MIDI messages.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.midi` (receive/send/filter/make/get/list). Needs TiXL channel/controller conventions and event timing mapped to Vuo trigger events.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## MidiControlOutput

- TiXL full path: `Lib.io.midi.MidiControlOutput`
- Namespace: `Lib.io.midi`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/midi/MidiControlOutput.cs`
  - .t3 defaults: `Operators/Lib/io/midi/MidiControlOutput.t3`
  - docs: `.help/docs/operators/lib/io/midi/MidiControlOutput.md`
  - related shader / helper source: `NAudio.Midi`, `Operators.Utils.MidiConnectionManager`, `T3.Core.Utils`.
- Purpose: Send 7 bit ControlChange or Channel Pressure events to the selected device. The value is given either as an integer value between 0 and 127 or as a normalized float value between 0.0 and 1.0. Values can either be sent continuously (one value per frame) or only when the trigger input is sent a (temporary) boolean "true" value. Sending continuously can lead to flooding of the receiving device, so if possible, use the trigger option. You can for instance use an [AnimValue]s "WasHit" output as the trigger and set it to a frequency that is sufficiently smooth for your purpose to reduce the load. This node can be used in combination with [MidiNoteOutput] [MidiPitchbendOutput] and [MidiTriggerOutput] to create generative music or control hardware. See the [MidiNoteOutputExample] AKA: aftertouch, pressure, cc, controller
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - CCorPressure: int, default=Unknown, enum=['ContinuosController_CC', 'ChannelPressure'], semantic role Unknown from slot name/docs unless stated.
  - ChannelNumber: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ControllerNumber: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Device: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendMode: int, default=Unknown, enum=['SendContinuously', 'SendWhenTriggered'], semantic role Unknown from slot name/docs unless stated.
  - TriggerSend: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - UseValueFloat: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Value: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ValueFloat: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime MIDI device/event behavior; exact note/controller/sysex packing is in the C# node/helper and should be fixture-tested against Vuo MIDI messages.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.midi` (receive/send/filter/make/get/list). Needs TiXL channel/controller conventions and event timing mapped to Vuo trigger events.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## OscInput

- TiXL full path: `Lib.io.osc.OscInput`
- Namespace: `Lib.io.osc`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/osc/OscInput.cs`
  - .t3 defaults: `Operators/Lib/io/osc/OscInput.t3`
  - docs: `.help/docs/operators/lib/io/osc/OscInput.md`
  - related shader / helper source: `Rug.Osc`, `T3.Core.IO.OscConnectionManager`, `T3.Core.IO.SimulatedIoBus`, `System.Net.Sockets`, `System.Text.RegularExpressions`.
- Purpose: Receives OSC input from a connected server. In most scenarios, it is useful to record some input messages and then pick the relevant addresses by using a [SelectFloatFromDict] or [SelectVec2FromDict] operator. You might want to adjust the default OSC port in the settings window to see a visual indication of incoming messages in the timeline bar.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Address: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - FilterKeys: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - GroupKeysAsPaths: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - IsListening: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Port: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - PrintLogMessages: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SearchFilterKey: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SearchPattern: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - UseKeyValuePairs: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Contents: Dict<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Values: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - WasTrigger: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime OSC network receive/send behavior through TiXL OSC connection layer; address/value type conversion needs explicit fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.osc` (receive/send/make/get/filter/list). Network port/firewall and OSC argument typing are realtime/device concerns.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## OscOutput

- TiXL full path: `Lib.io.osc.OscOutput`
- Namespace: `Lib.io.osc`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/osc/OscOutput.cs`
  - .t3 defaults: `Operators/Lib/io/osc/OscOutput.t3`
  - docs: `.help/docs/operators/lib/io/osc/OscOutput.md`
  - related shader / helper source: `Rug.Osc` / `OscSender`; exact TiXL helper outside this node is Unknown.
- Purpose: Send out OSC signals to a receiver.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Address: string, default='/tooll3', semantic role Unknown from slot name/docs unless stated.
  - IntValues: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - IntValuesList: System.Collections.Generic.List<int>, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - IpAddress: string, default='127.0.0.1', semantic role Unknown from slot name/docs unless stated.
  - OnlySendChanges: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Port: int, default=9000, semantic role Unknown from slot name/docs unless stated.
  - Reconnect: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Strings: string, default='Hello, my name is ...', semantic role Unknown from slot name/docs unless stated.
  - Values: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - ValuesList: System.Collections.Generic.List<float>, default={'Values': []}, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime OSC network receive/send behavior through TiXL OSC connection layer; address/value type conversion needs explicit fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.osc` (receive/send/make/get/filter/list). Network port/firewall and OSC argument typing are realtime/device concerns.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Vuo has direct node set; realtime event/device mapping must be designed.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## VideoDeviceInput

- TiXL full path: `Lib.io.video.VideoDeviceInput`
- Namespace: `Lib.io.video`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/video/VideoDeviceInput.cs`
  - .t3 defaults: `Operators/Lib/io/video/VideoDeviceInput.t3`
  - docs: `.help/docs/operators/lib/io/video/VideoDeviceInput.md`
  - related shader / helper source: TiXL video capture/device layer (`T3.Core.Video`, DirectShow/Media Foundation details Unknown).
- Purpose: Captures live video from a connected device like a webcam, capture card, or NDI source. This operator provides access to video devices available on your system. You can select the device, resolution, and frame rate. It also allows for basic image manipulation like flipping and repositioning. Tips: - To save system resources, enable DeactivateWhenNotShowing. This will pause the capture when the operator is not being rendered. - Use OpenSettings to access the device-specific configuration dialog for more advanced options like exposure and color balance. - The Status output provides useful information for troubleshooting connection issues. AKA: webcam, camera, capture card, ndi, directshow, video input
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Active: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ApplyRotationData: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - CustomFps: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - CustomResolution: Int2, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - DeactivateWhenNotShowing: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - FlipHorizontally: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - FlipVertically: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - InputDeviceName: string, default='', semantic role Unknown from slot name/docs unless stated.
  - OpenSettings: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Reconnect: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Reposition: Vector2, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ResolutionFpsType: int, default=Unknown, enum=['DeviceDefault = 0', 'Custom = 1'], semantic role Unknown from slot name/docs unless stated.
  - Scale: Vector2, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Resolution: Int2, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Texture: Texture2D, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - UpdateCount: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime frame/file/stream/device behavior returning TiXL textures or resolution/update metadata; GPU texture ownership and camera permissions are first-class risks.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.video`; image texture/display via `vuo.image`. NDI/Spout/MediaPipe/SwiftCam are vendor/device-specific; camera permission/realtime frame delivery risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## VideoStreamInput

- TiXL full path: `Lib.io.video.VideoStreamInput`
- Namespace: `Lib.io.video`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/io/video/VideoStreamInput.cs`
  - .t3 defaults: `Operators/Lib/io/video/VideoStreamInput.t3`
  - docs: `.help/docs/operators/lib/io/video/VideoStreamInput.md`
  - related shader / helper source: Network video stream/decoder; exact backend Unknown from compact evidence.
- Purpose: Receives a video stream from a network source. This operator connects to a network video stream (e.g., RTSP, HTTP, RTMP) and provides the video as a texture. It is useful for bringing in live feeds from IP cameras, streaming servers, or other network video sources. Tips: - Use the Connect toggle to start and stop receiving the stream. - If the stream is interrupted, use the Reconnect trigger to try and establish the connection again. - The Status output provides valuable information for debugging connection issues. AKA: video stream, rtsp, rtmp, http, ip camera, network video
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Connect: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Reconnect: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Resolution: Int2, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Url: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime frame/file/stream/device behavior returning TiXL textures or resolution/update metadata; GPU texture ownership and camera permissions are first-class risks.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.video`; image texture/display via `vuo.image`. NDI/Spout/MediaPipe/SwiftCam are vendor/device-specific; camera permission/realtime frame delivery risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## PlayVideo

- TiXL full path: `Lib.io.video.PlayVideo`
- Namespace: `Lib.io.video`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/video/PlayVideo.cs`
  - .t3 defaults: `Operators/Lib/io/video/PlayVideo.t3`
  - docs: `.help/docs/operators/lib/io/video/PlayVideo.md`
  - related shader / helper source: Unknown.
- Purpose: Uses Windows Media Foundation to play a video file. To ensure seek precision while editing, it enforces seeking if timeline playback is paused. If timeline playback is running, it will only seek if the video playback drift exceeds the resync threshold. If this threshold is too small, playback will stutter. If it's excessively large, syncing might be off. [SetCommandTime] can be used to control / offset the video playback and speed: [PlayVideo] -> [Layer2D] -> [SetCommandTime] Important: Media Foundation returns textures in BGRA format. This might not work for some draw operators like [Layer2d]. Please insert a [ConvertFormat] operator to convert the format. Note: If this is flickering, you can try to deactivate Idle Motion from the timeline control bar.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - IsPreciseAtPlayback: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Loop: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - OverrideTimeInSecs: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Path: string, default='Examples:videos/spray-1080p.mp4', semantic role Unknown from slot name/docs unless stated.
  - ResyncThreshold: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Volume: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Duration: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - HasCompleted: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - UpdateCount: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Realtime frame/file/stream/device behavior returning TiXL textures or resolution/update metadata; GPU texture ownership and camera permissions are first-class risks.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.video`; image texture/display via `vuo.image`. NDI/Spout/MediaPipe/SwiftCam are vendor/device-specific; camera permission/realtime frame delivery risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - C: Mixed IO/control behavior; needs Vuo-specific event/device design.
- First implementation recommendation:
  - Prototype as Vuo composition/custom node only after realtime/render/event contract is explicit.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## NdiInput

- TiXL full path: `Lib.io.video.NdiInput`
- Namespace: `Lib.io.video`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `None`
  - .t3 defaults: `None`
  - docs: `.help/docs/operators/lib/io/video/NdiInput.md`
  - related shader / helper source: NDI operator package; vendor SDK/runtime dependency Unknown from checked evidence.
- Purpose: NDI live video input
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - Unknown / none in C# evidence.
- Runtime behavior:
  - Realtime frame/file/stream/device behavior returning TiXL textures or resolution/update metadata; GPU texture ownership and camera permissions are first-class risks.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.video`; image texture/display via `vuo.image`. NDI/Spout/MediaPipe/SwiftCam are vendor/device-specific; camera permission/realtime frame delivery risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## SpoutInput

- TiXL full path: `Lib.io.video.SpoutInput`
- Namespace: `Lib.io.video`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `None`
  - .t3 defaults: `None`
  - docs: `.help/docs/operators/lib/io/video/SpoutInput.md`
  - related shader / helper source: Spout operator package; Windows GPU interop dependency.
- Purpose: Spout live video input
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - Unknown / none in C# evidence.
- Runtime behavior:
  - Realtime frame/file/stream/device behavior returning TiXL textures or resolution/update metadata; GPU texture ownership and camera permissions are first-class risks.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo set: `vuo.video`; image texture/display via `vuo.image`. NDI/Spout/MediaPipe/SwiftCam are vendor/device-specific; camera permission/realtime frame delivery risk.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## ReadFile

- TiXL full path: `Lib.io.file.ReadFile`
- Namespace: `Lib.io.file`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/file/ReadFile.cs`
  - .t3 defaults: `Operators/Lib/io/file/ReadFile.t3`
  - docs: `.help/docs/operators/lib/io/file/ReadFile.md`
  - related shader / helper source: File system path/encoding helpers; exact encoding and error behavior Unknown.
- Purpose: Reads a file on the local disk and outputs the content as a string Opposite of: [WriteToFile] Useful combination: [PickStringPart] Also see: [RequestUrl] which adds online capabilities and [GetAttributeFromJsonString]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - FilePath: string, default='', semantic role Unknown from slot name/docs unless stated.
  - TriggerUpdate: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - File system read/write/list behavior; path resolution, encoding, and error reporting need source-backed fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.file`, `vuo.data`, `vuo.url`. File sandbox/path permissions and encoding/defaults need fixture.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - Unknown.

## WriteToFile

- TiXL full path: `Lib.io.file.WriteToFile`
- Namespace: `Lib.io.file`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/file/WriteToFile.cs`
  - .t3 defaults: `Operators/Lib/io/file/WriteToFile.t3`
  - docs: `.help/docs/operators/lib/io/file/WriteToFile.md`
  - related shader / helper source: File system path/encoding helpers; exact encoding and error behavior Unknown.
- Purpose: Writes the incoming string into a predefined file on the local disk Info: This operator is unable to create files. The file has to exist in order to write something into it. Depending on the user and operating system, Tooll might need admin privileges. Also see: [ReadFile] [RequestUrl] [FilesInFolder]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Content: string, default='', semantic role Unknown from slot name/docs unless stated.
  - Filepath: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - OutFilepath: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - File system read/write/list behavior; path resolution, encoding, and error reporting need source-backed fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.file`, `vuo.data`, `vuo.url`. File sandbox/path permissions and encoding/defaults need fixture.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - Unknown.

## FilesInFolder

- TiXL full path: `Lib.io.file.FilesInFolder`
- Namespace: `Lib.io.file`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/file/FilesInFolder.cs`
  - .t3 defaults: `Operators/Lib/io/file/FilesInFolder.t3`
  - docs: `.help/docs/operators/lib/io/file/FilesInFolder.md`
  - related shader / helper source: File system path/encoding helpers; exact encoding and error behavior Unknown.
- Purpose: Scans a folder for all existing files and creates a list that can be used with [PickFromStringList] It also counts files in a folder and outputs the amount as an integer. See the example [FilesInFolderExample] and [FadingSlideShow] for what might be a useful combination Other interesting related ops: [ReadFile] [RequestUrl] [PickStringPart] [WriteToFile] [GetAttributeFromJsonString]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Filter: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Folder: string, default='Examples:images/sequences/agingFacesFemale', semantic role Unknown from slot name/docs unless stated.
  - TriggerUpdate: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Files: List<string>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - NumberOfFiles: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - File system read/write/list behavior; path resolution, encoding, and error reporting need source-backed fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.file`, `vuo.data`, `vuo.url`. File sandbox/path permissions and encoding/defaults need fixture.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - Unknown.

## GetAttributeFromJsonString

- TiXL full path: `Lib.io.json.GetAttributeFromJsonString`
- Namespace: `Lib.io.json`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/json/GetAttributeFromJsonString.cs`
  - .t3 defaults: `Operators/Lib/io/json/GetAttributeFromJsonString.t3`
  - docs: `.help/docs/operators/lib/io/json/GetAttributeFromJsonString.md`
  - related shader / helper source: `Newtonsoft.Json.JsonConvert.DeserializeObject<DataTable>` and `System.Data`; parse failure behavior beyond status output is Unknown.
- Purpose: Loads a JSON string from a URL and outputs the defined part as a string. Simpler alternative [RequestUrl] Also see [LoadImageFromUrl] [WriteToFile] [ReadFile] [PickStringPart] [FilesInFolder]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ColumnName: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - JsonString: string, default='https://cataas.com/cat', semantic role Unknown from slot name/docs unless stated.
  - RowIndex: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Columns: List<string>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - RowCount: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Text/URL JSON helper behavior; parse failure, key path syntax, and HTTP lifecycle need source-backed fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Vuo has `vuo.data.fetch`/`vuo.url` and text/data primitives; no exact JSON object helper confirmed in checked node set, so JSON parsing may require custom node or composition.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - Unknown.

## RequestUrl

- TiXL full path: `Lib.io.json.RequestUrl`
- Namespace: `Lib.io.json`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/io/json/RequestUrl.cs`
  - .t3 defaults: `Operators/Lib/io/json/RequestUrl.t3`
  - docs: `.help/docs/operators/lib/io/json/RequestUrl.md`
  - related shader / helper source: `System.Net.Http.HttpClient`, `HttpClientHandler` with default credentials, and `T3.Core.Utils`.
- Purpose: Loads a URL and outputs the source text as a string. Hint: The string contains what you see when you open the URL with a browser and select 'View Page Source' Useful combinations: [PickStringPart] Also see [LoadImageFromUrl] [GetAttributeFromJsonString]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - TriggerRequest: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Url: string, default='https://cataas.com/cat', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Text/URL JSON helper behavior; parse failure, key path syntax, and HTTP lifecycle need source-backed fixture.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Vuo has `vuo.data.fetch`/`vuo.url` and text/data primitives; no exact JSON object helper confirmed in checked node set, so JSON parsing may require custom node or composition.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable data/text/file concept; Vuo has URL/data/file support but exact parsing/defaults need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - Unknown.

## WebServer

- TiXL full path: `Lib.io.http.WebServer`
- Namespace: `Lib.io.http`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `None`
  - .t3 defaults: `None`
  - docs: `.help/docs/operators/lib/io/http/WebServer.md`
  - related shader / helper source: C# network HTTP listener/server implementation; lifecycle/status/dropdown helpers.
- Purpose: Starts a simple HTTP web server to serve content over the network. This operator can be used to create simple web interfaces, provide data to other applications via a basic REST API, or serve files. The content served to connecting clients is determined by the HtmlContent input. Tips: - Use the Listen toggle to start and stop the server. - If Port is set to 0, the server will automatically pick a free port. - The server's URL will be http://[LocalIpAddress]:[Port]/ AKA: http, server, web, rest, api
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - Unknown / none in C# evidence.
- Runtime behavior:
  - Runs an HTTP server/listener inside TiXL graph update lifecycle; request parsing, response state, port binding, and firewall behavior are blocking details.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: No direct Vuo HTTP server node set found in `external/vuo/node`; client URL/data fetch exists. Server mode is network permission/realtime service behavior.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## TcpServer

- TiXL full path: `Lib.io.tcp.TcpServer`
- Namespace: `Lib.io.tcp`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/io/tcp/TcpServer.cs`
  - .t3 defaults: `Operators/Lib/io/tcp/TcpServer.t3`
  - docs: `.help/docs/operators/lib/io/tcp/TcpServer.md`
  - related shader / helper source: C# TCP listener/client lifecycle with async tasks.
- Purpose: Creates a TCP server to listen for incoming connections and receive data. This operator starts a TCP server on a specified IP address and port, allowing multiple clients to connect simultaneously. It receives data from all connected clients and can broadcast messages to them. Tips: - Use Listen to start and stop the server. - ConnectionCount shows how many clients are currently connected. - Messages sent via the Message input are broadcast to all connected clients. AKA: tcp, network, socket, server
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Listen: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - LocalIpAddress: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Message: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Port: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - PrintToLog: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendOnChange: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - ConnectionCount: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - IsListening: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Result: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Async socket lifecycle with connect/listen toggles, queues, parsing modes, status, and realtime event bridging; teardown/reconnect semantics are critical.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: No direct generic UDP/TCP/WebSocket Vuo node set found in checked tree. Would require custom network nodes and permission/firewall lifecycle design.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## TcpClient

- TiXL full path: `Lib.io.tcp.TcpClient`
- Namespace: `Lib.io.tcp`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/io/tcp/TcpClient.cs`
  - .t3 defaults: `Operators/Lib/io/tcp/TcpClient.t3`
  - docs: `.help/docs/operators/lib/io/tcp/TcpClient.md`
  - related shader / helper source: C# TCP client socket lifecycle with async tasks.
- Purpose: Connects to a TCP server to send and receive data. This operator establishes a client connection to a specified TCP server. It can send messages constructed from various parts and receives data from the server, which can be processed as a continuous string or as a list of lines. Tips: - Use SendOnChange to automatically send data whenever the MessageParts input changes. - For manual control over sending, use the SendTrigger input. - Check the IsConnected output to verify the connection status before attempting to send data. AKA: tcp, network, socket, client
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Connect: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Host: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - ListLength: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - LocalIpAddress: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - MessageParts: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Port: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - PrintToLog: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendOnChange: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SendTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Separator: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - IsConnected: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - ReceivedLines: List<string>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - ReceivedString: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - WasTrigger: bool, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Async socket lifecycle with connect/listen toggles, queues, parsing modes, status, and realtime event bridging; teardown/reconnect semantics are critical.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: No direct generic UDP/TCP/WebSocket Vuo node set found in checked tree. Would require custom network nodes and permission/firewall lifecycle design.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## WebSocketServer

- TiXL full path: `Lib.io.websocket.WebSocketServer`
- Namespace: `Lib.io.websocket`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `None`
  - .t3 defaults: `None`
  - docs: `.help/docs/operators/lib/io/websocket/WebSocketServer.md`
  - related shader / helper source: `HttpListener`, `System.Net.WebSockets`, `System.Text.Json`, async client dictionary/queues.
- Purpose: Creates a WebSocket server to enable real-time, two-way communication with web clients. This operator is ideal for building interactive web-based UIs, remote controls, or dashboards that can send data to and receive data from T3. It can broadcast messages to all connected clients and parse incoming messages from them. Tips: - Use Listen to start and stop the server. - The server address will be ws://[LocalIpAddress]:[Port]/[Path]. - Messages sent from T3 via the Message input are broadcast to all connected clients. - Incoming messages from clients are available on the LastReceived... outputs. AKA: websocket, server, network, web, socket, real-time
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - Unknown / none in C# evidence.
- Runtime behavior:
  - Async socket lifecycle with connect/listen toggles, queues, parsing modes, status, and realtime event bridging; teardown/reconnect semantics are critical.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: No direct generic UDP/TCP/WebSocket Vuo node set found in checked tree. Would require custom network nodes and permission/firewall lifecycle design.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## WebSocketClient

- TiXL full path: `Lib.io.websocket.WebSocketClient`
- Namespace: `Lib.io.websocket`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `None`
  - .t3 defaults: `None`
  - docs: `.help/docs/operators/lib/io/websocket/WebSocketClient.md`
  - related shader / helper source: `ClientWebSocket`, `System.Text.Json`, async receive queue and parsing modes.
- Purpose: Connects to a WebSocket server to enable real-time, two-way communication. This operator establishes a client connection to a WebSocket server, allowing you to send and receive messages. It's useful for interacting with web-based applications, APIs, or other systems that use the WebSocket protocol. Tips: - The Url should start with 'ws://' or 'wss://' (for secure connections). - Use SendOnChange or SendTrigger to control when messages are sent. - Received messages can be accessed as a raw string, a list of lines, or parsed into a dictionary or list of parts, depending on the ParsingMode. AKA: websocket, socket, web, real-time, client
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Unknown / none in C# evidence.
- Outputs:
  - Unknown / none in C# evidence.
- Runtime behavior:
  - Async socket lifecycle with connect/listen toggles, queues, parsing modes, status, and realtime event bridging; teardown/reconnect semantics are critical.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: No direct generic UDP/TCP/WebSocket Vuo node set found in checked tree. Would require custom network nodes and permission/firewall lifecycle design.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Device/network/realtime or TiXL/vendor-specific integration; document behavior before Vuo implementation.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - device permission / realtime scheduling / teardown.

## GetFloatVar

- TiXL full path: `Lib.flow.context.GetFloatVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/GetFloatVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/GetFloatVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/GetFloatVar.md`
  - related shader / helper source: `EvaluationContext.FloatVariables`; `ICustomDropdownHolder` lists available variables.
- Purpose: Fetches a float variable from the context. Use [SetFloatVar] to set the variable further up the graph. Also [GetIntVar] Important: If you're using this together with [Loop], don't forget to use 'f' as the normalized progress variable.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - FallbackDefault: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default='f', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: float, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Reads named value from `EvaluationContext` typed dictionary and falls back when missing; dropdown lists current context variable names in source variants.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## SetFloatVar

- TiXL full path: `Lib.flow.context.SetFloatVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/SetFloatVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/SetFloatVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/SetFloatVar.md`
  - related shader / helper source: `EvaluationContext.FloatVariables`; temporarily scopes around `SubGraph.GetValue(context)` and optional clear behavior.
- Purpose: Writes a float value to the context's float variable dictionary. Use [ContextFloat] to read this value further down the graph.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ClearAfterExecution: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - FloatValue: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SubGraph: Command, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Writes named value into `EvaluationContext`; when `SubGraph` is connected, value is scoped around subgraph execution and previous value may be restored.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## GetStringVar

- TiXL full path: `Lib.flow.context.GetStringVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/GetStringVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/GetStringVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/GetStringVar.md`
  - related shader / helper source: `EvaluationContext.StringVariables`; `ICustomDropdownHolder`.
- Purpose: Fetches a float variable from the context. Use [SetStringVar] to set the variable further up the graph. Important: If you're using this together with [Loop], don't forget to use 's' as the normalized progress variable. Example: [SetContextVariableExample]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - FallbackDefault: string, default='', semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default='s', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Reads named value from `EvaluationContext` typed dictionary and falls back when missing; dropdown lists current context variable names in source variants.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## SetStringVar

- TiXL full path: `Lib.flow.context.SetStringVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/SetStringVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/SetStringVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/SetStringVar.md`
  - related shader / helper source: `EvaluationContext.StringVariables`; scoped subgraph write/restore semantics.
- Purpose: Writes a string value to the context's float variable dictionary. Use [GetStringVar] to read this value further down the graph. Example: [SetContextVariableExample]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ClearAfterExecution: bool, default=False, semantic role Unknown from slot name/docs unless stated.
  - StringValue: string, default='', semantic role Unknown from slot name/docs unless stated.
  - SubGraph: Command, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default='s', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Writes named value into `EvaluationContext`; when `SubGraph` is connected, value is scoped around subgraph execution and previous value may be restored.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## GetIntVar

- TiXL full path: `Lib.flow.context.GetIntVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/GetIntVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/GetIntVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/GetIntVar.md`
  - related shader / helper source: `EvaluationContext.IntVariables`; log enum behavior present in source.
- Purpose: Reads a variable from the context. Also see [GetFloatVar]
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - FallbackValue: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - LogUpdates: int, default=Unknown, enum=['None', 'Warnings', 'Changes', 'AllUpdates'], semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default='i', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Reads named value from `EvaluationContext` typed dictionary and falls back when missing; dropdown lists current context variable names in source variants.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## SetIntVar

- TiXL full path: `Lib.flow.context.SetIntVar`
- Namespace: `Lib.flow.context`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/context/SetIntVar.cs`
  - .t3 defaults: `Operators/Lib/flow/context/SetIntVar.t3`
  - docs: `.help/docs/operators/lib/flow/context/SetIntVar.md`
  - related shader / helper source: `EvaluationContext.IntVariables`; scoped subgraph write/restore semantics.
- Purpose: Sets or overwrites an int variable that can be retrieved by [GetIntVar] further down (left in) the graph.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ClearAfterExecution: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - LogLevel: int, default=Unknown, enum=['None', 'Warnings', 'Changes', 'AllUpdates'], semantic role Unknown from slot name/docs unless stated.
  - SubGraph: Command, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Value: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - VariableName: string, default='i', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Writes named value into `EvaluationContext`; when `SubGraph` is connected, value is scoped around subgraph execution and previous value may be restored.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL app state/context-specific. Vuo equivalents are event/data flow, Hold/Share/Record data nodes, or subcomposition published ports, but not a scoped EvaluationContext dictionary.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: TiXL EvaluationContext variable/app-state semantics; Vuo has data/event memory but no direct scoped context dictionary.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## BlendScenes

- TiXL full path: `Lib.flow.BlendScenes`
- Namespace: `Lib.flow`
- Clone status: doc_and_csharp_verified
- Source evidence:
  - C#: `Operators/Lib/flow/BlendScenes.cs`
  - .t3 defaults: `Operators/Lib/flow/BlendScenes.t3`
  - docs: `.help/docs/operators/lib/flow/BlendScenes.md`
  - related shader / helper source: `Command` multi-input draw scene graph; blending behavior is not visible in `BlendScenes.cs`, so runtime implementation path is Unknown.
- Purpose: Uses a float index to alpha blend between draw scenes.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - BlendFraction: float, default=0.0, semantic role Unknown from slot name/docs unless stated.
  - Scenes: Command, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: Command, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - C# class declares `Command` multi-input and `BlendFraction`; actual alpha-blend scene execution is not visible in this class and must be resolved in TiXL command evaluation/runtime.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: TiXL Command execution/timeline scene semantics. Vuo has event doors/walls, Select, Hold, and scene/image/layer composition, but no direct Command object equivalent.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - D: Command graph execution/timeline semantics are TiXL app runtime concepts, not direct Vuo node code.
- First implementation recommendation:
  - Document only for first pass; design runtime bridge or defer until app-state/device lifecycle exists.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - TiXL app-state or Command semantics.

## SplitString

- TiXL full path: `Lib.string.list.SplitString`
- Namespace: `Lib.string.list`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/list/SplitString.cs`
  - .t3 defaults: `Operators/Lib/string/list/SplitString.t3`
  - docs: `.help/docs/operators/lib/string/list/SplitString.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Splits a string by the provided separation character. Returns a List<string> that can be parsed with [PickFromStringList].
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Split: string, default='\\n', semantic role Unknown from slot name/docs unless stated.
  - String: string, default='.', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Count: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Fragments: List<string>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable text/list operation; Vuo type/list edge cases need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## JoinStringList

- TiXL full path: `Lib.string.list.JoinStringList`
- Namespace: `Lib.string.list`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/list/JoinStringList.cs`
  - .t3 defaults: `Operators/Lib/string/list/JoinStringList.t3`
  - docs: `.help/docs/operators/lib/string/list/JoinStringList.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Joins the members of a string with a separator.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Input: List<string>, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Separator: string, default='\\n', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable text/list operation; Vuo type/list edge cases need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## KeepStrings

- TiXL full path: `Lib.string.list.KeepStrings`
- Namespace: `Lib.string.list`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/list/KeepStrings.cs`
  - .t3 defaults: `Operators/Lib/string/list/KeepStrings.t3`
  - docs: `.help/docs/operators/lib/string/list/KeepStrings.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Collects the input string to a list.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - ClearTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Index: int, default=0, semantic role Unknown from slot name/docs unless stated.
  - InsertMode: int, default=Unknown, enum=['Append', 'Insert', 'Overwrite', 'UseIndex'], semantic role Unknown from slot name/docs unless stated.
  - InsertTrigger: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - MaxCount: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - NewString: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - OnlyOnChanges: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Count: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - InsertTimes: List<float>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Strings: List<string>, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable text/list operation; Vuo type/list edge cases need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## PickStringFromList

- TiXL full path: `Lib.string.list.PickStringFromList`
- Namespace: `Lib.string.list`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/list/PickStringFromList.cs`
  - .t3 defaults: `Operators/Lib/string/list/PickStringFromList.t3`
  - docs: `.help/docs/operators/lib/string/list/PickStringFromList.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Can switch between different strings if the 'Fragments' output of a [SplitString] operator is used as an input
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Index: int, default=0, semantic role Unknown from slot name/docs unless stated.
  - Input: List<string>, default={'Values': []}, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Count: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
  - Selected: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - B: Portable text/list operation; Vuo type/list edge cases need contract.
- First implementation recommendation:
  - Batch after deciding Vuo type/list/generic shape and writing edge-case fixtures.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## FloatToString

- TiXL full path: `Lib.string.convert.FloatToString`
- Namespace: `Lib.string.convert`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/convert/FloatToString.cs`
  - .t3 defaults: `Operators/Lib/string/convert/FloatToString.t3`
  - docs: `.help/docs/operators/lib/string/convert/FloatToString.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Converts a float value into a string. You can use the .net string format descriptor for various effects, like... Here are some examples for the value 1.2345 {0:P1} -> 123.45% {0:000.000} -> 001.234 {0:0} -> 1 {0:G3} -> 1.23 need {0:1.2} cookies  -> need 1.2 cookies
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Format: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Value: float, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## IntToString

- TiXL full path: `Lib.string.convert.IntToString`
- Namespace: `Lib.string.convert`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/convert/IntToString.cs`
  - .t3 defaults: `Operators/Lib/string/convert/IntToString.t3`
  - docs: `.help/docs/operators/lib/string/convert/IntToString.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Converts an [IntValue] to a string. Tip: You can override the string formatting to something like... "{0:0} times" add prefixes or suffixes.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Format: string, default='{0:0}', semantic role Unknown from slot name/docs unless stated.
  - Value: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## Vec3ToString

- TiXL full path: `Lib.string.convert.Vec3ToString`
- Namespace: `Lib.string.convert`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/convert/Vec3ToString.cs`
  - .t3 defaults: `Operators/Lib/string/convert/Vec3ToString.t3`
  - docs: `.help/docs/operators/lib/string/convert/Vec3ToString.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Converts a Vector3 into a string. Formats examples: ({0:F2}, {1:F2}, {2:F2}) Result: (0.00, 1.00, 2.00) X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2} Result: X:    1.50 Y:    2.50 Z:    3.50
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - Format: string, default='', semantic role Unknown from slot name/docs unless stated.
  - Vector: Vector3, default={'X': 0.0, 'Y': 0.0, 'Z': 0.0}, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Output: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## IndexOf

- TiXL full path: `Lib.string.search.IndexOf`
- Namespace: `Lib.string.search`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/search/IndexOf.cs`
  - .t3 defaults: `Operators/Lib/string/search/IndexOf.t3`
  - docs: `.help/docs/operators/lib/string/search/IndexOf.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Searches the original string for a search pattern and outputs the position at which the pattern was found
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - OriginalString: string, default='', semantic role Unknown from slot name/docs unless stated.
  - SearchPattern: string, default='', semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Index: int, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## SearchAndReplace

- TiXL full path: `Lib.string.search.SearchAndReplace`
- Namespace: `Lib.string.search`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/search/SearchAndReplace.cs`
  - .t3 defaults: `Operators/Lib/string/search/SearchAndReplace.t3`
  - docs: `.help/docs/operators/lib/string/search/SearchAndReplace.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Looks for characters or strings within another string and returns a new string with the matches replaced.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - OriginalString: string, default='', semantic role Unknown from slot name/docs unless stated.
  - Replace: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - SearchPattern: string, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - UseRegex: bool, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## SubString

- TiXL full path: `Lib.string.search.SubString`
- Namespace: `Lib.string.search`
- Clone status: doc_verified_no_csharp_match
- Source evidence:
  - C#: `Operators/Lib/string/search/SubString.cs`
  - .t3 defaults: `Operators/Lib/string/search/SubString.t3`
  - docs: `.help/docs/operators/lib/string/search/SubString.md`
  - related shader / helper source: .NET string/list APIs; culture/regex/indexing semantics should be fixture-tested.
- Purpose: Returns a substring of the connect string. This can be useful for trimming a string to a maximum length.
- Conversion: Research-only. Keep TiXL source behavior as contract; do not assume Vuo parity until a fixture proves event timing, defaults, and edge cases.
- Inputs:
  - InputText: string, default='', semantic role Unknown from slot name/docs unless stated.
  - Length: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
  - Start: int, default=Unknown, semantic role Unknown from slot name/docs unless stated.
- Outputs:
  - Result: string, semantic role from docs/source name; exact event cadence Unknown unless marked DirtyFlagTrigger in source.
- Runtime behavior:
  - Pure .NET-style string/list operation; exact indexing base, modulo, regex, culture, and invalid-format behavior should be captured in fixtures.
  - important edge cases: Unknown unless named above; preserve TiXL docs/source behavior over guessed Vuo behavior.
- Observed graph usage:
  - common incoming nodes: spec index in_edges=Unknown; `.t3` adjacency is usage evidence only.
  - common outgoing nodes: spec index out_edges=Unknown; exact examples need graph-level audit if implementing.
- Vuo mapping:
  - Vuo input types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - Vuo output types: map TiXL primitive/list/texture/command types case-by-case; Unknown until node contract is designed.
  - direct built-in Vuo equivalent, if any: Existing Vuo sets: `vuo.text` and `vuo.list` cover split/find/replace/case/count/append/list operations; exact indexing/regex/format semantics need tests.
  - missing Vuo support: TiXL-specific `Command`/`EvaluationContext`/vendor device/runtime semantics where applicable; Unknown for unverified helper behavior.
- Porting grade:
  - A: Pure text/value transform or predicate; good Vuo C node/subcomposition candidate.
- First implementation recommendation:
  - Good first-pass candidate after tiny fixture for defaults, edge cases, and Vuo text/event behavior.
- Verification fixture:
  - Minimal TiXL graph with default constants, one changed input, one edge/failure case, and recorded output/event timing; compare to Vuo composition/custom node behavior.
- Risks / unknowns:
  - indexing, culture, regex, Unicode behavior.

## First Batch Recommendation

Most worth doing first, if the goal is fast verified Vuo value-node coverage rather than device runtime work:

1. `Lib.string.convert.FloatToString`
2. `Lib.string.convert.IntToString`
3. `Lib.string.search.IndexOf`
4. `Lib.string.search.SearchAndReplace`
5. `Lib.string.search.SubString`
6. `Lib.string.list.SplitString`
7. `Lib.string.list.JoinStringList`
8. `Lib.string.list.PickStringFromList`
9. `Lib.string.list.StringLength`
10. `Lib.data.object.PickObject`

Biggest blocker: TiXL IO and flow nodes are not just functions. Many depend on `EvaluationContext`, `Command` graph execution, app timeline/audio analysis state, device lifecycles, background socket threads, GPU textures, or vendor SDKs. Vuo has strong event/data node sets for audio/MIDI/OSC/video/serial/text/list, but parity needs explicit event timing, permission, teardown, and type-shape contracts before code.
