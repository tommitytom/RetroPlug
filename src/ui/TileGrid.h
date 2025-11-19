#pragma once

#include "ui/View.h"
#include "core/RetroPlugProject.h"
#include "ui/SystemView.h"
#include "ui/RootContainer.h"

namespace rp {
	constexpr size_t INVALID_TILE_INDEX = -1;

	class TileGrid : public RootContainer {
		FwRegisterObject()
	private:
		RetroPlugProject& _project;
		entt::entity _selectedTileEntity = entt::null;
		entt::entity _requestedTileEntity = entt::null;
		uint32 _version = -1;

	public:
		TileGrid(RetroPlugProject& project): _project(project) {}
		~TileGrid() {}

		void requestSelection(entt::entity entity) {
			_requestedTileEntity = entity;
		}

		void onInitialize() override {
			getLayout().setFlexDirection(fw::FlexDirection::Row);
		}

		bool onKey(const fw::KeyEvent& event) override {
			if (event.down && event.key == fw::VirtualKey::Tab) {
				for (size_t i = 0; i < getChildCount(); ++i) {
					if (getChildAs<TileView>(i)->getEntity() == _selectedTileEntity) {
						const size_t selectedIdx = (i + 1) % getChildCount();
						_selectedTileEntity = getChildAs<TileView>(selectedIdx)->getEntity();
						break;
					}
				}

				return true;
			}

			return false;
		}

		void onUpdate(f32 delta) override {
			if (_project.getVersion() != _version) {
				rebuildUi();
			}

			if (_requestedTileEntity != entt::null) {
				entt::registry& registry = _project.getRegistry();
				if (registry.valid(_requestedTileEntity)) {
					bool found = false;
					for (size_t i = 0; i < getChildCount(); ++i) {
						if (getChild(i)->asRaw<TileView>()->getEntity() == _requestedTileEntity) {
							_selectedTileEntity = _requestedTileEntity;
							found = true;
							break;
						}
					}

					if (!found) {
						spdlog::error("Tried to select a tile for an invalid entity {}", (size_t)_requestedTileEntity);
					}
				} else {
					spdlog::error("Tried to select a tile for an invalid entity {}", (size_t)_requestedTileEntity);
				}

				_requestedTileEntity = entt::null;
			}

			for (size_t i = 0; i < getChildCount(); ++i) {
				TileView* tile = getChild(i)->asRaw<TileView>();
				if (tile->getEntity() == _selectedTileEntity) {
					tile->focus();
					tile->setAlpha(1.0f);
				} else {
					tile->setAlpha(0.5f);
				}
			}

			fw::DimensionF dimensions{
				160.0f * (f32)std::max((int32)getChildCount(), 1),
				144.0f
			};

			getLayout().setDimensions(fw::Dimension(dimensions));
		}

		void onRender(fw::Canvas& canvas) override {
			canvas.fillRect(getDimensions(), fw::Color4F::black);
		}

		void rebuildUi() {
			this->removeChildren();

			entt::registry& registry = _project.getRegistry();
			entt::entity selectedTileEntity = entt::null;

			for (const auto& [e, system] : registry.view<SystemComponent>().each()) {
				auto systemView = addChild(std::make_shared<SystemView>(_project, e));
				systemView->getLayout().setDimensions(fw::Dimension{ 160, 144 });

				if (selectedTileEntity == entt::null || _selectedTileEntity == e) {
					selectedTileEntity = e;
				}

				eachHook(system.systemType, _project.getHooksContext().serviceHooks, [&](const SystemHookBase& hook) {
					fw::ViewPtr overlay = hook.onCreateOverlay(registry, e);
					if (overlay) {
						systemView->addChild(overlay);
					}
				});

				// TODO: Spawn child tiles
			}

			_selectedTileEntity = selectedTileEntity;
			_version = _project.getVersion();
		}
	};
}
