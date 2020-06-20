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

RetroPlugView::RetroPlugView(IRECT b, UiLuaContext* lua, AudioContextProxy* proxy, AudioController* audioController)
	: IControl(b), _lua(lua), _proxy(proxy), _audioController(audioController) {
	proxy->videoCallback = [&](const VideoStream& video) {
		const auto& systems = _proxy->getProject()->systems;
		for (const SystemDescPtr& system : systems) {
			if (system->state == SystemState::Running) {
				_views[system->idx]->WriteFrame(video.buffers[system->idx]);
			}
		}

		_timeSinceVideo = 0;
	};

	_proxy->setRenderingEnabled(true);
}

RetroPlugView::~RetroPlugView() {
	_proxy->setRenderingEnabled(false);
}

void RetroPlugView::OnInit() {
	UpdateLayout();
}

bool RetroPlugView::OnKey(const IKeyPress& key, bool down) {
	return _lua->onKey(key, down);
}

void RetroPlugView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	_lua->onDoubleClick(x, y, mod);
	ProcessDialog();
	UpdateLayout();
	UpdateSelected();
}

/*void RetroPlugView::SetZoom(int zoom) {
	Project* project = _proxy->getProject();
	project->settings.zoom = zoom + 1;

	for (auto& view : _views) {
		view->SetZoom(project->settings.zoom);
	}
}*/

void RetroPlugView::ProcessDialog() {
	ViewWrapper* viewWrapper = _lua->getViewWrapper();
	DialogRequestPtr dialog = viewWrapper->fetchDialogRequest();
	if (dialog) {
		switch (dialog->type) {
		case DialogType::Save: {
			std::string p = ws2s(BasicFileSave(GetUI(), dialog->filters, tstr(dialog->fileName)));
			std::vector<std::string> paths;
			paths.push_back(p);
			_lua->handleDialogCallback(paths);
			break;
		}
		case DialogType::Directory:
		case DialogType::Load: {
			std::vector<tstring> res = BasicFileOpen(
				GetUI(),
				dialog->filters,
				dialog->multiSelect,
				dialog->type == DialogType::Directory
			);

			std::vector<std::string> paths;
			for (size_t i = 0; i < res.size(); ++i) {
				paths.push_back(ws2s(res[i]));
			}

			_lua->handleDialogCallback(paths);

			break;
		}
		}

		UpdateLayout();
		UpdateSelected();
	}
}

void RetroPlugView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	_lua->onMouseDown(x, y, mod);

	ViewWrapper* viewWrapper = _lua->getViewWrapper();
	Menu* menu = viewWrapper->fetchMenu();
	if (menu) {
		_menu.Clear();

		// Show menu
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

				ProcessDialog();
				UpdateLayout();
				UpdateSelected();
			}
		});

		createMenu(&_menu, menu, callbacks);
		delete menu;
		
		GetUI()->CreatePopupMenu(*this, _menu, x, y);

		UpdateLayout();
		UpdateSelected();
	}

	/*SelectActiveAtPoint(x, y);

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
		SystemDescPtr active = _proxy->getActiveInstance();
		if (active) {
			switch (active->state) {
			case SystemState::Running: {
				SystemView* view = GetActiveView();

				if (!active->sourceSavData) {
					active->sourceSavData = std::make_shared<DataBuffer<char>>(DEFAULT_SRAM_SIZE);
				}

				// Update SRAM for the selected system since it might be used to generate the menu
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
			case SystemState::RomMissing:
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

				DialogRequest dialogRequest;
				if (_lua->getDialogRequest(dialogRequest)) {
					switch (dialogRequest.type) {
					case DialogType::Save: {
						std::string p = ws2s(BasicFileSave(GetUI(), dialogRequest.filters, tstr(dialogRequest.fileName)));
						std::vector<std::string> paths;
						paths.push_back(p);
						_lua->handleDialogCallback(paths);
						break;
					}
					case DialogType::Directory:
					case DialogType::Load: {
						std::vector<tstring> res = BasicFileOpen(
							GetUI(), 
							dialogRequest.filters, 
							dialogRequest.multiSelect,
							dialogRequest.type == DialogType::Directory
						);

						std::vector<std::string> paths;
						for (size_t i = 0; i < res.size(); ++i) {
							paths.push_back(ws2s(res[i]));
						}

						_lua->handleDialogCallback(paths);

						break;
					}
					}
				}
				
				UpdateLayout();
				UpdateSelected();
			}
		});

		createMenu(&_menu, &root, callbacks);
		GetUI()->CreatePopupMenu(*this, _menu, x, y);
	}*/
}

