#include "Font.h"

#include <spdlog/spdlog.h>

#include "foundation/FsUtil.h"
#include "fonts/Karla-Regular.h"

namespace fw {
	FontProvider::FontProvider() {
		std::vector<std::byte> data;
		data.resize(Karla_Regular_len);
		memcpy(data.data(), Karla_Regular, Karla_Regular_len);

		_default = std::make_shared<Font>(std::move(data));
	}

	std::shared_ptr<Resource> FontProvider::load(std::string_view uri) {
		if (fs::exists(uri)) {
			uintmax_t fileSize = fs::file_size(uri);
			std::vector<std::byte> fileData = fw::FsUtil::readFile(uri);

			if (fileData.size() > 0) {
				return std::make_shared<Font>(std::move(fileData));
			} else {
				spdlog::error("Failed to load font at {}: The file contains no data", uri);
			}
		} else {
			spdlog::error("Failed to load font at {}: The file does not exist", uri);
		}

		return _default;
	}

	std::shared_ptr<Resource> FontProvider::create(const FontDesc& desc, std::vector<std::string>& deps) {
		return std::make_shared<Font>(desc.data);
	}

	bool FontProvider::update(Font& fontFace, const FontDesc& desc) {
		return false;
	}
}
