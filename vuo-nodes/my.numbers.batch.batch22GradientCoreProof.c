/**
 * @file
 * my.numbers.batch.batch22GradientCoreProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 22 BuildGradient/DefineGradient/SampleGradient.
 */

#include "VuoColor.h"
#include "VuoImageRenderer.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch22GradientCoreProof",
					 "description" : "Proof-only image adapter for Batch 22 gradient construction and sampling.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 builtSampleColor;
	uniform vec4 definedSampleColor;
	uniform vec4 builtAverageColor;
	uniform vec4 definedAverageColor;
	uniform float builtStepCount;
	uniform float definedStepCount;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = st.y > 0.5 ? builtAverageColor.rgb : definedAverageColor.rgb;
		if (st.y > 0.58 && st.y < 0.9)
			color = builtSampleColor.rgb;
		if (st.y > 0.1 && st.y < 0.42)
			color = definedSampleColor.rgb;

		float builtBar = step(0.08, st.x) * step(st.x, 0.08 + clamp(builtStepCount / 5.0, 0.0, 1.0) * 0.84);
		float definedBar = step(0.08, st.x) * step(st.x, 0.08 + clamp(definedStepCount / 5.0, 0.0, 1.0) * 0.84);
		float topRow = step(0.9, st.y);
		float bottomRow = step(st.y, 0.1);
		color = mix(color, vec3(0.90, 0.88, 0.52), topRow * builtBar);
		color = mix(color, vec3(0.45, 0.82, 0.88), bottomRow * definedBar);
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

	instance->shader = VuoShader_make("my_Batch22GradientCoreProof Shader");
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
		VuoInputData(VuoColor) builtSampleColor,
		VuoInputData(VuoColor) definedSampleColor,
		VuoInputData(VuoList_VuoColor) builtGradientColors,
		VuoInputData(VuoList_VuoReal) builtGradientPositions,
		VuoInputData(VuoList_VuoColor) definedGradientColors,
		VuoInputData(VuoList_VuoReal) definedGradientPositions,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)builtGradientPositions;
	(void)definedGradientPositions;

	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoColor((*instance)->shader, "builtSampleColor", builtSampleColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "definedSampleColor", definedSampleColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "builtAverageColor", averageColor(builtGradientColors));
	VuoShader_setUniform_VuoColor((*instance)->shader, "definedAverageColor", averageColor(definedGradientColors));
	VuoShader_setUniform_VuoReal((*instance)->shader, "builtStepCount", builtGradientColors ? (VuoReal)VuoListGetCount_VuoColor(builtGradientColors) : 0.0);
	VuoShader_setUniform_VuoReal((*instance)->shader, "definedStepCount", definedGradientColors ? (VuoReal)VuoListGetCount_VuoColor(definedGradientColors) : 0.0);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
