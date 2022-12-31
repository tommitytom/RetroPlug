#pragma once

#include "foundation/Math.h"
#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"

namespace fw::engine {
	struct FontDesc {
		std::vector<std::byte> data;
	};

	class Font : public Resource {
	private:
		std::vector<std::byte> _data;

	public:
		using DescT = FontDesc;

		Font() : Resource(entt::type_id<Font>()) {}
		Font(std::vector<std::byte>&& data) noexcept : Resource(entt::type_id<Font>()), _data(std::move(data)) {}
		Font(const std::vector<std::byte>& data) : Resource(entt::type_id<Font>()), _data(data) {}
		~Font() = default;

		const std::vector<std::byte>& getData() const {
			return _data;
		}
	};

	using FontPtr = std::shared_ptr<Font>;
	using FontHandle = TypedResourceHandle<Font>;

	class FontProvider : public TypedResourceProvider<Font> {
	private:
		std::shared_ptr<Font> _default;

	public:
		FontProvider();
		~FontProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const FontDesc& desc, std::vector<std::string>& deps) override;

		bool update(Font& fontFace, const FontDesc& desc) override;
	};


	struct FontFaceDesc {
		std::string font;
		f32 size;
		std::string codePoints;
	};

	class FontFace : public Resource {
	public:
		using DescT = FontFaceDesc;

		FontFace() : Resource(entt::type_id<FontFace>()) {}
		~FontFace() = default;
	};

	using FontFacePtr = std::shared_ptr<FontFace>;
	using FontFaceHandle = TypedResourceHandle<FontFace>;
}
