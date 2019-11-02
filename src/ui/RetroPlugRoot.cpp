#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "util/File.h"
#include "util/fs.h"
#include "util/Serializer.h"
#include "Keys.h"


RetroPlugRoot::RetroPlugRoot(IRECT b, RetroPlug* plug, EHost host): IControl(b), _plug(plug), _host(host) {
	_padManager = new gainput::InputManager();
	//_padMap = new gainput::InputMap(*_padManager);

	_padId = _padManager->CreateDevice<gainput::InputDevicePad>();

	memset(_padButtons, 0, sizeof(_padButtons));
}

RetroPlugRoot::~RetroPlugRoot() {
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		auto plug = _plug->getPlug(i);
		if (plug && plug->active()) {
			plug->disableRendering(true);
		}
	}
}

void RetroPlugRoot::OnInit() {
	size_t plugCount = _plug->instanceCount();
	if (plugCount == 0) {
		NewProject();
	} else {
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			auto plug = _plug->getPlug(i);
			if (plug) {
				AddView(plug);
			}
		}
	}
}

bool RetroPlugRoot::OnKey(const IKeyPress& key, bool down) {
	if (_active) {
        /*if (key.VK == VirtualKeys::E) {
            static bool alt = true;
            int windowW = alt ? 320 : 480;
            int windowH = alt ? 288 : 320;
            alt = !alt;
            GetUI()->SetSizeConstraints(320, windowW, 288, windowH);
            GetUI()->Resize(windowW, windowH, 1);
            SetTargetAndDrawRECTs(IRECT(0, 0, windowW, windowH));
            GetUI()->GetControl(0)->SetTargetAndDrawRECTs(GetRECT());
            return true;
        }*/
        
		if (key.VK == VirtualKeys::Tab && down) {
			_activeIdx = (_activeIdx + 1) % _views.size();
			SetActive(_views[_activeIdx]);
			return true;
		}

		return _active->OnKey(key, down);
	}

	return false;
}

void RetroPlugRoot::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	if (_active) {
		auto plug = _active->Plug();
		if (!plug->active()) {
			if (plug->romPath().empty()) {
				OpenLoadProjectOrRomDialog();
			} else {
				OpenFindRomDialog();
			}
		}
	}
}

void RetroPlugRoot::OnMouseDown(float x, float y, const IMouseMod& mod) {
	SelectActiveAtPoint(x, y);

	if (mod.R) {
		if (_active) {
			auto plug = _active->Plug();
			_menu.Clear();

			if (plug->active()) {
				IPopupMenu* projectMenu = CreateProjectMenu(true);
				_active->CreateMenu(&_menu, projectMenu);

				projectMenu->SetFunction([this](int idx, IPopupMenu::Item * itemChosen) {
					switch ((ProjectMenuItems)idx) {
					case ProjectMenuItems::New: NewProject(); break;
					case ProjectMenuItems::Save: SaveProject(); break;
					case ProjectMenuItems::SaveAs: SaveProjectAs(); break;
					case ProjectMenuItems::Load: OpenLoadProjectDialog(); break;
					case ProjectMenuItems::RemoveInstance: RemoveActive(); break;
					}
				});
			} else if (!plug->romPath().empty()) {
				_menu.AddItem("Find ROM...");
				_menu.AddItem("Load Project...");
				_menu.SetFunction([=](int idx, IPopupMenu::Item*) {	
					if (idx == 0) {
						OpenFindRomDialog();
					} else {
						OpenLoadProjectDialog();
					}
				});
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
}

void RetroPlugRoot::Draw(IGraphics & g) {
	for (auto view : _views) {
		view->Draw(g);
	}

	_padManager->Update();
	
	for (int i = 0; i < gainput::PadButtonCount_; ++i) {
		bool down = _padManager->GetDevice(_padId)->GetBool(gainput::PadButtonStart + i);
		if (_padButtons[i] != down) {
			_padButtons[i] = down;
			if (_active) {
				_active->OnGamepad(i + gainput::PadButtonStart, down);
			}
		}
	}
}

void RetroPlugRoot::OnDrop(float x, float y, const char* str) {
	SelectActiveAtPoint(x, y);

	auto plug = _active->Plug();
	
	tstring path = tstr(str);
	tstring ext = tstr(fs::path(path).extension().wstring());

	Lsdj& lsdj = plug->lsdj();
	if (lsdj.found) {
		std::string error;
		
		if (ext == TSTR(".kit")) {
			int idx = lsdj.findEmptyKit();
			if (idx != -1) {
				if (lsdj.loadKit(path, idx, error)) {
					lsdj.patchKits(plug->romData());
					plug->updateRom();
					plug->reset(plug->model(), true);
				} else {
					// log err
				}
			}

			return;
		} else if (ext == TSTR(".lsdsng")) {
			// Load lsdj song
			plug->saveBattery(lsdj.saveData);
			std::vector<int> ids = lsdj.importSongs({ path }, error);
			if (ids.size() > 0 && ids[0] != -1) {
				_active->LoadSong(ids[0]);
			} else {
				// log err
			}

			return;
		}
	}

	if (ext == TSTR(".retroplug")) {
		LoadProject(path);
	} else if (ext == TSTR(".gb") || ext == TSTR(".gbc")) {
		_active->LoadRom(path);
	} else if (ext == TSTR(".sav")) {
		plug->loadBattery({ path }, true);
	}
}

void RetroPlugRoot::CreatePlugInstance(EmulatorView* view, CreateInstanceType type) {
	SameBoyPlugPtr source = view->Plug();
	
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

	SameBoyPlugPtr target = _plug->addInstance(EmulatorType::SameBoy);
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

	_plug->updateLinkTargets();
}

EmulatorView* RetroPlugRoot::AddView(SameBoyPlugPtr plug) {
	EmulatorView* view = new EmulatorView(plug, _plug, GetUI());
	_views.push_back(view);

	SetActive(view);
	_activeIdx = _views.size() - 1;

	UpdateLayout();

	if (_views.size() > 1) {
		if (plug->lsdj().found) {
			plug->setGameLink(true);
		}

		if (_views.size() == 2) {
			auto otherPlug = _plug->plugs()[0];
			if (otherPlug->lsdj().found) {
				otherPlug->setGameLink(true);
			}
		}
	}

	if (!plug->active() && !plug->romPath().empty()) {
		view->ShowText("Unable to find", fs::path(plug->romPath()).filename().string());
	}

	return view;
}

void RetroPlugRoot::UpdateLayout() {
	size_t count = _views.size();
	assert(count > 0);

	InstanceLayout layout = _plug->layout();
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

	for (size_t i = 0; i < _views.size(); i++) {
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
}

void RetroPlugRoot::SetActive(EmulatorView* view) {
	if (_active) {
		_active->SetAlpha(0.75f);
	}

	_active = view;
	_active->SetAlpha(1.0f);

	view->DisableRendering(false);
}

IPopupMenu* RetroPlugRoot::CreateProjectMenu(bool loaded) {
	IPopupMenu* instanceMenu = createInstanceMenu(loaded, _views.size() < 4);
	IPopupMenu* layoutMenu = createLayoutMenu(_plug->layout());
	IPopupMenu* saveOptionsMenu = createSaveOptionsMenu(_plug->saveType());
	IPopupMenu* audioRouting = createAudioRoutingMenu(_plug->audioRouting());
	IPopupMenu* midiRouting = createMidiRoutingMenu(_plug->midiRouting());

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

	layoutMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		_plug->setLayout((InstanceLayout)idx);
		UpdateLayout();
	});

	saveOptionsMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
		_plug->setSaveType((SaveStateType)idx);
	});

	audioRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		_plug->setAudioRouting((AudioChannelRouting)idx);
	});

	midiRouting->SetFunction([=](int idx, IPopupMenu::Item*) {
		_plug->setMidiRouting((MidiChannelRouting)idx);
	});

	return menu;
}

