#pragma once

#include "PaRingBuffer.h"
#include <assert.h>

inline bool isPowerOfTwo(size_t in) {
	return (in & (in - 1)) == 0;
}

template <typename T>
class RingBuffer
{
private:
	T* mp_data;
	PaUtilRingBuffer m_buffer;

public:
	RingBuffer(): mp_data(nullptr) {}
	RingBuffer(size_t size) {
		init(size);
	}

	~RingBuffer() {
		if (mp_data) {
			delete[] mp_data;
		}
	}

	T* data() {
		return mp_data;
	}

	void init(size_t size) {
		assert(isPowerOfTwo(size));
		mp_data = new T[size];
		PaUtil_InitializeRingBuffer(&m_buffer, sizeof(T), size, mp_data);
	}

	void advanceWrite(size_t count) {
		PaUtil_AdvanceRingBufferWriteIndex(&m_buffer, count);
	}

	void advanceRead(size_t count) {
		PaUtil_AdvanceRingBufferReadIndex(&m_buffer, count);
	}

	size_t writeAvailable() {
		return PaUtil_GetRingBufferWriteAvailable(&m_buffer);
	}

	size_t readAvailable() {
		return PaUtil_GetRingBufferReadAvailable(&m_buffer);
	}

	void clear() {
		PaUtil_FlushRingBuffer(&m_buffer);
	}

	bool writeValue(const T& v) {
		return PaUtil_WriteRingBuffer(&m_buffer, &v, 1) == 1;
	}

	bool write(const T* v, size_t count) {
		return PaUtil_WriteRingBuffer(&m_buffer, v, count) == 1;
	}

	bool readValue(T& v) {
		PaUtil_ReadRingBuffer(&m_buffer, &v, 1);
		return true;
	}

	T readValue() {
		T ret; readValue(ret);
		return ret;
	}

	size_t read(T* v) {
		return PaUtil_ReadRingBuffer(&m_buffer, v, readAvailable());
	}

	size_t read(T* v, size_t count) {
		size_t available = readAvailable();
		if (available < count) count = available;
		return PaUtil_ReadRingBuffer(&m_buffer, v, count);
	}
};
