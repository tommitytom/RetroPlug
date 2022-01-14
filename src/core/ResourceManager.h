#pragma once

#include <memory>
#include <unordered_map>

#include "platform/Types.h"
#include "util/DataBuffer.h"

namespace rp {
	using ResourceHash = uint64;

	struct Resource {
		ResourceHash hash = 0;
		Uint8BufferPtr data;
	};
	
	using ResourcePtr = std::shared_ptr<Resource>;

	class ResourceManager {
	private:
		std::unordered_map<std::string, ResourcePtr> _resources;
		// TODO: File watcher

	public:
		ResourcePtr loadResource(const std::string& path);

		ResourcePtr getResource(const std::string& path);

		std::unordered_map<std::string, ResourcePtr>& getResources() {
			return _resources;
		}

		const std::unordered_map<std::string, ResourcePtr>& getResources() const {
			return _resources;
		}

		void update();
	};
}