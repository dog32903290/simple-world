#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch38GlitchProof","description":"Proof-only compositor for Batch 38 glitch nodes.","keywords":["tixl","batch38","glitch","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D glitchDisplaceImage;
	uniform sampler2D rgbTvImage;
	uniform sampler2D sortPixelGlitchImage;
	uniform sampler2D subdivisionStretchImage;
	varying vec2 fragmentTextureCoordinate;
	void main() {
		vec2 st = fragmentTextureCoordinate;
		float band = floor(clamp(st.x, 0.0, 0.9999) * 4.0);
		vec2 localSt = vec2(fract(st.x * 4.0), st.y);
		vec4 color = (band < 1.0 ? texture2D(glitchDisplaceImage, localSt) : (band < 2.0 ? texture2D(rgbTvImage, localSt) : (band < 3.0 ? texture2D(sortPixelGlitchImage, localSt) : texture2D(subdivisionStretchImage, localSt))));
		float edge = step(localSt.x, 0.015) + step(0.985, localSt.x);
		if (edge > 0.0) color.rgb = mix(color.rgb, vec3(1.0, 0.2, 0.95), 0.55);
		gl_FragColor = color;
	}
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *instance=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(instance, free); instance->shader=VuoShader_make("my_Batch38GlitchProof Shader"); VuoShader_addSource(instance->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(instance->shader); return instance; }
void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoImage) glitchDisplaceImage,
		VuoInputData(VuoImage) rgbTvImage,
		VuoInputData(VuoImage) sortPixelGlitchImage,
		VuoInputData(VuoImage) subdivisionStretchImage,
		VuoInputData(VuoInteger, {"default":640}) width,
		VuoInputData(VuoInteger, {"default":160}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{ VuoInteger renderWidth=width<1?640:width; VuoInteger renderHeight=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader, "glitchDisplaceImage", glitchDisplaceImage ? glitchDisplaceImage : VuoImage_makeColorImage((VuoColor){0.02,0.02,0.025,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "rgbTvImage", rgbTvImage ? rgbTvImage : VuoImage_makeColorImage((VuoColor){0.02,0.02,0.025,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "sortPixelGlitchImage", sortPixelGlitchImage ? sortPixelGlitchImage : VuoImage_makeColorImage((VuoColor){0.02,0.02,0.025,1}, renderWidth, renderHeight));
	VuoShader_setUniform_VuoImage((*instance)->shader, "subdivisionStretchImage", subdivisionStretchImage ? subdivisionStretchImage : VuoImage_makeColorImage((VuoColor){0.02,0.02,0.025,1}, renderWidth, renderHeight)); *image=VuoImageRenderer_render((*instance)->shader,renderWidth,renderHeight,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
