/**
 * @file
 * my.numbers.batch.batch51Vec3Proof node implementation.
 *
 * Proof-only image adapter for Batch 51 Lib.numbers.vec3.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch51Vec3Proof","description":"Proof-only compositor for Batch 51 Lib.numbers.vec3.","keywords":["tixl","batch51","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float addVec3Value;
	uniform float blendVector3Value;
	uniform float crossVec3Value;
	uniform float dampVec3Value;
	uniform float dotVec3Value;
	uniform float eulerToAxisAngleValue;
	uniform float hasVec3ChangedValue;
	uniform float lerpVec3Value;
	uniform float magnitudeValue;
	uniform float mulMatrixValue;
	uniform float normalizeVector3Value;
	uniform float perlinNoise3Value;
	uniform float pickVector3Value;
	uniform float rotateVector3Value;
	uniform float roundVec3Value;
	uniform float scaleVector3Value;
	uniform float subVec3Value;
	uniform float transformVec3Value;
	uniform float vec2MagnitudeValue;
	uniform float vec3DistanceValue;
	uniform float vector3ComponentsValue;
	uniform float vector3GizmoValue;
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
		float row = floor((1.0 - st.y) * float(22));
		float value = 0.0;
		if (row == 0.0) value = addVec3Value;
		if (row == 1.0) value = blendVector3Value;
		if (row == 2.0) value = crossVec3Value;
		if (row == 3.0) value = dampVec3Value;
		if (row == 4.0) value = dotVec3Value;
		if (row == 5.0) value = eulerToAxisAngleValue;
		if (row == 6.0) value = hasVec3ChangedValue;
		if (row == 7.0) value = lerpVec3Value;
		if (row == 8.0) value = magnitudeValue;
		if (row == 9.0) value = mulMatrixValue;
		if (row == 10.0) value = normalizeVector3Value;
		if (row == 11.0) value = perlinNoise3Value;
		if (row == 12.0) value = pickVector3Value;
		if (row == 13.0) value = rotateVector3Value;
		if (row == 14.0) value = roundVec3Value;
		if (row == 15.0) value = scaleVector3Value;
		if (row == 16.0) value = subVec3Value;
		if (row == 17.0) value = transformVec3Value;
		if (row == 18.0) value = vec2MagnitudeValue;
		if (row == 19.0) value = vec3DistanceValue;
		if (row == 20.0) value = vector3ComponentsValue;
		if (row == 21.0) value = vector3GizmoValue;
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float(22 - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float(22)));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch51Vec3Proof Shader");
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
		VuoInputData(VuoReal, {"default":0.1}) addVec3Value,
		VuoInputData(VuoReal, {"default":0.2}) blendVector3Value,
		VuoInputData(VuoReal, {"default":0.3}) crossVec3Value,
		VuoInputData(VuoReal, {"default":0.4}) dampVec3Value,
		VuoInputData(VuoReal, {"default":0.5}) dotVec3Value,
		VuoInputData(VuoReal, {"default":0.6}) eulerToAxisAngleValue,
		VuoInputData(VuoReal, {"default":0.7}) hasVec3ChangedValue,
		VuoInputData(VuoReal, {"default":0.8}) lerpVec3Value,
		VuoInputData(VuoReal, {"default":0.9}) magnitudeValue,
		VuoInputData(VuoReal, {"default":1.0}) mulMatrixValue,
		VuoInputData(VuoReal, {"default":1.1}) normalizeVector3Value,
		VuoInputData(VuoReal, {"default":1.2}) perlinNoise3Value,
		VuoInputData(VuoReal, {"default":1.3}) pickVector3Value,
		VuoInputData(VuoReal, {"default":1.4}) rotateVector3Value,
		VuoInputData(VuoReal, {"default":1.5}) roundVec3Value,
		VuoInputData(VuoReal, {"default":1.6}) scaleVector3Value,
		VuoInputData(VuoReal, {"default":1.7}) subVec3Value,
		VuoInputData(VuoReal, {"default":1.8}) transformVec3Value,
		VuoInputData(VuoReal, {"default":1.9}) vec2MagnitudeValue,
		VuoInputData(VuoReal, {"default":2.0}) vec3DistanceValue,
		VuoInputData(VuoReal, {"default":2.1}) vector3ComponentsValue,
		VuoInputData(VuoReal, {"default":2.2}) vector3GizmoValue,

		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
	VuoShader_setUniform_VuoReal((*instance)->shader, "addVec3Value", addVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blendVector3Value", blendVector3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "crossVec3Value", crossVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "dampVec3Value", dampVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "dotVec3Value", dotVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "eulerToAxisAngleValue", eulerToAxisAngleValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "hasVec3ChangedValue", hasVec3ChangedValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "lerpVec3Value", lerpVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "magnitudeValue", magnitudeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "mulMatrixValue", mulMatrixValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "normalizeVector3Value", normalizeVector3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "perlinNoise3Value", perlinNoise3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickVector3Value", pickVector3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotateVector3Value", rotateVector3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "roundVec3Value", roundVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scaleVector3Value", scaleVector3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "subVec3Value", subVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "transformVec3Value", transformVec3Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vec2MagnitudeValue", vec2MagnitudeValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vec3DistanceValue", vec3DistanceValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vector3ComponentsValue", vector3ComponentsValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vector3GizmoValue", vector3GizmoValue);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
