#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "platform/Shell.h"
#include "util/File.h"
#include "util/fs.h"
#include "util/cxxtimer.hpp"
#include "Keys.h"

#include "Menu.h"

const float ACTIVE_ALPHA = 1.0f;
const float INACTIVE_ALPHA = 0.75f;
const size_t DEFAULT_SRAM_SIZE = 0x20000;

const double VIDEO_STREAM_TIMEOUT = 1000.0;

RetroPlugView::RetroPlugView(IRECT b, UiLuaContext* lua, RetroPlugProxy* proxy, AudioController* audioController)
	: IControl(b), _lua(lua), _proxy(proxy), _audioController(audioController) {
	proxy->videoCallback = [&](const VideoStream& video) {
		for (size_t i = 0; i < _proxy->getInstanceCount(); ++i) {
			if (_proxy->getInstance((InstanceIndex)i)->state == EmulatorInstanceState::Running) {
				_views[i]->WriteFrame(video.buffers[i]);
			}
		}

		_timeSinceVideo = 0;
	};
}

RetroPlugView::~RetroPlugView() {

}

void RetroPlugView::OnInit() {
	UpdateLayout();
}

bool RetroPlugView::OnKey(const IKeyPress& key, bool down) {
	return _lua->onKey(key, down);
}

void RetroPlugView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	EmulatorInstanceDescPtr active = _proxy->getActiveInstance();
	if (!active || active->emulatorType == EmulatorType::Placeholder) {
		OpenLoadProjectOrRomDialog();
	} else {
		if (active->state == EmulatorInstanceState::RomMissing) {
			OpenFindRomDialog();
		}
	}
}

void RetroPlugView::SetZoom(int zoom) {
	Project* project = _proxy->getProject();
	project->settings.zoom = zoom + 1;

	for (auto& view : _views) {
		view->SetZoom(project->settings.zoom);
	}
}

