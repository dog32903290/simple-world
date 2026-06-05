/**
 * @file
 * my.image.use.combineMaterialChannels node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_CombineMaterialChannels
 * - Category: Operators/Lib/image/use
 * - Source: external/tixl/Operators/Lib/image/use/CombineMaterialChannels.cs
 * - Default: GenerateMips=true, Resolution=(0,0), RemapRoughness=identity from CombineMaterialChannels.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/use/CombineMaterialChannels.hlsl.
 * Vuo body-layer limit: TiXL Curve input is fixed to the default identity remap.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_CombineMaterialChannels",
					 "description" : "TiXL CombineMaterialChannels bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/use/CombineMaterialChannels.cs. Category: Operators/Lib/image/use. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: GenerateMips=true, Resolution=(0,0), RemapRoughness=identity.",
					 "keywords" : [ "tixl", "texture2d", "image", "material", "roughness.r", "metallic.g", "occlusion.r", "CombineMaterialChannels.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D roughness;
	uniform sampler2D metallic;
	uniform sampler2D occlusion;
	uniform bool hasRoughness;
	uniform bool hasMetallic;
	uniform bool hasOcclusion;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		float r = hasRoughness ? texture2D(roughness, st).r : 0.5; // roughness.r
		float m = hasMetallic ? texture2D(metallic, st).g : 0.0;  // metallic.g
		float o = hasOcclusion ? texture2D(occlusion, st).r : 1.0; // occlusion.r
		r = clamp(r, 0.0, 1.0); // identity remap
		gl_FragColor = vec4(r, m, o, 1.0);
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
	instance->shader = VuoShader_make("my_CombineMaterialChannels Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, VuoImage primary, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0) return requested;
	if (primary) return width ? primary->pixelsWide : primary->pixelsHigh;
	return 160;
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
		VuoInputData(VuoImage) roughness,
		VuoInputData(VuoImage) metallic,
		VuoInputData(VuoImage) occlusion,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":true}) generateMips,
		VuoOutputData(VuoImage, {"name":"Output"}) output
)
{
	VuoImage primary = roughness ? roughness : (metallic ? metallic : occlusion);
	VuoInteger renderWidth = renderDimension(resolution, primary, true);
	VuoInteger renderHeight = renderDimension(resolution, primary, false);
	VuoImage safeRoughness = imageOrColor(roughness, (VuoColor){0.5, 0.5, 0.5, 1.0}, renderWidth, renderHeight);
	VuoImage safeMetallic = imageOrColor(metallic, (VuoColor){0.0, 0.0, 0.0, 1.0}, renderWidth, renderHeight);
	VuoImage safeOcclusion = imageOrColor(occlusion, (VuoColor){1.0, 1.0, 1.0, 1.0}, renderWidth, renderHeight);

	VuoShader_setUniform_VuoImage((*instance)->shader, "roughness", safeRoughness);
	VuoShader_setUniform_VuoImage((*instance)->shader, "metallic", safeMetallic);
	VuoShader_setUniform_VuoImage((*instance)->shader, "occlusion", safeOcclusion);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "hasRoughness", roughness != NULL);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "hasMetallic", metallic != NULL);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "hasOcclusion", occlusion != NULL);
	*output = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
