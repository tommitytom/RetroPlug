#pragma once

#include <string>
#include "foundation/DataBuffer.h"
#include "foundation/Math.h"
//#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"

namespace fw {
	struct DataResourceDesc {
		std::string uri;
		Uint8Buffer data;
	};

	template <typename T>
	class DataResource : public Resource {
	protected:
		DataResourceDesc _desc;

	public:
		using DescT = DataResourceDesc;

		DataResource(const DataResourceDesc& desc) : Resource(entt::type_id<T>()), _desc(desc) {}
		DataResource(DataResourceDesc&& desc) : Resource(entt::type_id<T>()), _desc(std::move(desc)) {}
		~DataResource() = default;

		const DataResourceDesc& getDesc() const {
			return _desc;
		}
		
		std::string_view getUri() const {
			return _desc.uri;
		}
		
		const Uint8Buffer& getData() const {
			return _desc.data;
		}

		void setDesc(DataResourceDesc&& desc) {
			_desc = std::move(desc);
		}

		void setDesc(const DataResourceDesc& desc) {
			_desc = desc;
		}

	private:
		

		//template <typename T>
		//friend class DataResourceProvider;
	};

	template <typename T>
	class DataResourceProvider : public TypedResourceProvider<T> {
	private:
		std::shared_ptr<T> _default;
		std::vector<std::string> _extensions;

	public:
		DataResourceProvider(std::vector<std::string>&& extensions): _extensions(std::move(extensions)), _default( std::make_shared<T>(DataResourceDesc{})) {}
		~DataResourceProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override {
			if (fs::exists(uri)) {
				Uint8Buffer data;
				
				if (fw::FsUtil::readFile(uri, &data)) {
					return std::make_shared<T>(DataResourceDesc {
						.uri = std::string(uri),
						.data = std::move(data)
					});
				}
			}
			
			spdlog::error("Failed to load resource {}: The file does not exist", uri);
			
			return _default;
		}

		std::shared_ptr<Resource> create(const DataResourceDesc& desc, std::vector<std::string>& deps) override {
			return std::make_shared<T>(desc);
		}

		bool update(T& res, const DataResourceDesc& desc) override {
			res.setDesc(desc);
			return true;
		}
	};
}

#define DefineDataResourceType(T, exts) \
	class T##Resource : public fw::DataResource<T##Resource> {}; \
	class T##Provider : public fw::DataResourceProvider<T##Resource>{ public: T##Provider() : fw::DataResourceProvider<T##Resource>(exts) {} }; \
	using T##Handle = fw::TypedResourceHandle<T##Resource>; \
