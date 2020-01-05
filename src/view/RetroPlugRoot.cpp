#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "util/File.h"
#include "util/fs.h"
#include "util/Serializer.h"
#include "util/cxxtimer.hpp"
#include "Keys.h"

RetroPlugView::RetroPlugView(IRECT b, LuaContext* lua, RetroPlugProxy* proxy): IControl(b), _lua(lua), _proxy(proxy) {
	proxy->videoCallback = [&](const VideoStream& video) {
		if (_views.size() == MAX_INSTANCES) {
			for (size_t i = 0; i < MAX_INSTANCES; ++i) {
				const VideoBuffer& buffer = video.buffers[i];
				if (buffer.data.get()) {
					_views[i]->WriteFrame(buffer);
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
	if (!active) {
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
				_active->CreateMenu(&_menu, projectMenu);

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
					}
					else {
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
				case BasicMenuItems::LoadRom: _active->OpenLoadRomDialog(GameboyModel::Auto); break;
				}
			});

			modelMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
				_active->OpenLoadRomDialog((GameboyModel)(idx + 1));
			});
		}

		GetUI()->CreatePopupMenu(*((IControl*)this), _menu, x, y);
	}
}

void RetroPlugView::Draw(IGraphics& g) {
	_frameTimer.stop();
	double delta = _frameTimer.count();
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

void RetroPlugView::CreatePlugInstance(EmulatorView* view, CreateInstanceType type) {
	/*SameBoyPlugPtr source = view->Plug();
	
	tstring romPath;
	if (type == CreateInstanceType::LoadRom) {
		std::vector<FileDialogFilters> types = {
			{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
		};

		std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
		if (paths.size() > 0) {
			romPath = paths[0];
		}
	} else {
		romPath = source->romPath();
	}	

	if (romPath.size() == 0) {
		return;
	}

	SameBoyPlugPtr target = _lua->addInstance(EmulatorType::SameBoy);
	target->init(romPath, source->model(), false);

	if (source->lsdj().found) {
		target->lsdj().kitData = source->lsdj().kitData;
		target->lsdj().patchKits(target->romData());
		target->updateRom();
	}

	if (type == CreateInstanceType::Duplicate) {
		size_t stateSize = source->saveStateSize();
		std::byte* buf = new std::byte[stateSize];
		source->saveState(buf, stateSize);
		target->loadState(buf, stateSize);
		delete[] buf;
	}

	AddView(target);

	_plug->updateLinkTargets();*/
}

void RetroPlugView::UpdateLayout() {
	if (_views.empty()) {
		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			_views.push_back(new EmulatorView(i, _lua, _proxy, GetUI()));
		}
	}

	size_t count = _proxy->getInstanceCount();
	if (count == 0) {
		count = 1;
		_views[0]->ShowText("Double click to", "load a rom...");
	}

	InstanceLayout layout = _proxy->getProject()->settings.layout;
	if (layout == InstanceLayout::Auto) {
		if (count < 4) {
			layout = InstanceLayout::Row;
		} else {
			layout = InstanceLayout::Grid;
		}
	}

	int windowW = 320;
	int windowH = 288;

	if (layout == InstanceLayout::Row) {
		windowW = count * 320;
	} else if (layout == InstanceLayout::Column) {
		windowH = count * 288;
	} else if (layout == InstanceLayout::Grid) {
		if (count > 2) {
			windowW = 2 * 320;
			windowH = 2 * 288;
		} else {
			windowW = count * 320;
		}
	}

	GetUI()->SetSizeConstraints(320, windowW, 288, windowH);
	GetUI()->Resize(windowW, windowH, 1);
	SetTargetAndDrawRECTs(IRECT(0, 0, windowW, windowH));
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

		int x = gridX * 320;
		int y = gridY * 288;

		IRECT b(x, y, x + 320, y + 288);
		_views[i]->SetArea(b);
	}

	for (size_t i = count; i < _views.size(); ++i) {
		_views[i]->HideText();
	}
}

void RetroPlugView::UpdateActive() {
	InstanceIndex idx = _proxy->activeIdx();

	if (_activeIdx != idx) {
		if (_activeIdx != NO_ACTIVE_INSTANCE) {
			_views[_activeIdx]->SetAlpha(0.75f);
		}
	}

	if (idx != NO_ACTIVE_INSTANCE) {
		_views[_activeIdx]->SetAlpha(1.0f);
	}

	_activeIdx = idx;
}

void RetroPlugView::SetActive(size_t index) {
	_lua->setActive(index);
	UpdateActive();
}

IPopupMenu* RetroPlugView::CreateProjectMenu(bool loaded) {
	Project* project = _proxy->getProject();

	IPopupMenu* instanceMenu = createInstanceMenu(loaded, _views.size() < 4);
	IPopupMenu* layoutMenu = createLayoutMenu(project->settings.layout);
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
		CreatePlugInstance(_active, (CreateInstanceType)idx);
	});

	layoutMenu->SetFunction([&](int idx, IPopupMenu::Item* itemChosen) {
		project->settings.layout = (InstanceLayout)idx;
		_proxy->updateSettings();
		UpdateLayout();
	});

	saveOptionsMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
		project->settings.saveType = (SaveStateType)idx;
	});

	audioRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		project->settings.audioRouting = (AudioChannelRouting)idx;
		_proxy->updateSettings();
	});

	midiRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		project->settings.midiRouting = (MidiChannelRouting)idx;
		_proxy->updateSettings();
	});

	return menu;
}

void RetroPlugView::CloseProject() {
	//_lua->closeProject();
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
	}

	//_lua.saveProject(_proxy->getProject()->path);

	/*std::string data;
	serialize(data, *_plug);
	writeFile(_plug->projectPath(), data);*/
}

void RetroPlugView::SaveProjectAs() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("RetroPlug Projects"), TSTR("*.retroplug") }
	};

	tstring path = BasicFileSave(GetUI(), types);
	if (path.size() > 0) {
		//_lua.saveProject(path);
	}
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
		//_active->LoadRom(path);
		_lua->loadRom(_activeIdx, ws2s(path));
	}
}

void RetroPlugView::LoadProject(const tstring& path) {
	//_lua->loadProject(path);
	UpdateLayout();
	/*std::string data;
	if (readFile(path, data)) {
		CloseProject();
		deserialize(data.c_str(), *_plug);
		_plug->setProjectPath(path);

		if (_plug->instanceCount() > 0) {
			for (size_t i = 0; i < MAX_INSTANCES; i++) {
				auto plug = _plug->plugs()[i];
				if (plug) {
					AddView(plug);
				}

				_activeIdx = 0;
				SetActive(_activeIdx);
			}
		} else {
			NewProject();
		}
	}

	_plug->updateLinkTargets();*/
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
}
