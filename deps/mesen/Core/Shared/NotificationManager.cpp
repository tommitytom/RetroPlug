#include "pch.h"
#include <algorithm>
#include "Shared/NotificationManager.h"

namespace {
	// RetroPlug: the copy SendNotification iterates (a listener may unregister while the notification runs, so the
	// list cannot be iterated under the lock) is reused per thread, and per nesting depth: a listener that sends a
	// notification of its own gets a buffer of its own from the pool, so nesting stays correct. The pool grows to
	// the deepest nesting a thread has seen and never again, so a frame's notifications allocate nothing.
	//
	// Each buffer is held behind its own pointer: growing the pool for a deeper nesting moves the pool's elements,
	// and a reference to a buffer must stay valid across the nested send that grew it.
	thread_local vector<unique_ptr<vector<weak_ptr<INotificationListener>>>> dispatchBuffers;
	thread_local size_t dispatchDepth = 0;

	vector<weak_ptr<INotificationListener>>& takeDispatchBuffer()
	{
		if(dispatchBuffers.size() <= dispatchDepth) {
			dispatchBuffers.push_back(std::make_unique<vector<weak_ptr<INotificationListener>>>());
		}
		unique_ptr<vector<weak_ptr<INotificationListener>>>& buffer = dispatchBuffers[dispatchDepth++];
		return *buffer;
	}

	// One SendNotification's buffer: taken from the pool on construction, handed back on any exit (a listener is
	// arbitrary code and may throw). It hands its references back with it: a copy that kept its weak_ptrs past the
	// send would hold a dead listener's control block alive until the next send at that depth overwrote the buffer,
	// so removing one of two consoles would free it on the audio thread inside the survivor's next block. clear()
	// keeps the capacity, so the next send still allocates nothing.
	struct DispatchBuffer
	{
		vector<weak_ptr<INotificationListener>>& listeners;

		DispatchBuffer() : listeners(takeDispatchBuffer()) {}

		~DispatchBuffer()
		{
			listeners.clear();
			dispatchDepth--;
		}
	};
}

void NotificationManager::RegisterNotificationListener(shared_ptr<INotificationListener> notificationListener)
{
	auto lock = _lock.AcquireSafe();

	for(weak_ptr<INotificationListener> listener : _listeners) {
		if(listener.lock() == notificationListener) {
			//This listener is already registered, do nothing
			return;
		}
	}

	_listeners.push_back(notificationListener);
}

void NotificationManager::CleanupNotificationListeners()
{
	auto lock = _lock.AcquireSafe();

	//Remove expired listeners
	_listeners.erase(
		std::remove_if(
			_listeners.begin(),
			_listeners.end(),
			[](weak_ptr<INotificationListener> ptr) { return ptr.expired(); }
		),
		_listeners.end()
	);
}

void NotificationManager::SendNotification(ConsoleNotificationType type, void* parameter)
{
	DispatchBuffer dispatch;
	vector<weak_ptr<INotificationListener>>& listeners = dispatch.listeners;
	{
		auto lock = _lock.AcquireSafe();
		CleanupNotificationListeners();
		listeners = _listeners;
	}

	//Iterate on a copy without using a lock
	for(weak_ptr<INotificationListener>& notificationListener : listeners) {
		shared_ptr<INotificationListener> listener = notificationListener.lock();
		if(listener) {
			listener->ProcessNotification(type, parameter);
		}
	}
}
