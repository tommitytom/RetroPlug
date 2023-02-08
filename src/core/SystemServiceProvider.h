#pragma once

#include <string>
#include <string_view>
#include "core/Forward.h"
#include "ui/View.h"

namespace rp {
	class SystemServiceProvider {
	public:
		virtual bool match(const LoadConfig& loadConfig) = 0;

		virtual SystemServiceType getType() = 0;

		virtual SystemOverlayPtr onCreateUi() { return nullptr; }

		virtual SystemServicePtr onCreateService() const { return nullptr; }
	};
}