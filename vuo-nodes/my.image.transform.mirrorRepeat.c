/**
 * @file
 * my.image.transform.mirrorRepeat node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MirrorRepeat
 * - Category: Operators/Lib/image/transform
 * - Source: external/tixl/Operators/Lib/image/transform/MirrorRepeat.cs
 * - Default: Image=null, RotateMirror=0, RotateImage=0, Width=1, Offset=0, OffsetEdge=0, Offsetimage=(0,0), ShadeAmount=0, ShadeColor=(0.000001,0.000001,0.000001,1), Resolution=(-1,-1) from MirrorRepeat.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/fx/MirrorRepeat.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_MirrorRepeat",
					 "description" : "TiXL MirrorRepeat bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/transform/MirrorRepeat.cs. Category: Operators/Lib/image/transform. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, RotateMirror=0, RotateImage=0, Width=1, Offset=0, OffsetEdge=0, Offsetimage=(0,0), ShadeAmount=0, ShadeColor=(0.000001,0.000001,0.000001,1), Resolution=(-1,-1).",
					 "keywords" : [ "tixl", "texture2d", "image", "mirror", "repeat", "MirrorRepeat.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D image;
	uniform vec2 imageSize;
	uniform vec2 targetSize;
	uniform float rotateMirror;
	uniform float rotateImage;
	uniform float width;
	uniform float offset;
	uniform float offsetEdge;
	uniform vec2 offsetImage;
	uniform float shadeAmount;
	uniform vec4 shadeColor;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		float safeWidth = max(abs(width), 0.0001);
		float rotateScreenRad = (-rotateMirror + rotateImage - 90.0) / 180.0 * 3.141578;
		float imageAspect = imageSize.x / max(imageSize.y, 1.0);
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float sina = sin(-rotateScreenRad - 3.141578 / 2.0);
		float cosa = cos(-rotateScreenRad - 3.141578 / 2.0);
		p = vec2(cosa * p.x - sina * p.y, cosa * p.y + sina * p.x);
		float mirrorRotationRad = (rotateImage - 90.0) / 180.0 * 3.141578;
		vec2 angle = vec2(sin(mirrorRotationRad), cos(mirrorRotationRad));
		float dist = dot(p, angle) + mod(offset, 2.0);
		float shade = 0.0;
		float d = 0.0;
		float mDist = mod(dist, 2.0 * safeWidth);
		if (dist > safeWidth)
		{
			if (mDist > safeWidth)
			{
				shade = 1.0;
				d = -2.0 * (mDist - safeWidth);
			}
		}
		else if (dist < 0.0)
		{
			mDist *= -1.0;
			if (mDist < safeWidth)
				shade = 1.0;
			else
				d = -2.0 * (mDist - safeWidth);
		}
		d -= dist - mDist;
		d += mod(offset, 2.0) + offsetEdge;
		p += d * angle;
		p.x /= aspectRatio;
		p *= vec2(aspectRatio / imageAspect, 1.0);
		p += vec2(0.5) + offsetImage * vec2(1.0 / imageAspect, 1.0);
		vec4 texColor = texture2D(image, 1.0 - abs(fract(p * 0.5) * 2.0 - 1.0));
		gl_FragColor = clamp(mix(texColor, shadeColor, shade * shadeAmount), 0.0, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_MirrorRepeat Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, VuoImage image, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0) return requested;
	if (image) return width ? image->pixelsWide : image->pixelsHigh;
	return 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":-360.0,"suggestedMax":360.0,"suggestedStep":1.0}) rotateMirror,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":-360.0,"suggestedMax":360.0,"suggestedStep":1.0}) rotateImage,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.001,"suggestedMax":8.0,"suggestedStep":0.01}) width,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":-4.0,"suggestedMax":4.0,"suggestedStep":0.01}) offset,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":-4.0,"suggestedMax":4.0,"suggestedStep":0.01}) offsetEdge,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offsetimage,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) shadeAmount,
		VuoInputData(VuoColor, {"default":{"r":0.000001,"g":0.000001,"b":0.000001,"a":1.0}}) shadeColor,
		VuoInputData(VuoPoint2d, {"default":{"x":-1.0,"y":-1.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	if (!image)
	{
		*textureOutput = NULL;
		return;
	}
	VuoInteger renderWidth = renderDimension(resolution, image, true);
	VuoInteger renderHeight = renderDimension(resolution, image, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "image", image);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "imageSize", (VuoPoint2d){image->pixelsWide, image->pixelsHigh});
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotateMirror", rotateMirror);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotateImage", rotateImage);
	VuoShader_setUniform_VuoReal((*instance)->shader, "width", width);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offsetEdge", offsetEdge);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offsetImage", offsetimage);
	VuoShader_setUniform_VuoReal((*instance)->shader, "shadeAmount", shadeAmount);
	VuoShader_setUniform_VuoColor((*instance)->shader, "shadeColor", shadeColor);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImage_getColorDepth(image));
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
