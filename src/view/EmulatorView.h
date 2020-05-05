#pragma once

#include <string>

#include "IControl.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"
#include "ContextMenu.h"
#include "util/RomWatcher.h"

#include "model/UiLuaContext.h"
//#include "model/AudioContextProxy.h"

#include <map>
#include <set>

using namespace iplug;
using namespace igraphics;

class SystemView {
private:
	int _imageId = -1;
	NVGpaint _imgPaint;
	float _alpha = 1.0f;

	IPopupMenu _menu;

	//std::map<std::string, int> _settings;

	Dimension2 _dimensions;
	char* _frameBuffer = nullptr;
	size_t _frameBufferSize = 0;
	bool _frameDirty = false;

	SystemIndex _index;

	IRECT _area;
	IGraphics* _graphics;

	bool _showText = false;
	ITextControl* _textIds[2] = { nullptr };

	int _zoom = 2;

public:
	SystemView(SystemIndex idx, IGraphics* graphics);
	~SystemView();

	void SetZoom(int zoom) { _zoom = zoom; }

	void WriteFrame(const VideoBuffer& buffer);

	void ShowText(const std::string& row1, const std::string& row2);

	void HideText();

	void UpdateTextPosition();

	void SetArea(const IRECT& area);

	const IRECT& GetArea() const { return _area; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	void Draw(IGraphics& g, double delta);

	SystemIndex GetIndex() const { return _index; }

	void DeleteFrame();

private:
	void DrawPixelBuffer(NVGcontext* vg);
};
