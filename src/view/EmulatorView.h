#pragma once

#include <string>

#include "IControl.h"
#include "KeyMap.h"
#include "LsdjKeyMap.h"
#include "nanovg.h"
#include "ContextMenu.h"
#include "util/RomWatcher.h"

#include "model/LuaContext.h"
#include "model/RetroPlugProxy.h"

#include <map>
#include <set>

using namespace iplug;
using namespace igraphics;

class EmulatorView {
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

	InstanceIndex _index;
	LuaContext* _lua;
	RetroPlugProxy* _proxy;

	IRECT _area;
	IGraphics* _graphics;

	bool _showText = false;
	ITextControl* _textIds[2] = { nullptr };

	int _zoom = 2;

public:
	EmulatorView(InstanceIndex idx, LuaContext* lua, RetroPlugProxy* proxy, IGraphics* graphics);
	~EmulatorView();

	void SetZoom(int zoom) { _zoom = zoom; }

	void WriteFrame(const VideoBuffer& buffer);

	void ShowText(const std::string& row1, const std::string& row2);

	void HideText();

	void UpdateTextPosition();

	void SetArea(const IRECT& area);

	const IRECT& GetArea() const { return _area; }

	void SetAlpha(float alpha) { _alpha = alpha; }

	void Draw(IGraphics& g, double delta);

	void CreateMenu(IPopupMenu* root, IPopupMenu* projectMenu);

	void OpenLoadRomDialog(GameboyModel model);

	void DisableRendering(bool disable);

	InstanceIndex getIndex() const { return _index; }

	void DeleteFrame();

private:
	void DrawPixelBuffer(NVGcontext* vg);

	IPopupMenu* CreateSettingsMenu();

	IPopupMenu* CreateSystemMenu();

	void OpenLoadSramDialog();

	void OpenSaveSramDialog();

	void OpenReplaceRomDialog();
	
	void OpenSaveRomDialog();

	void ToggleKeyboardMode();

	void ResetSystem(bool fast);
};
