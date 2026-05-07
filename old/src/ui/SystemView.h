#pragma once

#include "ui/View.h"
#include "foundation/Image.h"
#include "ui/TileView.h"

namespace rp {
	class SystemView : public TileView {
		FwRegisterObject()
	private:
		RetroPlugProject& _project;
		orb::RectF _textureArea;
		orb::TextureHandle _texture;

	public:
		SystemView(RetroPlugProject& project, entt::entity entity): TileView(entity), _project(project) {
			setFocusPolicy(orb::FocusPolicy::Click);
		}

		void onUpdate(f32 delta) override {
			if (_project.getRegistry().valid(getEntity()) == false) {
				return;
			}

			const VideoFrameComponent* videoFrame = _project.getRegistry().try_get<VideoFrameComponent>(getEntity());
			if (videoFrame) {
				setFrameBuffer(*videoFrame->frame);
			}
		}

		bool onButton(const orb::ButtonEvent& event) override {
			orb::PadButtonType button = event.button;

			if (button == orb::PadButtonType::LeftStickDown) button = orb::PadButtonType::Down;
			if (button == orb::PadButtonType::LeftStickUp) button = orb::PadButtonType::Up;
			if (button == orb::PadButtonType::LeftStickLeft) button = orb::PadButtonType::Left;
			if (button == orb::PadButtonType::LeftStickRight) button = orb::PadButtonType::Right;

			_project.getEventNode().trySend("Audio"_hs, PadButtonEvent{
					.entity = getEntity(),
					.button = button,
					.down = event.down
				});

			return true;
		}

		bool onKey(const orb::KeyEvent& event) override {
			/*if (event.key == orb::VirtualKey::R) {
				_project.getEventNode().trySend("Audio"_hs, ResetSystemEntityEvent{
					.entity = getEntity()
				});

				return true;
			}*/

			const InputConfig& inputConfig = _project.getInputConfig();
			auto found = inputConfig.keyboard.find(event.key);
			if (found != inputConfig.keyboard.end()) {
				// If there is no SRAM, we should probably mark the state as dirty
				if (!_project.hasSystemMemory(getEntity(), MemoryType::Sram)) {
					_project.getContext().dirty = true;
				}

				_project.getEventNode().trySend("Audio"_hs, PadButtonEvent{
					.entity = getEntity(),
					.button = found->second,
					.down = event.down
				});

				return true;
			}

			return false;
		}

		void onRender(orb::Canvas& canvas) override {
			if (_texture.isValid()) [[likely]] {
				canvas.texture(_texture, getDimensionsF(), orb::Color4F(1, 1, 1, getAlpha()));
			} else {
				canvas.fillRect(_textureArea, orb::Color4F(0, 0, 0, getAlpha()));
			}
		}

	private:
		void setFrameBuffer(const orb::Image& frameBuffer) {
			if (frameBuffer.dimensions() != orb::Dimension::zero) {
				size_t dataSize = frameBuffer.getBuffer().size() * 4;
				std::vector<uint8> data(dataSize);
				memcpy(data.data(), frameBuffer.getData(), dataSize);

				if (_texture.isValid() && (orb::Dimension)_textureArea.dimensions == frameBuffer.dimensions()) {
					[[likely]]
					getResourceManager().update(_texture, orb::TextureDesc{
						.dimensions = frameBuffer.dimensions(),
						.depth = 4,
						.data = std::move(data)
					});
				} else {
					_texture = getResourceManager().create<orb::Texture>(orb::TextureDesc{
						.dimensions = frameBuffer.dimensions(),
						.depth = 4,
						.data = std::move(data)
					});

					_textureArea = { 0.0f, 0.0f, (f32)frameBuffer.dimensions().w, (f32)frameBuffer.dimensions().h };

					getLayout().setDimensions(frameBuffer.dimensions());
				}
			}
		}
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
