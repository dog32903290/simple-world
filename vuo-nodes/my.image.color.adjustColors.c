/**
 * @file
 * my.image.color.adjustColors node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_AdjustColors
 * - Category: Operators/Lib/image/color
 * - Source: external/tixl/Operators/Lib/image/color/AdjustColors.cs
 * - Default: Texture2d=null, Exposure=1, Brightness=0, Contrast=0, Saturation=1, Hue=0, Vignette=0, OrangeTeal=0, Colorize=(1,1,1,0), Background=(0,0,0,1) from AdjustColors.t3
 * - Primary output: Texture2D Output (ColorForTextures #9F008A)
 *
 * Bounded Vuo shader adapter for Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl.
 * Vuo body-layer limit: DXGI formats, compute passes, depth gradients, LUT precision, mips, and multi-pass texture setup are represented by a single-pass color proof.
 */

VuoModuleMetadata({
					 "title" : "my_AdjustColors",
					 "description" : "TiXL AdjustColors bounded Vuo shader adapter. Source: external/tixl/Operators/Lib/image/color/AdjustColors.cs. Category: Operators/Lib/image/color. Primary output: Texture2D Output (ColorForTextures #9F008A). Default: Texture2d=null, Exposure=1, Brightness=0, Contrast=0, Saturation=1, Hue=0, Vignette=0, OrangeTeal=0, Colorize=(1,1,1,0), Background=(0,0,0,1).",
					 "keywords" : [ "tixl", "texture2d", "image", "color", "AdjustColors", "AdjustColors.hlsl", "bounded approximation", "ColorForTextures", "#9F008A" ],
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
	uniform float exposure;
	uniform float brightness;
	uniform float contrast;
	uniform float saturation;
	uniform float hue;
	uniform float amount;
	uniform float mode;
	uniform float gammaValue;
	uniform bool enable;
	uniform bool clampResult;
	uniform bool correctGamma;
	uniform bool dontColorAlpha;
	uniform vec2 gainAndBias;
	uniform vec2 center;
	uniform vec4 colorA;
	uniform vec4 colorB;
	uniform vec4 colorC;
	uniform vec4 colorD;
	uniform vec4 background;
	uniform vec4 keyColor;
	varying vec2 fragmentTextureCoordinate;

	vec4 source(vec2 uv)
	{
		return texture2D(sourceImage, clamp(uv, 0.0, 1.0));
	}

	float luma(vec3 c)
	{
		return dot(c, vec3(0.299, 0.587, 0.114));
	}

	vec3 rgb2hsv(vec3 c)
	{
		vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
		vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
		vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
		float d = q.x - min(q.w, q.y);
		float e = 1.0e-10;
		return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
	}

	vec3 hsv2rgb(vec3 c)
	{
		vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
		return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
	}

	vec3 applyHueSat(vec3 rgb, float h, float s)
	{
		vec3 hsv = rgb2hsv(max(rgb, vec3(0.0)));
		hsv.x = fract(hsv.x + h);
		hsv.y *= s;
		return hsv2rgb(hsv);
	}

	void main()
	{
		vec2 uv = fragmentTextureCoordinate;
		vec4 original = source(uv);
		vec4 color = original;
		float lum = luma(original.rgb);

		if (filterKind == 0)
		{
			color.rgb = applyHueSat(color.rgb, hue, saturation);
			color.rgb = (color.rgb - 0.5) * (1.0 + contrast) + 0.5 + brightness;
			color.rgb *= exposure;
			color.rgb = mix(color.rgb, mix(color.rgb, colorA.rgb, colorA.a), amount);
			vec2 p = uv - 0.5 - center;
			float vig = smoothstep(0.72, 0.25, length(p));
			color.rgb = mix(background.rgb, color.rgb, vig);
		}
		else if (filterKind == 1)
		{
			vec4 r = colorA * original.r;
			vec4 g = colorB * original.g;
			vec4 b = colorC * original.b;
			color = r + g + b + colorD;
		}
		else if (filterKind == 2 || filterKind == 3)
		{
			vec3 lifted = color.rgb + (colorA.rgb - 0.5) * colorA.a;
			vec3 gammaAdjusted = pow(max(lifted, vec3(0.0)), vec3(1.0 / max(colorB.a * 2.0, 0.05))) + (colorB.rgb - 0.5) * 0.15;
			color.rgb = gammaAdjusted * (1.0 + (colorC.rgb - 0.5) * 2.0 * colorC.a);
			color.rgb = mix(vec3(luma(color.rgb)), color.rgb, saturation);
			if (filterKind == 3)
			{
				float depthApprox = uv.y;
				vec3 depthTint = mix(colorD.rgb, colorC.rgb, smoothstep(0.1, 1.0, depthApprox));
				color.rgb = mix(color.rgb, depthTint, 0.22);
			}
		}
		else if (filterKind == 4)
		{
			if (mode < 0.5)
				color.rgb = vec3(lum);
			else if (mode < 1.5)
				color.rgb = rgb2hsv(color.rgb);
			else
				color.rgb = applyHueSat(color.rgb, 0.08, 1.2);
		}
		else if (filterKind == 5)
		{
			if (enable)
				color.rgb = floor(clamp(color.rgb * exposure, 0.0, 1.0) * 31.0) / 31.0;
		}
		else if (filterKind == 6)
		{
			color.rgb = applyHueSat(color.rgb, hue, saturation) * exposure;
		}
		else if (filterKind == 7)
		{
			float dist = distance(color.rgb, keyColor.rgb);
			float matte = smoothstep(amount, amount + 0.25, dist + brightness * lum);
			color = mix(background, vec4(color.rgb * exposure, color.a), matte);
		}
		else if (filterKind == 8)
		{
			float t = clamp(lum * exposure * gainAndBias.x + gainAndBias.y - 0.5 + mode * 0.03, 0.0, 1.0);
			color.rgb = mix(colorA.rgb, colorB.rgb, t);
			if (!dontColorAlpha)
				color.a = mix(colorA.a, colorB.a, t) * original.a;
		}
		else if (filterKind == 9)
		{
			vec3 mapped = mix(colorA.rgb, colorB.rgb, clamp(lum * exposure * gainAndBias.x + gainAndBias.y - 0.5, 0.0, 1.0));
			color.rgb = mix(color.rgb, mapped, amount);
		}
		else
		{
			vec3 hdr = color.rgb * exposure;
			if (mode < 1.5)
				color.rgb = hdr / (hdr + vec3(1.0));
			else if (mode < 3.5)
				color.rgb = 1.0 - exp(-hdr);
			else
				color.rgb = clamp((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
			if (correctGamma)
				color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / max(gammaValue, 0.05)));
		}

		if (clampResult)
			color = clamp(color, 0.0, 1.0);
		gl_FragColor = color;
	}
);

struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->shader = VuoShader_make("my_AdjustColors Shader");
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
	return VuoImage_makeColorImage((VuoColor){0.35, 0.35, 0.35, 1.0}, (unsigned int)width, (unsigned int)height);
}
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) texture2d,
		VuoInputData(VuoReal, {"default":1.0}) exposure,
		VuoInputData(VuoReal, {"default":0.0}) brightness,
		VuoInputData(VuoReal, {"default":0.0}) contrast,
		VuoInputData(VuoReal, {"default":1.0}) saturation,
		VuoInputData(VuoReal, {"default":0.0}) hue,
		VuoInputData(VuoReal, {"default":1.0}) amount,
		VuoInputData(VuoReal, {"default":0.0}) mode,
		VuoInputData(VuoReal, {"default":2.2}) gammaValue,
		VuoInputData(VuoBoolean, {"default":true}) enable,
		VuoInputData(VuoBoolean, {"default":true}) clampResult,
		VuoInputData(VuoBoolean, {"default":false}) correctGamma,
		VuoInputData(VuoBoolean, {"default":false}) dontColorAlpha,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.5}}) gainAndBias,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) center,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":0.0}}) colorA,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) colorB,
		VuoInputData(VuoColor, {"default":{"r":0.5,"g":0.5,"b":0.5,"a":0.506}}) colorC,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":0.0}}) colorD,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) background,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) keyColor,
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) resolution,
		VuoOutputData(VuoImage, {"name":"Output"}) textureOutput
)
{
	VuoInteger renderWidth = renderDimension(resolution, true);
	VuoInteger renderHeight = renderDimension(resolution, false);
	VuoShader_setUniform_VuoImage((*instance)->shader, "sourceImage", imageOrFallback(texture2d, renderWidth, renderHeight));
	VuoShader_setUniform_VuoInteger((*instance)->shader, "filterKind", 0);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "targetSize", (VuoPoint2d){renderWidth, renderHeight});
	VuoShader_setUniform_VuoReal((*instance)->shader, "exposure", exposure);
	VuoShader_setUniform_VuoReal((*instance)->shader, "brightness", brightness);
	VuoShader_setUniform_VuoReal((*instance)->shader, "contrast", contrast);
	VuoShader_setUniform_VuoReal((*instance)->shader, "saturation", saturation);
	VuoShader_setUniform_VuoReal((*instance)->shader, "hue", hue);
	VuoShader_setUniform_VuoReal((*instance)->shader, "amount", amount);
	VuoShader_setUniform_VuoReal((*instance)->shader, "mode", mode);
	VuoShader_setUniform_VuoReal((*instance)->shader, "gammaValue", gammaValue);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "enable", enable);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "clampResult", clampResult);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "correctGamma", correctGamma);
	VuoShader_setUniform_VuoBoolean((*instance)->shader, "dontColorAlpha", dontColorAlpha);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "gainAndBias", gainAndBias);
	VuoShader_setUniform_VuoPoint2d((*instance)->shader, "center", center);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorA", colorA);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorB", colorB);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorC", colorC);
	VuoShader_setUniform_VuoColor((*instance)->shader, "colorD", colorD);
	VuoShader_setUniform_VuoColor((*instance)->shader, "background", background);
	VuoShader_setUniform_VuoColor((*instance)->shader, "keyColor", keyColor);
	*textureOutput = VuoImageRenderer_render((*instance)->shader, renderWidth, renderHeight, VuoImageColorDepth_8);
}
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
