#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>

#include <texture-atlas.h>
#include <texture-font.h>

#include "foundation/Types.h"

namespace fw {
	class Font {
	private:
		struct SizedFont {
			f32 size;
			ftgl::texture_atlas_t* atlas = nullptr;
			ftgl::texture_font_t* font = nullptr;
		};

		std::filesystem::path _path;
		std::string _name;
		std::vector<SizedFont> _sizes;

	public:
		Font(const std::string& name, const std::filesystem::path& path): _path(path), _name(name) {}
		Font(Font&& other) noexcept {
			_path = std::move(other._path);
			_name = std::move(other._name);
			_sizes = std::move(other._sizes);
		}
		~Font() {
			for (SizedFont& f : _sizes) {
				ftgl::texture_font_delete(f.font);
			}
		}

		void addLayer() {

		}
	};

	using TextureAtlasHandle = size_t;
	using TextureHandle = size_t;
	using FontHandle = size_t;
	const size_t INVALID_HANDLE = 0;

	class Graphics {
	private:
		std::vector<Font> _fonts;
		std::vector<ftgl::texture_atlas_t*> _atlases;

	public:
		Graphics() = default;
		~Graphics() {
			_fonts.clear();

			for (ftgl::texture_atlas_t* atlas : _atlases) {
				ftgl::texture_atlas_delete(atlas);
			}
		}

		FontHandle loadFontFromFile(const std::string& name, const std::filesystem::path& path) {
			return FontHandle{};
		}

		FontHandle loadFontFromMemory(const std::string& name, const char* data, size_t size) {
			return FontHandle{};
		}
	};
}
