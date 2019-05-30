#include "RetroPlugRoot.h"

#include <cmath>
#include "platform/FileDialog.h"
#include "util/File.h"
#include "util/Serializer.h"

RetroPlugRoot::RetroPlugRoot(IRECT b, RetroPlug* plug): IControl(b), _plug(plug) {
	
}

void RetroPlugRoot::OnInit() {
	NewProject();
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

void RetroPlugRoot::OnMouseDown(float x, float y, const IMouseMod& mod) {
	for (auto view : _views) {
		if (view->GetRECT().Contains(x, y)) {
			_activeIdx = GetViewIndex(view);
			SetActive(_views[_activeIdx]);
			view->OnMouseDown(x, y, mod);
			break;
		}
	}
}

void RetroPlugRoot::CreatePlugInstance(EmulatorView* view, CreateInstanceType type) {
	SameBoyPlugPtr source = view->Plug();
	
	std::string romPath;
	if (type == CreateInstanceType::LoadRom) {
		std::vector<FileDialogFilters> types = {
			{ L"GameBoy Roms", L"*.gb;*.gbc" }
		};

		std::vector<std::wstring> paths = BasicFileOpen(types, false);
		if (paths.size() > 0) {
			romPath = ws2s(paths[0]);
		}
	} else {
		romPath = source->romPath();
	}	

	if (romPath.size() == 0) {
		return;
	}

	SameBoyPlugPtr target = _plug->addInstance(EmulatorType::SameBoy);
	target->init(romPath, GameboyModel::DmgB);

	if (type == CreateInstanceType::Duplicate) {
		size_t stateSize = source->saveStateSize();
		char* buf = new char[stateSize];
		source->saveState(buf, stateSize);
		target->loadState(buf, stateSize);
		delete[] buf;
	}

	AddView(target);

	_plug->updateLinkTargets();
}

EmulatorView* RetroPlugRoot::AddView(SameBoyPlugPtr plug) {
	IRECT b(0, 0, 0, 0);
	EmulatorView* view = new EmulatorView(b, plug, _plug);
	view->OnProjectMenuRequest = [this](IPopupMenu * target, bool loaded) { return CreateProjectMenu(target, loaded); };

	GetUI()->AttachControl(view);
	GetUI()->BringToFront(this);
	_views.push_back(view);

	SetActive(view);
	_activeIdx = _views.size() - 1;

	UpdateLayout();

	if (_views.size() > 1) {
		plug->setGameLink(true);
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			auto plug = _plug->plugs()[i];
			if (plug) {
				plug->setGameLink(true);
			}
		}
	}

	return view;
}

void RetroPlugRoot::UpdateLayout() {
	size_t count = _views.size();
	assert(count > 0);

	RetroPlugLayout layout = _layout;
	if (layout == RetroPlugLayout::Auto) {
		if (count < 4) {
			layout = RetroPlugLayout::Row;
		} else {
			layout = RetroPlugLayout::Grid;
		}
	}

	int windowW = 320;
	int windowH = 288;

	if (layout == RetroPlugLayout::Row) {
		windowW = count * 320;
	} else if (layout == RetroPlugLayout::Column) {
		windowH = count * 288;
	} else if (layout == RetroPlugLayout::Grid) {
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

		if (layout == RetroPlugLayout::Row) {
			gridX = i;
		} else if (layout == RetroPlugLayout::Column) {
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
		_views[i]->SetTargetAndDrawRECTs(b);
		_views[i]->UpdateTextPosition();
	}
}

void RetroPlugRoot::SetActive(EmulatorView* view) {
	if (_active) {
		_active->SetAlpha(0.75f);
	}

	_active = view;
	_active->SetAlpha(1.0f);
}

IPopupMenu* createInstanceMenu(bool loaded) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)CreateInstanceType::LoadRom);
	menu->AddItem("Same ROM", (int)CreateInstanceType::SameRom, loaded ? 0 : IPopupMenu::Item::kDisabled);
	menu->AddItem("Duplicate", (int)CreateInstanceType::Duplicate, loaded ? 0 : IPopupMenu::Item::kDisabled);
	return menu;
}

IPopupMenu* createLayoutMenu(RetroPlugLayout checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Auto", (int)RetroPlugLayout::Auto);
	menu->AddItem("Column", (int)RetroPlugLayout::Column);
	menu->AddItem("Row", (int)RetroPlugLayout::Row);
	menu->AddItem("Grid", (int)RetroPlugLayout::Grid);
	menu->CheckItemAlone((int)checked);
	return menu;
}

IPopupMenu* createSaveOptionsMenu(SaveModes checked) {
	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Save SRAM", (int)SaveModes::SaveSram);
	menu->AddItem("Save State", (int)SaveModes::SaveState);
	menu->CheckItemAlone((int)checked);
	return menu;
}

void RetroPlugRoot::CreateProjectMenu(IPopupMenu* target, bool loaded) {
	IPopupMenu* instanceMenu = createInstanceMenu(loaded);
	IPopupMenu* layoutMenu = createLayoutMenu(_layout);
	IPopupMenu* saveOptionsMenu = createSaveOptionsMenu(_saveMode);

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

	target->AddItem("Project", menu, (int)RootMenuItems::Project);

	menu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		switch ((ProjectMenuItems)idx) {
		case ProjectMenuItems::New: NewProject(); break;
		case ProjectMenuItems::Save: SaveProject(); break;
		case ProjectMenuItems::SaveAs: SaveProjectAs(); break;
		case ProjectMenuItems::Load: LoadProject(); break;
		case ProjectMenuItems::RemoveInstance: RemoveActive(); break;
		}
	});

	instanceMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		CreatePlugInstance(_active, (CreateInstanceType)idx);
	});

	layoutMenu->SetFunction([this](int idx, IPopupMenu::Item * itemChosen) {
		_layout = (RetroPlugLayout)idx;
		UpdateLayout();
	});
}

void RetroPlugRoot::CloseProject() {
	_plug->clear();
	IGraphics* g = GetUI();

	if (g) {
		for (auto view : _views) {
			GetUI()->RemoveControl(view);
		}
	}

	_views.clear();
}

void RetroPlugRoot::NewProject() {
	CloseProject();

	SameBoyPlugPtr plug = _plug->addInstance(EmulatorType::SameBoy);
	EmulatorView* view = AddView(plug);
	view->ShowText(true);
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

void RetroPlugRoot::LoadProject() {
	std::vector<FileDialogFilters> types = {
		{ L"RetroPlug Projects", L"*.retroplug" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::string data;
		if (readFile(paths[0], data)) {
			CloseProject();
			Deserialize(data.c_str(), *_plug);

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
}

void RetroPlugRoot::RemoveActive() {
	_plug->removeInstance(_activeIdx);
	GetUI()->RemoveControl(_active);
	
	_views.erase(_views.begin() + _activeIdx);

	if (_activeIdx >= _views.size()) {
		_activeIdx = _views.size() - 1;
	}

	SetActive(_views[_activeIdx]);
	UpdateLayout();
}
