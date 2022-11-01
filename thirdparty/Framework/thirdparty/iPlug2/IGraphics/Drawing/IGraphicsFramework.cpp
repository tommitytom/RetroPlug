#include "IGraphicsFramework.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

using namespace iplug;
using namespace igraphics;

void IGraphicsFramework::OnViewInitialized(void* pContext)
{
  HWND hwnd = WindowFromDC((HDC)pContext);

  bgfx::PlatformData pd;
  pd.nwh = (void*)hwnd;
  bgfx::setPlatformData(pd);

  bgfx::Init bgfxInit;
  bgfxInit.type = bgfx::RendererType::Direct3D12;
  bgfxInit.resolution.width = 1024;
  bgfxInit.resolution.height = 669;
  //bgfxInit.resolution.reset = BGFX_RESET_VSYNC; // | BGFX_RESET_MSAA_X2;
  bgfxInit.platformData.nwh = (void*)hwnd;

  bgfx::init(bgfxInit);

  bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0xFF0000FF, 0.0f);
  bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
  bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

  std::cout << pContext << std::endl;
}

void IGraphicsFramework::BeginFrame()
{
  bgfx::touch(0);
  bgfx::frame();
  std::cout << "BeginFrame" << std::endl;
}

void IGraphicsFramework::EndFrame()
{
  std::cout << "EndFrame" << std::endl;
}

void IGraphicsFramework::OnViewDestroyed() {
  
}

void IGraphicsFramework::DrawResize() {
  
}
