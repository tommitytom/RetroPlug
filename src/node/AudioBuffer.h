#pragma once

#include "foundation/DataBuffer.h"

namespace rp {
	class AudioBuffer {
	private:
		Float32Buffer _buffer;

	public:
		AudioBuffer() {}
		AudioBuffer(AudioBuffer&& other) noexcept {
			*this = std::move(other);
		}

		AudioBuffer(Float32Buffer&& buffer) noexcept {
			_buffer = std::move(buffer);
		}

		AudioBuffer(size_t size) : _buffer(size) {}

		AudioBuffer& operator=(AudioBuffer&& other) noexcept {
			_buffer = std::move(other._buffer);
			return *this;
		}

		f32 get(size_t idx) const {
			return _buffer.get(idx);
		}

		void set(size_t idx, f32 v) {
			_buffer.set(idx, v);
		}

		f32& operator[](size_t idx) {
			return _buffer.get(idx);
		}

		f32 operator[](size_t idx) const {
			return _buffer.get(idx);
		}

		Float32Buffer& getBuffer() {
			return _buffer;
		}

		const Float32Buffer& getBuffer() const {
			return _buffer;
		}

		size_t size() const {
			return _buffer.size();
		}

		AudioBuffer slice(size_t pos, size_t size) {
			return AudioBuffer(_buffer.slice(pos, size));
		}

		void resize(size_t size) {
			_buffer.resize(size);
		}

		void clear(f32 value) {
			_buffer.clear(value);
		}

		void write(const AudioBuffer& other) {
			_buffer.write(other._buffer);
		}

		void add(const AudioBuffer& other) {
			for (size_t i = 0; i < other._buffer.size(); ++i) {
				_buffer[i] *= other._buffer[i];
			}
		}
	};
}
