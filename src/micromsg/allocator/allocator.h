#pragma once

#include <map>
#include <string>
#include <utility>
#include "mmassert.h"
#include "allocator/types.h"
#include "allocator/uniqueptr.h"
#include "allocator/sharedptr.h"

namespace micromsg {
	const size_t MAX_BIN_COUNT = 128;

	template <typename T>
	void destruct(void* ptr) {
		reinterpret_cast<T*>(ptr)->~T();
	}

	struct BinDesc {
		size_t count = 0;
		DataQueue* queue = nullptr;
	};

	class Allocator {
	private:
		char* _data = nullptr;
		size_t _dataSize = 0;

		std::map<size_t, BinDesc> _bins;
		DataQueue* _binLookup[MAX_BIN_COUNT] = { nullptr };

		bool _active = false;

	public:
		Allocator() {}
		~Allocator() { cleanup(); }

		template <typename T, typename ...Args>
		UniquePtr<T> allocUnique(Args&& ...args) {
			return allocBlock<T>();
			//return allocBlock<T>(std::forward<Args>(args)...);
		}

		template <typename T, typename ...Args>
		SharedPtr<T> allocShared(Args&& ...args) {
			return allocBlock<T>();
			//return allocBlock<T>(std::forward<Args>(args)...);
		}

		template <typename T, typename ...Args>
		T* alloc(Args&& ...args) {
			//ControlBlock* block = allocBlock<T>(std::forward<Args>(args)...);
			ControlBlock* block = allocBlock<T>();
			if (block) {
				return reinterpret_cast<T*>(block + 1);
			}
			
			return nullptr;
		}

		template <typename T>
		void free(T* ptr) {
			ControlBlock* block = reinterpret_cast<ControlBlock*>(ptr) - 1;
			block->destructor(ptr);
			block->destructor = nullptr;
			bool success = block->queue->try_push(block);
			mm_assert(success);
		}

		template <typename T>
		void addType(size_t count) {
			reserveChunks(sizeof(T), count);
		}

		void reserveChunks(size_t bin, size_t count) {
			mm_assert(!_active);
			_bins[bin + sizeof(ControlBlock)].count += count;
		}

		void commit() {
			mm_assert(!_active);

			for (auto& bin : _bins) {
				_dataSize += bin.first * bin.second.count;
			}

			_data = new char[_dataSize];
			memset(_data, 0, _dataSize);

			char* ptr = _data;
			for (auto& bin : _bins) {
				size_t blockSize = bin.first;
				BinDesc& d = bin.second;

				std::cout << "Adding bin: " << blockSize << " x" << d.count << std::endl;

				DataQueue* queue = new DataQueue(d.count);
				d.queue = queue;

				for (size_t i = 0; i < d.count; ++i) {
					ControlBlock* block = reinterpret_cast<ControlBlock*>(ptr);
					block->queue = queue;
					bool success = queue->try_push(block);
					assert(success);
					ptr += blockSize;
				}

				if (blockSize < MAX_BIN_COUNT) {
					_binLookup[blockSize] = queue;
				}
			}

			std::cout << "Total allocated: " << _dataSize << " bytes" << std::endl;
			_active = true;
		}

	private:
		template <typename T>
		ControlBlock* allocBlock() {
			mm_assert(_active);

			constexpr int totalSize = sizeof(ControlBlock) + sizeof(T);

			DataQueue* bin = nullptr;
			if constexpr (totalSize < MAX_BIN_COUNT) {
				bin = _binLookup[totalSize];
				mm_assert(bin != nullptr);
			} else {
				auto& found = _bins.find(totalSize);
				mm_assert(found != _bins.end());
				if (found != _bins.end()) {
					bin = found->second.queue;
				}
			}

			if (bin) {
				ControlBlock* block = nullptr;
				if (bin->try_pop(block)) {
					new (block + 1) T();
					block->destructor = destruct<T>;
					return block;
				}

				std::cout << "Ran out of blocks of size " << totalSize << std::endl;
				mm_assert_m(false, "Ran out of blocks");
			}
			
			return nullptr;
		}

		void cleanup() {
			for (auto& bin : _bins) {
				delete bin.second.queue;
			}

			delete[] _data;
		}
	};
}
