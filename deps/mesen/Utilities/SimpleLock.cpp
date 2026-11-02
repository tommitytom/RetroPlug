#include "pch.h"
#include <assert.h>
#include "SimpleLock.h"
#include <Timer.h>

thread_local std::thread::id SimpleLock::_threadID = std::this_thread::get_id();

// RetroPlug: the holder check is a plain load on every target, never a hidden lock.
static_assert(std::atomic<std::thread::id>::is_always_lock_free, "SimpleLock's holder id must be a lock-free atomic");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "SimpleLock's count must be a lock-free atomic");

SimpleLock::SimpleLock()
{
	_lock.clear();
	_lockCount.store(0, std::memory_order_relaxed);
	_holderThreadID.store(std::thread::id(), std::memory_order_relaxed);
}

SimpleLock::~SimpleLock()
{
}

LockHandler SimpleLock::AcquireSafe()
{
	return LockHandler(this);
}

void SimpleLock::Acquire()
{
	if(_lockCount.load(std::memory_order_relaxed) == 0 || _holderThreadID.load(std::memory_order_relaxed) != _threadID) {
		WaitForAcquire(0);
		_holderThreadID.store(_threadID, std::memory_order_relaxed);
		_lockCount.store(1, std::memory_order_relaxed);
	} else {
		//Same thread can acquire the same lock multiple times
		_lockCount.store(_lockCount.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
	}
}

bool SimpleLock::WaitForAcquire(uint32_t msTimeout)
{
	if(_lock.test_and_set()) {
		Timer timer;
		int attempts = 0;
		while(_lock.test_and_set()) {
			if(++attempts >= 10000) {
				//Sleep between attempts after failing to acquire lock 10k times
				std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(msTimeout > 0 ? 10 : 1));
			}
			if(msTimeout > 0 && timer.GetElapsedMS() > msTimeout) {
				return false;
			}
		}
	}
	return true;
}

bool SimpleLock::TryAcquire(uint32_t msTimeout)
{
	if(_lockCount.load(std::memory_order_relaxed) == 0 || _holderThreadID.load(std::memory_order_relaxed) != _threadID) {
		if(!WaitForAcquire(msTimeout)) {
			return false;
		}
		_holderThreadID.store(_threadID, std::memory_order_relaxed);
		_lockCount.store(1, std::memory_order_relaxed);
	} else {
		//Same thread can acquire the same lock multiple times
		_lockCount.store(_lockCount.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
	}

	return true;
}

bool SimpleLock::IsFree()
{
	return _lockCount.load(std::memory_order_relaxed) == 0;
}

bool SimpleLock::IsLockedByCurrentThread()
{
	return _lockCount.load(std::memory_order_relaxed) > 0 && _holderThreadID.load(std::memory_order_relaxed) == _threadID;
}

void SimpleLock::WaitForRelease()
{
	//Wait until we are able to grab a lock, and then release it again
	Acquire();
	Release();
}

void SimpleLock::Release()
{
	if(IsLockedByCurrentThread()) {
		const uint32_t count = _lockCount.load(std::memory_order_relaxed) - 1;
		_lockCount.store(count, std::memory_order_relaxed);
		if(count == 0) {
			_holderThreadID.store(std::thread::id(), std::memory_order_relaxed);
			_lock.clear();
		}
	} else {
		assert(false);
	}
}


LockHandler::LockHandler(SimpleLock *lock)
{
	_lock = lock;
	_lock->Acquire();
}

void LockHandler::Release()
{
	if(!_released) {
		_released = true;
		_lock->Release();
	}
}

LockHandler::~LockHandler()
{
	if(!_released) {
		_lock->Release();
	}
}