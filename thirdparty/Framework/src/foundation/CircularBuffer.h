#pragma once

#include "foundation/DataBuffer.h"

namespace fw {
	template <typename T>
	class CircularBuffer {
	private:
		DataBuffer<T> _buffer;
		size_t _writePosition = 1;
		size_t _readPosition = 0;

	public:
		CircularBuffer(size_t size) : _buffer(size) { _buffer.clear(); }

		size_t write(const DataBuffer<T>& other) {
			size_t writeSize = writeAvailable();
			if (writeSize > other.size()) {
				writeSize = other.size();
			}

			if (writeSize > 0) {
				size_t endPos = _writePosition + writeSize;

				if (endPos <= _buffer.size()) {
					_buffer.slice(_writePosition, writeSize).copyFrom(other);
				} else {
					size_t size1 = _buffer.size() - _writePosition;
					size_t size2 = endPos - _buffer.size();

					_buffer.slice(_writePosition, size1).copyFrom(other.slice(0, size1));
					_buffer.slice(0, size2).copyFrom(other.slice(size1, size2));
				}

				_writePosition = (_writePosition + writeSize) % _buffer.size();
				assert(_writePosition != _readPosition);
			}	

			return writeSize;
		}

		size_t read(DataBuffer<T>& target, bool clear = false) {
			size_t readSize = readAvailable();
			if (readSize > target.size()) {
				readSize = target.size();
			}

			if (readSize > 0) {
				size_t endPos = _readPosition + readSize;

				if (endPos <= _buffer.size()) {
					DataBuffer<T> sourceSlice = _buffer.slice(_readPosition, readSize);
					target.copyFrom(sourceSlice);

					if (clear) {
						sourceSlice.clear();
					}
				} else {
					size_t size1 = _buffer.size() - _readPosition;
					size_t size2 = endPos - _buffer.size();

					DataBuffer<T> source1 = _buffer.slice(_readPosition, size1);
					DataBuffer<T> source2 = _buffer.slice(0, size2);

					target.slice(0, size1).copyFrom(source1);
					target.slice(size1, size2).copyFrom(source2);

					if (clear) {
						source1.clear();
						source2.clear();
					}
				}

				_readPosition = (_readPosition + readSize) % _buffer.size();
				assert(_writePosition != _readPosition);
			}

			return readSize;
		}

		void clear() {
			_buffer.clear();
		}

		void setWritePosition(size_t position) {
			assert(position < _buffer.size());
			assert(position != _readPosition);
			_writePosition = position;
		}

		size_t writeAvailable() const {
			assert(_writePosition != _readPosition);

			if (_writePosition > _readPosition) {
				return ((_buffer.size() - _writePosition) + _readPosition) - 1;
			} else {
				return (_readPosition - _writePosition) - 1;
			}
		}

		size_t readAvailable() const {
			assert(_writePosition != _readPosition);

			if (_writePosition > _readPosition) {
				return (_writePosition - _readPosition) - 1;
			} else {
				return ((_buffer.size() - _readPosition) + _writePosition) - 1;
			}
		}

		bool isWriteValid(size_t size) const {
			return size < writeAvailable();
		}

		bool isReadValid(size_t size) const {
			return size < readAvailable();
		}
	};
}
