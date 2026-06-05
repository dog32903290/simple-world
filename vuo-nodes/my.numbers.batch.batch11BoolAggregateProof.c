/**
 * @file
 * my.numbers.batch.batch11BoolAggregateProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 11 boolean aggregate nodes.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch11BoolAggregateProof",
					 "description" : "Proof-only image adapter for Batch 11 All/Any boolean aggregate nodes.",
					 "keywords" : [ "tixl", "bool", "all", "any", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float allValue;
	uniform float anyValue;
	varying vec2 fragmentTextureCoordinate;

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.22;
		float bottom = top - rowHeight * 0.42;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.10, st.x) * step(st.x, 0.10 + value * 0.80);
		return inRow * inBar;
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.014, 0.017, 0.021);
		float allMask = barMask(st, 0.0, 2.0, mix(0.18, 1.0, allValue));
		float anyMask = barMask(st, 1.0, 2.0, mix(0.18, 1.0, anyValue));
		color = mix(color, vec3(0.20, 0.78, 0.58), allMask);
		color = mix(color, vec3(0.88, 0.62, 0.24), anyMask);
		float grid = step(0.996, fract(st.y * 2.0));
		color += vec3(0.08, 0.09, 0.095) * grid;
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

	instance->shader = VuoShader_make("my_Batch11BoolAggregateProof Shader");
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
		VuoInputData(VuoBoolean, {"default":true}) allValue,
		VuoInputData(VuoBoolean, {"default":true}) anyValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "allValue", allValue ? 1.0 : 0.0);
	VuoShader_setUniform_VuoReal((*instance)->shader, "anyValue", anyValue ? 1.0 : 0.0);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