void RetroPlugView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	SelectActiveAtPoint(x, y);

	if (mod.R) {
		// Temporarily disable multiple instances in Ableton on Mac due to a bug
#ifdef WIN32
		bool multiInstance = true;
#else
		bool multiInstance = _host != EHost::kHostAbletonLive;
#endif

		_menu.Clear();
		Menu root;

		Project* project = _proxy->getProject();
		EmulatorInstanceDescPtr active = _proxy->getActiveInstance();
		if (active) {
			switch (active->state) {
			case EmulatorInstanceState::Running: {
				EmulatorView* view = GetActiveView();

				root.title(active->romName)
					.separator()
					.subMenu("Project")
						.action("New", [&]() { NewProject(); })
						.action("Load...", [&]() { OpenLoadProjectDialog(); })
						.action("Save", [&]() { SaveProject(); })
						.action("Save As...", [&]() { SaveProjectAs(); })
						.separator()
						.subMenu("Save Options")
							.multiSelect({ "Save SRAM", "Save State" }, &project->settings.saveType)
							.parent()
						.separator()
						.subMenu("Add Instance", multiInstance && project->instances.size() < MAX_INSTANCES)
							.action("Load ROM...", [&]() { OpenLoadRomDialog(NO_ACTIVE_INSTANCE, GameboyModel::Auto); })
							.action("Same ROM as Selected", [&]() { _lua->loadRom(NO_ACTIVE_INSTANCE, active->romPath, active->sameBoySettings.model); })
							.action("Duplicate Selected", [&]() { _lua->duplicateInstance(_activeIdx); })
							.parent()
						.action("Remove Instance", [&]() { RemoveActive(); }, _proxy->getInstanceCount() > 1 && multiInstance)
						.subMenu("Layout", multiInstance)
							.multiSelect({ "Auto", "Row", "Column", "Grid" }, &project->settings.layout)
							.parent()
						.subMenu("Zoom")
					.multiSelect({ "1x", "2x", "3x", "4x" }, project->settings.zoom - 1, [&](int value) { SetZoom(value); })
							.parent()
						.separator()
						.subMenu("Audio Routing", multiInstance)
							.multiSelect({ "Stereo Mixdown", "Two Channels Per Instance" }, &project->settings.audioRouting)
							.parent()
						.subMenu("MIDI Routing", multiInstance)
							.multiSelect({ 
								"All Channels to All Instances", 
								"Four Channels Per Instance",
								"One Channel Per Instance",
							}, &project->settings.midiRouting)
							.parent()
						.parent()
					.subMenu("System")
						.action("Load ROM...", [&]() { OpenLoadRomDialog(_activeIdx, GameboyModel::Auto); })
						.subMenu("Load ROM As")
							.action("AGB...", [&]() { OpenLoadRomDialog(_activeIdx, GameboyModel::Agb); })
							.action("CGB C...", [&]() { OpenLoadRomDialog(_activeIdx, GameboyModel::CgbC); })
							.action("CGB E (default)...", [&]() { OpenLoadRomDialog(_activeIdx, GameboyModel::CgbE); })
							.action("DMG B...", [&]() { OpenLoadRomDialog(_activeIdx, GameboyModel::DmgB); })
							.parent()
						.action("Reset", [&]() { _lua->resetInstance(_activeIdx, GameboyModel::Auto); })
						.subMenu("Reset As")
							.action("AGB", [&]() { _lua->resetInstance(_activeIdx, GameboyModel::Agb); })
							.action("CGB C", [&]() { _lua->resetInstance(_activeIdx, GameboyModel::CgbC); })
							.action("CGB E (default)", [&]() { _lua->resetInstance(_activeIdx, GameboyModel::CgbE); })
							.action("DMG B", [&]() { _lua->resetInstance(_activeIdx, GameboyModel::DmgB); })
							.parent()
						.separator()
						.action("New .sav", [&]() { _lua->newSram(_activeIdx); })
						.action("Load .sav...", [&]() { OpenLoadSavDialog(); })
						.action("Save .sav", [&]() { _lua->saveSram(_activeIdx, ""); })
						.action("Save .sav As...", [&]() { OpenSaveSavDialog(); })
						.separator()
						.subMenu("UI Components").parent()
						.subMenu("Audio Components").parent()
						.parent()
					.subMenu("Settings")
						.action("Open Settings Folder...", [&]() { openShellFolder(getContentPath()); })
						.parent()
					.separator()
					.select("Game Link", &active->sameBoySettings.gameLink);

				if (!active->sourceSavData) {
					active->sourceSavData = std::make_shared<DataBuffer<char>>(DEFAULT_SRAM_SIZE);
				}

				// Update SRAM for the selected instance since it might be used to generate the menu
				_audioController->getSram(_activeIdx, active->sourceSavData);

				std::vector<Menu*> menus;
				_audioController->onMenu(_activeIdx, menus);
				_lua->onMenu(menus);

				for (Menu* item : menus) {
					mergeMenu(item, &root);
					delete item;
				}

				break;
			}
			case EmulatorInstanceState::RomMissing:
				root.action("Find ROM...", [&]() { OpenFindRomDialog(); })
					.action("Load Project...", [&]() { OpenLoadProjectDialog(); });

				break;
			}
		} else {
			root.action("Load Project...", [&]() { OpenLoadProjectDialog(); })
				.action("Load ROM...", [&]() { OpenLoadProjectOrRomDialog(); });
		}

		MenuCallbackMap callbacks;
		callbacks.reserve(1000);
		_menu.SetFunction([&](IPopupMenu* menu) {
			IPopupMenu::Item* chosen = menu->GetChosenItem();
			if (chosen) {
				int tag = chosen->GetTag();
				if (tag >= 0 && tag < callbacks.size()) {
					callbacks[tag]();
				} else if (tag >= LUA_UI_MENU_ID_OFFSET) {
					_lua->onMenuResult(tag);
					_proxy->onMenuResult(tag);
				}

				std::vector<FileDialogFilters> filters;
				DialogType dialog = _lua->getDialogFilters(filters);
				switch (dialog) {
				case DialogType::Save: {
					std::string p = ws2s(BasicFileSave(GetUI(), filters));
					std::vector<std::string> paths;
					paths.push_back(p);
					_lua->handleDialogCallback(paths);
					break;
				}
				case DialogType::Load: {
					std::vector<tstring> res = BasicFileOpen(GetUI(), filters, true, false);
					std::vector<std::string> paths;
					for (size_t i = 0; i < res.size(); ++i) {
						paths.push_back(ws2s(res[i]));
					}

					_lua->handleDialogCallback(paths);

					break;
				}
				}

				UpdateLayout();
				UpdateActive();
			}
		});

		createMenu(&_menu, &root, callbacks);
		GetUI()->CreatePopupMenu(*this, _menu, x, y);
	}
}

