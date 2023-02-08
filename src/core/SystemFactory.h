#pragma once

#include <string_view>
#include <vector>
#include "core/Forward.h"
#include "ui/View.h"

namespace rp {
	class SystemFactory {
	private:
		std::vector<SystemProviderPtr> _providers;
		std::vector<SystemServiceProviderPtr> _services;

	public:
		template <typename T>
		void addSystemProvider() {
			_providers.push_back(std::make_shared<T>());
		}

		void addSystemProvider(SystemProviderPtr provider) {
			_providers.push_back(provider);
		}

		template <typename T>
		void addSystemServiceProvider() {
			_services.push_back(std::make_shared<T>());
		}

		void addSystemServiceProvider(SystemServiceProviderPtr provider) {
			_services.push_back(provider);
		}

		SystemPtr createSystem(SystemId systemId, SystemType systemType) const;

		SystemServicePtr createSystemService(SystemServiceType systemServiceType) const;

		SystemOverlayPtr createSystemServiceUi(SystemServiceType systemServiceType) const;

		std::vector<SystemType> getRomLoaders(std::string_view path) const;

		std::vector<SystemType> getSramLoaders(std::string_view path) const;

		std::vector<SystemServiceType> getRelevantServiceTypes(const LoadConfig& loadConfig) const;

		const std::vector<SystemProviderPtr>& getSystemProviders() const {
			return _providers;
		}

		const std::vector<SystemServiceProviderPtr>& getSystemServiceProviders() const {
			return _services;
		}
	};
}
