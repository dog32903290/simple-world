/**
 * @file
 * my.image.fx.glitch.rgbTv node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_RgbTV
 * - Category: Operators/Lib/image/fx/glitch
 * - Source: external/tixl/Operators/Lib/image/fx/glitch/RgbTV.cs
 * - Default: Image=null, Visibility=1, PatternAmount=0.2, ImageBrightess=0.5, BlackLevel=-0.100000024, ImageContrast=1, PatternSize=0.025, ShiftColumns=0.5, Gaps=0.03, GlitchAmount=1, Noise=0.1, Buldge=0.15, Vignette=1, Resolution=(0,0) from RgbTV.t3
 * - Primary output: Texture2D TextureOutput (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl.
 * Vuo body-layer limit: DX11 render targets, compute sorting, override buffers, gradients, mips, and multi-pass feedback are represented by a single-pass visual proof.
 */

VuoModuleMetadata({
					 "title" : "my_RgbTV",
					 "description" : "TiXL RgbTV bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/fx/glitch/RgbTV.cs. Category: Operators/Lib/image/fx/glitch. Primary output: Texture2D TextureOutput (ColorForTextures #9F008A). Default: Image=null, Visibility=1, PatternAmount=0.2, ImageBrightess=0.5, BlackLevel=-0.100000024, ImageContrast=1, PatternSize=0.025, ShiftColumns=0.5, Gaps=0.03, GlitchAmount=1, Noise=0.1, Buldge=0.15, Vignette=1, Resolution=(0,0).",
					 "keywords" : [ "tixl", "texture2d", "image", "fx", "glitch", "RgbTV", "RgbTV.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
					 "version" : "1.0.0",
					 "dependencies" : [ "VuoImageRenderer" ],
				 });

#include "VuoImageRenderer.h"
#include "VuoPoint2d.h"
#include <math.h>


static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D sourceImage;
	uniform int filterKind;
	uniform vec2 targetSize;

	uniform int rows;
	uniform int columns;
	uniform float size;
	uniform float amount;
	uniform float threshold;
	uniform vec2 stretch;
	uniform vec2 offset;
	uniform vec2 scatter;
	uniform vec4 colorize;
	uniform float colorRatio;
	uniform int seed;
	uniform int mode;

	uniform float visibility;
	uniform float patternAmount;
	uniform float imageBrightness;
	uniform float blackLevel;
	uniform float imageContrast;
	uniform float patternSize;
	uniform float shiftColumns;
	uniform float gaps;
	uniform float glitchAmount;
	uniform float noise;
	uniform float buldge;
	uniform float vignette;

	uniform bool vertical;
	uniform bool scanHighlights;
	uniform float extend;
	uniform vec4 backgroundColor;
	uniform vec4 streakColor;
	uniform float gradientBias;
	uniform float scatterThreshold;
	uniform float addGrain;
	uniform float maxSteps;
	uniform float fadeStreaks;
	uniform float lumaBias;

	uniform int maxSubdivisions;
	uniform bool useAspectForSplit;
	uniform float splitCenter;
	uniform float splitCenterVariation;
	uniform float directionBias;
	uniform vec2 scrollOffset;
	uniform float randomPhase;
	uniform int randomSeed;
	uniform float gapWidth;
	uniform float feather;
	uniform vec4 gapColor;
	uniform float textureFx;

	varying vec2 fragmentTextureCoordinate;

	float hash(vec2 p)
	{
		p += float(seed + randomSeed) * 0.013;
		return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
	}

	vec4 source(vec2 uv)
	{
		return texture2D(sourceImage, clamp(uv, 0.0, 1.0));
	}

	float luma(vec3 c)
	{
		return dot(c, vec3(0.299, 0.587, 0.114));
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec2 px = 1.0 / max(targetSize, vec2(1.0));
		vec4 original = source(uv);
		vec4 color = original;

		if (filterKind == 0)
		{
			vec2 cells = max(vec2(float(columns), float(rows)), vec2(1.0));
			vec2 cell = floor((uv + offset * 0.05) * cells);
			float r = hash(cell + vec2(float(mode), 13.0));
			float gate = step(threshold, r);
			float rowPulse = step(0.66, hash(vec2(cell.y, float(seed))));
			vec2 dir = vec2(hash(cell) - 0.5, hash(cell.yx + scatter * 17.0) - 0.5);
			dir *= vec2(stretch.x, stretch.y) * px * amount * size * 10.0;
			vec4 displaced = source(uv + dir * gate * rowPulse);
			color = mix(displaced, vec4(displaced.rgb * (1.0 - colorRatio) + colorize.rgb * colorRatio, displaced.a), gate * rowPulse);
		}
		else if (filterKind == 1)
		{
			vec2 centered = uv - 0.5;
			float d = dot(centered, centered);
			vec2 bulged = uv + centered * d * buldge;
			float line = sin((uv.y / max(patternSize, 0.002)) * 3.14159265);
			float gap = step(1.0 - gaps, fract(uv.y / max(patternSize * 2.0, 0.002)));
			float n = hash(floor(vec2(uv.y * 120.0, uv.x * 24.0)));
			float rowShift = (hash(vec2(floor(uv.y * 32.0), 4.0)) - 0.5) * shiftColumns * glitchAmount * 0.035;
			vec2 guv = bulged + vec2(rowShift + (n - 0.5) * noise * 0.02, 0.0);
			float ch = (1.0 + patternAmount) * px.x * 5.0;
			color.r = source(guv + vec2(ch, 0.0)).r;
			color.g = source(guv).g;
			color.b = source(guv - vec2(ch, 0.0)).b;
			color.rgb = (color.rgb + imageBrightness + blackLevel - 0.5) * imageContrast + 0.5;
			color.rgb *= mix(1.0, 0.75 + 0.25 * line, patternAmount);
			color.rgb += (n - 0.5) * noise;
			color.rgb *= 1.0 - gap * 0.65;
			color.rgb *= mix(1.0, smoothstep(0.62, 0.08, d), vignette);
			color = mix(original, color, visibility);
		}
		else if (filterKind == 2)
		{
			float axis = vertical ? uv.x : uv.y;
			float across = vertical ? uv.y : uv.x;
			float lum = luma(original.rgb) + lumaBias;
			float hot = scanHighlights ? step(threshold, lum) : step(lum, threshold + 0.35);
			float streak = smoothstep(0.0, 1.0, fract(axis * max(maxSteps, 1.0) * 0.006 + offset));
			float scatterGate = step(scatterThreshold, hash(vec2(floor(across * 64.0), floor(axis * 8.0))));
			float pull = (extend + gradientBias * streak + 0.08) * hot * scatterGate;
			vec2 sampleUv = uv + (vertical ? vec2(0.0, pull * 0.09) : vec2(pull * 0.09, 0.0));
			vec4 sorted = source(sampleUv);
			sorted.rgb = mix(sorted.rgb, streakColor.rgb, hot * 0.25);
			sorted.rgb += (hash(uv * targetSize) - 0.5) * addGrain;
			color = mix(mix(backgroundColor, sorted, sorted.a), sorted, 1.0 - fadeStreaks * 0.5);
		}
		else
		{
			vec2 p = uv + scrollOffset * 0.05;
			float bands = pow(2.0, clamp(float(maxSubdivisions) * 0.18, 1.0, 7.0));
			vec2 cell = floor(p * bands);
			float r = hash(cell + randomPhase);
			float split = splitCenter + (r - 0.5) * splitCenterVariation;
			bool xMajor = useAspectForSplit ? targetSize.x > targetSize.y : (r + directionBias > 0.5);
			vec2 local = fract(p * bands);
			float gap = xMajor ? smoothstep(gapWidth + feather, gapWidth, abs(local.x - split)) : smoothstep(gapWidth + feather, gapWidth, abs(local.y - split));
			vec2 warped = uv;
			if (xMajor)
				warped.x = (floor(p.x * bands) + split + (local.x - split) * (1.0 + textureFx)) / bands;
			else
				warped.y = (floor(p.y * bands) + split + (local.y - split) * (1.0 + textureFx)) / bands;
			color = source(warped);
			color = mix(color, gapColor, gap * smoothstep(0.0, 1.0, threshold + 0.6));
		}

		gl_FragColor = clamp(color, 0.0, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_RgbTV Shader");
	VuoShader_addSource(instance->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(instance->shader);
	return instance;
}
static VuoInteger renderDimension(VuoPoint2d resolution, bool width)
{
	VuoInteger requested = (VuoInteger)llround(width ? resolution.x : resolution.y);
	return requested > 0 ? requested : 320;
}
static VuoImage imageOrFallback(VuoImage image, VuoInteger width, VuoInteger height)
{
	if (image) return image;
	return VuoImage_makeColorImage((VuoColor){0.08, 0.09, 0.12, 1.0}, (unsigned int)width, (unsigned int)height);
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) image,
		VuoInputData(VuoReal, {"default":1.0}) visibility,
		VuoInputData(VuoReal, {"default":0.2}) patternAmount,
		VuoInputData(VuoReal, {"default":0.5}) imageBrightness,
		VuoInputData(VuoReal, {"default":-0.100000024}) blackLevel,
		VuoInputData(VuoReal, {"default":1.0}) imageContrast,
		VuoInputData(VuoReal, {"default":0.025}) patternSize,
		VuoInputData(VuoReal, {"default":0.5}) shiftColumns,
		VuoInputData(VuoReal, {"default":0.03}) gaps,
		VuoInputData(VuoReal, {"default":1.0}) glitchAmount,
		VuoInputData(VuoReal, {"default":0.1}) noise,
		VuoInputData(VuoReal, {"default":0.15}) buldge,
		VuoInputData(VuoReal, {"default":1.0}) vignette,

		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"TextureOutput"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "sourceImage", imageOrFallback(image, renderWidth, renderHeight));
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", 1);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoReal((*instance)->shader, "visibility", visibility);
	VuoShader_setUniform_VuoReal((*instance)->shader, "patternAmount", patternAmount);
	VuoShader_setUniform_VuoReal((*instance)->shader, "imageBrightness", imageBrightness);
	VuoShader_setUniform_VuoReal((*instance)->shader, "blackLevel", blackLevel);
	VuoShader_setUniform_VuoReal((*instance)->shader, "imageContrast", imageContrast);
	VuoShader_setUniform_VuoReal((*instance)->shader, "patternSize", patternSize);
	VuoShader_setUniform_VuoReal((*instance)->shader, "shiftColumns", shiftColumns);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gaps", gaps);
	VuoShader_setUniform_VuoReal((*instance)->shader, "glitchAmount", glitchAmount);
	VuoShader_setUniform_VuoReal((*instance)->shader, "noise", noise);
	VuoShader_setUniform_VuoReal((*instance)->shader, "buldge", buldge);
	VuoShader_setUniform_VuoReal((*instance)->shader, "vignette", vignette);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
