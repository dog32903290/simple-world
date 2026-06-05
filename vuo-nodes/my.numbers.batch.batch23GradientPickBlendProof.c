/**
 * @file
 * my.numbers.batch.batch23GradientPickBlendProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 23 PickGradient/BlendGradients.
 */

#include "VuoColor.h"
#include "VuoImageRenderer.h"
#include "VuoList_VuoColor.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch23GradientPickBlendProof",
					 "description" : "Proof-only image adapter for Batch 23 gradient picking and blending.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "pick", "blend", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 pickedSampleColor;
	uniform vec4 blendedSampleColor;
	uniform vec4 pickedAverageColor;
	uniform vec4 blendedAverageColor;
	uniform float pickedStepCount;
	uniform float blendedStepCount;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = st.y > 0.5 ? pickedAverageColor.rgb : blendedAverageColor.rgb;
		if (st.y > 0.58 && st.y < 0.90)
			color = pickedSampleColor.rgb;
		if (st.y > 0.10 && st.y < 0.42)
			color = blendedSampleColor.rgb;

		float pickedBar = step(0.08, st.x) * step(st.x, 0.08 + clamp(pickedStepCount / 5.0, 0.0, 1.0) * 0.84);
		float blendedBar = step(0.08, st.x) * step(st.x, 0.08 + clamp(blendedStepCount / 5.0, 0.0, 1.0) * 0.84);
		color = mix(color, vec3(0.95, 0.84, 0.35), step(0.90, st.y) * pickedBar);
		color = mix(color, vec3(0.38, 0.78, 0.95), step(st.y, 0.10) * blendedBar);
		color += vec3(0.08) * step(0.997, abs(st.y - 0.5));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

static VuoColor averageColor(VuoList_VuoColor list)
{
	unsigned long count = list ? VuoListGetCount_VuoColor(list) : 0;
	if (count == 0)
		return (VuoColor){0.0, 0.0, 0.0, 1.0};

	VuoReal r = 0.0;
	VuoReal g = 0.0;
	VuoReal b = 0.0;
	VuoReal a = 0.0;
	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoColor c = VuoListGetValue_VuoColor(list, i);
		r += c.r;
		g += c.g;
		b += c.b;
		a += c.a;
	}

	return (VuoColor){r / count, g / count, b / count, a / count};
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch23GradientPickBlendProof Shader");
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
		VuoInputData(VuoColor) pickedSampleColor,
		VuoInputData(VuoColor) blendedSampleColor,
		VuoInputData(VuoList_VuoColor) pickedGradientColors,
		VuoInputData(VuoList_VuoColor) blendedGradientColors,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoColor((*instance)->shader, "pickedSampleColor", pickedSampleColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "blendedSampleColor", blendedSampleColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "pickedAverageColor", averageColor(pickedGradientColors));
	VuoShader_setUniform_VuoColor((*instance)->shader, "blendedAverageColor", averageColor(blendedGradientColors));
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickedStepCount", pickedGradientColors ? (VuoReal)VuoListGetCount_VuoColor(pickedGradientColors) : 0.0);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blendedStepCount", blendedGradientColors ? (VuoReal)VuoListGetCount_VuoColor(blendedGradientColors) : 0.0);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
