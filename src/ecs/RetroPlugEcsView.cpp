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
//#include "ecs/LsdjInstance.h"

namespace rp {
	const char* json_str =
		"{"
		"  \"systems\": ["
		"    {"
		"      \"components\": ["
		"        {"
		"          \"type\": 2738470842,"
		"          \"name\": \"rp::SystemLoadComponent\","
		"          \"data\": {"
		"            \"entries\": {"
		"              \"rom\": { \"path\": .\\LSDj-v5.0.3.gb\" },"
		"              \"sram\": { \"path\": .\\LSDj-v5.0.3.sav\" }"
		"            }"
		"          }"
		"        },"
		"        {"
		"          \"type\": 2711173061,"
		"          \"name\": \"rp::SameBoyComponent\","
		"          \"data\": { \"model\": \"CgbC\", \"fastBoot\": true }"
		"        },"
		"        {"
		"          \"type\": 1454910132,"
		"          \"name\": \"rp::LsdjComponent\","
		"          \"data\": { \"kits\": {} }"
		"        }"
		"      ]"
		"    }"
		"  ]"
		"}";

	class EcsSystemView : public fw::View {
		FwRegisterObject()
	private:
		entt::entity _entity = entt::null;
		fw::RectF _textureArea;
		fw::TextureHandle _texture;

	public:
		EcsSystemView() {
			setFocusPolicy(fw::FocusPolicy::Click);
		}

		void setEntity(entt::entity e) {
			_entity = e;
		}

		entt::entity getEntity() const {
			return _entity;
		}

		void setFrameBuffer(const fw::Image& frameBuffer) {
			if (frameBuffer.dimensions() != fw::Dimension::zero) {
				size_t dataSize = frameBuffer.getBuffer().size() * 4;
				std::vector<uint8> data(dataSize);
				memcpy(data.data(), frameBuffer.getData(), dataSize);

				if (_texture.isValid() && (fw::Dimension)_textureArea.dimensions == frameBuffer.dimensions()) {
					[[likely]]
					getResourceManager().update(_texture, fw::TextureDesc{
						.dimensions = frameBuffer.dimensions(),
						.depth = 4,
						.data = std::move(data)
					});
				} else {
					_texture = getResourceManager().create<fw::Texture>(fw::TextureDesc{
						.dimensions = frameBuffer.dimensions(),
						.depth = 4,
						.data = std::move(data)
					});

					_textureArea = { 0.0f, 0.0f, (f32)frameBuffer.dimensions().w, (f32)frameBuffer.dimensions().h };

					getLayout().setDimensions(frameBuffer.dimensions());
				}
			}
		}

		void onUpdate(f32 delta) override {

		}

		void onRender(fw::Canvas& canvas) override {
			if (_texture.isValid()) [[likely]] {
				canvas.texture(_texture, getDimensionsF(), fw::Color4F(1, 1, 1, getAlpha()));
			} else {
				canvas.fillRect(_textureArea, fw::Color4F(1, 1, 1, getAlpha()));
			}
		}
	};

	using EcsSystemViewPtr = std::shared_ptr<EcsSystemView>;

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
		std::vector<std::filesystem::path> fsPaths;
		for (const std::string& path : paths) {
			fsPaths.push_back(path);
		}

		_project.loadFromPaths(fsPaths);

		return true;
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

		entt::registry& registry = getRegistry();

