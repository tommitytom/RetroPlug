#include "ResourceManager.h"

#include <spdlog/spdlog.h>

#include "util/fs.h"

using namespace rp;

ResourcePtr ResourceManager::loadResource(const std::string& path) {
	ResourcePtr found = getResource(path);
	if (found) {
		return found;
	}

	ResourcePtr resource = std::make_shared<Resource>(Resource{
		.data = std::make_shared<Uint8Buffer>()
	});

	if (fsutil::readFile(path, resource->data.get()) == 0) {
		spdlog::error("Failed to open {}", path);
		return nullptr;
	}

	resource->hash = resource->data->hash(0);
	_resources[path] = resource;

	return resource;
}

ResourcePtr ResourceManager::getResource(const std::string& path) {
	auto found = _resources.find(path);
	if (found != _resources.end()) {
		return found->second;
	}

	return nullptr;
}

void ResourceManager::update() {

}
