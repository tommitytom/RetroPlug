#pragma once

#include "ui/View.h"

namespace fw {
	class SplitterView : public View {
		RegisterObject()
	private:
		ViewPtr _left;
		ViewPtr _right;
		f32 _splitPos = 0.5f;

	public:
		SplitterView() {
			getLayout().setLayoutDirection(LayoutDirection::LTR);
			getLayout().setFlexDirection(FlexDirection::Row);
			getLayout().setJustifyContent(FlexJustify::FlexStart);
			getLayout().setFlexAlignItems(FlexAlign::FlexStart);
			getLayout().setFlexAlignContent(FlexAlign::Stretch);
		}

		void setSplitPercentage(f32 perc) {
			_splitPos = perc;

			if (_left) {
				_left->getLayout().setDimensions(FlexDimensionValue{
					FlexValue(FlexUnit::Percent, _splitPos * 100.0f),
					FlexValue(FlexUnit::Percent, 100.0f)
				});
			}

			if (_right) {
				_right->getLayout().setDimensions(FlexDimensionValue{
					FlexValue(FlexUnit::Percent, (1.0f - _splitPos) * 100.0f),
					FlexValue(FlexUnit::Percent, 100.0f)
				});
			}
		}

		void setLeft(ViewPtr view) {
			addChild(view);
			view->pushToBack();
			_left = view;
			setSplitPercentage(_splitPos);
		}

		void setRight(ViewPtr view) {
			addChild(view);
			view->bringToFront();
			_right = view;
			setSplitPercentage(_splitPos);
		}
	};

	using SplitterViewPtr = std::shared_ptr<SplitterView>;
}

#include <refl.hpp>

REFL_AUTO(
	type(fw::SplitterView, bases<fw::View>)
)
