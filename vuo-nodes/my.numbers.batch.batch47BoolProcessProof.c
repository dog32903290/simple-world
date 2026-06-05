/**
 * @file
 * my.numbers.batch.batch47BoolProcessProof node implementation.
 *
 * Proof-only image adapter for Batch 47 Lib.numbers.bool.process.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch47BoolProcessProof","description":"Proof-only compositor for Batch 47 Lib.numbers.bool.process.","keywords":["tixl","batch47","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float cacheBooleanValue;
	uniform float delayBooleanValue;
	uniform float delayTriggerChangeValue;
	uniform float keepBooleanValue;
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
		float row = floor((1.0 - st.y) * float(4));
		float value = 0.0;
		if (row == 0.0) value = cacheBooleanValue;
		if (row == 1.0) value = delayBooleanValue;
		if (row == 2.0) value = delayTriggerChangeValue;
		if (row == 3.0) value = keepBooleanValue;
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float(4 - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float(4)));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch47BoolProcessProof Shader");
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
		VuoInputData(VuoReal, {"default":0.1}) cacheBooleanValue,
		VuoInputData(VuoReal, {"default":0.2}) delayBooleanValue,
		VuoInputData(VuoReal, {"default":0.3}) delayTriggerChangeValue,
		VuoInputData(VuoReal, {"default":0.4}) keepBooleanValue,

		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
	VuoShader_setUniform_VuoReal((*instance)->shader, "cacheBooleanValue", cacheBooleanValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "delayBooleanValue", delayBooleanValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "delayTriggerChangeValue", delayTriggerChangeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "keepBooleanValue", keepBooleanValue);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
