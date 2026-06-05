/**
 * @file
 * my.image.transform.transformImage node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_TransformImage
 * - Category: Operators/Lib/image/transform
 * - Source: external/tixl/Operators/Lib/image/transform/TransformImage.cs
 * - Default: Image=null, Offset=(0,0), Stretch=(1,1), Scale=1, Rotation=0, Resolution=(0,0), ResolutionFactor=(1,1), GenerateMips=false, Filter=MinMagMipLinear, WrapMode=2 from TransformImage.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/fx/TransformImage.hlsl.
 * Vuo body-layer limit: filter/mipmap and TiXL DX11 sampler modes are approximated;
 * the visible offset/stretch/scale/rotation/wrap sampling law is preserved.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_TransformImage",
					 "description" : "TiXL TransformImage bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/transform/TransformImage.cs. Category: Operators/Lib/image/transform. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Offset=(0,0), Stretch=(1,1), Scale=1, Rotation=0, Resolution=(0,0), ResolutionFactor=(1,1), GenerateMips=false, Filter=MinMagMipLinear, WrapMode=2.",
					 "keywords" : [ "tixl", "texture2d", "image", "transform", "TransformImage.hlsl", "bounded approximation", "wrapMode", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D image;
	uniform vec2 sourceSize;
	uniform vec2 targetSize;
	uniform vec2 offset;
	uniform vec2 stretch;
	uniform float scale;
	uniform float rotation;
	uniform int wrapMode;
	varying vec2 fragmentTextureCoordinate;

	vec2 wrapUv(vec2 p)
	{
		if (wrapMode == 0)
			return fract(p);
		if (wrapMode == 1 || wrapMode == 4)
			return 1.0 - abs(fract(p * 0.5) * 2.0 - 1.0);
		return clamp(p, 0.0, 1.0);
	}

	void main()
	{
		float sourceAspectRatio = sourceSize.x / max(sourceSize.y, 1.0);
		float targetAspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 safeStretch = max(abs(stretch), vec2(0.0001));
		float safeScale = max(abs(scale), 0.0001);
		vec2 divisions = vec2(sourceAspectRatio / safeStretch.x, 1.0 / safeStretch.y) / safeScale;
		vec2 p = fragmentTextureCoordinate + offset * vec2(-1.0, 1.0) - vec2(0.5);
		float imageRotationRad = (-rotation - 90.0) / 180.0 * 3.141578;
		float sina = sin(-imageRotationRad - 3.141578 / 2.0);
		float cosa = cos(-imageRotationRad - 3.141578 / 2.0);
		p.x *= sourceAspectRatio;
		p = vec2(cosa * p.x - sina * p.y, cosa * p.y + sina * p.x);
		p.x *= targetAspectRatio / sourceAspectRatio;
		vec2 samplePos = p * divisions + vec2(0.5);
		bool outside = samplePos.x < 0.0 || samplePos.x > 1.0 || samplePos.y < 0.0 || samplePos.y > 1.0;
		vec2 uv = wrapUv(samplePos);
		vec4 color = texture2D(image, uv);
		if ((wrapMode == 2 || wrapMode == 3 || wrapMode == 4) && outside)
			color = wrapMode == 3 ? vec4(0.0) : texture2D(image, clamp(samplePos, 0.0, 1.0));
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_TransformImage Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, VuoPoint2d factor, VuoImage image, bool width)
{
	VuoReal requested = width ? resolution.x : resolution.y;
	VuoReal scale = width ? factor.x : factor.y;
	VuoInteger base = (VuoInteger)llround(requested);
	if (base <= 0 && image)
		base = width ? image->pixelsWide : image->pixelsHigh;
	if (base <= 0)
		base = 160;
	VuoInteger value = (VuoInteger)llround((VuoReal)base * (scale == 0 ? 1.0 : scale));
	if (value < 1) return 1;
	if (value > 4096) return 4096;
	return value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.01,"suggestedMax":8.0,"suggestedStep":0.01}) scale,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":-360.0,"suggestedMax":360.0,"suggestedStep":1.0}) rotation,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) resolutionFactor,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoInputData(VuoInteger, {"default":2,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) wrapMode,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	if (!image)
	{
		*textureOutput = NULL;
		return;
	}
	VuoInteger renderWidth = renderDimension(resolution, resolutionFactor, image, true);
	VuoInteger renderHeight = renderDimension(resolution, resolutionFactor, image, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "image", image);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "sourceSize", (VuoPoint2d){image->pixelsWide, image->pixelsHigh});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotation", rotation);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "wrapMode", wrapMode);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(image));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
