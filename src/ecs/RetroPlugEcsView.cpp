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
		"              \"rom\": { \"path\": \"C:\\\\retro\\\\LSDj-v5.0.3.gb\" },"
		"              \"sram\": { \"path\": \"C:\\\\retro\\\\LSDj-v5.0.3.sav\" }"
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
		_project.addSystem(paths);
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

	void focusSystem(const fw::ViewPtr& view) {
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

		using EcsSystemViewPtr = std::shared_ptr<EcsSystemView>;

		uint32 i = 1;
		entt::entity selectedSystem = entt::null;
		std::vector<EcsSystemViewPtr> systemViews;
		for (const auto& [e, c] : view.each()) {
			auto systemView = addChild<EcsSystemView>(fmt::format("Gameboy {}", i++));
			systemView->setEntity(e);
			systemView->getLayout().setDimensions(fw::Dimension{ 160, 144 });
			systemViews.push_back(systemView);

			if (selectedSystem == entt::null || _selectedSystem == e) {
				selectedSystem = e;
			}

			eachHook(systemType, _project.getContext().serviceHooks, [&](const SystemHookBase& hook) {
				fw::ViewPtr overlay = hook.onCreateOverlay(registry, e);
				if (overlay) {
					systemView->addChild(overlay);
				}
			});

			subscribe<fw::KeyEvent>(systemView, [this, e](const fw::KeyEvent& ev) {
				if (ev.down && ev.key == fw::VirtualKey::F5) {
					fw::Uint8Buffer archive((uint8*)json_str, strlen(json_str), false);
					//_project.deserialize(archive);

					entt::entity entity = _project.addSystem(SystemLoadComponent{
						.entries = {
							{ "rom", { "C:\\retro\\LSDj-v5.0.3.gb" } },
							{ "sram", { "C:\\retro\\LSDj-v5.0.3.sav" } }
						},
					}, SameBoyComponent{
						.model = GameboyModel::CgbC,
						.fastBoot = true
					});
					return;
				}

				fw::ButtonType button = mapKeyToButton(ev.key);
				if (button == fw::ButtonType::MAX) {
					return;
				}

				_project.getEventNode().trySend("Audio"_hs, ButtonEvent{
					.entity = e,
					.button = (int)button,
					.down = ev.down
				});
			});
		}

		for (const EcsSystemViewPtr& systemView : systemViews) {
			if (systemView->getEntity() == selectedSystem) {
				focusSystem(systemView);
			} else {
				fw::PanelViewPtr panel = systemView->addChild<fw::PanelView>("Overlay");
				panel->fitToParent();
				panel->setColor(fw::Color4F(0, 0, 0, 0.5f));
			}
		}

		_selectedSystem = selectedSystem;

		fw::DimensionF dimensions{
			160.0f * (f32)std::max((int32)view.size(), 1),
			144.0f
		};

		dimensions *= getScale();

		getLayout().setDimensions(fw::Dimension(dimensions));
	}

	bool RetroPlugEcsView::onKey(const fw::KeyEvent& event) {
		if (event.down && event.key == fw::VirtualKey::F5) {
			fw::Uint8Buffer archive((uint8*)json_str, strlen(json_str), false);
			//_project.deserialize(archive);

			entt::entity entity = _project.addSystem(SystemLoadComponent{
				.entries = {
					{ "rom", { "C:\\retro\\LSDj-v5.0.3.gb" } },
					{ "sram", { "C:\\retro\\LSDj-v5.0.3.sav" } }
				},
			}, SameBoyComponent{
				.model = GameboyModel::CgbC,
				.fastBoot = true
			});

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
