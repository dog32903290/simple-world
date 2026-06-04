/**
 * @file
 * my.runtime.clock.mainClock node implementation.
 *
 * My World host-clock adapter for Vuo proof compositions.
 */

VuoModuleMetadata({
					 "title" : "my_MainClock",
					 "description" : "My World runtime clock adapter. It centralizes Vuo host frame events into one graph-level renderTick, frameIndex, time, and deltaTime. This is a Vuo body-layer scheduler proof, not a final native scheduler or audio-clock contract.",
					 "keywords" : [ "my-world", "runtime", "clock", "frame", "renderTick", "scheduler" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

struct nodeInstanceData
{
	VuoInteger frameIndex;
	VuoReal previousHostTime;
	bool hasPreviousHostTime;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->frameIndex = 0;
	instance->previousHostTime = 0.0;
	instance->hasPreviousHostTime = false;

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputData(VuoReal, {"name":"HostTime","default":0.0}) HostTime,
		VuoInputEvent({"data":"HostTime","eventBlocking":"none"}) clockIn,
		VuoOutputData(VuoInteger, {"name":"FrameIndex"}) FrameIndex,
		VuoOutputData(VuoReal, {"name":"Time"}) Time,
		VuoOutputData(VuoReal, {"name":"DeltaTime"}) DeltaTime,
		VuoOutputEvent() renderTick
)
{
	VuoReal safeDeltaTime = 0.0;
	if ((*instance)->hasPreviousHostTime)
	{
		safeDeltaTime = HostTime - (*instance)->previousHostTime;
		if (safeDeltaTime < 0.0)
			safeDeltaTime = 0.0;
		if (safeDeltaTime > 1.0)
			safeDeltaTime = 1.0;
	}

	(*instance)->previousHostTime = HostTime;
	(*instance)->hasPreviousHostTime = true;

	*FrameIndex = (*instance)->frameIndex;
	*Time = HostTime;
	*DeltaTime = safeDeltaTime;
	*renderTick = true;

	(*instance)->frameIndex += 1;
}
