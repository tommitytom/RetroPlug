#include "mz.h"
#include "mz_os.h"
#include "mz_strm.h"
#include "mz_strm_buf.h"
#include "mz_strm_os.h"
#include "mz_strm_mem.h"
#include "mz_strm_split.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include <stdio.h> 
#include <assert.h>
#include <string>
#include <string_view>
#include <vector>

namespace zipp {
	enum class CompressionMethod : uint16_t {
		Store = 0,
		Deflate = 8,
		BZip2 = 12,
		Lzma = 14,
		Aes = 99
	};

	enum class CompressionLevel : int16_t {
		Default = -1,
		Fast = 2,
		Normal = 6,
		Best = 9
	};

	struct Entry {
		std::string name;
		size_t size = 0;
	};

	struct WriterSettings {
		CompressionMethod method = CompressionMethod::Lzma;
		CompressionLevel level = CompressionLevel::Best;
	};

	class Writer {
	private:
		void* _handle = nullptr;
		void* _memStream = nullptr;
		bool _valid = false;
		WriterSettings _settings;

	public:
		Writer(std::string_view path, const WriterSettings& settings = WriterSettings()) {
			mz_zip_writer_create(&_handle);
			int32_t err = mz_zip_writer_open_file(_handle, path.data(), 0, 0);
			_valid = err == MZ_OK;

			if (_valid) {
				//setup(settings);
			} else {
				mz_zip_writer_delete(&_handle);
				_handle = nullptr;
			}
		}

		Writer(const WriterSettings& settings = WriterSettings()) : _valid(false) {
			mz_zip_writer_create(&_handle);

			mz_stream_mem_create(&_memStream);
			mz_stream_mem_set_grow_size(_memStream, 1024);
			int32_t err = mz_stream_open(_memStream, NULL, MZ_OPEN_MODE_CREATE);
			_valid = err == MZ_OK;

			if (_valid) {
				err = mz_zip_open(&_handle, _memStream, MZ_OPEN_MODE_WRITE);
				_valid = err == MZ_OK;
			}
		}

		~Writer() {
			close();
		}

		std::string_view getBuffer() const {
			if (_memStream) {
				void* buffer;
				mz_stream_mem_get_buffer(_memStream, (const void**)&buffer);
				mz_stream_mem_seek(_memStream, 0, MZ_SEEK_END);
				size_t bufferSize = (size_t)mz_stream_mem_tell(_memStream);
				return std::string_view((const char*)buffer, bufferSize);
			}

			assert(_memStream);
			return std::string_view();
		}

		bool isValid() const {
			return _valid;
		}

		void close() {
			if (_handle) {
				mz_zip_writer_close(_handle);

				if (_memStream) {
					mz_stream_mem_delete(&_memStream);
					_memStream = nullptr;
				}
				
				mz_zip_writer_delete(&_handle);
				_handle = nullptr;
				_valid = false;
			}
		}

		bool add(std::string_view fileName, const std::vector<std::byte>& data) {
			return add(fileName, (const char*)data.data(), data.size());
		}

		bool add(std::string_view fileName, const char* data, size_t size) {
			mz_zip_file entry = {};
			entry.filename = fileName.data();
			entry.filename_size = fileName.size();
			entry.compression_method = (uint16_t)_settings.method;
			int32_t err = mz_zip_writer_add_buffer(_handle, (void*)data, size, &entry);
			return err == MZ_OK;
		}

	private:
		void setup(const WriterSettings& settings) {
			mz_zip_writer_set_compress_method(_handle, (uint16_t)settings.method);
			mz_zip_writer_set_compress_level(_handle, (int16_t)settings.level);
			_settings = settings;
		}
	};

	class Reader {
	private:
		void* _handle = nullptr;
		bool _valid = false;

	public:
		Reader() {}
		Reader(std::string_view path) {
			mz_zip_reader_create(&_handle);
			int32_t err = mz_zip_reader_open_file(_handle, path.data());
			_valid = err == MZ_OK;

			if (_valid) {
				//setup(settings);
			} else {
				mz_zip_reader_delete(&_handle);
				_handle = nullptr;
			}
		}

