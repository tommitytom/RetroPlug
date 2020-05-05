#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "Buttons.h"

#include <sstream>

#include "ConfigLoader.h"
#include "rapidjson/document.h"

SystemView::SystemView(SystemIndex idx, IGraphics* graphics)
	: _index(idx), _graphics(graphics)
{
	/*_settings = {
		{ "Color Correction", 2 },
		{ "High-pass Filter", 1 }
	};*/

	for (size_t i = 0; i < 2; i++) {
		_textIds[i] = new ITextControl(IRECT(0, -100, 0, 0), "", IText(23, COLOR_WHITE));
		graphics->AttachControl(_textIds[i]);
	}
}

SystemView::~SystemView() {
	HideText();
	DeleteFrame();
}

void SystemView::DeleteFrame() {
	if (_imageId != -1) {
		NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
		nvgDeleteImage(ctx, _imageId);
		nvgBeginPath(ctx);
		nvgRect(ctx, _area.L, _area.T, _area.W(), _area.H());
		NVGcolor black = { 0, 0, 0, 1 };
		nvgFillColor(ctx, black);
		nvgFill(ctx);

		_imageId = -1;
	}

	if (_frameBuffer) {
		delete[] _frameBuffer;
		_frameBuffer = nullptr;
		_frameBufferSize = 0;
	}
}

void SystemView::WriteFrame(const VideoBuffer& buffer) {
	if (buffer.data.get()) {
		if (buffer.data.count() > _frameBufferSize) {
			if (_frameBuffer) {
				delete[] _frameBuffer;
			}

			_dimensions = buffer.dimensions;

			_frameBufferSize = buffer.data.count();
			_frameBuffer = new char[_frameBufferSize];

			if (_imageId != -1) {
				NVGcontext* ctx = (NVGcontext*)_graphics->GetDrawContext();
				nvgDeleteImage(ctx, _imageId);
				_imageId = -1;
			}
		}

		memcpy(_frameBuffer, buffer.data.get(), _frameBufferSize);
		_frameDirty = true;
	}
}

void SystemView::ShowText(const std::string & row1, const std::string & row2) {
	_showText = true;
	_textIds[0]->SetStr(row1.c_str());
	_textIds[1]->SetStr(row2.c_str());
	UpdateTextPosition();
}

void SystemView::HideText() {
	_showText = false;
	UpdateTextPosition();
}

void SystemView::UpdateTextPosition() {
	if (_showText) {
		float mid = _area.H() / 2;
		IRECT topRow(_area.L, mid - 25, _area.R, mid);
		IRECT bottomRow(_area.L, mid, _area.R, mid + 25);
		_textIds[0]->SetTargetAndDrawRECTs(topRow);
		_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
	} else {
		_textIds[0]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
		_textIds[1]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
	}
}

void SystemView::SetArea(const IRECT & area) {
	_area = area;
	UpdateTextPosition();
}

void SystemView::Draw(IGraphics& g, double delta) {
	NVGcontext* vg = (NVGcontext*)g.GetDrawContext();
	if (_index != NO_ACTIVE_SYSTEM) {
		DrawPixelBuffer(vg);
	}
}

void SystemView::DrawPixelBuffer(NVGcontext* vg) {
	if (_frameDirty && _frameBuffer) {
		if (_imageId == -1) {
			_imageId = nvgCreateImageRGBA(vg, _dimensions.w, _dimensions.h, NVG_IMAGE_NEAREST, (const unsigned char*)_frameBuffer);
		} else {
			nvgUpdateImage(vg, _imageId, (const unsigned char*)_frameBuffer);
		}

		_frameDirty = false;
	}

	if (_imageId != -1) {
		nvgBeginPath(vg);

		NVGpaint imgPaint = nvgImagePattern(vg, _area.L, _area.T, _dimensions.w * _zoom, _dimensions.h * _zoom, 0, _imageId, _alpha);
		nvgRect(vg, _area.L, _area.T, _area.W(), _area.H());
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
	}
}

/*IPopupMenu* SystemView::CreateSettingsMenu() {
	IPopupMenu* settingsMenu = new IPopupMenu();

	// TODO: These should be moved in to the SameBoy wrapper
	std::map<std::string, std::vector<std::string>> settings;
	settings["Color Correction"] = {
		"Off",
		"Correct Curves",
		"Emulate Hardware",
		"Preserve Brightness"
	};

	settings["High-pass Filter"] = {
		"Off",
		"Accurate",
		"Remove DC Offset"
	};

	for (auto& setting : settings) {
		const std::string& name = setting.first;
		IPopupMenu* settingMenu = new IPopupMenu(0, true);
		for (size_t i = 0; i < setting.second.size(); i++) {
			auto& option = setting.second[i];
			settingMenu->AddItem(option.c_str(), i);
		}

		settingMenu->CheckItem(_settings[name], true);
		settingsMenu->AddItem(name.c_str(), settingMenu);
		settingMenu->SetFunction([this, name](int indexInMenu, IPopupMenu::Item* itemChosen) {
			_settings[name] = indexInMenu;
			_plug->setSetting(name, indexInMenu);
		});
	}

	settingsMenu->AddSeparator();
	settingsMenu->AddItem("Open Settings Folder...");
	
	return settingsMenu;
}*/

/*void SystemView::OpenReplaceRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(_graphics, types, false);
	if (paths.size() > 0) {
		std::vector<std::byte> saveData;
		_plug->saveBattery(saveData);

		_plug->init(paths[0], _plug->model(), false);
		_plug->loadBattery(saveData, false);

		_plug->disableRendering(false);
		HideText();
	}
}*/
