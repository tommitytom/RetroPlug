#include "RetroPlugView.h"

#include "foundation/Replicator.h"
#include "core/RetroPlugComponents.h"
#include "core/HierarchyUtil.h"

#include "ui/HexEditor.h"
#include "ui/LsdjHdPlayer.h"
#include "ui/LoadingView.h"
#include "ui/PanelView.h"
#include "ui/SliderView.h"
#include "ui/SystemView.h"
#include "ui/TextureView.h"
#include "ui/TileGrid.h"

#include "application/WindowManager.h"

#include "sameboy/SameBoyComponents.h"

namespace rp {
	RetroPlugView::RetroPlugView(RetroPlugProject& project) : View({ 480, 432 }), _project(project) {
		setName(std::format("RetroPlug v{}", RP_VERSION));
		setFocusPolicy(fw::FocusPolicy::Click);
	}

	void RetroPlugView::onInitialize() {
		setScale(3.0f);
		fw::ViewLayout& layout = getLayout();
		layout.setFlexDirection(fw::FlexDirection::Row);
		layout.setFlexWrap(fw::FlexWrap::Wrap);
		//getLayout().setOverflow(fw::FlexOverflow::Visible);
	}

	bool RetroPlugView::onDrop(const std::vector<std::string>& paths) {
#ifndef FW_PLATFORM_WEB
		std::vector<std::filesystem::path> fsPaths;
		for (const std::string& path : paths) {
			fsPaths.push_back(path);
		}

		_project.loadFromPaths(fsPaths);
		return true;
#endif

		return false;
	}

	void RetroPlugView::onUpdate(f32 deltaTime) {
		_project.onUpdate(deltaTime);

		if (_project.getVersion() != _version) {
			rebuildUi();
			_version = _project.getVersion();
		}

		fw::DimensionF dimensions{
			160.0f,
			144.0f
		};

		if (_rootContainer) {
			dimensions = _rootContainer->getDimensionsF();
		}

		dimensions *= getScale();

		getLayout().setDimensions(fw::Dimension(dimensions));
	}

	void RetroPlugView::onRender(fw::Canvas& canvas) {
		canvas.fillRect(getDimensions(), fw::Color4F::red);
	}

	void RetroPlugView::rebuildUi() {
		entt::registry& registry = getRegistry();

		std::string projectName = _project.getProjectName();
		if (!projectName.empty()) projectName += " | ";
		projectName += std::format("RetroPlug v{}", RP_VERSION);
		setName(projectName);

		bool loadScreenVisible = _rootContainer && _rootContainer->isType<LoadingView>();
		bool projectLoading = _project.getContext().loading;

		if (loadScreenVisible != projectLoading) {
			removeChildren();

			if (projectLoading) {
				_rootContainer = addChild<LoadingView>("Loading Screen");
			} else if (loadScreenVisible) {
				_rootContainer->remove();
				_rootContainer = nullptr;
			}

			loadScreenVisible = projectLoading;
		}

		const size_t systemCount = _project.getRegistry().view<SystemComponent>().size();
		if (!_rootContainer && systemCount > 0) {
			_rootContainer = addChild(std::make_shared<TileGrid>(_project));
		}
	}

	void RetroPlugView::setRootContainer(const std::shared_ptr<RootContainer>& container) {
		_rootContainer = container;
		this->removeChildren();
		if (_rootContainer) {
			this->addChild(_rootContainer);
		}
	}

	bool RetroPlugView::onKey(const fw::KeyEvent& event) {
#ifdef FW_PLATFORM_WEB
		return false;
#endif

		if (event.down && event.key == fw::VirtualKey::S) {
			auto systemIds = _project.getSystemIds();
			if (systemIds.empty()) return false;

			auto systemMemory = _project.getSystemMemory(entt::entity(systemIds[0]), MemoryType::Ram, AccessType::Read);

			HexEditorPtr hexWindow = std::make_shared<HexEditor>();
			hexWindow->setData(systemMemory.getBuffer().ref());

			getState<fw::app::WindowManager>().createWindow(hexWindow, nullptr, "");
			//_project.saveToFile("C:\\retro\\test.rplg");
		}

		/*auto systemIds = _project.getSystemIds();
		if (event.down && event.key == fw::VirtualKey::H && systemIds.size() > 0) {
			if (_rootContainer) {
				_rootContainer->remove();
			}

			_rootContainer = addChild(std::make_unique<LsdjHdPlayer>(_project, entt::entity(systemIds[0])))->asShared<LsdjHdPlayer>();
		}*/

		if (event.down && event.key == fw::VirtualKey::F7) {
			_project.addSystemAsync(SystemLoadComponent{
				.entries = {
					{ "rom", { "C:\\retro\\LSDj-v5.0.3.gb" } },
					{ "sram", { "C:\\retro\\LSDj-v5.0.3.sav" } }
				},
			}, SameBoyComponent{
				.model = GameboyModel::CgbC,
				.fastBoot = true
			});
		}

		if (event.down && event.key == fw::VirtualKey::F6) {
			//_project.deserialize(archive);

			//_project.loadFromPaths({ "C:\\retro\\LSDj-v5.0.3.sav" });

			_project.loadFromPathsAsync({ "C:\\retro\\lsdj942bitbrigade_1.gbc", "C:\\retro\\lsdj942bitbrigade_1.sav" });

			//_project.loadFromPathsAsync({ "C:\\Users\\Tom\\Downloads\\lsdj9_4_2\\lsdj9_4_2.gb" });

			//_project.loadFromFileAsync({ "C:\\retro\\LSDj-v5.0.3.rplg" });


			/*
			_project.addSystemAsync(SystemLoadComponent{
				.entries = {
					{ "rom", { "C:\\retro\\LSDj-v5.0.3.gb" } },
					{ "sram", { "C:\\retro\\LSDj-v5.0.3.sav" } }
				},
			}, SameBoyComponent{
				.model = GameboyModel::CgbC,
				.fastBoot = true
			});
			*/
			/*entt::entity e = SineGenerator::emplace(registry);

			auto slider4 = addChild<fw::SliderView>("Frequency Slider");
			slider4->setArea({ 10, 150, 300, 30 });
			slider4->setRange(20.0f, 5000.0f);
			slider4->ValueChangeEvent = [&registry, e](f32 value) {
				fw::Replicator::patchField<&SineComponent::frequency>(registry, e, value);
			};*/

			return true;
		}

		return false;
	}
}
