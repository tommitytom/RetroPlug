#pragma once

#include <string>
#include <string_view>
#include "core/Forward.h"

namespace rp {
	class SystemProvider {
	public:
		virtual SystemType getType() const = 0;

		virtual SystemPtr createSystem() const = 0;

		virtual SystemProcessorPtr createProcessor() const { return nullptr; }

		virtual bool canLoadRom(std::string_view path) const { return false; }

		virtual bool canLoadSram(std::string_view path) const { return false; }

		virtual std::string getRomName(const fw::Uint8Buffer& romData) const { return ""; }
	};

	using SystemProviderPtr = std::shared_ptr<SystemProvider>;
}
