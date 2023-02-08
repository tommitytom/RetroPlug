#include "SystemFactory.h"

#include "core/System.h"
#include "core/SystemProvider.h"
#include "core/SystemServiceProvider.h"
#include "core/SystemService.h"

namespace rp {
	SystemPtr SystemFactory::createSystem(SystemId systemId, SystemType systemType) const {
		SystemProviderPtr found;

		for (const SystemProviderPtr& provider : _providers) {
			if (provider->getType() == systemType) {
				found = provider;
				break;
			}
		}

		assert(found);

		SystemPtr system = found->createSystem();
		system->setId(systemId);

		return system;
	}

	SystemServicePtr SystemFactory::createSystemService(SystemServiceType systemServiceType) const {
		SystemServiceProviderPtr found;

		for (const SystemServiceProviderPtr& provider : _services) {
			if (provider->getType() == systemServiceType) {
				found = provider;
				break;
			}
		}

		assert(found);

		SystemServicePtr service = found->onCreateService();
		return service;
	}

	SystemOverlayPtr SystemFactory::createSystemServiceUi(SystemServiceType systemServiceType) const {
		SystemServiceProviderPtr found;

		for (const SystemServiceProviderPtr& provider : _services) {
			if (provider->getType() == systemServiceType) {
				found = provider;
				break;
			}
		}

		assert(found);

		SystemOverlayPtr serviceView = found->onCreateUi();
		return serviceView;
	}

	std::vector<SystemType> SystemFactory::getRomLoaders(std::string_view path) const {
		std::vector<SystemType> ret;

		for (const SystemProviderPtr& provider : _providers) {
			if (provider->canLoadRom(path)) {
				ret.push_back(provider->getType());
			}
		}

		return ret;
	}

	std::vector<SystemType> SystemFactory::getSramLoaders(std::string_view path) const {
		std::vector<SystemType> ret;

		for (const SystemProviderPtr& provider : _providers) {
			if (provider->canLoadSram(path)) {
				ret.push_back(provider->getType());
			}
		}

		return ret;
	}

	std::vector<SystemServiceType> SystemFactory::getRelevantServiceTypes(const LoadConfig& loadConfig) const {
		std::vector<SystemServiceType> ret;

		for (const SystemServiceProviderPtr& provider : _services) {
			if (provider->match(loadConfig)) {
				ret.push_back(provider->getType());
			}
		}

		return ret;
	}
}
