/**
 * @file
 * my.numbers.batch.batch7IntLogicProcessProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 7 integer logic/process nodes.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch7IntLogicProcessProof",
					 "description" : "Proof-only Vuo image adapter for Batch 7 integer logic/process nodes. It consumes CompareInt, PickInt, ClampInt, FloatToInt, GetAPrime, MaxInt, and MinInt outputs and renders visible rows; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "numbers", "int", "logic", "process", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float compareIsTrue;
	uniform float compareResultValue;
	uniform float pickIntValue;
	uniform float clampIntValue;
	uniform float floatToIntValue;
	uniform float primeValue;
	uniform float maxIntValue;
	uniform float minIntValue;
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
		float h = index / 8.0;
		return 0.52 + 0.48 * cos(vec3(0.1, 2.3, 4.4) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.018, 0.019, 0.023);
		float values[8];
		values[0] = compareIsTrue > 0.5 ? 0.92 : 0.18;
		values[1] = normalizeSigned(compareResultValue, 32.0);
		values[2] = normalizeSigned(pickIntValue, 64.0);
		values[3] = normalizeSigned(clampIntValue, 24.0);
		values[4] = normalizeSigned(floatToIntValue, 24.0);
		values[5] = normalizeSigned(primeValue, 32.0);
		values[6] = normalizeSigned(maxIntValue, 64.0);
		values[7] = normalizeSigned(minIntValue, 64.0);

		for (int i = 0; i < 8; ++i)
		{
			float mask = barMask(st, float(i), 8.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 8.0));
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

	instance->shader = VuoShader_make("my_Batch7IntLogicProcessProof Shader");
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
		VuoInputData(VuoBoolean, {"default":true}) compareIsTrue,
		VuoInputData(VuoInteger, {"default":10}) compareResultValue,
		VuoInputData(VuoInteger, {"default":33}) pickIntValue,
		VuoInputData(VuoInteger, {"default":10}) clampIntValue,
		VuoInputData(VuoInteger, {"default":-3}) floatToIntValue,
		VuoInputData(VuoInteger, {"default":13}) primeValue,
		VuoInputData(VuoInteger, {"default":33}) maxIntValue,
		VuoInputData(VuoInteger, {"default":-4}) minIntValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "compareIsTrue", compareIsTrue ? 1.0 : 0.0);
	VuoShader_setUniform_VuoReal((*instance)->shader, "compareResultValue", (VuoReal)compareResultValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickIntValue", (VuoReal)pickIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "clampIntValue", (VuoReal)clampIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "floatToIntValue", (VuoReal)floatToIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "primeValue", (VuoReal)primeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "maxIntValue", (VuoReal)maxIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "minIntValue", (VuoReal)minIntValue);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
