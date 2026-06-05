/**
 * @file
 * my.numbers.batch.batch10IntsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 10 integer-list nodes.
 */

#include "VuoImageRenderer.h"
#include "VuoList_VuoInteger.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch10IntsProof",
					 "description" : "Proof-only image adapter for Batch 10 IntListLength/IntsToList/MergeIntLists/PickIntFromList/SetIntListValue nodes.",
					 "keywords" : [ "tixl", "ints", "list", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float lengthValue;
	uniform float pickedValue;
	uniform float intsToListSum;
	uniform float mergedListSum;
	uniform float setListSum;
	varying vec2 fragmentTextureCoordinate;

	float normValue(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.04, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.16;
		float bottom = top - rowHeight * 0.56;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 5.0;
		return 0.48 + 0.52 * cos(vec3(0.25, 2.1, 4.7) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.018, 0.022);
		float values[5];
		values[0] = normValue(lengthValue, 16.0);
		values[1] = normValue(pickedValue, 64.0);
		values[2] = normValue(intsToListSum, 128.0);
		values[3] = normValue(mergedListSum, 128.0);
		values[4] = normValue(setListSum, 128.0);

		for (int i = 0; i < 5; ++i)
		{
			float mask = barMask(st, float(i), 5.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 5.0));
		color += vec3(0.08, 0.085, 0.09) * grid;
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

static VuoInteger listSum(VuoList_VuoInteger list)
{
	VuoInteger sum = 0;
	unsigned long count = list ? VuoListGetCount_VuoInteger(list) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		sum += VuoListGetValue_VuoInteger(list, i);
	return sum;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch10IntsProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

static unsigned int clampDimension(VuoInteger value)
{
	if (value < 64)
		return 64;
	if (value > 4096)
		return 4096;
	return (unsigned int)value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoInteger, {"default":6}) lengthValue,
		VuoInputData(VuoInteger, {"default":30}) pickedValue,
		VuoInputData(VuoList_VuoInteger) intsToList,
		VuoInputData(VuoList_VuoInteger) mergedList,
		VuoInputData(VuoList_VuoInteger) setList,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "lengthValue", (VuoReal)lengthValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickedValue", (VuoReal)pickedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "intsToListSum", (VuoReal)listSum(intsToList));
	VuoShader_setUniform_VuoReal((*instance)->shader, "mergedListSum", (VuoReal)listSum(mergedList));
	VuoShader_setUniform_VuoReal((*instance)->shader, "setListSum", (VuoReal)listSum(setList));

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
