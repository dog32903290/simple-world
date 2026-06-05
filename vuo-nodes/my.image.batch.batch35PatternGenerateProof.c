/**
 * @file
 * my.image.batch.batch35PatternGenerateProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 35 image/generate/pattern nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch35PatternGenerateProof",
					 "description" : "Proof-only compositor for Batch 35 TiXL image/generate/pattern nodes. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch35", "texture2d", "image", "generate", "pattern", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D fraserGridImage;
	uniform sampler2D numberPatternImage;
	uniform sampler2D rasterImage;
	uniform sampler2D ringsImage;
	uniform sampler2D ryojiPattern1Image;
	uniform sampler2D ryojiPattern2Image;
	uniform sampler2D sinFormImage;
	uniform sampler2D valueRasterImage;
	uniform sampler2D zollnerPatternImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 9.0);
		vec2 localSt = vec2(fract(st.x * 9.0), st.y);
		vec4 color = (band < 1.0 ? texture2D(fraserGridImage, localSt) : (band < 2.0 ? texture2D(numberPatternImage, localSt) : (band < 3.0 ? texture2D(rasterImage, localSt) : (band < 4.0 ? texture2D(ringsImage, localSt) : (band < 5.0 ? texture2D(ryojiPattern1Image, localSt) : (band < 6.0 ? texture2D(ryojiPattern2Image, localSt) : (band < 7.0 ? texture2D(sinFormImage, localSt) : (band < 8.0 ? texture2D(valueRasterImage, localSt) : texture2D(zollnerPatternImage, localSt)))))))));
		float edge = step(localSt.x, 0.018) + step(0.982, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.4);
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Batch35PatternGenerateProof Shader");
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
		VuoInputData(VuoImage) fraserGridImage,
		VuoInputData(VuoImage) numberPatternImage,
		VuoInputData(VuoImage) rasterImage,
		VuoInputData(VuoImage) ringsImage,
		VuoInputData(VuoImage) ryojiPattern1Image,
		VuoInputData(VuoImage) ryojiPattern2Image,
		VuoInputData(VuoImage) sinFormImage,
		VuoInputData(VuoImage) valueRasterImage,
		VuoInputData(VuoImage) zollnerPatternImage,
		VuoInputData(VuoInteger, {"default":1440,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 1440 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoShader_setUniform_VuoImage((*instance)->shader, "fraserGridImage", imageOrColor(fraserGridImage, (VuoColor){0.15 + 0.07 * 0, 0.15 + 0.07 * 0, 0.15 + 0.07 * 0, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "numberPatternImage", imageOrColor(numberPatternImage, (VuoColor){0.15 + 0.07 * 1, 0.15 + 0.07 * 1, 0.15 + 0.07 * 1, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "rasterImage", imageOrColor(rasterImage, (VuoColor){0.15 + 0.07 * 2, 0.15 + 0.07 * 2, 0.15 + 0.07 * 2, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "ringsImage", imageOrColor(ringsImage, (VuoColor){0.15 + 0.07 * 3, 0.15 + 0.07 * 3, 0.15 + 0.07 * 3, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "ryojiPattern1Image", imageOrColor(ryojiPattern1Image, (VuoColor){0.15 + 0.07 * 4, 0.15 + 0.07 * 4, 0.15 + 0.07 * 4, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "ryojiPattern2Image", imageOrColor(ryojiPattern2Image, (VuoColor){0.15 + 0.07 * 5, 0.15 + 0.07 * 5, 0.15 + 0.07 * 5, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "sinFormImage", imageOrColor(sinFormImage, (VuoColor){0.15 + 0.07 * 6, 0.15 + 0.07 * 6, 0.15 + 0.07 * 6, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "valueRasterImage", imageOrColor(valueRasterImage, (VuoColor){0.15 + 0.07 * 7, 0.15 + 0.07 * 7, 0.15 + 0.07 * 7, 1.0}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "zollnerPatternImage", imageOrColor(zollnerPatternImage, (VuoColor){0.15 + 0.07 * 8, 0.15 + 0.07 * 8, 0.15 + 0.07 * 8, 1.0}, renderWidth, renderHeight));
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
