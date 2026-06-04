/**
 * @file
 * my.image.use.keepPreviousFrame node implementation.
 *
 * Stateful Vuo body-layer adapter for TiXL Operators/Lib/image/use/KeepPreviousFrame.
 */

#include "VuoImage.h"

VuoModuleMetadata({
					 "title" : "my_KeepPreviousFrame",
					 "description" : "TiXL KeepPreviousFrame image-memory adapter. Source: external/tixl/Operators/Lib/image/use/KeepPreviousFrame.cs. Category: Operators/Lib/image/use. Primary output: Texture2D / VuoImage. ColorForTextures #9F008A. Stateful body-layer adapter with CurrentFrame and PreviousFrame outputs.",
					 "keywords" : [ "tixl", "texture2d", "image", "previous", "feedback", "state" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

struct nodeInstanceData
{
	VuoImage currentFrame;
	VuoImage previousFrame;
	bool previousValid;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->currentFrame = NULL;
	instance->previousFrame = NULL;
	instance->previousValid = false;

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() update,
		VuoInputData(VuoImage, {"name":"ImageA"}) ImageA,
		VuoInputData(VuoBoolean, {"default":true}) Keep,
		VuoOutputData(VuoImage, {"name":"CurrentFrame"}) CurrentFrame,
		VuoOutputData(VuoImage, {"name":"PreviousFrame"}) PreviousFrame
)
{
	if (Keep && ImageA)
	{
		if ((*instance)->previousFrame)
			VuoRelease((*instance)->previousFrame);

		(*instance)->previousFrame = (*instance)->currentFrame;
		(*instance)->previousValid = ((*instance)->previousFrame != NULL);

		(*instance)->currentFrame = ImageA;
		VuoRetain(ImageA);
	}

	*CurrentFrame = (*instance)->currentFrame;
	*PreviousFrame = (*instance)->previousValid ? (*instance)->previousFrame : NULL;
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	if ((*instance)->currentFrame)
		VuoRelease((*instance)->currentFrame);
	if ((*instance)->previousFrame)
		VuoRelease((*instance)->previousFrame);
}
