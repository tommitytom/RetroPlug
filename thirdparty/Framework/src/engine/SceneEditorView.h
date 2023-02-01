#pragma once

#include "ui/View.h"

#include "engine/EngineUtil.h"
#include "engine/RelationshipComponent.h"
#include "engine/SceneGraphUtil.h"
#include "engine/SceneView.h"
#include "engine/SpriteComponent.h"
#include "engine/TransformComponent.h"
#include "engine/SelectionUtil.h"

namespace fw {
	class SceneEditorView : public SceneView {
	private:
		entt::entity _sprite = entt::null;
		entt::entity _camera = entt::null;
		PointF _velocity;

		bool _draggingGrid = false;
		Point _dragClickPos;
		PointF _dragCameraStart;

	public:
		SceneEditorView() {
			setType<SceneEditorView>();
			setFocusPolicy(FocusPolicy::Click);
		}

		void onInitialize() override {
			SceneView::onInitialize();

			createState<SelectionSingleton>();

			entt::registry& reg = getRegistry();

			_camera = EngineUtil::createOrthographicCamera(reg);
			_sprite = EngineUtil::createSprite(reg, "textures/circle-512.png");

			entt::entity grid = WorldUtil::createEntity(reg);
			reg.emplace<GridComponent>(grid);

			SelectionUtil::setup(reg);
		}

		void onUpdate(f32 delta) override {
			TransformComponent& cameraTrans = getRegistry().get<TransformComponent>(_camera);
			cameraTrans.position += _velocity * delta;

			getRegistry().view<SelectionEvent>().each([&](const SelectionEvent& ev) {
				//ev.selected
			});

			SceneView::onUpdate(delta);
		}

		bool onMouseButton(MouseButton::Enum button, bool down, Point pos) override {
			if (button == MouseButton::Left) {
				_draggingGrid = down;

				if (down) {
					const TransformComponent& cameraTrans = getRegistry().get<TransformComponent>(_camera);
					_dragClickPos = pos;
					_dragCameraStart = cameraTrans.position;
				}
			}

			if (button == MouseButton::Right && down) {
				std::pair<entt::registry*, entt::entity> selectedEntity;
				//SelectionUtil::setSelection(getRegistry(), selectedEntity, true);

				SpriteComponent& sprite = getRegistry().get<SpriteComponent>(_sprite);

				getState<SelectionSingleton>()->selected.clear();
				getState<SelectionSingleton>()->selected.push_back(entt::meta_handle(sprite)->as_ref());
			}

			return true;
		}

		bool onMouseMove(Point pos) override {
			if (_draggingGrid) {
				TransformComponent& cameraTrans = getRegistry().get<TransformComponent>(_camera);
				cameraTrans.position = _dragCameraStart + (PointF)(_dragClickPos - pos);
			}

			return true;
		}

		void onRender(fw::Canvas& canvas) override {
			entt::registry& reg = getRegistry();

			beginSceneRender(reg, canvas);
			GridRenderSystem::update(reg, canvas);
			SpriteRenderSystem::update(reg, canvas);
			endSceneRender(canvas);
		}

		bool onKey(VirtualKey::Enum key, bool down) override {
			f32 _cameraMoveSpeed = 300;
			switch (key) {
			case VirtualKey::LeftArrow: _velocity.x = down ? -_cameraMoveSpeed : 0.0f; break;
			case VirtualKey::RightArrow: _velocity.x = down ? _cameraMoveSpeed : 0.0f; break;
			case VirtualKey::UpArrow: _velocity.y = down ? -_cameraMoveSpeed : 0.0f; break;
			case VirtualKey::DownArrow: _velocity.y = down ? _cameraMoveSpeed : 0.0f; break;
			}

			return true;
		}
	};
}
