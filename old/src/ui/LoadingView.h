#pragma once

#include "foundation/Constants.h"
#include "ui/RootContainer.h"

namespace rp {
	class LoadingView : public RootContainer {
		FwRegisterObject()
	private:
		f32 _rotation = 0.0f;

	public:
		LoadingView() {
			getLayout().setDimensions(orb::Dimension{ 160, 144 });
		}
		~LoadingView() {}

		void onUpdate(f32 delta) override {
			_rotation = fmodf(_rotation + delta, orb::PI2);
		}

		void onRender(orb::Canvas& canvas) override {
			orb::DimensionF dimensions = getDimensionsF();
			canvas.fillRect(dimensions, orb::Color4F::black);

			//canvas.translate({ dimensions.w / 2.0f, dimensions.h / 2.0f });
			//canvas.setRotation(_rotation);
			//canvas.fillRect(dimensions / 2.0f, orb::Color4::white);

			canvas.setTextAlign(orb::TextAlignFlags::Center | orb::TextAlignFlags::Middle);
			canvas.text(getDimensionsF(), "Loading...", orb::Color4F::white);
		}
	};
}
