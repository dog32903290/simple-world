/**
 * @file
 * my.image.generate.noise.tileableNoise node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_TileableNoise
 * - Category: Operators/Lib/image/generate/noise
 * - Source: external/tixl/Operators/Lib/image/generate/noise/TileableNoise.cs
 * - Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), Detail=1, Octaves=2, Gain=0.5, Lacunarity=2, RandomPhase=5, Offset=(0,0), Contrast=1.7, GainAndBias=(0.5,0.5), Scale=1, Resolution=(1024,1024), GenerateMips=false, OutputFormat=R16G16B16A16_Float from TileableNoise.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for external/tixl/Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl.
 * Vuo body-layer limit: tileable fbm is preserved, while TiXL DXGI output format is ignored by VuoImageRenderer.
 */

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>

VuoModuleMetadata({
					 "title" : "my_TileableNoise",
					 "description" : "TiXL TileableNoise bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/generate/noise/TileableNoise.cs. Category: Operators/Lib/image/generate/noise. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: ColorA=(0,0,0,1), ColorB=(1,1,1,1), Detail=1, Octaves=2, Gain=0.5, Lacunarity=2, RandomPhase=5, Offset=(0,0), Contrast=1.7, GainAndBias=(0.5,0.5), Scale=1, Resolution=(1024,1024), GenerateMips=false, OutputFormat=R16G16B16A16_Float.",
					 "keywords" : [ "tixl", "texture2d", "image", "generate", "noise", "tileable", "PerlinNoise2d.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform int detail;
	uniform int octaves;
	uniform float gain;
	uniform float lacunarity;
	uniform float randomPhase;
	uniform vec2 offset;
	uniform float contrast;
	uniform vec2 gainAndBias;
	uniform float scale;
	varying vec2 fragmentTextureCoordinate;

	float hash(vec3 p)
	{
		p = fract(p * 0.1031);
		p += dot(p, p.yzx + 33.33);
		return fract((p.x + p.y) * p.z);
	}

	float tileHash(vec3 cell, vec3 period)
	{
		return hash(mod(cell, period));
	}

	float tileValueNoise(vec3 p, vec3 period)
	{
		vec3 i = floor(p);
		vec3 f = fract(p);
		f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
		float n000 = tileHash(i + vec3(0.0, 0.0, 0.0), period);
		float n100 = tileHash(i + vec3(1.0, 0.0, 0.0), period);
		float n010 = tileHash(i + vec3(0.0, 1.0, 0.0), period);
		float n110 = tileHash(i + vec3(1.0, 1.0, 0.0), period);
		float n001 = tileHash(i + vec3(0.0, 0.0, 1.0), period);
		float n101 = tileHash(i + vec3(1.0, 0.0, 1.0), period);
		float n011 = tileHash(i + vec3(0.0, 1.0, 1.0), period);
		float n111 = tileHash(i + vec3(1.0, 1.0, 1.0), period);
		float nx00 = mix(n000, n100, f.x);
		float nx10 = mix(n010, n110, f.x);
		float nx01 = mix(n001, n101, f.x);
		float nx11 = mix(n011, n111, f.x);
		return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
	}

	float applyGainAndBias(float v, vec2 gb)
	{
		float bias = clamp(gb.y, 0.0001, 0.9999);
		float g = clamp(gb.x, 0.0001, 0.9999);
		v = pow(clamp(v, 0.00001, 0.99999), log(bias) / log(0.5));
		return v < 0.5 ? 0.5 * pow(2.0 * v, log(g) / log(0.5)) : 1.0 - 0.5 * pow(2.0 - 2.0 * v, log(g) / log(0.5));
	}

	void main()
	{
		float d = max(float(detail), 1.0);
		vec3 p = vec3((fragmentTextureCoordinate * d + offset + vec2(666.0)) * max(scale, 0.0001), randomPhase);
		vec3 period = vec3(d, d, 1024.0);
		int steps = clamp(octaves, 1, 6);
		float sum = 0.0;
		float amp = 0.5;
		float norm = 0.0;
		float freq = 1.0;
		for (int i = 0; i < 6; ++i)
		{
			if (i < steps)
			{
				sum += amp * (tileValueNoise(p * freq, period * freq) * 2.0 - 1.0);
				norm += amp;
				freq *= max(lacunarity, 0.0001);
				amp *= gain;
			}
		}
		float v = (sum / max(norm, 0.0001)) * 0.5 * contrast + 0.5;
		v = applyGainAndBias(v, gainAndBias);
		gl_FragColor = mix(colorA, colorB, clamp(v, 0.0, 1.0));
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_TileableNoise Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}

static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 1024;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.000001,"b":0.000001,"a":1.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":0.99999,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoInteger, {"default":1,"suggestedMin":1,"suggestedMax":32,"suggestedStep":1}) detail,
		VuoInputData(VuoInteger, {"default":2,"suggestedMin":1,"suggestedMax":6,"suggestedStep":1}) octaves,
		VuoInputData(VuoReal, {"default":0.5}) gain,
		VuoInputData(VuoReal, {"default":2.0}) lacunarity,
		VuoInputData(VuoReal, {"default":5.0}) randomPhase,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) offset,
		VuoInputData(VuoReal, {"default":1.7}) contrast,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoPoint2d, {"default":{"x":1024.0,"y":1024.0}}) resolution,
		VuoInputData(VuoBoolean, {"default":false}) generateMips,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "detail", detail);
	VuoShader_setUniform_VuoInteger((*instance)->shader, "octaves", octaves);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gain", gain);
	VuoShader_setUniform_VuoReal((*instance)->shader, "lacunarity", lacunarity);
	VuoShader_setUniform_VuoReal((*instance)->shader, "randomPhase", randomPhase);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "offset", offset);
	VuoShader_setUniform_VuoReal((*instance)->shader, "contrast", contrast);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoReal((*instance)->shader, "scale", scale);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
