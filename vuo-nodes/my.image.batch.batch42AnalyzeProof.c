#include "VuoImageRenderer.h"
VuoModuleMetadata({"title":"my_Batch42AnalyzeProof","description":"Proof compositor for Batch 42 Lib.image.analyze.","keywords":["tixl","batch42","analyze","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});
static const char *fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	uniform sampler2D compareImagesImage;
	uniform sampler2D detectMotionImage;
	uniform sampler2D getImageBrightnessImage;
	uniform sampler2D imageLevelsImage;
	uniform sampler2D opticalFlowImage;
	uniform sampler2D removeStaticBackgroundImage;
	uniform sampler2D waveFormImage;
	varying vec2 fragmentTextureCoordinate;
	void main() { vec2 st=fragmentTextureCoordinate; float band=floor(clamp(st.x,0.0,0.9999)*7.0); vec2 localSt=vec2(fract(st.x*7.0),st.y); vec4 color=(band < 1.0 ? texture2D(compareImagesImage, localSt) : (band < 2.0 ? texture2D(detectMotionImage, localSt) : (band < 3.0 ? texture2D(getImageBrightnessImage, localSt) : (band < 4.0 ? texture2D(imageLevelsImage, localSt) : (band < 5.0 ? texture2D(opticalFlowImage, localSt) : (band < 6.0 ? texture2D(removeStaticBackgroundImage, localSt) : texture2D(waveFormImage, localSt))))))); float edge=step(localSt.x,0.012)+step(0.988,localSt.x); if(edge>0.0) color.rgb=mix(color.rgb,vec3(1.0),0.35); gl_FragColor=color; }
);
struct nodeInstanceData { VuoShader shader; };
struct nodeInstanceData * nodeInstanceInit(void) { struct nodeInstanceData *i=(struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData)); VuoRegister(i,free); i->shader=VuoShader_make("my_Batch42AnalyzeProof Shader"); VuoShader_addSource(i->shader,VuoMesh_IndividualTriangles,NULL,NULL,fragmentShaderSource); VuoRetain(i->shader); return i; }
void nodeInstanceEvent(VuoInstanceData(struct nodeInstanceData *) instance,VuoInputEvent() renderTick,
		VuoInputData(VuoImage) compareImagesImage,
		VuoInputData(VuoImage) detectMotionImage,
		VuoInputData(VuoImage) getImageBrightnessImage,
		VuoInputData(VuoImage) imageLevelsImage,
		VuoInputData(VuoImage) opticalFlowImage,
		VuoInputData(VuoImage) removeStaticBackgroundImage,
		VuoInputData(VuoImage) waveFormImage,
		VuoInputData(VuoInteger,{"default":1120}) width,VuoInputData(VuoInteger,{"default":160}) height,VuoOutputData(VuoImage,{"name":"Image"}) image)
{ VuoInteger w=width<1?1120:width,h=height<1?160:height; VuoShader_setUniform_VuoImage((*instance)->shader,"compareImagesImage",compareImagesImage?compareImagesImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"detectMotionImage",detectMotionImage?detectMotionImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"getImageBrightnessImage",getImageBrightnessImage?getImageBrightnessImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"imageLevelsImage",imageLevelsImage?imageLevelsImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"opticalFlowImage",opticalFlowImage?opticalFlowImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"removeStaticBackgroundImage",removeStaticBackgroundImage?removeStaticBackgroundImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h));
	VuoShader_setUniform_VuoImage((*instance)->shader,"waveFormImage",waveFormImage?waveFormImage:VuoImage_makeColorImage((VuoColor){0.02,0.02,0.02,1},w,h)); *image=VuoImageRenderer_render((*instance)->shader,w,h,VuoImageColorDepth_8); }
void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance) { VuoRelease((*instance)->shader); }
