#pragma once 

#include "ui/View.h"

#include "DockOverlay.h"

namespace orb {
	class Dock : public View {
		FwRegisterObject();
	private:
		ViewPtr _dockedRoot;
		ViewPtr _floatingWinows;
		DockOverlayPtr _overlay;

	public:
		Dock() {}
		~Dock() = default;

		void onInitialize() override;

		void setRoot(ViewPtr root);
	};

	using DockPtr = std::shared_ptr<Dock>;
}
