/**
 * @file
 * my.image.generate.basic.nGon node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_NGon
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/NGon.cs
 * - Default: Image=null, Fill=(1,1,1,1), Background=(0,0,0,0), Sides=3, Radius=0.25, Curvature=0, Blades=0, Feather=0.05, Round=0, FeatherBias=0, Position=(0,0), Rotate=-90, Resolution=(0,0), BlendMode=0 from NGon.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/NGon.hlsl.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_NGon",
					 "description" : "TiXL NGon bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/NGon.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Fill=(1,1,1,1), Background=(0,0,0,0), Sides=3, Radius=0.25, Curvature=0, Blades=0, Feather=0.05, Round=0, FeatherBias=0, Position=(0,0), Rotate=-90, Resolution=(0,0), BlendMode=0.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "ngon", "NGon.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 fill;
	uniform vec4 background;
	uniform vec2 position;
	uniform float roundness;
	uniform float feather;
	uniform float gradientBias;
	uniform float rotate;
	uniform float sides;
	uniform float radius;
	uniform float curvature;
	uniform float blades;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;
	const float TAU = 6.283185307;

	float sdNgon(vec2 p, float r, float n)
	{
		float invN = 1.0 / max(n, 3.0);
		vec2 rp = vec2(atan(p.y, p.x), length(p));
		rp.x /= TAU;
		rp.x = mod(rp.x + invN * 0.5, invN) - 0.5 * invN;
		rp.x *= rp.x > 0.0 ? (1.0 - clamp(blades, 0.0, 1.0)) : 1.0;
		rp.y = mix(rp.y, r, clamp(curvature, 0.0, 1.0));
		rp.x *= TAU;
		p = vec2(cos(rp.x), sin(rp.x)) * rp.y;
		vec2 b = vec2(r, r * tan(TAU * invN * 0.5));
		vec2 d = abs(p) - b;
		return length(max(d, vec2(0.0))) + min(d.x, 0.0);
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float imageRotationRad = (-rotate - 90.0) / 180.0 * 3.141578;
		float sina = sin(-imageRotationRad - 3.141578 / 2.0);
		float cosa = cos(-imageRotationRad - 3.141578 / 2.0);
		p = vec2(cosa * p.x - sina * p.y, cosa * p.y + sina * p.x);
		p += position.yx;
		float d = sdNgon(p, radius, sides);
		d = smoothstep(roundness / 2.0 - feather / 4.0, roundness / 2.0 + feather / 4.0, d);
		float dBiased = gradientBias >= 0.0 ? pow(d, gradientBias + 1.0) : 1.0 - pow(clamp(1.0 - d, 0.0, 10.0), -gradientBias + 1.0);
		gl_FragColor = mix(fill, background, dBiased);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_NGon Shader");
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
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) fill,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}) background,
		VuoInputData(VuoReal, {"default":3.0,"suggestedMin":3.0,"suggestedMax":24.0,"suggestedStep":1.0}) sides,
		VuoInputData(VuoReal, {"default":0.25}) radius,
		VuoInputData(VuoReal, {"default":0.0}) curvature,
		VuoInputData(VuoReal, {"default":0.0}) blades,
		VuoInputData(VuoReal, {"default":0.05}) feather,
		VuoInputData(VuoReal, {"default":0.0}) round,
		VuoInputData(VuoReal, {"default":0.0}) featherBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) position,
		VuoInputData(VuoReal, {"default":-90.0}) rotate,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "fill", fill);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "position", position);
	VuoShader_setUniform_VuoReal((*instance)->shader, "roundness", round);
	VuoShader_setUniform_VuoReal((*instance)->shader, "feather", feather);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gradientBias", featherBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotate", rotate);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sides", sides);
	VuoShader_setUniform_VuoReal((*instance)->shader, "radius", radius);
	VuoShader_setUniform_VuoReal((*instance)->shader, "curvature", curvature);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blades", blades);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