void RetroPlugView::Draw(IGraphics& g) {
	_frameTimer.stop();
	double delta = (double)_frameTimer.count();
	_frameTimer.reset();
	_frameTimer.start();

	UpdateActive();

	// TODO: Probably only do this after changing layout?
	SetZoom(_proxy->getProject()->settings.zoom - 1);

	onFrame(delta);
	//_lua->update(delta);
	_proxy->update(delta);

	_timeSinceVideo += delta;
	
	for (size_t i = 0; i < _proxy->getInstanceCount(); ++i) {
		EmulatorView* view = _views[i];
		const EmulatorInstanceDescPtr& instance = _proxy->getInstance(i);

		if (instance->state == EmulatorInstanceState::Running) {
			if (_timeSinceVideo < VIDEO_STREAM_TIMEOUT) {
				view->Draw(g, delta);
			} else {
				instance->state = EmulatorInstanceState::VideoFeedLost;
				view->ShowText("Audio timeout", "Check DAW settings");
				view->DeleteFrame();
			}
		} else if (instance->state == EmulatorInstanceState::VideoFeedLost) {
			//std::cout << _timeSinceVideo << std::endl;
			if (_timeSinceVideo < VIDEO_STREAM_TIMEOUT) {
				view->HideText();
				view->Draw(g, delta);
				instance->state = EmulatorInstanceState::Running;
			}
		}
	}
}

void RetroPlugView::OnDrop(float x, float y, const char* str) {
	SelectActiveAtPoint(x, y);
	_lua->onDrop(str);
	UpdateLayout();
}

void RetroPlugView::UpdateLayout() {
	if (_views.empty()) {
		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			_views.push_back(new EmulatorView(i, GetUI()));
		}
	}

	for (size_t i = 0; i < MAX_INSTANCES; ++i) {
		_views[i]->HideText();
	}

	Project::Settings& settings = _proxy->getProject()->settings;
	int frameW = FRAME_WIDTH * settings.zoom;
	int frameH = FRAME_HEIGHT * settings.zoom;

	int windowW = frameW;
	int windowH = frameH;

	size_t count = _proxy->getInstanceCount();
	if (count == 0) {
		count = 1;
		_views[0]->ShowText("Double click to", "load a rom...");
		_views[0]->DeleteFrame();
	}

	InstanceLayout layout = settings.layout;
	if (layout == InstanceLayout::Auto) {
		if (count < 4) {
			layout = InstanceLayout::Row;
		} else {
			layout = InstanceLayout::Grid;
		}
	}

	if (layout == InstanceLayout::Row) {
		windowW = count * frameW;
	} else if (layout == InstanceLayout::Column) {
		windowH = count * frameH;
	} else if (layout == InstanceLayout::Grid) {
		if (count > 2) {
			windowW = 2 * frameW;
			windowH = 2 * frameH;
		} else {
			windowW = count * frameW;
		}
	}

	GetUI()->SetSizeConstraints(frameW, windowW, frameH, windowH);
	GetUI()->Resize(windowW, windowH, 1);
	SetTargetAndDrawRECTs(IRECT(0.0f, 0.0f, (float)windowW, (float)windowH));
	GetUI()->GetControl(0)->SetTargetAndDrawRECTs(GetRECT());

	for (size_t i = 0; i < count; i++) {
		int gridX = 0;
		int gridY = 0;

		if (layout == InstanceLayout::Row) {
			gridX = i;
		} else if (layout == InstanceLayout::Column) {
			gridY = i;
		} else {
			if (i < 2) {
				gridX = i;
			} else {
				gridX = i - 2;
				gridY = 1;
			}
		}

		int x = gridX * frameW;
		int y = gridY * frameH;

		IRECT b((float)x, (float)y, (float)(x + frameW), (float)(y + frameH));
		_views[i]->SetArea(b);
	}

	for (size_t i = count; i < _views.size(); ++i) {
		_views[i]->HideText();
		_views[i]->DeleteFrame();
	}
}

