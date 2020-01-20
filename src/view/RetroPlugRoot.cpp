#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "util/File.h"
#include "util/fs.h"
#include "util/Serializer.h"
#include "util/cxxtimer.hpp"
#include "Keys.h"

const float ACTIVE_ALPHA = 1.0f;
const float INACTIVE_ALPHA = 0.75f;

RetroPlugView::RetroPlugView(IRECT b, LuaContext* lua, RetroPlugProxy* proxy): IControl(b), _lua(lua), _proxy(proxy) {
	proxy->videoCallback = [&](const VideoStream& video) {
		if (_views.size() == MAX_INSTANCES) {
			for (InstanceIndex i = 0; i < MAX_INSTANCES; ++i) {
				if (_proxy->getInstance(i)->state == EmulatorInstanceState::Running) {
					_views[i]->WriteFrame(video.buffers[i]);
				}
			}
		}
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
	EmulatorInstanceDesc* active = _proxy->getActiveInstance();
	if (!active || active->emulatorType == EmulatorType::Placeholder) {
		OpenLoadProjectOrRomDialog();
	} else {
		if (active->state == EmulatorInstanceState::RomMissing) {
			OpenFindRomDialog();
		}
	}
}

void RetroPlugView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	SelectActiveAtPoint(x, y);

	if (mod.R) {
		_menu.Clear();
		EmulatorInstanceDesc* active = _proxy->getActiveInstance();
		if (active) {
			switch (active->state) {
				case EmulatorInstanceState::Running: {
				IPopupMenu* projectMenu = CreateProjectMenu(true);
				GetActiveView()->CreateMenu(&_menu, projectMenu);

				projectMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
					switch ((ProjectMenuItems)idx) {
					case ProjectMenuItems::New: NewProject(); break;
					case ProjectMenuItems::Save: SaveProject(); break;
					case ProjectMenuItems::SaveAs: SaveProjectAs(); break;
					case ProjectMenuItems::Load: OpenLoadProjectDialog(); break;
					case ProjectMenuItems::RemoveInstance: RemoveActive(); break;
					}
				});
				break;
			}
			case EmulatorInstanceState::RomMissing:
				_menu.AddItem("Find ROM...");
				_menu.AddItem("Load Project...");
				_menu.SetFunction([=](int idx, IPopupMenu::Item*) {
					if (idx == 0) {
						OpenFindRomDialog();
					} else {
						OpenLoadProjectDialog();
					}
				});
				break;
			}
		} else {
			IPopupMenu* modelMenu = createModelMenu(true);
			createBasicMenu(&_menu, modelMenu);

			_menu.SetFunction([=](int idx, IPopupMenu::Item*) {
				switch ((BasicMenuItems)idx) {
				case BasicMenuItems::LoadProject: OpenLoadProjectDialog(); break;
				case BasicMenuItems::LoadRom: break;// _active->OpenLoadRomDialog(GameboyModel::Auto); break;
				}
			});

			modelMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
				//_active->OpenLoadRomDialog((GameboyModel)(idx + 1));
			});
		}

		GetUI()->CreatePopupMenu(*((IControl*)this), _menu, x, y);
	}
}

void RetroPlugView::Draw(IGraphics& g) {
	_frameTimer.stop();
	double delta = (double)_frameTimer.count();

	UpdateActive();

	onFrame(delta);
	//_lua->update(delta);
	_proxy->update(delta);

	for (auto view : _views) {
		view->Draw(g, delta);
	}
	
	_frameTimer.start();
}

void RetroPlugView::OnDrop(float x, float y, const char* str) {
	SelectActiveAtPoint(x, y);
	_lua->onDrop(str);
	UpdateLayout();
}

