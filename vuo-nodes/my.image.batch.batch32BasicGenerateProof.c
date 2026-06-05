/**
 * @file
 * my.image.batch.batch32BasicGenerateProof node implementation.
 *
 * Proof-only Vuo image compositor for Batch 32 image/generate/basic nodes.
 */

#include "VuoImageRenderer.h"

VuoModuleMetadata({
					 "title" : "my_Batch32BasicGenerateProof",
					 "description" : "Proof-only compositor for Batch 32 TiXL CheckerBoard/LinearGradient/RadialGradient/RoundedRect nodes. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D Image (ColorForTextures #9F008A).",
					 "keywords" : [ "tixl", "batch32", "texture2d", "image", "generate", "basic", "proof", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D checkerImage;
	uniform sampler2D linearImage;
	uniform sampler2D radialImage;
	uniform sampler2D roundedRectImage;
	varying vec2 fragmentTextureCoordinate;
	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 4.0);
		vec2 localSt = vec2(fract(st.x * 4.0), st.y);
		vec4 color = band < 1.0 ? texture2D(checkerImage, localSt)
			: (band < 2.0 ? texture2D(linearImage, localSt)
			: (band < 3.0 ? texture2D(radialImage, localSt)
			: texture2D(roundedRectImage, localSt)));
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
	instance->shader = VuoShader_make("my_Batch32BasicGenerateProof Shader");
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
		VuoInputData(VuoImage) checkerImage,
		VuoInputData(VuoImage) linearImage,
		VuoInputData(VuoImage) radialImage,
		VuoInputData(VuoImage) roundedRectImage,
		VuoInputData(VuoInteger, {"default":640,"suggestedMin":64,"suggestedMax":2048,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":160,"suggestedMin":32,"suggestedMax":1024,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	VuoInteger renderWidth = width < 1 ? 640 : width;
	VuoInteger renderHeight = height < 1 ? 160 : height;
	VuoImage safeChecker = imageOrColor(checkerImage, (VuoColor){0.2, 0.2, 0.2, 1.0}, renderWidth, renderHeight);
	VuoImage safeLinear = imageOrColor(linearImage, (VuoColor){0.4, 0.4, 0.4, 1.0}, renderWidth, renderHeight);
	VuoImage safeRadial = imageOrColor(radialImage, (VuoColor){0.6, 0.6, 0.6, 1.0}, renderWidth, renderHeight);
	VuoImage safeRounded = imageOrColor(roundedRectImage, (VuoColor){0.8, 0.8, 0.8, 1.0}, renderWidth, renderHeight);
	VuoShader_setUniform_VuoImage((*instance)->shader, "checkerImage", safeChecker);
	VuoShader_setUniform_VuoImage((*instance)->shader, "linearImage", safeLinear);
	VuoShader_setUniform_VuoImage((*instance)->shader, "radialImage", safeRadial);
	VuoShader_setUniform_VuoImage((*instance)->shader, "roundedRectImage", safeRounded);
	*image = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
