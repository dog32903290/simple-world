/**
 * @file
 * my.numbers.batch.batch54AnimAnimatorsProof node implementation.
 *
 * Proof-only image adapter for Batch 54 Lib.numbers.anim.animators.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch54AnimAnimatorsProof","description":"Proof-only compositor for Batch 54 Lib.numbers.anim.animators.","keywords":["tixl","batch54","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float animBooleanValue;
	uniform float animFloatListValue;
	uniform float animIntValue;
	uniform float animValueValue;
	uniform float animVec2Value;
	uniform float animVec3Value;
	uniform float oscillateVec2Value;
	uniform float oscillateVec3Value;
	uniform float sequenceAnimValue;
	uniform float triggerAnimValue;
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
		float row = floor((1.0 - st.y) * float(10));
		float value = 0.0;
		if (row == 0.0) value = animBooleanValue;
		if (row == 1.0) value = animFloatListValue;
		if (row == 2.0) value = animIntValue;
		if (row == 3.0) value = animValueValue;
		if (row == 4.0) value = animVec2Value;
		if (row == 5.0) value = animVec3Value;
		if (row == 6.0) value = oscillateVec2Value;
		if (row == 7.0) value = oscillateVec3Value;
		if (row == 8.0) value = sequenceAnimValue;
		if (row == 9.0) value = triggerAnimValue;
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float(10 - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float(10)));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch54AnimAnimatorsProof Shader");
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
		VuoInputData(VuoReal, {"default":0.1}) animBooleanValue,
		VuoInputData(VuoReal, {"default":0.2}) animFloatListValue,
		VuoInputData(VuoReal, {"default":0.3}) animIntValue,
		VuoInputData(VuoReal, {"default":0.4}) animValueValue,
		VuoInputData(VuoReal, {"default":0.5}) animVec2Value,
		VuoInputData(VuoReal, {"default":0.6}) animVec3Value,
		VuoInputData(VuoReal, {"default":0.7}) oscillateVec2Value,
		VuoInputData(VuoReal, {"default":0.8}) oscillateVec3Value,
		VuoInputData(VuoReal, {"default":0.9}) sequenceAnimValue,
		VuoInputData(VuoReal, {"default":1.0}) triggerAnimValue,

		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
	VuoShader_setUniform_VuoReal((*instance)->shader, "animBooleanValue", animBooleanValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animFloatListValue", animFloatListValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animIntValue", animIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animValueValue", animValueValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animVec2Value", animVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "animVec3Value", animVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "oscillateVec2Value", oscillateVec2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "oscillateVec3Value", oscillateVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sequenceAnimValue", sequenceAnimValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "triggerAnimValue", triggerAnimValue);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
