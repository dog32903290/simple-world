/**
 * @file
 * my.numbers.batch.gradeANumbersProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 1 Grade A number nodes.
 * This is not a TiXL node. It makes value-node outputs visible in Vuo.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch1GradeANumbersProof",
					 "description" : "Proof-only image adapter for Batch 1 Grade A number nodes. It consumes manufactured node outputs and renders visible bars/status blocks; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "numbers", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec2 resolution;
	uniform float smoothStepValue;
	uniform float sinValue;
	uniform float cosValue;
	uniform float intAddValue;
	uniform float subIntsValue;
	uniform float multiplyIntValue;
	uniform float intDivValue;
	uniform float modIntValue;
	uniform float intToFloatValue;
	uniform int isIntEvenValue;
	varying vec2 fragmentTextureCoordinate;

	float normalizeSigned(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.0, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float rowTop = 1.0 - index * rowHeight;
		float rowBottom = rowTop - rowHeight * 0.72;
		float inRow = step(rowBottom, st.y) * step(st.y, rowTop);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 10.0;
		return 0.55 + 0.45 * cos(vec3(0.0, 2.1, 4.2) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.02, 0.035);
		float values[10];
		values[0] = clamp(smoothStepValue, 0.0, 1.0);
		values[1] = normalizeSigned(sinValue, 2.0);
		values[2] = normalizeSigned(cosValue, 2.0);
		values[3] = normalizeSigned(intAddValue, 16.0);
		values[4] = normalizeSigned(subIntsValue, 16.0);
		values[5] = normalizeSigned(multiplyIntValue, 24.0);
		values[6] = normalizeSigned(intDivValue, 16.0);
		values[7] = normalizeSigned(modIntValue, 16.0);
		values[8] = normalizeSigned(intToFloatValue, 32.0);
		values[9] = isIntEvenValue == 0 ? 0.18 : 0.92;

		for (int i = 0; i < 10; ++i)
		{
			float mask = barMask(st, float(i), 10.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 10.0));
		color += vec3(0.08, 0.1, 0.12) * grid;
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

	instance->shader = VuoShader_make("my_Batch1GradeANumbersProof Shader");
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
		VuoInputData(VuoReal, {"default":0.5}) smoothStepValue,
		VuoInputData(VuoReal, {"default":1.0}) sinValue,
		VuoInputData(VuoReal, {"default":-1.0}) cosValue,
		VuoInputData(VuoInteger, {"default":7}) intAddValue,
		VuoInputData(VuoInteger, {"default":-8}) subIntsValue,
		VuoInputData(VuoInteger, {"default":12}) multiplyIntValue,
		VuoInputData(VuoInteger, {"default":3}) intDivValue,
		VuoInputData(VuoInteger, {"default":1}) modIntValue,
		VuoInputData(VuoReal, {"default":42.0}) intToFloatValue,
		VuoInputData(VuoBoolean, {"default":true}) isIntEvenValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "resolution", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "smoothStepValue", smoothStepValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "sinValue", sinValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "cosValue", cosValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "intAddValue", (VuoReal)intAddValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "subIntsValue", (VuoReal)subIntsValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "multiplyIntValue", (VuoReal)multiplyIntValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "intDivValue", (VuoReal)intDivValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "modIntValue", (VuoReal)modIntValue);
	VuoShader_setUniform_VuoReal   ((*instance)->shader, "intToFloatValue", intToFloatValue);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "isIntEvenValue", isIntEvenValue ? 1 : 0);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