void RetroPlugView::CreatePlugInstance(CreateInstanceType type) {
	switch (type) {
	case CreateInstanceType::LoadRom: {
		std::vector<FileDialogFilters> types = {
			{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
		};

		std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
		if (paths.size() > 0) {
			_lua->loadRom(NO_ACTIVE_INSTANCE, ws2s(paths[0]));
		}
		break;
	}
	case CreateInstanceType::SameRom: {
		const EmulatorInstanceDesc* active = _proxy->getActiveInstance();
		if (active) {
			_lua->loadRom(NO_ACTIVE_INSTANCE, active->romPath);
		}
		break;
	}
	case CreateInstanceType::Duplicate: {
		const EmulatorInstanceDesc* active = _proxy->getActiveInstance();
		if (active) {
			_lua->duplicateInstance(active->idx);
		}
		break;
	}
	}

	UpdateLayout();
	UpdateActive();
}

void RetroPlugView::UpdateLayout() {
	if (_views.empty()) {
		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			_views.push_back(new EmulatorView(i, _lua, _proxy, GetUI()));
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

IPopupMenu* RetroPlugView::CreateProjectMenu(bool loaded) {
	Project* project = _proxy->getProject();
	
	IPopupMenu* instanceMenu = createInstanceMenu(loaded, _proxy->getInstanceCount() < 4);
	IPopupMenu* layoutMenu = createLayoutMenu(project->settings.layout);
	IPopupMenu* zoomMenu = createZoomMenu(project->settings.zoom);
	IPopupMenu* saveOptionsMenu = createSaveOptionsMenu(project->settings.saveType);
	IPopupMenu* audioRouting = createAudioRoutingMenu(project->settings.audioRouting);
	IPopupMenu* midiRouting = createMidiRoutingMenu(project->settings.midiRouting);

	// Temporarily disable multiple instances in Ableton on Mac due to a bug
	#ifdef WIN32
	bool multiInstance = true;
	#else
	bool multiInstance = _host != EHost::kHostAbletonLive;
	#endif

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("New", (int)ProjectMenuItems::New);
	menu->AddItem("Load...", (int)ProjectMenuItems::Load);
	menu->AddItem("Save", (int)ProjectMenuItems::Save);
	menu->AddItem("Save As...", (int)ProjectMenuItems::SaveAs);
	menu->AddSeparator((int)ProjectMenuItems::Sep1);
	menu->AddItem("Save Options", saveOptionsMenu, (int)ProjectMenuItems::SaveOptions);
	menu->AddSeparator((int)ProjectMenuItems::Sep2);

	if (multiInstance) {
		menu->AddItem("Add Instance", instanceMenu, (int)ProjectMenuItems::AddInstance);
		menu->AddItem("Remove Instance", (int)ProjectMenuItems::RemoveInstance, _views.size() < 2 ? IPopupMenu::Item::kDisabled : 0);
		menu->AddItem("Layout", layoutMenu, (int)ProjectMenuItems::Layout);
		menu->AddItem("Zoom", zoomMenu, (int)ProjectMenuItems::Zoom);
		menu->AddSeparator((int)ProjectMenuItems::Sep3);
		menu->AddItem("Audio Routing", audioRouting, (int)ProjectMenuItems::AudioRouting);
		menu->AddItem("MIDI Routing", midiRouting, (int)ProjectMenuItems::MidiRouting);
	} else {
		menu->AddItem("Add Instance", (int)ProjectMenuItems::AddInstance, IPopupMenu::Item::kDisabled);
		menu->AddItem("Remove Instance", (int)ProjectMenuItems::RemoveInstance, IPopupMenu::Item::kDisabled);
		menu->AddItem("Layout", (int)ProjectMenuItems::Layout, IPopupMenu::Item::kDisabled);
		menu->AddSeparator((int)ProjectMenuItems::Sep3);
		menu->AddItem("Audio Routing", (int)ProjectMenuItems::AudioRouting, IPopupMenu::Item::kDisabled);
		menu->AddItem("MIDI Routing", (int)ProjectMenuItems::MidiRouting, IPopupMenu::Item::kDisabled);
	}

	instanceMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		CreatePlugInstance((CreateInstanceType)idx);
	});

	layoutMenu->SetFunction([&](int idx, IPopupMenu::Item* itemChosen) {
		Project* project = _proxy->getProject();
		project->settings.layout = (InstanceLayout)idx;
		_proxy->updateSettings();
		UpdateLayout();
	});

	zoomMenu->SetFunction([&](int idx, IPopupMenu::Item* itemChosen) {
		Project* project = _proxy->getProject();
		project->settings.zoom = idx + 1;
		for (size_t i = 0; i < _views.size(); ++i) {
			_views[i]->SetZoom(project->settings.zoom);
		}

		UpdateLayout();
	});

	saveOptionsMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
		Project* project = _proxy->getProject();
		project->settings.saveType = (SaveStateType)idx;
	});

	audioRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		Project* project = _proxy->getProject();
		project->settings.audioRouting = (AudioChannelRouting)idx;
		_proxy->updateSettings();
	});

	midiRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		Project* project = _proxy->getProject();
		project->settings.midiRouting = (MidiChannelRouting)idx;
		_proxy->updateSettings();
	});

	return menu;
}

void RetroPlugView::CloseProject() {
	_lua->closeProject();
	UpdateLayout();
	UpdateActive();

	/*IGraphics* g = GetUI();

	if (g) {
		for (auto view : _views) {
			delete view;
		}
	}*/	
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
	std::vector<FileDialogFilters> types = {
		{ TSTR("RetroPlug Projects"), TSTR("*.retroplug") }
	};

	tstring path = BasicFileSave(GetUI(), types);
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
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
	if (paths.size() > 0) {
		//_lua->findRom(_proxy->activeIdx(), paths[0]);
	}
}

void RetroPlugView::OpenLoadProjectOrRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("All Supported Types"), TSTR("*.gb;*.gbc;*.retroplug") },
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") },
		{ TSTR("RetroPlug Project"), TSTR("*.retroplug") },
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
		_lua->loadRom(_activeIdx, ws2s(path));
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

void RetroPlugView::RemoveActive() {
	_lua->removeInstance(_activeIdx);
	UpdateLayout();
	UpdateActive();
}
