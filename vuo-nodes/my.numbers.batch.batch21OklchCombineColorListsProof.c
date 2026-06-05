/**
 * @file
 * my.numbers.batch.batch21OklchCombineColorListsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 21 OKLChToColor/CombineColorLists.
 */

#include "VuoColor.h"
#include "VuoImageRenderer.h"
#include "VuoList_VuoColor.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch21OklchCombineColorListsProof",
					 "description" : "Proof-only image adapter for Batch 21 OKLChToColor/CombineColorLists.",
					 "keywords" : [ "tixl", "numbers", "color", "oklch", "list", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 oklchColor;
	uniform vec4 combinedAverageColor;
	uniform float combinedLength;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = st.y > 0.62 ? oklchColor.rgb : combinedAverageColor.rgb;
		float lengthBar = clamp(combinedLength / 6.0, 0.0, 1.0);
		float row = step(0.0, st.y) * step(st.y, 0.22);
		float bar = step(0.08, st.x) * step(st.x, 0.08 + lengthBar * 0.84);
		color = mix(color, vec3(0.86, 0.88, 0.50), row * bar);
		float grid = step(0.996, abs(st.y - 0.62));
		color += vec3(0.08, 0.085, 0.09) * grid;
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

	instance->shader = VuoShader_make("my_Batch21OklchCombineColorListsProof Shader");
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
		VuoInputData(VuoColor) oklchColor,
		VuoInputData(VuoList_VuoColor) combinedList,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoColor((*instance)->shader, "oklchColor", oklchColor);
	VuoShader_setUniform_VuoColor((*instance)->shader, "combinedAverageColor", averageColor(combinedList));
	VuoShader_setUniform_VuoReal((*instance)->shader, "combinedLength", combinedList ? (VuoReal)VuoListGetCount_VuoColor(combinedList) : 0.0);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
