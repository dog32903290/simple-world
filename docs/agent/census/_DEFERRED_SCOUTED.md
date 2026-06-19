
## transport-YELLOW consumers (scouted Cut86 session 2026-06-20, DEFER to 補縫/Phase-C per 柏為 6-block directive)
Seam already built (Cut85 TransportSnapshot on cookStatefulValueOp rail). 3 clean READ-only leaves, SEQUENTIAL (share stateful_value_ops.cpp + node_registry_math.cpp). Build order + blueprint:
1. ConvertTime (0 state) — bpm bars<->secs. step: Result=mode==0? Time*240/bpm : Time*bpm/240. NodeSpec Time(Float)+Mode(Widget::Enum BarsToSeconds/SecondsToBars). golden bpm=120: B2S(1)=2, S2B(2)=1; bpm=240 proves live bpm read.
2. RunTime (0 state) — out=tr.runTimeSecs. golden 3 frames dt=0.5 -> 0.5/1.0/1.5, independent of scrub/pause (R-1 origin fork visible here, name it).
3. DelayTriggerChange (6 state: lastTrue/lastFalse/lastChange/triggered/stateBeforeChange/delayedHeld) — DelayTriggerChange.cs:30-95, .t3 default TimeMode=6(AppRunTime_InSecs)/Mode=0(DelayTrue)/DelayDuration=1.0. TWO-edge change detector (NOT WasTriggered rising). 7 TimeModes map to snapshot (F-1: LocalTime==PlayTime==t.position both legs). DelayTrue first-second delayed=true is FAITHFUL (don't seed s0). golden legs A(delay rise)/B(DelayFalse)/C(bpm conv TimeMode=1)/D(held-state reconstruct DelayBoth).
EXCLUDED: DelayBoolean(Queue 500>s12)/WasTrigger(context-var seam)/GetFrameSpeedFactor(needs FrameSpeedFactor field not in POD)/Set*(write-back RED).
