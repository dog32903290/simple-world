/**
 * @file
 * my.image.generate.basic.checkerBoard node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_CheckerBoard
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/CheckerBoard.cs
 * - Default: ColorA=(0.20212764,0.20212561,0.20212561,1), ColorB=(0.12056738,0.120566174,0.120566174,1), Stretch=(1,1), Scale=1, UseAspectRatio=true, Offset=(0,0), Resolution=(0,0), GenerateMips=false from CheckerBoard.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/CheckerBoard.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_CheckerBoard",
					 "description" : "TiXL CheckerBoard Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/CheckerBoard.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: ColorA=(0.20212764,0.20212561,0.20212561,1), ColorB=(0.12056738,0.120566174,0.120566174,1), Stretch=(1,1), Scale=1, UseAspectRatio=true, Offset=(0,0), Resolution=(0,0), GenerateMips=false.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "checkerboard", "CheckerBoard.hlsl", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 stretch;
	uniform bool useAspectRatio;
	uniform float scale;
	uniform vec2 offset;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate;
		if (useAspectRatio)
		{
			p -= vec2(0.5);
			p.x *= aspectRatio;
		}
		p /= max(abs(stretch * scale), vec2(0.0001));
		p += offset * vec2(-1.0, 1.0);
		vec2 a = fract(p);
		float t = ((a.x > 0.5 && a.y < 0.5) || (a.x < 0.5 && a.y > 0.5)) ? 0.0 : 1.0;
		gl_FragColor = mix(colorA, colorB, t);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_CheckerBoard Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	if (requested > 0) return requested;
	return 160;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":0.20212764,"g":0.20212561,"b":0.20212561,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.12056738,"g":0.120566174,"b":0.120566174,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.01,"suggestedMax":16.0,"suggestedStep":0.01}) scale,
		VuoInputData(VuoBoolean, {"default":true}) useAspectRatio,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "stretch", stretch);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "useAspectRatio", useAspectRatio);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
