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
			_menu = IPopupMenu();

			if (IsActive()) {
				IPopupMenu* projectMenu = CreateProjectMenu(true);
				_active->CreateMenu(&_menu, projectMenu);

				projectMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
					switch ((ProjectMenuItems)idx) {
					case ProjectMenuItems::New: NewProject(); break;
					case ProjectMenuItems::Save: SaveProject(); break;
					case ProjectMenuItems::SaveAs: SaveProjectAs(); break;
					case ProjectMenuItems::Load: LoadProject(); break;
					case ProjectMenuItems::RemoveInstance: RemoveActive(); break;
					}
				});
			} else {
				IPopupMenu* modelMenu = createModelMenu(true);
				createBasicMenu(&_menu, modelMenu);

				_menu.SetFunction([=](int idx, IPopupMenu::Item*) {
					switch ((BasicMenuItems)idx) {
					case BasicMenuItems::LoadProject: LoadProject(); break;
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
	if (IsActive() && _showText) {
		ShowText(false);
	}

	for (auto view : _views) {
		view->Draw(g);
	}
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
		char* buf = new char[stateSize];
		source->saveState(buf, stateSize);
		target->loadState(buf, stateSize);
		delete[] buf;
	}

	AddView(target);

	_plug->updateLinkTargets();
}

EmulatorView* RetroPlugRoot::AddView(SameBoyPlugPtr plug) {
	EmulatorView* view = new EmulatorView(plug, _plug);
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
		UpdateTextPosition();
	}
}

void RetroPlugRoot::ShowText(bool show) {
	if (_showText == show) {
		return;
	}

	const IRECT b = GetRECT();
	float mid = b.H() / 2;
	IRECT topRow(b.L, mid - 25, b.R, mid);
	IRECT bottomRow(b.L, mid, b.R, mid + 25);

	if (show) {
		if (!_textIds[0]) {
			_textIds[0] = new ITextControl(topRow, "Double click to", IText(23, COLOR_WHITE));
			_textIds[1] = new ITextControl(bottomRow, "load a ROM...", IText(23, COLOR_WHITE));

			GetUI()->AttachControl(_textIds[0]);
			GetUI()->AttachControl(_textIds[1]);
		} else {
			_textIds[0]->SetTargetAndDrawRECTs(topRow);
			_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
		}
	} else {
		if (_textIds[0]) {
			_textIds[0]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
			_textIds[1]->SetTargetAndDrawRECTs(IRECT(0, -100, 0, 0));
		}
	}

	_showText = show;
}

void RetroPlugRoot::UpdateTextPosition() {
	if (_showText) {
		assert(_textIds[0]);

		const IRECT b = GetRECT();
		float mid = b.H() / 2;
		IRECT topRow(b.L, mid - 25, b.R, mid);
		IRECT bottomRow(b.L, mid, b.R, mid + 25);

		_textIds[0]->SetTargetAndDrawRECTs(topRow);
		_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
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

	instanceMenu->SetFunction([this](int idx, IPopupMenu::Item* itemChosen) {
		CreatePlugInstance(_active, (CreateInstanceType)idx);
	});

	layoutMenu->SetFunction([this](int idx, IPopupMenu::Item * itemChosen) {
		_plug->setLayout((InstanceLayout)idx);
		UpdateLayout();
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
			view->Clear(GetUI());
			delete view;
		}
	}

	_views.clear();
}

void RetroPlugRoot::NewProject() {
	CloseProject();

	SameBoyPlugPtr plug = _plug->addInstance(EmulatorType::SameBoy);
	EmulatorView* view = AddView(plug);
	ShowText(true);
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
			_plug->setProjectPath(paths[0]);

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
	_active->Clear(GetUI());
	delete _active;
	_active = nullptr;
	
	_views.erase(_views.begin() + _activeIdx);

	if (_activeIdx >= _views.size()) {
		_activeIdx = _views.size() - 1;
	}

	SetActive(_views[_activeIdx]);
	UpdateLayout();
}
