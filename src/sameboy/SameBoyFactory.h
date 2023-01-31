#pragma once

#include "core/System.h"
#include "core/SystemProcessor.h"
#include "core/SystemProvider.h"
#include "sameboy/Constants.h"

namespace rp {
	class SameBoyProvider final : public SystemProvider {
	public:
		SystemType getType() const override { return SAMEBOY_GUID; }

		SystemPtr createSystem() const override;

		SystemProcessorPtr createProcessor() const override;

		bool canLoadRom(std::string_view path) const override;

		bool canLoadSram(std::string_view path) const override;

		std::string getRomName(const fw::Uint8Buffer& romData) const override;
	};
}
