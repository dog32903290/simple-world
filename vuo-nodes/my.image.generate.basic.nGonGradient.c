/**
 * @file
 * my.image.generate.basic.nGonGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_NGonGradient
 * - Category: Operators/Lib/image/generate/basic
 * - Source: external/tixl/Operators/Lib/image/generate/basic/NGonGradient.cs
 * - Default: Position=(0,0), Sides=5, Radius=0.33, Curvature=0, Roundness=1, Blades=0, Rotate=180, Gradient=white-to-black, Width=0.14, Offset=0, PingPong=false, Repeat=false, BiasAndGain=(0.5,0.5), BlendMode=0, Resolution=(0,0), Image=null from NGonGradient.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/NGonGradient.hlsl.
 * Vuo body-layer limit: TiXL Gradient datatype is represented as colorA/colorB.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_NGonGradient",
					 "description" : "TiXL NGonGradient bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/basic/NGonGradient.cs. Category: Operators/Lib/image/generate/basic. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Position=(0,0), Sides=5, Radius=0.33, Curvature=0, Roundness=1, Blades=0, Rotate=180, Gradient=white-to-black, Width=0.14, Offset=0, PingPong=false, Repeat=false, BiasAndGain=(0.5,0.5), BlendMode=0, Resolution=(0,0), Image=null.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "ngon", "gradient", "NGonGradient.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec2 position;
	uniform float sides;
	uniform float radius;
	uniform float curvature;
	uniform float blades;
	uniform float roundness;
	uniform float rotate;
	uniform float width;
	uniform float offset;
	uniform bool pingPong;
	uniform bool repeat;
	uniform vec2 targetSize;
	varying vec2 fragmentTextureCoordinate;

	float pingPongRepeat(float x)
	{
		float baseValue = x;
		float repeatValue = fract(baseValue);
		float pingPongValue = 1.0 - abs(fract(x * 0.5) * 2.0 - 1.0);
		float singlePingPong = abs(x);
		float value = repeat ? repeatValue : baseValue;
		value = pingPong ? (repeat ? pingPongValue : singlePingPong) : value;
		return repeat ? value : clamp(value, 0.0, 1.0);
	}

	float sdRegularPolygon(vec2 p, float r, float n)
	{
		float an = 3.141593 / max(n, 3.0);
		vec2 acs = vec2(cos(an), sin(an));
		float originalLen = length(p);
		float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
		bn *= bn > 0.0 ? (1.0 - clamp(blades, 0.0, 1.0)) : 1.0;
		p = length(p) * vec2(cos(bn), abs(sin(bn)));
		p -= r * acs;
		p.y += clamp(-p.y, 0.0, r * acs.y);
		p.y *= p.y > 0.0 ? clamp(roundness, 0.0, 1.0) : 1.0;
		float dist = length(p) * sign(p.x);
		dist += (r - originalLen) * curvature;
		return dist;
	}

	void main()
	{
		float aspectRatio = targetSize.x / max(targetSize.y, 1.0);
		vec2 p = fragmentTextureCoordinate - vec2(0.5);
		p.x *= aspectRatio;
		float a = rotate / 180.0 * 3.141578;
		float ca = cos(a);
		float sa = sin(a);
		p = vec2(p.x * ca + p.y * sa, p.x * sa - p.y * ca);
		p += position.yx;
		float c = sdRegularPolygon(p, radius, sides) * 2.0 - offset * width;
		c = pingPongRepeat(c / max(abs(width), 0.0001));
		gl_FragColor = mix(colorA, colorB, clamp(c, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_NGonGradient Shader");
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
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) colorB,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) position,
		VuoInputData(VuoReal, {"default":5.0,"suggestedMin":3.0,"suggestedMax":24.0,"suggestedStep":1.0}) sides,
		VuoInputData(VuoReal, {"default":0.33}) radius,
		VuoInputData(VuoReal, {"default":0.0}) curvature,
		VuoInputData(VuoReal, {"default":1.0}) roundness,
		VuoInputData(VuoReal, {"default":0.0}) blades,
		VuoInputData(VuoReal, {"default":180.0}) rotate,
		VuoInputData(VuoReal, {"default":0.14}) width,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoInputData(VuoBoolean, {"default":false}) pingPong,
		VuoInputData(VuoBoolean, {"default":false}) repeat,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) biasAndGain,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "position", position);
	VuoShader_setUniform_VuoReal((*instance)->shader, "sides", sides);
	VuoShader_setUniform_VuoReal((*instance)->shader, "radius", radius);
	VuoShader_setUniform_VuoReal((*instance)->shader, "curvature", curvature);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blades", blades);
	VuoShader_setUniform_VuoReal((*instance)->shader, "roundness", roundness);
	VuoShader_setUniform_VuoReal((*instance)->shader, "rotate", rotate);
	VuoShader_setUniform_VuoReal((*instance)->shader, "width", width);
	VuoShader_setUniform_VuoReal((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "pingPong", pingPong);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "repeat", repeat);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
