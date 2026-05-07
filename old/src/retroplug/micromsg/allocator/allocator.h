#pragma once

#include <map>
#include <string>
#include <utility>
#include "../mmassert.h"
#include "types.h"
#include "uniqueptr.h"
#include "sharedptr.h"
#include "../platform.h"

namespace micromsg {
	const size_t MAX_BIN_COUNT = 128;

	template <typename T>
	void destruct(void* ptr, size_t count) {
		T* p = reinterpret_cast<T*>(ptr);
		for (size_t i = 0; i < count; ++i) {
			(p + i)->~T();
		}
	}

	static void destruct_nop(void*, size_t) {}

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
			return allocBlockConstructed<T>();
			//return allocBlock<T>(std::forward<Args>(args)...);
		}

		template <typename T, typename ...Args>
		SharedPtr<T> allocShared(Args&& ...args) {
			return allocBlockConstructed<T>();
			//return allocBlock<T>(std::forward<Args>(args)...);
		}

		template <typename T, typename ...Args>
		T* alloc(Args&& ...args) {
			//ControlBlock* block = allocBlock<T>(std::forward<Args>(args)...);
			ControlBlock* block = allocBlockConstructed<T>();
			if (block) {
				return reinterpret_cast<T*>(block + 1);
			}
			
			return nullptr;
		}

		template <typename T>
		T* allocArray(size_t size) {
			ControlBlock* block = allocArrayBlockConstructed<T>(size);
			if (block) {
				return reinterpret_cast<T*>(block + 1);
			}
		}

		template <typename T>
		UniquePtr<T> allocArrayUnique(size_t size) {
			return allocArrayBlockConstructed<T>(size);
		}

		template <typename T>
		SharedPtr<T> allocArrayShared(size_t size) {
			return allocArrayBlockConstructed<T>(size);
		}

		template <typename T>
		bool canAlloc(size_t elementCount = 1) {
			DataQueue* bin = getBin(sizeof(ControlBlock) + sizeof(T) * elementCount);
			return bin && bin->size_approx() > 0;
		}

		template <typename T>
		void free(T* ptr) {
			ControlBlock* block = reinterpret_cast<ControlBlock*>(ptr) - 1;
			block->destructor(ptr, block->elementCount);
			block->destructor = nullptr;
			bool success = block->queue->enqueue(block);
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
					block->destructor = nullptr;
					block->elementCount = 0;

					bool success = queue->try_enqueue(block);
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
		ControlBlock* allocBlockConstructed() {
			const char* name = nullptr;
#ifdef RTTI_ENABLED
			name = typeid(T).name();
#endif
			ControlBlock* block = allocBlock(sizeof(T), name);
			if (block) {
				if constexpr (std::is_class<T>::value && std::is_default_constructible<T>::value) {
					new (block + 1) T();
				}

				if constexpr (std::is_class<T>::value && std::is_destructible<T>::value) {
					block->destructor = destruct<T>;
				}
			}

			return block;
		}

		template <typename T>
		ControlBlock* allocArrayBlockConstructed(size_t size) {
			const char* name = nullptr;
#ifdef RTTI_ENABLED
			name = typeid(T).name();
#endif
			ControlBlock* block = allocBlock(sizeof(T) * size, name);
			if (block) {
				block->elementCount = size;

				T* ptr = reinterpret_cast<T*>(block + 1);
				if constexpr (std::is_class<T>::value && std::is_default_constructible<T>::value) {
					for (size_t i = 0; i < size; ++i) {
						new (ptr + i) T();
					}
				}

				if constexpr (std::is_class<T>::value && std::is_destructible<T>::value) {
					block->destructor = destruct<T>;
				}
			}

			return block;
		}

		DataQueue* getBin(size_t size) {
			DataQueue* bin = nullptr;
			if (size < MAX_BIN_COUNT) {
				bin = _binLookup[size];
				mm_assert(bin != nullptr);
			} else {
				const auto& found = _bins.find(size);
				mm_assert(found != _bins.end());
				if (found != _bins.end()) {
					bin = found->second.queue;
				}
			}

			return bin;
		}

		ControlBlock* allocBlock(size_t size, const char* name) {
			mm_assert(_active);

			int totalSize = sizeof(ControlBlock) + size;

			DataQueue* bin = getBin(totalSize);
			if (bin) {
				ControlBlock* block = nullptr;
				if (bin->try_dequeue(block)) {
					block->destructor = destruct_nop;
					block->elementCount = 1;
					return block;
				}

				std::cout << "Ran out of blocks of size " << totalSize;
				if (name) std::cout << " allocating " << name;				
				std::cout << std::endl;

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
