/**
 * @file
 * my.numbers.batch.batch8MixedMathProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 8 mixed scalar/int math nodes.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch8MixedMathProof",
					 "description" : "Proof-only Vuo image adapter for Batch 8 Atan2/AddInts/MultiplyInts/SumInts nodes. It renders one visible row per manufactured output; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "numbers", "atan2", "int", "aggregate", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float atan2Value;
	uniform float addIntsValue;
	uniform float multiplyIntsValue;
	uniform float sumIntsValue;
	varying vec2 fragmentTextureCoordinate;

	float normalizeSigned(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.05, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float rowTop = 1.0 - index * rowHeight;
		float rowBottom = rowTop - rowHeight * 0.74;
		float inRow = step(rowBottom, st.y) * step(st.y, rowTop);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 4.0;
		return 0.52 + 0.48 * cos(vec3(0.15, 2.2, 4.5) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.018, 0.019, 0.023);
		float values[4];
		values[0] = normalizeSigned(atan2Value, 3.14159);
		values[1] = normalizeSigned(addIntsValue, 32.0);
		values[2] = normalizeSigned(multiplyIntsValue, 64.0);
		values[3] = normalizeSigned(sumIntsValue, 32.0);

		for (int i = 0; i < 4; ++i)
		{
			float mask = barMask(st, float(i), 4.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 4.0));
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

	instance->shader = VuoShader_make("my_Batch8MixedMathProof Shader");
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
		VuoInputData(VuoReal, {"default":1.57079632679}) atan2Value,
		VuoInputData(VuoInteger, {"default":7}) addIntsValue,
		VuoInputData(VuoInteger, {"default":24}) multiplyIntsValue,
		VuoInputData(VuoInteger, {"default":3}) sumIntsValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "atan2Value", atan2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "addIntsValue", (VuoReal)addIntsValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "multiplyIntsValue", (VuoReal)multiplyIntsValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sumIntsValue", (VuoReal)sumIntsValue);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
