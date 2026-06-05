/**
 * @file
 * my.numbers.batch.batch13DictSelectorsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 13 dict selector nodes.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include "VuoPoint3d.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch13DictSelectorsProof",
					 "description" : "Proof-only image adapter for Batch 13 SelectFloatFromDict/SelectBoolFromFloatDict/SelectVec2FromDict/SelectVec3FromDict nodes.",
					 "keywords" : [ "tixl", "dict", "selector", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float floatValue;
	uniform float boolValue;
	uniform vec2 vec2Value;
	uniform vec3 vec3Value;
	varying vec2 fragmentTextureCoordinate;

	float normValue(float value, float scale)
	{
		return clamp(value / scale, 0.04, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.16;
		float bottom = top - rowHeight * 0.52;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 7.0;
		return 0.48 + 0.52 * cos(vec3(0.15, 2.2, 4.7) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.014, 0.018, 0.022);
		float values[7];
		values[0] = normValue(floatValue, 1.0);
		values[1] = boolValue > 0.5 ? 0.92 : 0.08;
		values[2] = normValue(vec2Value.x, 4.0);
		values[3] = normValue(vec2Value.y, 4.0);
		values[4] = normValue(vec3Value.x, 32.0);
		values[5] = normValue(vec3Value.y, 32.0);
		values[6] = normValue(vec3Value.z, 32.0);

		for (int i = 0; i < 7; ++i)
		{
			float mask = barMask(st, float(i), 7.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 7.0));
		color += vec3(0.07, 0.08, 0.09) * grid;
		color += vec3(0.02, 0.016, 0.012) * smoothstep(0.0, 1.0, st.x);
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch13DictSelectorsProof Shader");
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
		VuoInputData(VuoReal, {"default":0.75}) floatValue,
		VuoInputData(VuoBoolean, {"default":true}) boolValue,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":2.0}}) vec2Value,
		VuoInputData(VuoPoint3d, {"default":{"x":10.0,"y":20.0,"z":30.0}}) vec3Value,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "floatValue", floatValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "boolValue", boolValue ? 1.0 : 0.0);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "vec2Value", vec2Value);
	VuoShader_setUniform_VuoPoint3d((*instance)->shader, "vec3Value", vec3Value);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
