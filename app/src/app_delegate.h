// app_delegate — AppDelegate and ViewDelegate class declarations.
// Mechanically split out of main.cpp (ARCHITECTURE rule 4: single file < 400 lines).
#pragma once

#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>

// Renderer is implemented in main.cpp; the full class definition is needed here so
// ViewDelegate::ViewDelegate can call `new Renderer(pDevice)` without an incomplete-type error.
class Renderer {
 public:
  explicit Renderer(MTL::Device* pDevice);
  ~Renderer();
  void draw(MTK::View* pView);

 private:
  MTL::Device* _pDevice;
  MTL::CommandQueue* _pCommandQueue;
};

class ViewDelegate : public MTK::ViewDelegate {
 public:
  explicit ViewDelegate(MTL::Device* pDevice);
  ~ViewDelegate() override;
  void drawInMTKView(MTK::View* pView) override;

 private:
  Renderer* _pRenderer;
};

class AppDelegate : public NS::ApplicationDelegate {
 public:
  ~AppDelegate() override;

  void applicationWillFinishLaunching(NS::Notification* pNotification) override;
  void applicationDidFinishLaunching(NS::Notification* pNotification) override;
  bool applicationShouldTerminateAfterLastWindowClosed(NS::Application* pSender) override;

 private:
  NS::Window* _pWindow = nullptr;
  MTK::View* _pMtkView = nullptr;
  MTL::Device* _pDevice = nullptr;
  ViewDelegate* _pViewDelegate = nullptr;
};
