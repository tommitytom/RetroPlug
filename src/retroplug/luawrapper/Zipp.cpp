#include "Wrappers.h"

#include <sol/sol.hpp>

#include "util/zipp.h"
#include "util/DataBuffer.h"

void luawrappers::registerZipp(sol::state& s) {
	s.new_enum("ZipCompressionMethod",
		"Store", zipp::CompressionMethod::Store,
		"BZip2", zipp::CompressionMethod::BZip2,
		"Deflate", zipp::CompressionMethod::Deflate,
		"Lzma", zipp::CompressionMethod::Lzma
	);

	s.new_enum("ZipCompressionLevel",
		"Default", zipp::CompressionLevel::Default,
		"Fast", zipp::CompressionLevel::Fast,
		"Normal", zipp::CompressionLevel::Normal,
		"Best", zipp::CompressionLevel::Best
	);

	s.new_usertype<zipp::Entry>("ZipEntry",
		"name", sol::readonly(&zipp::Entry::name),
		"size", sol::readonly(&zipp::Entry::size)
	);

	s.new_usertype<zipp::WriterSettings>("ZipWriterSettings",
		"method", &zipp::WriterSettings::method,
		"level", &zipp::WriterSettings::level
	);

	s.new_usertype<zipp::Reader>("ZipReader",
		"new", sol::factories(
			[](std::string_view path) { return std::make_shared<zipp::Reader>(path); },
			[](DataBuffer<char>* buffer) { return std::make_shared<zipp::Reader>(buffer->data(), buffer->size()); }
		),
		"read", sol::overload(
			[](zipp::Reader& reader, std::string_view filePath) {
				zipp::Entry entry = reader.getEntry(filePath);
				if (entry.size > 0) {
					DataBufferPtr buffer = std::make_shared<DataBuffer<char>>(entry.size);
					if (reader.read(filePath, buffer->data(), buffer->size())) {
						return buffer;
					}
				}

				return DataBufferPtr();
			},
			[](zipp::Reader& reader, std::string_view filePath, DataBuffer<char>* target) {
				zipp::Entry entry = reader.getEntry(filePath);
				if (entry.size == target->size()) {
					return reader.read(filePath, target->data(), target->size());
				}

				return false;
			}
		),
		"entries", sol::resolve<std::vector<zipp::Entry>()>(&zipp::Reader::entries),
		"isValid", &zipp::Reader::isValid,
		"close", &zipp::Reader::close
	);

	s.new_usertype<zipp::Writer>("ZipWriter",
		"new", sol::factories(
			[]() { return std::make_shared<zipp::Writer>(); },
			[](const zipp::WriterSettings& settings) { return std::make_shared<zipp::Writer>(settings); },
			[](std::string_view path) { return std::make_shared<zipp::Writer>(path); },
			[](std::string_view path, const zipp::WriterSettings& settings) { return std::make_shared<zipp::Writer>(path, settings); }
		),
		"add", sol::overload(
			[](zipp::Writer& writer, std::string_view filePath, DataBuffer<char>* buffer) {
				if (buffer) return writer.add(filePath, buffer->data(), buffer->size());
				return false;
			},
			[](zipp::Writer& writer, std::string_view filePath, std::string_view text) {
				return writer.add(filePath, text.data(), text.size());
			}
		),
		"close", &zipp::Writer::close,
		"free", &zipp::Writer::free,
		"isValid", &zipp::Writer::isValid,
		"getBuffer", &zipp::Writer::getBuffer,
		"copyTo", [](zipp::Writer& writer, DataBuffer<char>* buffer) {
			std::string_view data = writer.getBuffer();
			buffer->resize(data.size());
			buffer->write(data.data(), data.size());
		}
	);
}
