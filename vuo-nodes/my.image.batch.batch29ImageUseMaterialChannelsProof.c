/**
 * @file
 * my.image.batch.batch29ImageUseMaterialChannelsProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 29 material channel nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch29ImageUseMaterialChannelsProof",
					 "description" : "Proof-only compositor for Batch 29 TiXL material channel nodes. Category: Operators/Lib/image/use. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch29", "texture2d", "image", "material", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D combineMaterialChannels2Image;
	uniform sampler2D combineMaterialChannelsImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 2.0);
		vec2 localSt = vec2(fract(st.x * 2.0), st.y);
		vec4 color = band < 1.0 ? texture2D(combineMaterialChannels2Image, localSt) : texture2D(combineMaterialChannelsImage, localSt);
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
	instance->shader = VuoShader_make("my_Batch29ImageUseMaterialChannelsProof Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger clampDimension(VuoInteger value)
{
	if (value < 1) return 1;
	if (value > 4096) return 4096;
	return value;
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
		VuoInputData(VuoImage) combineMaterialChannels2Image,
		VuoInputData(VuoImage) combineMaterialChannelsImage,
		VuoInputData(VuoInteger, {"default":320,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = clampDimension(width);
	VuoInteger renderHeight = clampDimension(height);
	VuoImage safeCmc2 = imageOrColor(combineMaterialChannels2Image, (VuoColor){1.0, 1.0, 1.0, 1.0}, renderWidth, renderHeight);
	VuoImage safeCmc = imageOrColor(combineMaterialChannelsImage, (VuoColor){0.25, 0.75, 0.5, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "combineMaterialChannels2Image", safeCmc2);
	VuoShader_setUniform_VuoImage((*instance)->shader, "combineMaterialChannelsImage", safeCmc);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
