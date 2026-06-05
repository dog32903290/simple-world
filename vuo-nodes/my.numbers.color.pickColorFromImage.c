/**
 * @file
 * my.numbers.color.pickColorFromImage node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickColorFromImage
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/PickColorFromImage.cs
 * - Default: InputImage=null, Position=(0.0, 0.0), AlwaysUpdate=false from PickColorFromImage.t3
 * - Primary output: Vector4 Output (ColorForValues #868C8D)
 *
 * Vuo resource adapter: VuoImage maps TiXL Texture2D, read through a cached
 * CPU RGBA buffer to preserve TiXL's AlwaysUpdate/cached staging texture law.
 */

#include "VuoColor.h"
#include "VuoImage.h"
#include "VuoPoint2d.h"
#include <OpenGL/gl.h>
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_PickColorFromImage",
					 "description" : "Picks a color from a VuoImage using TiXL normalized pixel coordinates.",
					 "keywords" : [ "tixl", "numbers", "color", "image", "texture", "sample", "pick", "readback", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

struct nodeInstanceData
{
	unsigned char *cachedPixels;
	unsigned long cachedWidth;
	unsigned long cachedHeight;
	VuoColor previousOutput;
	bool hasPreviousOutput;
};

static long clampIndex(long value, long min, long max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static unsigned long pickColumn(VuoPoint2d position, unsigned long width)
{
	return (unsigned long)clampIndex((long)(position.x * (VuoReal)width), 0, (long)width - 1);
}

static unsigned long pickRow(VuoPoint2d position, unsigned long height)
{
	return (unsigned long)clampIndex((long)(position.y * (VuoReal)height), 0, (long)height - 1);
}

static bool refreshCachedPixels(struct nodeInstanceData *instance, VuoImage image, bool alwaysUpdate)
{
	if (!image || image->pixelsWide == 0 || image->pixelsHigh == 0)
		return false;

	unsigned long width = image->pixelsWide;
	unsigned long height = image->pixelsHigh;
	unsigned long bytes = width * height * 4;
	bool needsRefresh = alwaysUpdate || !instance->cachedPixels || instance->cachedWidth != width || instance->cachedHeight != height;
	if (!needsRefresh)
		return true;

	const unsigned char *rgba = VuoImage_getBuffer(image, GL_RGBA);
	if (!rgba)
		return false;

	unsigned char *copy = (unsigned char *)malloc(bytes);
	memcpy(copy, rgba, bytes);
	free(instance->cachedPixels);
	instance->cachedPixels = copy;
	instance->cachedWidth = width;
	instance->cachedHeight = height;
	return true;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->cachedPixels = NULL;
	instance->cachedWidth = 0;
	instance->cachedHeight = 0;
	instance->previousOutput = (VuoColor){0.0, 0.0, 0.0, 1.0};
	instance->hasPreviousOutput = false;

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() update,
		VuoInputData(VuoImage) inputImage,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) position,
		VuoInputData(VuoBoolean, {"default":false}) alwaysUpdate,
		VuoOutputData(VuoColor, {"name":"Output"}) output
)
{
	(void)update;

	if (!inputImage)
	{
		*output = (*instance)->previousOutput;
		return;
	}

	if (!refreshCachedPixels(*instance, inputImage, alwaysUpdate))
	{
		*output = (*instance)->previousOutput;
		return;
	}

	unsigned long column = pickColumn(position, (*instance)->cachedWidth);
	unsigned long row = pickRow(position, (*instance)->cachedHeight);
	unsigned long index = (row * (*instance)->cachedWidth + column) * 4;
	unsigned char *pixel = (*instance)->cachedPixels + index;

	VuoColor sampled = {
		(VuoReal)pixel[0] / 255.0,
		(VuoReal)pixel[1] / 255.0,
		(VuoReal)pixel[2] / 255.0,
		(VuoReal)pixel[3] / 255.0
	};

	(*instance)->previousOutput = sampled;
	(*instance)->hasPreviousOutput = true;
	*output = sampled;
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	free((*instance)->cachedPixels);
}
