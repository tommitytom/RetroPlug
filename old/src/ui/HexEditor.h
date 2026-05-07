#pragma once

#include "foundation/DataBuffer.h"
#include "ui/View.h"
#include "ui/HexGrid.h"
#include "ui/RootContainer.h"
#include "ui/ScrollBar.h"

namespace rp {
	class HexEditor : public RootContainer {
		FwRegisterObject()
	private:
		HexGridPtr _hexGrid;
		ScrollBarPtr _scrollBar;
		orb::Uint8Buffer _pendingData;

	public:
		HexEditor();
		~HexEditor() = default;

		void setData(orb::Uint8Buffer&& data);

		void onInitialize() override;

		void onRender(orb::Canvas& canvas) override;
	};

	using HexEditorPtr = std::shared_ptr<HexEditor>;
}
