#pragma once 

#include <variant>
#include <vector>

#include "platform/Types.h"
#include "util/DataBuffer.h"

namespace rp {
	enum class MemoryType {
		Unknown,
		Ram,
		Rom,
		Sram
	};

	struct MemoryPatch {
		MemoryType type;
		std::variant<uint8, uint16, uint32, Uint8Buffer> data;
		size_t offset;
	};

	class MemoryAccessor {
	private:
		MemoryType _type = MemoryType::Unknown;
		Uint8Buffer _data;
		size_t _offset = 0;
		std::vector<MemoryPatch>* _patches = nullptr;

	public:
		MemoryAccessor() {}
		MemoryAccessor(MemoryType type, Uint8Buffer data, size_t offset, std::vector<MemoryPatch>* patches) : _type(type), _data(data), _offset(offset), _patches(patches) {}

		std::vector<MemoryPatch>* getPatches() {
			return _patches;
		}

		void set(size_t idx, uint8 value) {
			assert(idx < _data.size());

			_data[idx] = value;

			if (_patches) {
				_patches->push_back(MemoryPatch{
					.type = _type,
					.data = value,
					.offset = _offset + idx
				});
			}
		}

		template <typename T>
		void write(size_t pos, const T& data) {
			assert(isValid());
			assert(pos + sizeof(T) <= _data.size());

			memcpy(_data.data() + pos, &data, sizeof(T));

			if (_patches) {
				_patches->push_back(MemoryPatch{
					.type = _type,
					.data = data,
					.offset = _offset + pos
				});
			}
		}

		void write(size_t pos, Uint8Buffer&& buffer) {
			assert(isValid());
			assert(pos + buffer.size() <= _data.size());

			_data.slice(pos, buffer.size()).copyFrom(buffer);

			if (_patches) {
				_patches->push_back(MemoryPatch{
					.type = _type,
					.data = buffer.isOwnerOfData() ? std::move(buffer) : buffer.clone(),
					.offset = _offset + pos
				});
			}			
		}

		void write(size_t pos, std::string_view text) {
			write(pos, Uint8Buffer((uint8*)text.data(), text.size()));
		}

		void write(size_t pos, const std::string& text) {
			write(pos, Uint8Buffer((uint8*)text.data(), text.size()));
		}

		void write(size_t pos, const Uint8Buffer& buffer) {
			assert(isValid());
			assert(pos + buffer.size() <= _data.size());

			_data.slice(pos, buffer.size()).copyFrom(buffer);

			if (_patches) {
				_patches->push_back(MemoryPatch{
					.type = _type,
					.data = buffer.isOwnerOfData() ? buffer : buffer.clone(),
					.offset = _offset + pos
				});
			}
		}		

		MemoryAccessor slice(size_t start, size_t size) {
			return MemoryAccessor(_type, _data.slice(start, size), _offset + start, _patches);
		}

		uint8 operator[](size_t idx) const {
			assert(idx < _data.size());
			return _data[idx];
		}

		bool isValid() const {
			return _type != MemoryType::Unknown;
		}

		const uint8* getData() const {
			return _data.data();
		}

		size_t getSize() const {
			return _data.size();
		}

		const Uint8Buffer& getBuffer() const {
			return _data;
		}
	};
}
