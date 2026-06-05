#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch39ColorProof","description":"Proof-only compositor for Batch 39 color nodes.","keywords":["tixl","batch39","color","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D adjustColorsImage;
	uniform sampler2D channelMixerImage;
	uniform sampler2D colorGradeImage;
	uniform sampler2D colorGradeDepthImage;
	uniform sampler2D convertColorsImage;
	uniform sampler2D convertFormatImage;
	uniform sampler2D hseImage;
	uniform sampler2D keyColorImage;
	uniform sampler2D remapColorImage;
	uniform sampler2D tintImage;
	uniform sampler2D toneMappingImage;
	varying vec2 fragmentTextureCoordinate;
	void main() {
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 11.0);
		vec2 localSt = vec2(fract(st.x * 11.0), st.y);
		vec4 color = (band < 1.0 ? texture2D(adjustColorsImage, localSt) : (band < 2.0 ? texture2D(channelMixerImage, localSt) : (band < 3.0 ? texture2D(colorGradeImage, localSt) : (band < 4.0 ? texture2D(colorGradeDepthImage, localSt) : (band < 5.0 ? texture2D(convertColorsImage, localSt) : (band < 6.0 ? texture2D(convertFormatImage, localSt) : (band < 7.0 ? texture2D(hseImage, localSt) : (band < 8.0 ? texture2D(keyColorImage, localSt) : (band < 9.0 ? texture2D(remapColorImage, localSt) : (band < 10.0 ? texture2D(tintImage, localSt) : texture2D(toneMappingImage, localSt)))))))))));
		float edge = step(localSt.x, 0.012) + step(0.988, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0), 0.38);
		gl_FragColor = color;
	}
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch39ColorProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) adjustColorsImage,
		VuoInputData(VuoImage) channelMixerImage,
		VuoInputData(VuoImage) colorGradeImage,
		VuoInputData(VuoImage) colorGradeDepthImage,
		VuoInputData(VuoImage) convertColorsImage,
		VuoInputData(VuoImage) convertFormatImage,
		VuoInputData(VuoImage) hseImage,
		VuoInputData(VuoImage) keyColorImage,
		VuoInputData(VuoImage) remapColorImage,
		VuoInputData(VuoImage) tintImage,
		VuoInputData(VuoImage) toneMappingImage,
		VuoInputData(VuoInteger, {"default":1320}) width,
		VuoInputData(VuoInteger, {"default":160}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{ VuoInteger renderWidth=width<1?1320:width; VuoInteger renderHeight=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader, "adjustColorsImage", adjustColorsImage ? adjustColorsImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "channelMixerImage", channelMixerImage ? channelMixerImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "colorGradeImage", colorGradeImage ? colorGradeImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "colorGradeDepthImage", colorGradeDepthImage ? colorGradeDepthImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "convertColorsImage", convertColorsImage ? convertColorsImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "convertFormatImage", convertFormatImage ? convertFormatImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "hseImage", hseImage ? hseImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "keyColorImage", keyColorImage ? keyColorImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "remapColorImage", remapColorImage ? remapColorImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "tintImage", tintImage ? tintImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "toneMappingImage", toneMappingImage ? toneMappingImage : VuoImage_makeColorImage((VuoColor){0.05,0.05,0.05,1}, renderWidth, renderHeight)); *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
