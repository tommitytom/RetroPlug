#pragma once

#include "core/SystemServiceProvider.h"
#include "lsdj/LsdjSettings.h"

namespace rp {
	class ArduinoboyServiceProvider : public SystemServiceProvider {
	public:
		bool match(const LoadConfig& loadConfig) override;

		SystemServiceType getType() override { return ARDUINOBOY_SERVICE_TYPE; }

		SystemOverlayPtr onCreateUi() override;

		SystemServicePtr onCreateService() const override;
	};
}
