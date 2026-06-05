/**
 * @file
 * my.numbers.batch.batch24KeepColorsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 24 KeepColors.
 */

#include "VuoColor.h"
#include "VuoImageRenderer.h"
#include "VuoList_VuoColor.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch24KeepColorsProof",
					 "description" : "Proof-only image adapter for Batch 24 KeepColors stateful list output.",
					 "keywords" : [ "tixl", "numbers", "color", "keep", "state", "list", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 averageColor;
	uniform float keptColorCount;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = mix(vec3(0.05, 0.055, 0.06), averageColor.rgb, 0.85);
		float countBar = step(0.08, st.x) * step(st.x, 0.08 + clamp(keptColorCount / 16.0, 0.0, 1.0) * 0.84);
		float barRow = step(0.08, st.y) * step(st.y, 0.22);
		float upper = step(0.35, st.y);
		color = mix(color, averageColor.rgb, upper);
		color = mix(color, vec3(0.94, 0.84, 0.38), barRow * countBar);
		color += vec3(0.10) * step(0.996, abs(st.y - 0.35));
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

	instance->shader = VuoShader_make("my_Batch24KeepColorsProof Shader");
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
		VuoInputData(VuoList_VuoColor) keptColors,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);
	VuoReal keptColorCount = keptColors ? (VuoReal)VuoListGetCount_VuoColor(keptColors) : 0.0;

	VuoShader_setUniform_VuoColor((*instance)->shader, "averageColor", averageColor(keptColors));
	VuoShader_setUniform_VuoReal((*instance)->shader, "keptColorCount", keptColorCount);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
