/**
 * @file
 * my.image.batch.batch33BasicGenerateShapesProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 33 image/generate/basic shape nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch33BasicGenerateShapesProof",
					 "description" : "Proof-only compositor for Batch 33 TiXL Blob/BoxGradient/NGon/NGonGradient nodes. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch33", "texture2d", "image", "generate", "basic", "shape", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D blobImage;
	uniform sampler2D boxGradientImage;
	uniform sampler2D nGonImage;
	uniform sampler2D nGonGradientImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 4.0);
		vec2 localSt = vec2(fract(st.x * 4.0), st.y);
		vec4 color = band < 1.0 ? texture2D(blobImage, localSt)
			: (band < 2.0 ? texture2D(boxGradientImage, localSt)
			: (band < 3.0 ? texture2D(nGonImage, localSt)
			: texture2D(nGonGradientImage, localSt)));
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
	instance->shader = VuoShader_make("my_Batch33BasicGenerateShapesProof Shader");
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
		VuoInputData(VuoImage) blobImage,
		VuoInputData(VuoImage) boxGradientImage,
		VuoInputData(VuoImage) nGonImage,
		VuoInputData(VuoImage) nGonGradientImage,
		VuoInputData(VuoInteger, {"default":640,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 640 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoImage safeBlob = imageOrColor(blobImage, (VuoColor){0.8, 0.8, 0.8, 1.0}, renderWidth, renderHeight);
	VuoImage safeBox = imageOrColor(boxGradientImage, (VuoColor){0.6, 0.6, 0.6, 1.0}, renderWidth, renderHeight);
	VuoImage safeNGon = imageOrColor(nGonImage, (VuoColor){0.4, 0.4, 0.4, 1.0}, renderWidth, renderHeight);
	VuoImage safeNGonGradient = imageOrColor(nGonGradientImage, (VuoColor){0.2, 0.2, 0.2, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "blobImage", safeBlob);
	VuoShader_setUniform_VuoImage((*instance)->shader, "boxGradientImage", safeBox);
	VuoShader_setUniform_VuoImage((*instance)->shader, "nGonImage", safeNGon);
	VuoShader_setUniform_VuoImage((*instance)->shader, "nGonGradientImage", safeNGonGradient);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
