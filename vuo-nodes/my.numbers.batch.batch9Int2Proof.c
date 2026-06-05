/**
 * @file
 * my.numbers.batch.batch9Int2Proof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 9 Int2 nodes.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch9Int2Proof",
					 "description" : "Proof-only image adapter for Batch 9 AddInt2/Int2Components/MakeResolution/MaxInt2/ScaleResolution/ScaleSize nodes. It visualizes each manufactured output; it is not a TiXL parity claim.",
					 "keywords" : [ "tixl", "int2", "resolution", "batch", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec2 addInt2Value;
	uniform float componentWidth;
	uniform float componentHeight;
	uniform float componentLength;
	uniform float componentAspect;
	uniform vec2 makeResolutionValue;
	uniform vec2 maxInt2Value;
	uniform vec2 scaleResolutionValue;
	uniform vec2 scaleSizeValue;
	varying vec2 fragmentTextureCoordinate;

	float normValue(float value, float scale)
	{
		return clamp(0.5 + value / scale, 0.04, 1.0);
	}

	float barMask(vec2 st, float index, float total, float value)
	{
		float rowHeight = 1.0 / total;
		float top = 1.0 - index * rowHeight - rowHeight * 0.12;
		float bottom = top - rowHeight * 0.58;
		float inRow = step(bottom, st.y) * step(st.y, top);
		float inBar = step(0.08, st.x) * step(st.x, 0.08 + value * 0.84);
		return inRow * inBar;
	}

	vec3 rowColor(float index)
	{
		float h = index / 8.0;
		return 0.50 + 0.50 * cos(vec3(0.35, 2.6, 4.2) + h * 6.28318);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.016, 0.018, 0.022);
		float values[8];
		values[0] = normValue(addInt2Value.x + addInt2Value.y, 640.0);
		values[1] = normValue(componentWidth, 640.0);
		values[2] = normValue(componentHeight, 480.0);
		values[3] = clamp(componentAspect / 4.0, 0.04, 1.0);
		values[4] = normValue(makeResolutionValue.x + makeResolutionValue.y, 1200.0);
		values[5] = normValue(maxInt2Value.x + maxInt2Value.y, 1200.0);
		values[6] = normValue(scaleResolutionValue.x + scaleResolutionValue.y, 1600.0);
		values[7] = normValue(scaleSizeValue.x + scaleSizeValue.y, 1600.0);

		for (int i = 0; i < 8; ++i)
		{
			float mask = barMask(st, float(i), 8.0, values[i]);
			color = mix(color, rowColor(float(i)), mask);
		}

		float grid = step(0.996, fract(st.y * 8.0));
		color += vec3(0.07, 0.08, 0.09) * grid;
		color += vec3(0.02, 0.015, 0.01) * smoothstep(0.0, 1.0, st.x);
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

	instance->shader = VuoShader_make("my_Batch9Int2Proof Shader");
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
		VuoInputData(VuoPoint2d, {"default":{"x":384.0,"y":216.0}}) addInt2Value,
		VuoInputData(VuoInteger, {"default":640}) componentWidth,
		VuoInputData(VuoInteger, {"default":360}) componentHeight,
		VuoInputData(VuoInteger, {"default":230400}) componentLength,
		VuoInputData(VuoReal, {"default":1.7777777778}) componentAspect,
		VuoInputData(VuoPoint2d, {"default":{"x":640.0,"y":360.0}}) makeResolutionValue,
		VuoInputData(VuoPoint2d, {"default":{"x":960.0,"y":540.0}}) maxInt2Value,
		VuoInputData(VuoPoint2d, {"default":{"x":320.0,"y":180.0}}) scaleResolutionValue,
		VuoInputData(VuoPoint2d, {"default":{"x":1280.0,"y":720.0}}) scaleSizeValue,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	unsigned int renderWidth = clampDimension(width);
	unsigned int renderHeight = clampDimension(height);

	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "addInt2Value", addInt2Value);
	VuoShader_setUniform_VuoReal((*instance)->shader, "componentWidth", (VuoReal)componentWidth);
	VuoShader_setUniform_VuoReal((*instance)->shader, "componentHeight", (VuoReal)componentHeight);
	VuoShader_setUniform_VuoReal((*instance)->shader, "componentLength", (VuoReal)componentLength);
	VuoShader_setUniform_VuoReal((*instance)->shader, "componentAspect", componentAspect);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "makeResolutionValue", makeResolutionValue);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "maxInt2Value", maxInt2Value);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "scaleResolutionValue", scaleResolutionValue);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "scaleSizeValue", scaleSizeValue);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
