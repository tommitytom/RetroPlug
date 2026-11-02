#pragma once 
#include "pch.h"
#include <atomic>
#include <thread>

class SimpleLock;

class LockHandler
{
private:
	SimpleLock *_lock;
	bool _released = false;
public:
	LockHandler(SimpleLock *lock);
	void Release();
	~LockHandler();
};

class SimpleLock
{
private:
	thread_local static std::thread::id _threadID;

	// RetroPlug: the holder and its count are atomics, read relaxed, because every thread that acquires reads them to
	// learn whether it already holds the lock while the holder may be writing them, which is a data race on plain
	// members (ThreadSanitizer saw it on MessageManager's process-wide lock, two cores built on two threads at once).
	// Relaxed is enough: only the holder writes them (so the count is a load and a store, not a locked read-modify-
	// write), a thread's own writes are visible to it, and the id another thread writes is never the reader's own;
	// what the lock protects is ordered by _lock's test_and_set and clear.
	std::atomic<std::thread::id> _holderThreadID;
	std::atomic<uint32_t> _lockCount;
	atomic_flag _lock;
	
	bool WaitForAcquire(uint32_t msTimeout);

public:
	SimpleLock();
	~SimpleLock();

	LockHandler AcquireSafe();

	void Acquire();
	bool TryAcquire(uint32_t msTimeout);
	bool IsFree();
	bool IsLockedByCurrentThread();
	void WaitForRelease();
	void Release();
};

