#pragma once

#include <vector>
#include <assert.h>
#include <iostream>
#include <string.h>

struct DataBlock {
	char* data;
	size_t size;
	size_t pos = 0;
};

class BlockAllocator {
private:
	std::vector<DataBlock> _blocks;
	size_t _blockIdx = 0;
	bool _enabled = false;

public:
	BlockAllocator(size_t size) {
		_blocks.push_back({ new char[size], size });
	}

	~BlockAllocator() {
		for (DataBlock& block : _blocks) {
			delete[] block.data;
		}
	}

	char* alloc(size_t size) {
		assert(_enabled);

		DataBlock* block = &_blocks[_blockIdx];
		while (block->pos + size > block->pos) {
			if (_blockIdx + 1 < _blocks.size()) {
				_blockIdx++;
			} else {
				size_t blockSize = block->size;
				if (size > blockSize) {
					blockSize = size;
				}

				_blocks.push_back({ new char[blockSize], blockSize });
			}
		}

		char* buf = block->data + block->pos;
		block->pos += size;

		memset(buf, 0, size);

		return buf;
	}

	template <typename T, typename ...Args>
	T* allocObj(size_t count, Args&&... args) {
		T* obj = reinterpret_cast<T*>(alloc(count));

		if constexpr (std::is_class<T>::value&& std::is_default_constructible<T>::value) {
			for (size_t i = 0; i < count; ++i) {
				new (obj + i) T(args);
			}
		}

		return obj;
	}

	template <typename T>
	void free(T* ptr) {
		// Ignore
	}

	void enable() {
		_enabled = true;
	}

	void reset() {
		_blockIdx = 0;
		_enabled = false;
	}

	bool dirty() {
		return _blockIdx > 0 || _blocks[0].pos > 0;
	}
};