		Reader(const char* data, size_t size) {
			mz_zip_reader_create(&_handle);
			int32_t err = mz_zip_reader_open_buffer(_handle, (uint8_t*)data, (int32_t)size, 0);
			_valid = err == MZ_OK;

			if (_valid) {
				//setup(settings);
			} else {
				mz_zip_reader_delete(&_handle);
				_handle = nullptr;
			}
		}

		Reader(const std::vector<std::byte>& data) : Reader((const char*)data.data(), data.size()) {}

		~Reader() {
			close();
		}

		std::vector<Entry> entries() {
			std::vector<Entry> items;
			entries(items);
			return items;
		}

		bool entries(std::vector<Entry>& target) {
			mz_zip_file* entryInfo = nullptr;
			int32_t err;

			err = mz_zip_reader_goto_first_entry(_handle);
			if (err != MZ_OK) return false;

			while (err == MZ_OK) {
				err = mz_zip_reader_entry_get_info(_handle, &entryInfo);
				if (err != MZ_OK) return false;

				target.push_back(Entry{
					.name = std::string(entryInfo->filename, entryInfo->filename_size),
					.size = (size_t)entryInfo->uncompressed_size
				});

				err = mz_zip_reader_goto_next_entry(_handle);
			}

			return err != MZ_OK && err != MZ_END_OF_LIST;
		}

		bool getEntry(std::string_view fileName, Entry& entry) {
			mz_zip_file* entryInfo = nullptr;
			int32_t err;

			err = mz_zip_reader_locate_entry(_handle, fileName.data(), 0);
			if (err != MZ_OK) return false;
			err = mz_zip_reader_entry_get_info(_handle, &entryInfo);
			if (err != MZ_OK) return false;

			entry = Entry{
				.name = std::string(entryInfo->filename, entryInfo->filename_size),
				.size = (size_t)entryInfo->uncompressed_size
			};

			return true;
		}

		Entry getEntry(std::string_view fileName) {
			Entry entry;
			getEntry(fileName, entry);
			return entry;
		}

		void close() {
			if (_handle) {
				mz_zip_reader_close(_handle);
				mz_zip_reader_delete(&_handle);
				_handle = nullptr;
				_valid = false;
			}
		}

		bool read(std::string_view fileName, char* target, size_t size) {
			mz_zip_file* entryInfo = nullptr;
			int32_t err;

			err = mz_zip_reader_locate_entry(_handle, fileName.data(), 0);
			if (err != MZ_OK) return false;
			err = mz_zip_reader_entry_get_info(_handle, &entryInfo);
			if (err != MZ_OK) return false;
			assert(size <= entryInfo->uncompressed_size);
			if (entryInfo->uncompressed_size > size) return false;
			err = mz_zip_reader_entry_open(_handle);
			if (err != MZ_OK) return false;
			err = mz_zip_reader_entry_read(_handle, target, (int32_t)size);
			if (err != size) return false;
			err = mz_zip_reader_entry_close(_handle);
			if (err != MZ_OK) return false;

			return true;
		}

		bool read(std::string_view fileName, std::vector<std::byte>& target) {
			mz_zip_file* entryInfo = nullptr;
			int32_t err;

			err = mz_zip_reader_locate_entry(_handle, fileName.data(), 0);
			if (err != MZ_OK) return false;
			err = mz_zip_reader_entry_get_info(_handle, &entryInfo);
			if (err != MZ_OK) return false;
			target.resize(entryInfo->uncompressed_size);
			err = mz_zip_reader_entry_open(_handle);
			if (err != MZ_OK) return false;
			err = mz_zip_reader_entry_read(_handle, target.data(), (int32_t)entryInfo->uncompressed_size);
			if (err != entryInfo->uncompressed_size) return false;
			err = mz_zip_reader_entry_close(_handle);
			if (err != MZ_OK) return false;

			return true;
		}

		bool isValid() const {
			return _valid;
		}
	};
}
