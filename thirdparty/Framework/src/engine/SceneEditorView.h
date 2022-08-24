#pragma once

#include "ui/View.h"

namespace fw {
	class SceneEditorView : public View {
	private:
		f32 _gridSize = 100.0f;

	public:
		SceneEditorView() {}

		void onRender(Canvas& canvas) override {
			DimensionF dimensions = (DimensionF)getDimensions();
			
			canvas.fillRect(dimensions, Color4F::darkGrey);

			f32 pxoff = 2.0f / dimensions.w;

			for (f32 x = 0; x < dimensions.w; x += _gridSize) {
				canvas.line({ x + pxoff, 0 }, { x + pxoff, dimensions.h }, Color4F::lightGrey);
			}

			for (f32 y = 0; y < dimensions.h; y += _gridSize) {
				canvas.line({ 0, y }, { dimensions.w, y }, Color4F::lightGrey);
			}
		}
	};
}