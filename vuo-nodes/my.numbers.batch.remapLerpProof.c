/**
 * @file
 * my.numbers.batch.remapLerpProof node implementation.
 *
 * Proof-only Vuo image adapter for corrected my_Remap and my_Lerp nodes.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch4RemapLerpProof",
					 "description" : "Proof-only Vuo image adapter for Batch 4 corrected Remap/Lerp nodes. It consumes my_Remap and my_Lerp outputs and renders two visible value rows; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "numbers", "remap", "lerp", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float remapValue;
	uniform float lerpValue;
	varying vec2 fragmentTextureCoordinate;

	float normalizeSigned(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.0, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float rowTop = 1.0 - index * rowHeight;
		float rowBottom = rowTop - rowHeight * 0.76;
		float inRow = step(rowBottom, st.y) * step(st.y, rowTop);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		return index < 0.5 ? vec3(0.22, 0.74, 0.86) : vec3(0.82, 0.70, 0.25);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.018, 0.020, 0.026);
		float values[2];
		values[0] = normalizeSigned(remapValue, 32.0);
		values[1] = normalizeSigned(lerpValue, 32.0);

		for (int i = 0; i < 2; ++i)
		{
			float mask = barMask(st, float(i), 2.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 2.0));
		color += vec3(0.08, 0.09, 0.10) * grid;
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

	instance->shader = VuoShader_make("my_Batch4RemapLerpProof Shader");
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
		VuoInputData(VuoReal, {"default":12.5}) remapValue,
		VuoInputData(VuoReal, {"default":20.0}) lerpValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "remapValue", remapValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "lerpValue", lerpValue);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
