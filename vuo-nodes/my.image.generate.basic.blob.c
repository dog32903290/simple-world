/**
 * @file
 * my.image.generate.basic.blob node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Blob
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/Blob.cs
 * - Default: Image=null, Color=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, Scale=0.5, Stretch=(1,1), Rotate=0, Feather=1, FeatherBias=0, Position=(0,0), GenerateMips=false, Resolution=(0,0), TextureFormat=R16G16B16A16_Float from Blob.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/Blob.hlsl.
 * Vuo body-layer limit: source-image blend, DXGI format, and mip behavior are bounded.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Blob",
					 "description" : "TiXL Blob bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/Blob.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Color=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, Scale=0.5, Stretch=(1,1), Rotate=0, Feather=1, FeatherBias=0, Position=(0,0), GenerateMips=false, Resolution=(0,0), TextureFormat=R16G16B16A16_Float.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "blob", "Blob.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 fill;
	uniform vec4 background;
	uniform vec2 stretch;
	uniform vec2 position;
	uniform float scale;
	uniform float feather;
	uniform float gradientBias;
	uniform float rotate;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float imageRotationRad = (-rotate - 90.0) / 180.0 * 3.141578;
		float sina = sin(-imageRotationRad - 3.141578 / 2.0);
		float cosa = cos(-imageRotationRad - 3.141578 / 2.0);
		p = vec2(cosa * p.x - sina * p.y, cosa * p.y + sina * p.x);
		p /= max(abs(stretch), vec2(0.0001));
		p -= position * vec2(1.0, -1.0);
		float d = length(p);
		float f = feather * scale / 2.0;
		d = smoothstep(scale / 2.0 - f, scale / 2.0 + f, d);
		float dBiased = gradientBias >= 0.0 ? pow(d, gradientBias + 1.0) : 1.0 - pow(clamp(1.0 - d, 0.0, 10.0), -gradientBias + 1.0);
		gl_FragColor = mix(fill, background, dBiased);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_Blob Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":0.0}}) background,
		VuoInputData(VuoReal, {"default":0.5}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":0.0}) rotate,
		VuoInputData(VuoReal, {"default":1.0}) feather,
		VuoInputData(VuoReal, {"default":0.0}) featherBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) position,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "fill", color);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "position", position);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoReal((*instance)->shader, "feather", feather);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gradientBias", featherBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotate", rotate);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
