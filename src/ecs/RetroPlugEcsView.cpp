#include "RetroPlugEcsView.h"

#include "foundation/Replicator.h"
#include "ui/SliderView.h"
#include "ui/TextureView.h"
#include "Components.h"
#include "SineGenerator.h"
#include "sameboy/SameBoyComponents.h"
#include "ecs/RetroPlugComponents.h"
#include "ecs/HierarchyUtil.h"
#include "ui/PanelView.h"
#include "ecs/EcsSystemView.h"
#include "ecs/LoadingView.h"
#include "ecs/TileGrid.h"
//#include "ecs/LsdjInstance.h"

namespace rp {
	RetroPlugEcsView::RetroPlugEcsView(RetroPlugProject& project) : View({ 480, 432 }), _project(project) {
		setName(fmt::format("RetroPlug v{}", RP_VERSION));
		setFocusPolicy(fw::FocusPolicy::Click);
	}

	void RetroPlugEcsView::onInitialize() {
		setScale(3.0f);
		fw::ViewLayout& layout = getLayout();
		layout.setFlexDirection(fw::FlexDirection::Row);
		layout.setFlexWrap(fw::FlexWrap::Wrap);
		//getLayout().setOverflow(fw::FlexOverflow::Visible);
	}

	bool RetroPlugEcsView::onDrop(const std::vector<std::string>& paths) {
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

	void RetroPlugEcsView::onUpdate(f32 deltaTime) {
		_project.onUpdate(deltaTime);

		if (_project.getVersion() != _version) {
			rebuildUi();
			_version = _project.getVersion();
		}
	}

	void RetroPlugEcsView::onRender(fw::Canvas& canvas) {
		canvas.fillRect(getDimensions(), fw::Color4F::red);
	}

	void RetroPlugEcsView::rebuildUi() {
		entt::registry& registry = getRegistry();

		std::string projectName = _project.getProjectName();
		if (!projectName.empty()) projectName += " | ";
		projectName += fmt::format("RetroPlug v{}", RP_VERSION);
		setName(projectName);

		bool loadScreenVisible = getChildCount() == 1 && getChild(0)->isType<LoadingView>();
		bool projectLoading = _project.getContext().loading;

		if (loadScreenVisible != projectLoading) {
			removeChildren();

			if (projectLoading) {
				addChild<LoadingView>("Loading Screen");
			}

			loadScreenVisible = projectLoading;
		}

		const size_t systemCount = _project.getRegistry().view<SystemComponent>().size();
		if (getChildCount() == 0 && systemCount > 0) {
			addChild(std::make_shared<TileGrid>(_project));
		}

		fw::DimensionF dimensions{
			160.0f,
			144.0f
		};

		dimensions *= getScale();

		getLayout().setDimensions(fw::Dimension(dimensions));
	}

	bool RetroPlugEcsView::onKey(const fw::KeyEvent& event) {
		if (event.down && event.key == fw::VirtualKey::S) {
			//_project.saveToFile("C:\\retro\\test.rplg");
		}

		if (event.down && event.key == fw::VirtualKey::F6) {
			//_project.deserialize(archive);

			//_project.loadFromPaths({ "C:\\retro\\LSDj-v5.0.3.sav" });

			_project.loadFromPathsAsync({ "C:\\retro\\lsdj942bitbrigade_1.gb", "C:\\retro\\lsdj942bitbrigade_1.sav" });

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