void RetroPlugView::Draw(IGraphics& g) {
	_frameTimer.stop();
	double delta = (double)_frameTimer.count();
	_frameTimer.reset();
	_frameTimer.start();

	UpdateSelected();

	onFrame(delta);
	_proxy->update(delta);
	_lua->update(delta);

	_timeSinceVideo += delta;
	
	const auto& systems = _proxy->getProject()->systems;
	for (size_t i = 0; i < systems.size(); ++i) {
		SystemView* view = _views[i];
		const SystemDescPtr& system = systems[i];

		if (system->state == SystemState::Running) {
			if (_timeSinceVideo < VIDEO_STREAM_TIMEOUT) {
				view->Draw(g, delta);
			} else {
				system->state = SystemState::VideoFeedLost;
				view->ShowText("Audio timeout", "Check DAW settings");
				view->DeleteFrame();
			}
		} else if (system->state == SystemState::VideoFeedLost) {
			if (_timeSinceVideo < VIDEO_STREAM_TIMEOUT) {
				view->HideText();
				view->Draw(g, delta);
				system->state = SystemState::Running;
			}
		}
	}
}

void RetroPlugView::OnDrop(float x, float y, const char* str) {
	_lua->onDrop(x, y, str);
}

void RetroPlugView::UpdateLayout() {
	if (_views.empty()) {
		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			_views.push_back(new SystemView(i, GetUI()));
		}
	}

	int zoom = _proxy->getProject()->settings.zoom;
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		_views[i]->HideText();
		_views[i]->SetZoom(zoom);
	}

	const Project::Settings& settings = _proxy->getProject()->settings;
	int frameW = FRAME_WIDTH * settings.zoom;
	int frameH = FRAME_HEIGHT * settings.zoom;

	int windowW = frameW;
	int windowH = frameH;

	size_t count = _proxy->getProject()->systems.size();
	if (count == 0) {
		count = 1;
		_views[0]->ShowText("Double click to", "load a rom...");
		_views[0]->DeleteFrame();
	}

	SystemLayout layout = settings.layout;
	if (layout == SystemLayout::Auto) {
		if (count < 4) {
			layout = SystemLayout::Row;
		} else {
			layout = SystemLayout::Grid;
		}
	}

	if (layout == SystemLayout::Row) {
		windowW = count * frameW;
	} else if (layout == SystemLayout::Column) {
		windowH = count * frameH;
	} else if (layout == SystemLayout::Grid) {
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

	auto& systems = _proxy->getProject()->systems;

	for (size_t i = 0; i < count; i++) {
		int gridX = 0;
		int gridY = 0;

		if (layout == SystemLayout::Row) {
			gridX = i;
		} else if (layout == SystemLayout::Column) {
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

		if (i < systems.size()) {
			systems[i]->area = Rect(b.L, b.T, b.W(), b.H());
		}
	}

	for (size_t i = count; i < _views.size(); ++i) {
		_views[i]->HideText();
		_views[i]->DeleteFrame();
	}
}

void RetroPlugView::UpdateSelected() {
	SystemIndex idx = _proxy->getProject()->selectedSystem;

	for (auto view : _views) {
		view->SetAlpha(INACTIVE_ALPHA);
	}

	/*if (_activeIdx != NO_ACTIVE_SYSTEM && idx != _activeIdx) {
		_views[_activeIdx]->SetAlpha(INACTIVE_ALPHA);
	}*/

	if (idx != NO_ACTIVE_SYSTEM) {
		_views[idx]->SetAlpha(ACTIVE_ALPHA);
	}

	_activeIdx = idx;
}

void RetroPlugView::RequestSave() {
	/*_proxy->requestSave([&](const FetchStateResponse& res) {
		_lua->saveProject(res);
	});*/
}
