/**
 * @file
 * my.string.batch.gradeAStringsProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 2 string nodes.
 * This is not a TiXL node. It makes string-node outputs visible in Vuo.
 */

#include "VuoImageRenderer.h"
#include "VuoList_VuoText.h"
#include "VuoText.h"
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_Batch2GradeAStringsProof",
					 "description" : "Proof-only image adapter for Batch 2 string nodes. It consumes manufactured node outputs, including VuoList_VuoText fragments, and renders visible bars/status blocks; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "string", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform float floatTextLength;
	uniform float intTextLength;
	uniform float indexValue;
	uniform float replacedLength;
	uniform float subStringLength;
	uniform float stringLengthValue;
	uniform float repeatLength;
	uniform float changeCaseLength;
	uniform float splitCountValue;
	uniform float joinedLength;
	varying vec2 fragmentTextureCoordinate;

	float normalizeLength(float value, float scale)
	{
		return clamp(value / scale, 0.05, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float rowTop = 1.0 - index * rowHeight;
		float rowBottom = rowTop - rowHeight * 0.70;
		float inRow = step(rowBottom, st.y) * step(st.y, rowTop);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 10.0;
		return 0.50 + 0.50 * cos(vec3(0.4, 2.5, 4.6) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.012, 0.028, 0.018);
		float values[10];
		values[0] = normalizeLength(floatTextLength, 16.0);
		values[1] = normalizeLength(intTextLength, 16.0);
		values[2] = clamp((indexValue + 1.0) / 16.0, 0.05, 1.0);
		values[3] = normalizeLength(replacedLength, 20.0);
		values[4] = normalizeLength(subStringLength, 16.0);
		values[5] = normalizeLength(stringLengthValue, 32.0);
		values[6] = normalizeLength(repeatLength, 32.0);
		values[7] = normalizeLength(changeCaseLength, 16.0);
		values[8] = normalizeLength(splitCountValue, 8.0);
		values[9] = normalizeLength(joinedLength, 32.0);

		for (int i = 0; i < 10; ++i)
		{
			float mask = barMask(st, float(i), 10.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 10.0));
		color += vec3(0.07, 0.12, 0.08) * grid;
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

	instance->shader = VuoShader_make("my_Batch2GradeAStringsProof Shader");
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

static VuoReal textLength(VuoText text)
{
	return text ? (VuoReal)strlen(text) : 0.0;
}

static VuoReal listCount(VuoList_VuoText list)
{
	return list ? (VuoReal)VuoListGetCount_VuoText(list) : 0.0;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoText, {"default":""}) floatText,
		VuoInputData(VuoText, {"default":""}) intText,
		VuoInputData(VuoInteger, {"default":-1}) indexValue,
		VuoInputData(VuoText, {"default":""}) replacedText,
		VuoInputData(VuoText, {"default":""}) subStringText,
		VuoInputData(VuoInteger, {"default":0}) stringLength,
		VuoInputData(VuoText, {"default":""}) repeatedText,
		VuoInputData(VuoText, {"default":""}) changeCaseText,
		VuoInputData(VuoList_VuoText) splitFragments,
		VuoInputData(VuoInteger, {"default":0}) splitCount,
		VuoInputData(VuoText, {"default":""}) joinedText,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoReal((*instance)->shader, "floatTextLength", textLength(floatText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "intTextLength", textLength(intText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "indexValue", (VuoReal)indexValue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "replacedLength", textLength(replacedText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "subStringLength", textLength(subStringText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "stringLengthValue", (VuoReal)stringLength);
	VuoShader_setUniform_VuoReal((*instance)->shader, "repeatLength", textLength(repeatedText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "changeCaseLength", textLength(changeCaseText));
	VuoShader_setUniform_VuoReal((*instance)->shader, "splitCountValue", splitCount > 0 ? (VuoReal)splitCount : listCount(splitFragments));
	VuoShader_setUniform_VuoReal((*instance)->shader, "joinedLength", textLength(joinedText));

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
