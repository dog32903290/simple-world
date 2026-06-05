#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch41StylizeProof","description":"Proof compositor for Batch 41 Lib.image.fx.stylize.","keywords":["tixl","batch41","stylize","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D asciiRenderImage;
	uniform sampler2D chromaticAbberationImage;
	uniform sampler2D colorPhysarumImage;
	uniform sampler2D detectEdgesImage;
	uniform sampler2D ditherImage;
	uniform sampler2D fakeLightImage;
	uniform sampler2D glowImage;
	uniform sampler2D honeyCombTilesImage;
	uniform sampler2D lightRaysFxImage;
	uniform sampler2D mosiacTilingImage;
	uniform sampler2D pixelateImage;
	uniform sampler2D screenCloseUpImage;
	uniform sampler2D starGlowStreaksImage;
	uniform sampler2D stepsImage;
	uniform sampler2D voronoiCellsImage;
	varying vec2 fragmentTextureCoordinate;
	void main() { vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*15.0); vec2 localSt=vec2(fract(st.x*15.0),st.y); vec4 color=(band < 1.0 ? texture2D(asciiRenderImage, localSt) : (band < 2.0 ? texture2D(chromaticAbberationImage, localSt) : (band < 3.0 ? texture2D(colorPhysarumImage, localSt) : (band < 4.0 ? texture2D(detectEdgesImage, localSt) : (band < 5.0 ? texture2D(ditherImage, localSt) : (band < 6.0 ? texture2D(fakeLightImage, localSt) : (band < 7.0 ? texture2D(glowImage, localSt) : (band < 8.0 ? texture2D(honeyCombTilesImage, localSt) : (band < 9.0 ? texture2D(lightRaysFxImage, localSt) : (band < 10.0 ? texture2D(mosiacTilingImage, localSt) : (band < 11.0 ? texture2D(pixelateImage, localSt) : (band < 12.0 ? texture2D(screenCloseUpImage, localSt) : (band < 13.0 ? texture2D(starGlowStreaksImage, localSt) : (band < 14.0 ? texture2D(stepsImage, localSt) : texture2D(voronoiCellsImage, localSt))))))))))))))); float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch41StylizeProof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
		VuoInputData(VuoImage) asciiRenderImage,
		VuoInputData(VuoImage) chromaticAbberationImage,
		VuoInputData(VuoImage) colorPhysarumImage,
		VuoInputData(VuoImage) detectEdgesImage,
		VuoInputData(VuoImage) ditherImage,
		VuoInputData(VuoImage) fakeLightImage,
		VuoInputData(VuoImage) glowImage,
		VuoInputData(VuoImage) honeyCombTilesImage,
		VuoInputData(VuoImage) lightRaysFxImage,
		VuoInputData(VuoImage) mosiacTilingImage,
		VuoInputData(VuoImage) pixelateImage,
		VuoInputData(VuoImage) screenCloseUpImage,
		VuoInputData(VuoImage) starGlowStreaksImage,
		VuoInputData(VuoImage) stepsImage,
		VuoInputData(VuoImage) voronoiCellsImage,
		VuoInputData(VuoInteger,{"default":2400}) width,VuoInputData(VuoInteger,{"default":160}) height,VuoOutputData(VuoImage,{"name":"Image"}) image)
{ VuoInteger w=width<1?2400:width,h=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader,"asciiRenderImage",asciiRenderImage?asciiRenderImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"chromaticAbberationImage",chromaticAbberationImage?chromaticAbberationImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"colorPhysarumImage",colorPhysarumImage?colorPhysarumImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"detectEdgesImage",detectEdgesImage?detectEdgesImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"ditherImage",ditherImage?ditherImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"fakeLightImage",fakeLightImage?fakeLightImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"glowImage",glowImage?glowImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"honeyCombTilesImage",honeyCombTilesImage?honeyCombTilesImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"lightRaysFxImage",lightRaysFxImage?lightRaysFxImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"mosiacTilingImage",mosiacTilingImage?mosiacTilingImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"pixelateImage",pixelateImage?pixelateImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"screenCloseUpImage",screenCloseUpImage?screenCloseUpImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"starGlowStreaksImage",starGlowStreaksImage?starGlowStreaksImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"stepsImage",stepsImage?stepsImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"voronoiCellsImage",voronoiCellsImage?voronoiCellsImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h)); *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
