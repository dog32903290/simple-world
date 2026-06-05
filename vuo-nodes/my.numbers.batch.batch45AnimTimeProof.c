/**
 * @file
 * my.numbers.batch.batch45AnimTimeProof node implementation.
 *
 * Proof-only image adapter for Batch 45 Lib.numbers.anim.time.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch45AnimTimeProof","description":"Proof-only compositor for Batch 45 Lib.numbers.anim.time.","keywords":["tixl","batch45","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float abletonLinkSyncValue;
	uniform float clipTimeValue;
	uniform float convertTimeValue;
	uniform float dateTimeInSecsValue;
	uniform float getFrameSpeedFactorValue;
	uniform float hasTimeChangedValue;
	uniform float lastFrameDurationValue;
	uniform float runTimeValue;
	uniform float setPlaybackSpeedValue;
	uniform float setPlaybackTimeValue;
	uniform float setTimeValue;
	uniform float stopWatchValue;
	uniform float timeValue;
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
		float row = floor((1.0 - st.y) * float(13));
		float value = 0.0;
		if (row == 0.0) value = abletonLinkSyncValue;
		if (row == 1.0) value = clipTimeValue;
		if (row == 2.0) value = convertTimeValue;
		if (row == 3.0) value = dateTimeInSecsValue;
		if (row == 4.0) value = getFrameSpeedFactorValue;
		if (row == 5.0) value = hasTimeChangedValue;
		if (row == 6.0) value = lastFrameDurationValue;
		if (row == 7.0) value = runTimeValue;
		if (row == 8.0) value = setPlaybackSpeedValue;
		if (row == 9.0) value = setPlaybackTimeValue;
		if (row == 10.0) value = setTimeValue;
		if (row == 11.0) value = stopWatchValue;
		if (row == 12.0) value = timeValue;
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float(13 - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float(13)));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch45AnimTimeProof Shader");
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
		VuoInputData(VuoReal, {"default":0.1}) abletonLinkSyncValue,
		VuoInputData(VuoReal, {"default":0.2}) clipTimeValue,
		VuoInputData(VuoReal, {"default":0.3}) convertTimeValue,
		VuoInputData(VuoReal, {"default":0.4}) dateTimeInSecsValue,
		VuoInputData(VuoReal, {"default":0.5}) getFrameSpeedFactorValue,
		VuoInputData(VuoReal, {"default":0.6}) hasTimeChangedValue,
		VuoInputData(VuoReal, {"default":0.7}) lastFrameDurationValue,
		VuoInputData(VuoReal, {"default":0.8}) runTimeValue,
		VuoInputData(VuoReal, {"default":0.9}) setPlaybackSpeedValue,
		VuoInputData(VuoReal, {"default":1.0}) setPlaybackTimeValue,
		VuoInputData(VuoReal, {"default":1.1}) setTimeValue,
		VuoInputData(VuoReal, {"default":1.2}) stopWatchValue,
		VuoInputData(VuoReal, {"default":1.3}) timeValue,

		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
	VuoShader_setUniform_VuoReal((*instance)->shader, "abletonLinkSyncValue", abletonLinkSyncValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "clipTimeValue", clipTimeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "convertTimeValue", convertTimeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "dateTimeInSecsValue", dateTimeInSecsValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "getFrameSpeedFactorValue", getFrameSpeedFactorValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "hasTimeChangedValue", hasTimeChangedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "lastFrameDurationValue", lastFrameDurationValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "runTimeValue", runTimeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "setPlaybackSpeedValue", setPlaybackSpeedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "setPlaybackTimeValue", setPlaybackTimeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "setTimeValue", setTimeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "stopWatchValue", stopWatchValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "timeValue", timeValue);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
