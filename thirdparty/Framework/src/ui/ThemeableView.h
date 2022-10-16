#pragma once 

#include "ui/View.h"

namespace fw {
	struct ButtonTheme {
		Color4F background;
	};

	template <typename ThemeT>
	class ThemeableView : public View {
	public:
		/*const ThemeT& getTheme() const {
			getShared<ThemeManager>()->getTheme<ThemeT>();
		}*/
	};
}
