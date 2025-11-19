#pragma once

#include "ui/View.h"

namespace orb {
	template <typename T>
	class ContextProvider : public View {
		FwRegisterObject()
	public:
		ContextProvider() = default;
		virtual ~ContextProvider() = default;

		void onInitialize() override final {
			// Check to see if a parent already provides this context
			
			// Add context

		}
	};

	template <typename T>
	class ContextAccessor {

	};
}