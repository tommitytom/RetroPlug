#pragma once

#include "ui/View.h"
#include "foundation/DataBuffer.h"
#include "ui/RootContainer.h"

namespace rp {
	struct HexScrollEvent {
		f32 position = 0.0f;
	};

	class HexGrid : public orb::View {
		FwRegisterObject()
	private:
		orb::Uint8Buffer _data;
		orb::FontFaceHandle _font;

		size_t _bytesPerRow = 16;
		size_t _rowOffset = 0;
		size_t _visibleRowCount = 0;
		size_t _totalRowCount = 0;

	public:
		HexGrid();
		~HexGrid() = default;

		f32 getViewablePercent() const {
			if (_totalRowCount == 0) return 1.0f;
			return (f32)_visibleRowCount / (f32)_totalRowCount;
		}

		void setScrollPosition(f32 position) {
			position = std::clamp(position, 0.0f, 1.0f);
			if (_totalRowCount > _visibleRowCount) {
				_rowOffset = (size_t)((_totalRowCount - _visibleRowCount) * position);
			} else {
				_rowOffset = 0;
			}
		}

		void setData(orb::Uint8Buffer&& data);

		void onInitialize() override;

		void onRender(orb::Canvas& canvas) override;

		bool onMouseScroll(const orb::MouseScrollEvent& ev) override;
	};

	using HexGridPtr = std::shared_ptr<HexGrid>;
}
