#include "IGraphicsFramework.h"

using namespace iplug;
using namespace igraphics;

void IGraphicsFramework::OnViewInitialized(void* pContext)
{
  _nativeWindowHandle = (void*)WindowFromDC((HDC)pContext);
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
