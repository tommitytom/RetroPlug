#pragma once

#include "foundation/Constants.h"
#include "ecs/RootContainer.h"

namespace rp {
	class LoadingView : public RootContainer {
		FwRegisterObject()
	private:
		f32 _rotation = 0.0f;

	public:
		LoadingView() {
			getLayout().setDimensions(fw::Dimension{ 160, 144 });
		}
		~LoadingView() {}

		void onUpdate(f32 delta) override {
			_rotation = fmodf(_rotation + delta, fw::PI2);
		}

		void onRender(fw::Canvas& canvas) override {
			fw::DimensionF dimensions = getDimensionsF();
			canvas.fillRect(dimensions, fw::Color4F::black);

			//canvas.translate({ dimensions.w / 2.0f, dimensions.h / 2.0f });
			//canvas.setRotation(_rotation);
			//canvas.fillRect(dimensions / 2.0f, fw::Color4::white);

			canvas.setTextAlign(fw::TextAlignFlags::Center | fw::TextAlignFlags::Middle);
			canvas.text(getDimensionsF(), "Loading...", fw::Color4F::white);
		}
	};
}
