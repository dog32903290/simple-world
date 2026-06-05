/**
 * @file
 * my.image.batch.batch30ImageUsePostfxProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 30 post-fx image/use nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch30ImageUsePostfxProof",
					 "description" : "Proof-only compositor for Batch 30 TiXL Fxaa/NormalMap/DepthBufferAsGrayScale nodes. Category: Operators/Lib/image/use. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch30", "texture2d", "image", "postfx", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D fxaaImage;
	uniform sampler2D normalMapImage;
	uniform sampler2D depthImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 3.0);
		vec2 localSt = vec2(fract(st.x * 3.0), st.y);
		vec4 color = band < 1.0 ? texture2D(fxaaImage, localSt) : (band < 2.0 ? texture2D(normalMapImage, localSt) : texture2D(depthImage, localSt));
		float edge = step(localSt.x, 0.025) + step(0.975, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.45);
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch30ImageUsePostfxProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoImage imageOrColor(VuoImage image, VuoColor color, VuoInteger width, VuoInteger height)
{
	if (image) return image;
	return VuoImage_makeColorImage(color, (unsigned int)width, (unsigned int)height);
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) fxaaImage,
		VuoInputData(VuoImage) normalMapImage,
		VuoInputData(VuoImage) depthImage,
		VuoInputData(VuoInteger, {"default":480,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 480 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoImage safeFxaa = imageOrColor(fxaaImage, (VuoColor){0.2, 0.2, 0.2, 1.0}, renderWidth, renderHeight);
	VuoImage safeNormal = imageOrColor(normalMapImage, (VuoColor){0.5, 0.5, 1.0, 1.0}, renderWidth, renderHeight);
	VuoImage safeDepth = imageOrColor(depthImage, (VuoColor){0.5, 0.5, 0.5, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "fxaaImage", safeFxaa);
	VuoShader_setUniform_VuoImage((*instance)->shader, "normalMapImage", safeNormal);
	VuoShader_setUniform_VuoImage((*instance)->shader, "depthImage", safeDepth);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
