/**
 * @file
 * my.numbers.batch.batch12FloatAggregateProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 12 float aggregate/process nodes.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch12FloatAggregateProof",
					 "description" : "Proof-only image adapter for Batch 12 Sum/BlendValues/RemapValues nodes.",
					 "keywords" : [ "tixl", "float", "sum", "blend", "remap", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float sumValue;
	uniform float blendValue;
	uniform float remapValue;
	varying vec2 fragmentTextureCoordinate;

	float normValue(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.04, 1.0);
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
		float h = index / 3.0;
		return 0.48 + 0.52 * cos(vec3(0.20, 2.4, 4.9) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.018, 0.022);
		float values[3];
		values[0] = normValue(sumValue, 64.0);
		values[1] = normValue(blendValue, 64.0);
		values[2] = normValue(remapValue, 96.0);

		for (int i = 0; i < 3; ++i)
		{
			float mask = barMask(st, float(i), 3.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 3.0));
		color += vec3(0.075, 0.08, 0.09) * grid;
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

	instance->shader = VuoShader_make("my_Batch12FloatAggregateProof Shader");
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
		VuoInputData(VuoReal, {"default":2.75}) sumValue,
		VuoInputData(VuoReal, {"default":32.5}) blendValue,
		VuoInputData(VuoReal, {"default":50.0}) remapValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "sumValue", sumValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blendValue", blendValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "remapValue", remapValue);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
