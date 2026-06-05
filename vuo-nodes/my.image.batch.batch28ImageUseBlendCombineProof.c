/**
 * @file
 * my.image.batch.batch28ImageUseBlendCombineProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 28 image/use blend/combine nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch28ImageUseBlendCombineProof",
					 "description" : "Proof-only compositor for Batch 28 TiXL image/use blend/combine nodes. Category: Operators/Lib/image/use. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch28", "texture2d", "image", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D blendImagesImage;
	uniform sampler2D blendWithMaskImage;
	uniform sampler2D combine3ImagesImage;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 3.0);
		vec2 localSt = vec2(fract(st.x * 3.0), st.y);
		vec4 color = band < 1.0 ? texture2D(blendImagesImage, localSt) : (band < 2.0 ? texture2D(blendWithMaskImage, localSt) : texture2D(combine3ImagesImage, localSt));
		float edge = step(localSt.x, 0.025) + step(0.975, localSt.x);
		if (edge > 0.0)
			color.rgb = mix(color.rgb, vec3(1.0), 0.45);
		gl_FragColor = color;
	}
);

struct nodeInstanceData
{
	VuoShader shader;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->shader = VuoShader_make("my_Batch28ImageUseBlendCombineProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);

	return instance;
}

static VuoInteger clampDimension(VuoInteger value)
{
	if (value < 1)
		return 1;
	if (value > 4096)
		return 4096;
	return value;
}

static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{
	if (image)
		return image;
	return VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) blendImagesImage,
		VuoInputData(VuoImage) blendWithMaskImage,
		VuoInputData(VuoImage) combine3ImagesImage,
		VuoInputData(VuoInteger, {"default":480,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = clampDimension(width);
	VuoInteger renderHeight = clampDimension(height);
	VuoImage safeBlendImages = imageOrColor(blendImagesImage, (VuoColor){0.0, 0.5, 0.5, 1.0}, renderWidth, renderHeight);
	VuoImage safeBlendWithMask = imageOrColor(blendWithMaskImage, (VuoColor){0.5, 0.5, 0.0, 1.0}, renderWidth, renderHeight);
	VuoImage safeCombine3 = imageOrColor(combine3ImagesImage, (VuoColor){1.0, 1.0, 1.0, 1.0}, renderWidth, renderHeight);

	VuoShader_setUniform_VuoImage((*instance)->shader, "blendImagesImage", safeBlendImages);
	VuoShader_setUniform_VuoImage((*instance)->shader, "blendWithMaskImage", safeBlendWithMask);
	VuoShader_setUniform_VuoImage((*instance)->shader, "combine3ImagesImage", safeCombine3);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
