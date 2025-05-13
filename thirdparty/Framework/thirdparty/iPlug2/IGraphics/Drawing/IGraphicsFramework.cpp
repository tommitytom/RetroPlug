#include "IGraphicsFramework.h"

using namespace iplug;
using namespace igraphics;

void IGraphicsFramework::OnViewInitialized(void* pContext)
{
  #ifdef FW_OS_WINDOWS
    _nativeWindowHandle = (void*)WindowFromDC((HDC)pContext);
  #else
  //_nativeWindowHandle = pContext;
  #endif
}

void IGraphicsFramework::BeginFrame()
{

}

void IGraphicsFramework::EndFrame()
{

}

void IGraphicsFramework::OnViewDestroyed() {

}

void IGraphicsFramework::DrawResize() {

}
