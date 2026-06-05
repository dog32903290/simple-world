/**
 * @file
 * my.numbers.batch.batch49Vec2Proof node implementation.
 *
 * Proof-only image adapter for Batch 49 Lib.numbers.vec2.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch49Vec2Proof","description":"Proof-only compositor for Batch 49 Lib.numbers.vec2.","keywords":["tixl","batch49","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float addVec2Value;
	uniform float dampVec2Value;
	uniform float divideVector2Value;
	uniform float dotVec2Value;
	uniform float gridPositionValue;
	uniform float hasVec2ChangedValue;
	uniform float int2ToVector2Value;
	uniform float padVec2RangeValue;
	uniform float perlinNoise2Value;
	uniform float pickVector2Value;
	uniform float remapVec2Value;
	uniform float scaleVector2Value;
	uniform float vec2ToVec3Value;
	uniform float vector2ComponentsValue;
	varying vec2 fragmentTextureCoordinate;

	float norm(float value)
	{
		return clamp(0.5 + value / 8.0, 0.04, 0.96);
	}

	vec3 rowColor(float index)
	{
		return 0.50 + 0.50 * cos(vec3(0.3, 2.1, 4.0) + index * 0.77);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.017, 0.020);
		float row = floor((1.0 - st.y) * float(14));
		float value = 0.0;
		if (row == 0.0) value = addVec2Value;
		if (row == 1.0) value = dampVec2Value;
		if (row == 2.0) value = divideVector2Value;
		if (row == 3.0) value = dotVec2Value;
		if (row == 4.0) value = gridPositionValue;
		if (row == 5.0) value = hasVec2ChangedValue;
		if (row == 6.0) value = int2ToVector2Value;
		if (row == 7.0) value = padVec2RangeValue;
		if (row == 8.0) value = perlinNoise2Value;
		if (row == 9.0) value = pickVector2Value;
		if (row == 10.0) value = remapVec2Value;
		if (row == 11.0) value = scaleVector2Value;
		if (row == 12.0) value = vec2ToVec3Value;
		if (row == 13.0) value = vector2ComponentsValue;
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float(14 - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float(14)));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch49Vec2Proof Shader");
	VuoShader_addSource(i->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(i->shader);
	return i;
}

static unsigned int clampDimension(VuoInteger value)
{
	if (value < 64) return 64;
	if (value > 4096) return 4096;
	return (unsigned int)value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal, {"default":0.1}) addVec2Value,
		VuoInputData(VuoReal, {"default":0.2}) dampVec2Value,
		VuoInputData(VuoReal, {"default":0.3}) divideVector2Value,
		VuoInputData(VuoReal, {"default":0.4}) dotVec2Value,
		VuoInputData(VuoReal, {"default":0.5}) gridPositionValue,
		VuoInputData(VuoReal, {"default":0.6}) hasVec2ChangedValue,
		VuoInputData(VuoReal, {"default":0.7}) int2ToVector2Value,
		VuoInputData(VuoReal, {"default":0.8}) padVec2RangeValue,
		VuoInputData(VuoReal, {"default":0.9}) perlinNoise2Value,
		VuoInputData(VuoReal, {"default":1.0}) pickVector2Value,
		VuoInputData(VuoReal, {"default":1.1}) remapVec2Value,
		VuoInputData(VuoReal, {"default":1.2}) scaleVector2Value,
		VuoInputData(VuoReal, {"default":1.3}) vec2ToVec3Value,
		VuoInputData(VuoReal, {"default":1.4}) vector2ComponentsValue,

		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
	VuoShader_setUniform_VuoReal((*instance)->shader, "addVec2Value", addVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "dampVec2Value", dampVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "divideVector2Value", divideVector2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "dotVec2Value", dotVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gridPositionValue", gridPositionValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "hasVec2ChangedValue", hasVec2ChangedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "int2ToVector2Value", int2ToVector2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "padVec2RangeValue", padVec2RangeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "perlinNoise2Value", perlinNoise2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickVector2Value", pickVector2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "remapVec2Value", remapVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scaleVector2Value", scaleVector2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vec2ToVec3Value", vec2ToVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vector2ComponentsValue", vector2ComponentsValue);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
