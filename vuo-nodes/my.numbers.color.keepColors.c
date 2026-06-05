/**
 * @file
 * my.numbers.color.keepColors node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_KeepColors
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/KeepColors.cs
 * - Default: Color=white, AddColorToList=true, MaxLength=100, Reset=false from KeepColors.t3
 * - Primary output: List<Vector4> Result (ColorForValues #868C8D)
 *
 * Stateful Vuo body-layer adapter: the instance stores TiXL's private _list.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_KeepColors",
					 "description" : "Keeps a stateful TiXL-style list of recent colors.",
					 "keywords" : [ "tixl", "numbers", "color", "list", "keep", "state", "memory", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

struct nodeInstanceData
{
	VuoList_VuoColor list;
};

static VuoInteger clampLength(VuoInteger maxLength)
{
	if (maxLength < 1)
		return 1;
	if (maxLength > 100000)
		return 100000;
	return maxLength;
}

static void rebuildFromBuffer(struct nodeInstanceData *instance, VuoColor *buffer, unsigned long count)
{
	if (instance->list)
		VuoRelease(instance->list);

	instance->list = VuoListCreate_VuoColor();
	VuoRetain(instance->list);
	for (unsigned long i = 0; i < count; ++i)
		VuoListAppendValue_VuoColor(instance->list, buffer[i]);
}

static void prependColor(struct nodeInstanceData *instance, VuoColor color)
{
	unsigned long oldCount = instance->list ? VuoListGetCount_VuoColor(instance->list) : 0;
	VuoColor *buffer = (VuoColor *)calloc(oldCount + 1, sizeof(VuoColor));
	buffer[0] = color;
	for (unsigned long i = 0; i < oldCount; ++i)
		buffer[i + 1] = VuoListGetValue_VuoColor(instance->list, i + 1);

	rebuildFromBuffer(instance, buffer, oldCount + 1);
	free(buffer);
}

static void trimToLength(struct nodeInstanceData *instance, VuoInteger maxLength)
{
	unsigned long count = instance->list ? VuoListGetCount_VuoColor(instance->list) : 0;
	if (count <= (unsigned long)maxLength)
		return;

	VuoColor *buffer = (VuoColor *)calloc((unsigned long)maxLength, sizeof(VuoColor));
	for (unsigned long i = 0; i < (unsigned long)maxLength; ++i)
		buffer[i] = VuoListGetValue_VuoColor(instance->list, i + 1);

	rebuildFromBuffer(instance, buffer, (unsigned long)maxLength);
	free(buffer);
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->list = VuoListCreate_VuoColor();
	VuoRetain(instance->list);

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() update,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color,
		VuoInputData(VuoBoolean, {"default":true}) addColorToList,
		VuoInputData(VuoInteger, {"default":100,"suggestedMin":1,"suggestedMax":100000,"suggestedStep":1}) maxLength,
		VuoInputData(VuoBoolean, {"default":false}) reset,
		VuoOutputData(VuoList_VuoColor, {"name":"Result"}) result
)
{
	(void)update;

	if (reset)
		rebuildFromBuffer(*instance, NULL, 0);

	if (addColorToList)
		prependColor(*instance, color);

	trimToLength(*instance, clampLength(maxLength));

	*result = VuoListCopy_VuoColor((*instance)->list);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	if ((*instance)->list)
		VuoRelease((*instance)->list);
}