void RetroPlugRoot::CloseProject() {
	_plug->clear();
	IGraphics* g = GetUI();

	if (g) {
		for (auto view : _views) {
			delete view;
		}
	}

	_views.clear();
	_active = nullptr;
	_activeIdx = -1;
}

void RetroPlugRoot::NewProject() {
	CloseProject();

	SameBoyPlugPtr plug = _plug->addInstance(EmulatorType::SameBoy);
	EmulatorView* view = AddView(plug);
	view->ShowText("Double click to", "load a rom...");
	UpdateLayout();
}

void RetroPlugRoot::SaveProject() {
	if (_plug->projectPath().empty()) {
		SaveProjectAs();
	}

	std::string data;
	serialize(data, *_plug);
	writeFile(_plug->projectPath(), data);
}

void RetroPlugRoot::SaveProjectAs() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("RetroPlug Projects"), TSTR("*.retroplug") }
	};

	tstring path = BasicFileSave(GetUI(), types);
	if (path.size() > 0) {
		_plug->setProjectPath(path);
		SaveProject();
	}
}

void RetroPlugRoot::OpenFindRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("GameBoy Roms"), TSTR("*.gb;*.gbc") }
	};

	std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
	if (paths.size() > 0) {
		tstring originalPath = _active->Plug()->romPath();
		for (size_t i = 0; i < _views.size(); i++) {
			auto plug = _views[i]->Plug();
			if (plug->romPath() == originalPath) {
				plug->init(paths[0], plug->model(), true);
				plug->disableRendering(false);
				_views[i]->HideText();
			}
		}
	}
}

void RetroPlugRoot::OpenLoadProjectOrRomDialog() {
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

void RetroPlugRoot::LoadProjectOrRom(const tstring& path) {
	tstring ext = tstr(fs::path(path).extension().string());
	if (ext == TSTR(".retroplug")) {
		LoadProject(path);
	} else if (ext == TSTR(".gb") || ext == TSTR(".gbc")) {
		_active->LoadRom(path);
	}
}

void RetroPlugRoot::LoadProject(const tstring& path) {
	std::string data;
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
				SetActive(_views[_activeIdx]);
			}
		} else {
			NewProject();
		}
	}

	_plug->updateLinkTargets();
}

void RetroPlugRoot::OpenLoadProjectDialog() {
	std::vector<FileDialogFilters> types = {
		{ TSTR("RetroPlug Projects"), TSTR("*.retroplug") }
	};

	std::vector<tstring> paths = BasicFileOpen(GetUI(), types, false);
	if (paths.size() > 0) {
		LoadProject(paths[0]);
	}
}

void RetroPlugRoot::RemoveActive() {
	_plug->removeInstance(_activeIdx);
	delete _active;
	_active = nullptr;
	
	_views.erase(_views.begin() + _activeIdx);

	if (_activeIdx >= _views.size()) {
		_activeIdx = _views.size() - 1;
	}

	SetActive(_views[_activeIdx]);
	UpdateLayout();
}
