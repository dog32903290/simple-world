/**
 * @file
 * my.image.generate.basic.renderTarget node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/image/generate/basic/RenderTarget.
 */

#include "VuoSceneRenderer.h"
#include "VuoMultisample.h"
#include "VuoText.h"

VuoModuleMetadata({
					 "title" : "my_RenderTarget",
					 "description" : "TiXL RenderTarget host adapter. Source: external/tixl/Operators/Lib/image/generate/basic/RenderTarget.cs. Category: Operators/Lib/image/generate/basic. Vuo body proof: vuo.scene.render.image2. Primary output: Texture2D / VuoImage. ColorForTextures #9F008A. This does not prove DirectX11 SRV/UAV/RTV TextureView parity.",
					 "keywords" : [ "tixl", "render", "target", "texture2d", "scene", "image", "command" ],
					 "version" : "1.0.0",
					 "dependencies" : [
						 "VuoSceneRenderer"
					 ],
				 });

struct nodeInstanceData
{
	VuoSceneRenderer *sceneRenderer;
};

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);

	instance->sceneRenderer = VuoSceneRenderer_make(1);
	VuoRetain(instance->sceneRenderer);

	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
		VuoInputData(VuoList_VuoSceneObject, {"name":"Command"}) Command,
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":1,"suggestedMax":4096,"suggestedStep":16}) Height,
		VuoInputData(VuoImageColorDepth, {"default":"8bpc"}) ColorDepth,
		VuoInputData(VuoMultisample, {"default":"4"}) Multisampling,
		VuoInputData(VuoText, {"default":"camera"}) CameraName,
		VuoOutputData(VuoImage, {"name":"ColorBuffer"}) ColorBuffer,
		VuoOutputData(VuoImage, {"name":"DepthBuffer"}) DepthBuffer
)
{
	VuoInteger renderWidth = Width < 1 ? 1 : Width;
	VuoInteger renderHeight = Height < 1 ? 1 : Height;
	VuoSceneObject rootSceneObject = VuoSceneObject_makeGroup(Command, VuoTransform_makeIdentity());

	VuoSceneRenderer_setRootSceneObject((*instance)->sceneRenderer, rootSceneObject);
	VuoSceneRenderer_setCameraName((*instance)->sceneRenderer, CameraName, true);
	VuoSceneRenderer_regenerateProjectionMatrix((*instance)->sceneRenderer, renderWidth, renderHeight);
	VuoSceneRenderer_renderToImage((*instance)->sceneRenderer, ColorBuffer, ColorDepth, Multisampling, DepthBuffer, true);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->sceneRenderer);
}
