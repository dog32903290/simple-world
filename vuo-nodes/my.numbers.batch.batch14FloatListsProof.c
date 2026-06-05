/**
 * @file
 * my.numbers.batch.batch14FloatListsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 14 float-list nodes.
 */

#include "VuoImageRenderer.h"
#include "VuoList_VuoReal.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch14FloatListsProof",
					 "description" : "Proof-only image adapter for Batch 14 FloatsToList/FloatListLength/PickFloatFromList/SetFloatListValue nodes.",
					 "keywords" : [ "tixl", "floats", "list", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float lengthValue;
	uniform float pickedValue;
	uniform float floatsToListSum;
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
		float h = index / 4.0;
		return 0.48 + 0.52 * cos(vec3(0.22, 2.25, 4.55) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.018, 0.022);
		float values[4];
		values[0] = normValue(lengthValue, 8.0);
		values[1] = normValue(pickedValue, 64.0);
		values[2] = normValue(floatsToListSum, 128.0);
		values[3] = normValue(setListSum, 128.0);

		for (int i = 0; i < 4; ++i)
		{
			float mask = barMask(st, float(i), 4.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 4.0));
		color += vec3(0.08, 0.085, 0.09) * grid;
		color += vec3(0.018, 0.012, 0.02) * smoothstep(0.0, 1.0, st.x);
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

static VuoReal listSum(VuoList_VuoReal list)
{
	VuoReal sum = 0.0;
	unsigned long count = list ? VuoListGetCount_VuoReal(list) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		sum += VuoListGetValue_VuoReal(list, i);
	return sum;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch14FloatListsProof Shader");
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
		VuoInputData(VuoInteger, {"default":3}) lengthValue,
		VuoInputData(VuoReal, {"default":30.0}) pickedValue,
		VuoInputData(VuoList_VuoReal) floatsToList,
		VuoInputData(VuoList_VuoReal) setList,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "lengthValue", (VuoReal)lengthValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickedValue", pickedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "floatsToListSum", listSum(floatsToList));
	VuoShader_setUniform_VuoReal((*instance)->shader, "setListSum", listSum(setList));

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
