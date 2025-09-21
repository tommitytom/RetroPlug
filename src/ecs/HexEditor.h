#pragma once

#include "ui/View.h"
#include "foundation/DataBuffer.h"
#include "ecs/RootContainer.h"
#include "ecs/HexGrid.h"
#include "ecs/ScrollBar.h"

namespace rp {
	class HexEditor : public RootContainer {
		FwRegisterObject()
	private:
		HexGridPtr _hexGrid;
		ScrollBarPtr _scrollBar;
		fw::Uint8Buffer _pendingData;

	public:
		HexEditor();
		~HexEditor() = default;

		void setData(fw::Uint8Buffer&& data);

		void onInitialize() override;

		void onRender(fw::Canvas& canvas) override;
	};

	using HexEditorPtr = std::shared_ptr<HexEditor>;
}