		for (const fw::ViewPtr& child : getChildren()) {
			auto systemView = child->asShared<EcsSystemView>();

			const entt::entity systemEntity = systemView->getEntity();
			if (registry.valid(systemEntity) == false) {
				continue;
			}

			const VideoFrameComponent* videoFrame = registry.try_get<VideoFrameComponent>(systemEntity);
			if (!videoFrame) {
				continue;
			}

			systemView->setFrameBuffer(*videoFrame->frame);
		}
	}

	fw::ButtonType mapKeyToButton(fw::VirtualKey key) {
		switch (key) {
		case fw::VirtualKey::UpArrow: return fw::ButtonType::Up;
		case fw::VirtualKey::DownArrow: return fw::ButtonType::Down;
		case fw::VirtualKey::LeftArrow: return fw::ButtonType::Left;
		case fw::VirtualKey::RightArrow: return fw::ButtonType::Right;
		case fw::VirtualKey::D: return fw::ButtonType::A;
		case fw::VirtualKey::W: return fw::ButtonType::B;
		case fw::VirtualKey::Enter: return fw::ButtonType::Start;
		case fw::VirtualKey::LeftShift: return fw::ButtonType::Select;
		default: return fw::ButtonType::MAX;
		}
	}

	void RetroPlugEcsView::focusSystem(const fw::ViewPtr& view) {
		if (view->getChildCount()) {
			view->getChild(view->getChildCount() - 1)->focus();
		} else {
			view->focus();
		}
	}

	void RetroPlugEcsView::rebuildUi() {
		entt::registry& registry = getRegistry();
		auto view = registry.view<SameBoyComponent>();
		entt::id_type systemType = entt::type_id<SameBoyComponent>().index();

		this->removeChildren();

		size_t i = 0;
		entt::entity selectedSystemEntity = entt::null;
		size_t selectedSystemIdx = INVALID_SYSTEM_INDEX;
		std::vector<EcsSystemViewPtr> systemViews;

		for (const auto& [e, c] : view.each()) {
			auto systemView = addChild<EcsSystemView>(fmt::format("Gameboy {}", i + 1));
			systemView->setEntity(e);
			systemView->getLayout().setDimensions(fw::Dimension{ 160, 144 });
			systemViews.push_back(systemView);

			if (selectedSystemEntity == entt::null || _selectedSystemEntity == e) {
				selectedSystemEntity = e;
				selectedSystemIdx = i;
			}

			eachHook(systemType, _project.getContext().serviceHooks, [&](const SystemHookBase& hook) {
				fw::ViewPtr overlay = hook.onCreateOverlay(registry, e);
				if (overlay) {
					systemView->addChild(overlay);
				}
			});

			subscribe<fw::KeyEvent>(systemView, std::function<bool(const fw::KeyEvent&)>([this, e](const fw::KeyEvent& ev) -> bool {
				if (ev.key == fw::VirtualKey::R) {
					_project.getEventNode().trySend("Audio"_hs, ResetSystemEntityEvent{
						.entity = e
					});
					return true;
				}

				fw::ButtonType button = mapKeyToButton(ev.key);
				if (button == fw::ButtonType::MAX) {
					return false;
				}

				_project.getEventNode().trySend("Audio"_hs, ButtonEvent{
					.entity = e,
					.button = (int)button,
					.down = ev.down
				});

				return true;
			}));

			i++;
		}

		_selectedSystemEntity = selectedSystemEntity;
		_selectedSystemIdx = selectedSystemIdx;

		updateFocus();

		fw::DimensionF dimensions{
			160.0f * (f32)std::max((int32)view.size(), 1),
			144.0f
		};

		dimensions *= getScale();

		getLayout().setDimensions(fw::Dimension(dimensions));
	}

	void RetroPlugEcsView::updateFocus() {
		for (size_t i = 0; i < getChildCount(); i++) {
			auto systemView = getChildAs<EcsSystemView>(i);
			fw::PanelViewPtr panel = systemView->findChild<fw::PanelView>();

			if (systemView->getEntity() == _selectedSystemEntity) {
				size_t nextIndex = (i + 1) % getChildCount();
				_selectedSystemEntity = getChildAs<EcsSystemView>(nextIndex)->getEntity();

				if (panel) {
					panel->remove();
				}

				if (systemView->getChildCount()) {
					systemView->getChild(systemView->getChildCount() - 1)->focus();
				} else {
					systemView->focus();
				}
			} else if (!panel) {
				fw::PanelViewPtr panel = systemView->addChild<fw::PanelView>("Overlay");
				panel->fitToParent();
				panel->setColor(fw::Color4F(0, 0, 0, 0.5f));
			}
		}
	}

	bool RetroPlugEcsView::onKey(const fw::KeyEvent& event) {
		if (event.down && event.key == fw::VirtualKey::Tab) {
			if (getChildCount()) {
				_selectedSystemIdx = (_selectedSystemIdx + 1) % getChildCount();
				_selectedSystemEntity =  getChildAs<EcsSystemView>(_selectedSystemIdx)->getEntity();
				updateFocus();
			}
		}

		if (event.down && event.key == fw::VirtualKey::F5) {
			fw::Uint8Buffer archive((uint8*)json_str, strlen(json_str), false);
			//_project.deserialize(archive);

			_project.loadFromFile("C:\\retro\\LSDj-v5.0.3.rplg");
			/*
			entt::entity entity = _project.addSystem(SystemLoadComponent{
				.entries = {
					{ "rom", { ".\\LSDj-v5.0.3.gb" } },
					{ "sram", { ".\\LSDj-v5.0.3.sav" } }
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
