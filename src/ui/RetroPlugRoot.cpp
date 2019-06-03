#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "util/File.h"
#include "util/Serializer.h"

RetroPlugRoot::RetroPlugRoot(IRECT b, RetroPlug* plug): IControl(b), _plug(plug) {
	
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
	for (auto view : _views) {
		if (view->GetArea().Contains(x, y)) {
			_activeIdx = GetViewIndex(view);
			SetActive(_views[_activeIdx]);
			break;
		}
	}

	if (mod.R) {
		if (_active) {
			auto plug = _active->Plug();
			_menu = IPopupMenu();

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
}

void RetroPlugRoot::OnDrop(const char* str) {
	LoadProjectOrRom(s2ws(str));
}

void RetroPlugRoot::CreatePlugInstance(EmulatorView* view, CreateInstanceType type) {
	SameBoyPlugPtr source = view->Plug();
	
	std::wstring romPath;
	if (type == CreateInstanceType::LoadRom) {
		std::vector<FileDialogFilters> types = {
			{ L"GameBoy Roms", L"*.gb;*.gbc" }
		};

		std::vector<std::wstring> paths = BasicFileOpen(types, false);
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
		plug->setGameLink(true);

		if (_views.size() == 2) {
			auto plug = _plug->plugs()[0];
			plug->setGameLink(true);
		}
	}

	if (!plug->active() && !plug->romPath().empty()) {
		view->ShowText("Unable to find", std::filesystem::path(plug->romPath()).filename().string());
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

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("New", (int)ProjectMenuItems::New);
	menu->AddItem("Load...", (int)ProjectMenuItems::Load);
	menu->AddItem("Save", (int)ProjectMenuItems::Save);
	menu->AddItem("Save As...", (int)ProjectMenuItems::SaveAs);
	menu->AddSeparator((int)ProjectMenuItems::Sep1);
	menu->AddItem("Save Options", saveOptionsMenu, (int)ProjectMenuItems::SaveOptions);
	menu->AddSeparator((int)ProjectMenuItems::Sep2);
	menu->AddItem("Add Instance", instanceMenu, (int)ProjectMenuItems::AddInstance);
	menu->AddItem("Remove Instance", (int)ProjectMenuItems::RemoveInstance, _views.size() < 2 ? IPopupMenu::Item::kDisabled : 0);
	menu->AddItem("Layout", layoutMenu, (int)ProjectMenuItems::Layout);

	instanceMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		CreatePlugInstance(_active, (CreateInstanceType)idx);
	});

	layoutMenu->SetFunction([this](int idx, IPopupMenu::Item * itemChosen) {
		_plug->setLayout((InstanceLayout)idx);
		UpdateLayout();
	});

	saveOptionsMenu->SetFunction([=](int idx, IPopupMenu::Item*) {
		_plug->setSaveType((SaveStateType)idx);
	});

	return menu;
}

void RetroPlugRoot::OnPopupMenuSelection(IPopupMenu * selectedMenu, int valIdx) {
	if (selectedMenu) {
		if (selectedMenu->GetFunction()) {
			selectedMenu->ExecFunction();
		}
	}
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
	Serialize(data, *_plug);
	writeFile(_plug->projectPath(), data);
}

void RetroPlugRoot::SaveProjectAs() {
	std::vector<FileDialogFilters> types = {
		{ L"RetroPlug Projects", L"*.retroplug" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() > 0) {
		_plug->setProjectPath(path);
		SaveProject();
	}
}

void RetroPlugRoot::OpenFindRomDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Roms", L"*.gb;*.gbc" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::wstring originalPath = _active->Plug()->romPath();
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
		{ L"All Supported Types", L"*.gb;*.gbc;*.retroplug" },
		{ L"GameBoy Roms", L"*.gb;*.gbc" },
		{ L"RetroPlug Project", L"*.retroplug" },
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		LoadProjectOrRom(paths[0]);
	}
}

void RetroPlugRoot::LoadProjectOrRom(const std::wstring& path) {
	std::wstring ext = std::filesystem::path(path).extension().wstring();
	if (ext == L".retroplug") {
		LoadProject(path);
	} else if (ext == L".gb" || ext == L".gbc") {
		_active->LoadRom(path);
	}
}

void RetroPlugRoot::LoadProject(const std::wstring& path) {
	std::string data;
	if (readFile(path, data)) {
		CloseProject();
		Deserialize(data.c_str(), *_plug);
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
}

void RetroPlugRoot::OpenLoadProjectDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"RetroPlug Projects", L"*.retroplug" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
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
