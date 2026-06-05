/**
 * @file
 * my.numbers.batch.batch25PickColorFromImageProof node implementation.
 *
 * Proof-only Vuo image adapter for Batch 25 PickColorFromImage.
 */

#include "VuoColor.h"
#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_Batch25PickColorFromImageProof",
					 "description" : "Proof-only image adapter for Batch 25 PickColorFromImage.",
					 "keywords" : [ "tixl", "numbers", "color", "image", "texture", "pick", "proof", "visual", "adapter" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoImageRenderer"
					 ],
				 });

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D sourceImage;
	uniform vec4 pickedColor;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec4 src = texture2D(sourceImage, st);
		vec3 color = st.x < 0.5 ? src.rgb : pickedColor.rgb;
		float divider = step(0.995, abs(st.x - 0.5));
		color += vec3(0.12) * divider;
		float swatch = step(0.08, st.x) * step(st.x, 0.42) * step(0.08, st.y) * step(st.y, 0.30);
		color = mix(color, pickedColor.rgb, swatch);
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

	instance->shader = VuoShader_make("my_Batch25PickColorFromImageProof Shader");
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
		VuoInputData(VuoImage) sourceImage,
		VuoInputData(VuoColor) pickedColor,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	if (!sourceImage)
	{
		*image = VuoImage_makeColorImage(pickedColor, clampDimension(width), clampDimension(height));
		return;
	}

	VuoShader_setUniform_VuoImage((*instance)->shader, "sourceImage", sourceImage);
	VuoShader_setUniform_VuoColor((*instance)->shader, "pickedColor", pickedColor);

	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
