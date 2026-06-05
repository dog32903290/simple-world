/**
 * @file
 * my.numbers.batch.batch19ColorListToIntsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 19 ColorListToInts.
 */

#include "VuoImageRenderer.h"
#include "VuoList_VuoInteger.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch19ColorListToIntsProof",
					 "description" : "Proof-only image adapter for Batch 19 ColorListToInts.",
					 "keywords" : [ "tixl", "numbers", "color", "int", "list", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float intListLength;
	uniform float intListSum;
	uniform float intListMax;
	varying vec2 fragmentTextureCoordinate;

	float norm(float value, float scale)
	{
		return clamp(value / scale, 0.04, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.16;
		float bottom = top - rowHeight * 0.58;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 3.0;
		return 0.48 + 0.52 * cos(vec3(0.22, 2.20, 4.55) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.014, 0.017, 0.021);
		float values[3];
		values[0] = norm(intListLength, 16.0);
		values[1] = norm(intListSum, 2048.0);
		values[2] = norm(intListMax, 255.0);

		for (int i = 0; i < 3; ++i)
		{
			float mask = barMask(st, float(i), 3.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 3.0));
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

static VuoInteger listMax(VuoList_VuoInteger list)
{
	VuoInteger max = 0;
	unsigned long count = list ? VuoListGetCount_VuoInteger(list) : 0;
	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoInteger value = VuoListGetValue_VuoInteger(list, i);
		if (i == 1 || value > max)
			max = value;
	}
	return max;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch19ColorListToIntsProof Shader");
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
		VuoInputData(VuoList_VuoInteger) intList,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "intListLength", intList ? (VuoReal)VuoListGetCount_VuoInteger(intList) : 0.0);
	VuoShader_setUniform_VuoReal((*instance)->shader, "intListSum", (VuoReal)listSum(intList));
	VuoShader_setUniform_VuoReal((*instance)->shader, "intListMax", (VuoReal)listMax(intList));

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
