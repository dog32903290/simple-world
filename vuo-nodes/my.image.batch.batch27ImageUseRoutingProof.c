/**
 * @file
 * my.image.batch.batch27ImageUseRoutingProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 27 image/use routing nodes:
 * FirstValidTexture, PickTexture, SwapTextures, UseFallbackTexture.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch27ImageUseRoutingProof",
					 "description" : "Proof-only compositor for Batch 27 TiXL image/use routing nodes. Category: Operators/Lib/image/use. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch27", "texture2d", "image", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D firstValidImage;
	uniform sampler2D pickedImage;
	uniform sampler2D swapAImage;
	uniform sampler2D swapBImage;
	uniform sampler2D fallbackImage;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 5.0);
		vec2 localSt = vec2(fract(st.x * 5.0), st.y);
		vec4 color;
		if (band < 1.0)
			color = texture2D(firstValidImage, localSt);
		else if (band < 2.0)
			color = texture2D(pickedImage, localSt);
		else if (band < 3.0)
			color = texture2D(swapAImage, localSt);
		else if (band < 4.0)
			color = texture2D(swapBImage, localSt);
		else
			color = texture2D(fallbackImage, localSt);

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

	instance->shader = VuoShader_make("my_Batch27ImageUseRoutingProof Shader");
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
		VuoInputData(VuoImage) firstValidImage,
		VuoInputData(VuoImage) pickedImage,
		VuoInputData(VuoImage) swapAImage,
		VuoInputData(VuoImage) swapBImage,
		VuoInputData(VuoImage) fallbackImage,
		VuoInputData(VuoInteger, {"default":640,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = clampDimension(width);
	VuoInteger renderHeight = clampDimension(height);

	VuoImage safeFirstValid = imageOrColor(firstValidImage, (VuoColor){0.03, 0.03, 0.03, 1.0}, renderWidth, renderHeight);
	VuoImage safePicked = imageOrColor(pickedImage, (VuoColor){0.03, 0.03, 0.03, 1.0}, renderWidth, renderHeight);
	VuoImage safeSwapA = imageOrColor(swapAImage, (VuoColor){0.03, 0.03, 0.03, 1.0}, renderWidth, renderHeight);
	VuoImage safeSwapB = imageOrColor(swapBImage, (VuoColor){0.03, 0.03, 0.03, 1.0}, renderWidth, renderHeight);
	VuoImage safeFallback = imageOrColor(fallbackImage, (VuoColor){0.03, 0.03, 0.03, 1.0}, renderWidth, renderHeight);

	VuoShader_setUniform_VuoImage((*instance)->shader, "firstValidImage", safeFirstValid);
	VuoShader_setUniform_VuoImage((*instance)->shader, "pickedImage", safePicked);
	VuoShader_setUniform_VuoImage((*instance)->shader, "swapAImage", safeSwapA);
	VuoShader_setUniform_VuoImage((*instance)->shader, "swapBImage", safeSwapB);
	VuoShader_setUniform_VuoImage((*instance)->shader, "fallbackImage", safeFallback);

	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
