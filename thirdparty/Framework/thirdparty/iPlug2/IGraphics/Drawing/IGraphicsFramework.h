/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#pragma once

#include "IGraphics.h"
#include "IPlugPlatform.h"

#include <iostream>

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

class IGraphicsFramework : public IGraphics
{
private:
  void* _nativeWindowHandle = nullptr;

public:
  IGraphicsFramework(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
    : IGraphics(dlg, w, h, fps, scale)
  {
  }
  ~IGraphicsFramework() {}

  const char* GetDrawingAPIStr() override { return "Framework"; }

  void* GetNativeWindowHandle() {
    return _nativeWindowHandle;
  }

  void BeginFrame() override;
  void EndFrame() override;
  void OnViewInitialized(void* pContext) override;
  void OnViewDestroyed() override;
  void DrawResize() override;

  void DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend) override {}

  void DrawDottedLine(const IColor& color, float x1, float y1, float x2, float y2, const IBlend* pBlend, float thickness, float dashLen) override {}
  void DrawDottedRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness, float dashLen) override {}

  void DrawFastDropShadow(const IRECT& innerBounds, const IRECT& outerBounds, float xyDrop = 5.f, float roundness = 0.f, float blur = 10.f, IBlend* pBlend = nullptr) override {}

  void PathClear() override {}
  void PathClose() override {}
  void PathArc(float cx, float cy, float r, float a1, float a2, EWinding winding) override {}
  void PathMoveTo(float x, float y) override {}
  void PathLineTo(float x, float y) override {}
  void PathCubicBezierTo(float c1x, float c1y, float c2x, float c2y, float x2, float y2) override {}
  void PathQuadraticBezierTo(float cx, float cy, float x2, float y2) override {}
  void PathSetWinding(bool clockwise) override {}
  void PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend) override {}
  void PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend) override {}

  IColor GetPoint(int x, int y) override { return IColor(); }
  void* GetDrawContext() override { return nullptr; }

  IBitmap LoadBitmap(const char* name, int nStates, bool framesAreHorizontal, int targetScale) override { return IBitmap(); }
  void ReleaseBitmap(const IBitmap& bitmap) override{};                       // NO-OP
  void RetainBitmap(const IBitmap& bitmap, const char* cacheName) override{}; // NO-OP
  bool BitmapExtSupported(const char* ext) override { return false; }

protected:
  APIBitmap* LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext) override { return nullptr; }
  APIBitmap* LoadAPIBitmap(const char* name, const void* pData, int dataSize, int scale) override { return nullptr; }
  APIBitmap* CreateAPIBitmap(int width, int height, float scale, double drawScale, bool cacheable = false) override { return nullptr; }

  bool LoadAPIFont(const char* fontID, const PlatformFontPtr& font) override { return false; }

  int AlphaChannel() const override { return 3; }

  bool FlippedBitmap() const override { return false; }

  void GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data) override {}
  void ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow) override {}

  float DoMeasureText(const IText& text, const char* str, IRECT& bounds) const override { return 0.0f; }
  void DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend) override {}

private:
  void PathTransformSetMatrix(const IMatrix& m) override {}
  void SetClipRegion(const IRECT& r) override {}
  void UpdateLayer() override {}
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE
