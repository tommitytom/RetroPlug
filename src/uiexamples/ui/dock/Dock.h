#pragma once 

#include "ui/View.h"

#include "DockOverlay.h"

namespace rp {
	class Dock : public View {
	private:
		ViewPtr _dockedRoot;
		ViewPtr _floatingWinows;
		DockOverlayPtr _overlay;

	public:
		Dock() { setType<Dock>(); }
		~Dock() = default;

		void onInitialized() override;

		void setRoot(ViewPtr root);
	};

	using DockPtr = std::shared_ptr<Dock>;
}
