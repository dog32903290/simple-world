/**
 * @file
 * my.numbers.batch.scalarValuesProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 3 scalar value nodes.
 * This is not a TiXL node. It makes bool, float-adjust, and float-basic outputs visible in Vuo.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch3ScalarValuesProof",
					 "description" : "Proof-only Vuo image adapter for Batch 3 scalar value nodes. It consumes manufactured bool, float-adjust, and float-basic node outputs and renders visible bars/status blocks; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "numbers", "bool", "float", "adjust", "basic", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float andValue;
	uniform float orValue;
	uniform float notValue;
	uniform float xorValue;
	uniform float boolToFloatValue;
	uniform float boolToIntValue;
	uniform float pickBoolValue;
	uniform float absValue;
	uniform float ceilValue;
	uniform float floorValue;
	uniform float roundValue;
	uniform float invertFloatValue;
	uniform float clampValue;
	uniform float addValue;
	uniform float subValue;
	uniform float multiplyValue;
	uniform float divValue;
	uniform float moduloValue;
	uniform float powValue;
	uniform float sqrtValue;
	varying vec2 fragmentTextureCoordinate;

	float normalizeSigned(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.0, 1.0);
	}

	float normalizePositive(float value, float scale)
	{
		return clamp(value / scale, 0.05, 1.0);
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
		float h = index / 20.0;
		return 0.52 + 0.48 * cos(vec3(0.2, 2.35, 4.45) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.018, 0.020, 0.024);
		float values[20];
		values[0] = andValue > 0.5 ? 0.92 : 0.18;
		values[1] = orValue > 0.5 ? 0.92 : 0.18;
		values[2] = notValue > 0.5 ? 0.92 : 0.18;
		values[3] = xorValue > 0.5 ? 0.92 : 0.18;
		values[4] = normalizePositive(boolToFloatValue, 8.0);
		values[5] = normalizeSigned(boolToIntValue, 16.0);
		values[6] = pickBoolValue > 0.5 ? 0.92 : 0.18;
		values[7] = normalizePositive(absValue, 8.0);
		values[8] = normalizeSigned(ceilValue, 8.0);
		values[9] = normalizeSigned(floorValue, 8.0);
		values[10] = normalizeSigned(roundValue, 8.0);
		values[11] = normalizeSigned(invertFloatValue, 8.0);
		values[12] = normalizePositive(clampValue, 8.0);
		values[13] = normalizeSigned(addValue, 16.0);
		values[14] = normalizeSigned(subValue, 16.0);
		values[15] = normalizeSigned(multiplyValue, 24.0);
		values[16] = normalizeSigned(divValue, 16.0);
		values[17] = normalizePositive(moduloValue, 8.0);
		values[18] = normalizePositive(powValue, 16.0);
		values[19] = normalizePositive(sqrtValue, 8.0);

		for (int i = 0; i < 20; ++i)
		{
			float mask = barMask(st, float(i), 20.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 20.0));
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

	instance->shader = VuoShader_make("my_Batch3ScalarValuesProof Shader");
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

static VuoReal boolReal(VuoBoolean value)
{
	return value ? 1.0 : 0.0;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoBoolean, {"default":false}) andValue,
		VuoInputData(VuoBoolean, {"default":true}) orValue,
		VuoInputData(VuoBoolean, {"default":true}) notValue,
		VuoInputData(VuoBoolean, {"default":true}) xorValue,
		VuoInputData(VuoReal, {"default":1.0}) boolToFloatValue,
		VuoInputData(VuoInteger, {"default":1}) boolToIntValue,
		VuoInputData(VuoBoolean, {"default":true}) pickBoolValue,
		VuoInputData(VuoReal, {"default":3.5}) absValue,
		VuoInputData(VuoReal, {"default":3.0}) ceilValue,
		VuoInputData(VuoReal, {"default":2.0}) floorValue,
		VuoInputData(VuoReal, {"default":1.0}) roundValue,
		VuoInputData(VuoReal, {"default":-4.0}) invertFloatValue,
		VuoInputData(VuoReal, {"default":1.0}) clampValue,
		VuoInputData(VuoReal, {"default":7.5}) addValue,
		VuoInputData(VuoReal, {"default":-8.0}) subValue,
		VuoInputData(VuoReal, {"default":-12.0}) multiplyValue,
		VuoInputData(VuoReal, {"default":3.5}) divValue,
		VuoInputData(VuoReal, {"default":2.0}) moduloValue,
		VuoInputData(VuoReal, {"default":2.0}) powValue,
		VuoInputData(VuoReal, {"default":3.0}) sqrtValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "andValue", boolReal(andValue));
	VuoShader_setUniform_VuoReal((*instance)->shader, "orValue", boolReal(orValue));
	VuoShader_setUniform_VuoReal((*instance)->shader, "notValue", boolReal(notValue));
	VuoShader_setUniform_VuoReal((*instance)->shader, "xorValue", boolReal(xorValue));
	VuoShader_setUniform_VuoReal((*instance)->shader, "boolToFloatValue", boolToFloatValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "boolToIntValue", (VuoReal)boolToIntValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "pickBoolValue", boolReal(pickBoolValue));
	VuoShader_setUniform_VuoReal((*instance)->shader, "absValue", absValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "ceilValue", ceilValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "floorValue", floorValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "roundValue", roundValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "invertFloatValue", invertFloatValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "clampValue", clampValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "addValue", addValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "subValue", subValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "multiplyValue", multiplyValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "divValue", divValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "moduloValue", moduloValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "powValue", powValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sqrtValue", sqrtValue);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
