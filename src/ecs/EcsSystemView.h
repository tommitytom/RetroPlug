#pragma once

#include "ui/View.h"
#include "foundation/Image.h"
#include "ecs/TileView.h"

namespace rp {
	class EcsSystemView : public TileView {
		FwRegisterObject()
	private:
		RetroPlugProject& _project;
		fw::RectF _textureArea;
		fw::TextureHandle _texture;

	public:
		EcsSystemView(RetroPlugProject& project, entt::entity entity): TileView(entity), _project(project) {
			setFocusPolicy(fw::FocusPolicy::Click);
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

		bool onKey(const fw::KeyEvent& event) override {
			if (event.key == fw::VirtualKey::R) {
				_project.getEventNode().trySend("Audio"_hs, ResetSystemEntityEvent{
					.entity = getEntity()
				});
				return true;
			}

			const InputConfig& inputConfig = _project.getInputConfig();
			auto found = inputConfig.keyboard.find(event.key);
			if (found != inputConfig.keyboard.end()) {
				_project.getEventNode().trySend("Audio"_hs, PadButtonEvent{
					.entity = getEntity(),
					.button = found->second,
					.down = event.down
				});

				return true;
			}

			return false;
		}

		void onRender(fw::Canvas& canvas) override {
			if (_texture.isValid()) [[likely]] {
				canvas.texture(_texture, getDimensionsF(), fw::Color4F(1, 1, 1, getAlpha()));
			} else {
				canvas.fillRect(_textureArea, fw::Color4F(0, 0, 0, getAlpha()));
			}
		}

	private:
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
	};

	using EcsSystemViewPtr = std::shared_ptr<EcsSystemView>;
}