void RetroPlugView::UpdateActive() {
	InstanceIndex idx = _proxy->activeIdx();

	if (_activeIdx != NO_ACTIVE_INSTANCE && idx != _activeIdx) {
		_views[_activeIdx]->SetAlpha(INACTIVE_ALPHA);
	}

	if (idx != NO_ACTIVE_INSTANCE) {
		_views[idx]->SetAlpha(ACTIVE_ALPHA);
	}

	_activeIdx = idx;
}

void RetroPlugView::SetActive(size_t index) {
	_lua->setActive(index);
	UpdateActive();
}

void RetroPlugView::RemoveActive() {
	_lua->removeInstance(_activeIdx);
	UpdateLayout();
	UpdateActive();
}

void RetroPlugView::CloseProject() {
	_lua->closeProject();
	UpdateLayout();
	UpdateActive();
}

void RetroPlugView::NewProject() {
	CloseProject();
}

void RetroPlugView::SaveProject() {
	if (_proxy->getProject()->path.empty()) {
		SaveProjectAs();
	} else {
		RequestSave();
	}
}

void RetroPlugView::SaveProjectAs() {
	tstring path = BasicFileSave(GetUI(), { RETROPLUG_PROJECT_FILTER });
	if (path.size() > 0) {
		_proxy->getProject()->path = ws2s(path);
		RequestSave();
	}
}

void RetroPlugView::RequestSave() {
	_proxy->requestSave([&](const FetchStateResponse& res) {
		_lua->saveProject(res);
	});
}

void RetroPlugView::OpenFindRomDialog() {
	std::vector<tstring> paths = BasicFileOpen(GetUI(), { GAMEBOY_ROM_FILTER }, false);
	if (paths.size() > 0) {
		_lua->findRom(_proxy->activeIdx(), ws2s(paths[0]));
	}
}

void RetroPlugView::OpenLoadProjectOrRomDialog() {
	std::vector<FileDialogFilters> types = {
		ALL_SUPPORTED_FILTER,
		GAMEBOY_ROM_FILTER,
		RETROPLUG_PROJECT_FILTER
	};

	std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
	if (paths.size() > 0) {
		LoadProjectOrRom(paths[0]);
	}
}

void RetroPlugView::LoadProjectOrRom(const tstring& path) {
	tstring ext = tstr(fs::path(path).extension().string());
	if (ext == TSTR(".retroplug")) {
		LoadProject(path);
	} else if (ext == TSTR(".gb") || ext == TSTR(".gbc")) {
		_lua->loadRom(_activeIdx, ws2s(path), GameboyModel::Auto);
	}
}

void RetroPlugView::LoadProject(const tstring& path) {
	_lua->loadProject(ws2s(path));
	UpdateLayout();
}

void RetroPlugView::OpenLoadProjectDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("RetroPlug Projects"), TSTR("*.retroplug") }
	};

	std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
	if (paths.size() > 0) {
		LoadProject(paths[0]);
	}
}
