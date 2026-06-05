/**
 * @file
 * my.numbers.batch.batch16FloatListProcessProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 16 float-list process nodes.
 */

#include "VuoImageRenderer.h"
#include <math.h>
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch16FloatListProcessProof",
					 "description" : "Proof-only image adapter for Batch 16 AnalyzeFloatList/SumRange/CompareFloatLists nodes.",
					 "keywords" : [ "tixl", "floats", "list", "process", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float minValue;
	uniform float maxValue;
	uniform float averageMean;
	uniform float allValid;
	uniform float sumRangeSelected;
	uniform float compareDifference;
	varying vec2 fragmentTextureCoordinate;

	float normalizeSigned(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.04, 1.0);
	}

	float normalizePositive(float value, float scale)
	{
		return clamp(value / scale, 0.04, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.15;
		float bottom = top - rowHeight * 0.56;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 6.0;
		return 0.48 + 0.52 * cos(vec3(0.12, 2.2, 4.6) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.014, 0.017, 0.021);
		float values[6];
		values[0] = normalizeSigned(minValue, 32.0);
		values[1] = normalizeSigned(maxValue, 32.0);
		values[2] = normalizeSigned(averageMean, 32.0);
		values[3] = allValid > 0.5 ? 0.92 : 0.18;
		values[4] = normalizePositive(sumRangeSelected, 48.0);
		values[5] = normalizePositive(compareDifference, 1.0);

		for (int i = 0; i < 6; ++i)
		{
			float mask = barMask(st, float(i), 6.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 6.0));
		color += vec3(0.08, 0.085, 0.09) * grid;
		color += vec3(0.018, 0.014, 0.010) * smoothstep(0.0, 1.0, st.x);
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

	instance->shader = VuoShader_make("my_Batch16FloatListProcessProof Shader");
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

static VuoReal cleanReal(VuoReal value, VuoReal fallback)
{
	return isfinite(value) ? value : fallback;
}

static VuoReal boolReal(VuoBoolean value)
{
	return value ? 1.0 : 0.0;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal) minValue,
		VuoInputData(VuoReal) maxValue,
		VuoInputData(VuoReal) averageMean,
		VuoInputData(VuoBoolean) allValid,
		VuoInputData(VuoReal) sumRangeSelected,
		VuoInputData(VuoReal) compareDifference,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "minValue", cleanReal(minValue, -32.0));
	VuoShader_setUniform_VuoReal((*instance)->shader, "maxValue", cleanReal(maxValue, 32.0));
	VuoShader_setUniform_VuoReal((*instance)->shader, "averageMean", cleanReal(averageMean, 0.0));
	VuoShader_setUniform_VuoReal((*instance)->shader, "allValid", boolReal(allValid));
	VuoShader_setUniform_VuoReal((*instance)->shader, "sumRangeSelected", cleanReal(sumRangeSelected, 0.0));
	VuoShader_setUniform_VuoReal((*instance)->shader, "compareDifference", cleanReal(compareDifference, 1.0));

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
