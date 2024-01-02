#pragma once 

#include "ui/View.h"

#include "DockOverlay.h"

namespace fw {
	class Dock : public View {
		RegisterObject();
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
