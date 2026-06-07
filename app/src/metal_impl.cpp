// Single translation unit that materializes the metal-cpp implementation.
// Every other .cpp includes the metal-cpp headers as declarations only;
// the *_PRIVATE_IMPLEMENTATION macros here emit the actual symbols exactly once.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <AppKit/AppKit.hpp>
#include <MetalKit/MetalKit.hpp>
