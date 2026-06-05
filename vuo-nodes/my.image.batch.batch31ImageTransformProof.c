/**
 * @file
 * my.image.batch.batch31ImageTransformProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 31 image/transform nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch31ImageTransformProof",
					 "description" : "Proof-only compositor for Batch 31 TiXL Crop/MakeTileableImage/MirrorRepeat/TransformImage nodes. Category: Operators/Lib/image/transform. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch31", "texture2d", "image", "transform", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D cropImage;
	uniform sampler2D tileableImage;
	uniform sampler2D mirrorImage;
	uniform sampler2D transformImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 4.0);
		vec2 localSt = vec2(fract(st.x * 4.0), st.y);
		vec4 color = band < 1.0 ? texture2D(cropImage, localSt)
			: (band < 2.0 ? texture2D(tileableImage, localSt)
			: (band < 3.0 ? texture2D(mirrorImage, localSt)
			: texture2D(transformImage, localSt)));
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
	instance->shader = VuoShader_make("my_Batch31ImageTransformProof Shader");
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
		VuoInputData(VuoImage) cropImage,
		VuoInputData(VuoImage) tileableImage,
		VuoInputData(VuoImage) mirrorImage,
		VuoInputData(VuoImage) transformImage,
		VuoInputData(VuoInteger, {"default":640,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 640 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoImage safeCrop = imageOrColor(cropImage, (VuoColor){1.0, 1.0, 1.0, 0.2}, renderWidth, renderHeight);
	VuoImage safeTileable = imageOrColor(tileableImage, (VuoColor){0.2, 0.2, 0.2, 1.0}, renderWidth, renderHeight);
	VuoImage safeMirror = imageOrColor(mirrorImage, (VuoColor){0.1, 0.4, 0.8, 1.0}, renderWidth, renderHeight);
	VuoImage safeTransform = imageOrColor(transformImage, (VuoColor){0.8, 0.25, 0.1, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "cropImage", safeCrop);
	VuoShader_setUniform_VuoImage((*instance)->shader, "tileableImage", safeTileable);
	VuoShader_setUniform_VuoImage((*instance)->shader, "mirrorImage", safeMirror);
	VuoShader_setUniform_VuoImage((*instance)->shader, "transformImage", safeTransform);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
