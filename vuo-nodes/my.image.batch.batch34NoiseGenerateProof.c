/**
 * @file
 * my.image.batch.batch34NoiseGenerateProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 34 image/generate/noise nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch34NoiseGenerateProof",
					 "description" : "Proof-only compositor for Batch 34 TiXL FractalNoise/Grain/ShardNoise/TileableNoise/WorleyNoise nodes. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch34", "texture2d", "image", "generate", "noise", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D fractalImage;
	uniform sampler2D grainImage;
	uniform sampler2D shardImage;
	uniform sampler2D tileableImage;
	uniform sampler2D worleyImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 5.0);
		vec2 localSt = vec2(fract(st.x * 5.0), st.y);
		vec4 color = band < 1.0 ? texture2D(fractalImage, localSt)
			: (band < 2.0 ? texture2D(grainImage, localSt)
			: (band < 3.0 ? texture2D(shardImage, localSt)
			: (band < 4.0 ? texture2D(tileableImage, localSt)
			: texture2D(worleyImage, localSt))));
		float edge = step(localSt.x, 0.02) + step(0.98, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.4);
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch34NoiseGenerateProof Shader");
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
		VuoInputData(VuoImage) fractalImage,
		VuoInputData(VuoImage) grainImage,
		VuoInputData(VuoImage) shardImage,
		VuoInputData(VuoImage) tileableImage,
		VuoInputData(VuoImage) worleyImage,
		VuoInputData(VuoInteger, {"default":800,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 800 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoImage safeFractal = imageOrColor(fractalImage, (VuoColor){0.2, 0.2, 0.2, 1.0}, renderWidth, renderHeight);
	VuoImage safeGrain = imageOrColor(grainImage, (VuoColor){0.35, 0.35, 0.35, 1.0}, renderWidth, renderHeight);
	VuoImage safeShard = imageOrColor(shardImage, (VuoColor){0.5, 0.5, 0.5, 1.0}, renderWidth, renderHeight);
	VuoImage safeTileable = imageOrColor(tileableImage, (VuoColor){0.65, 0.65, 0.65, 1.0}, renderWidth, renderHeight);
	VuoImage safeWorley = imageOrColor(worleyImage, (VuoColor){0.8, 0.8, 0.8, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "fractalImage", safeFractal);
	VuoShader_setUniform_VuoImage((*instance)->shader, "grainImage", safeGrain);
	VuoShader_setUniform_VuoImage((*instance)->shader, "shardImage", safeShard);
	VuoShader_setUniform_VuoImage((*instance)->shader, "tileableImage", safeTileable);
	VuoShader_setUniform_VuoImage((*instance)->shader, "worleyImage", safeWorley);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
