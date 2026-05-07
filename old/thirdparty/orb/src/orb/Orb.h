#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include <functional>

#include <entt/core/type_info.hpp>
#include "foundation/Types.h"

namespace fw {

// Forward declarations
template<typename T> class ObserverPtr;
class Observable;

// Lightweight observer that gets notified when object is destroyed
class ObserverBase {
public:
	virtual void onObservableDestroyed() = 0;
};

// Base class that can be observed
class Observable {
	mutable std::vector<ObserverBase*> observers;

public:
	virtual ~Observable() {
		for (auto* observer : observers) {
			observer->onObservableDestroyed();
		}
	}

	void addObserver(ObserverBase* observer) const {
		observers.push_back(observer);
	}

	void removeObserver(ObserverBase* observer) const {
		observers.erase(
			std::remove(observers.begin(), observers.end(), observer),
			observers.end()
		);
	}
};

// Smart observer pointer (like weak_ptr but lighter weight)
template<typename T>
class ObserverPtr : private ObserverBase {
	T* ptr = nullptr;

public:
	ObserverPtr() = default;

	explicit ObserverPtr(T* p) : ptr(p) {
		if (ptr) {
			ptr->addObserver(this);
		}
	}

	ObserverPtr(const ObserverPtr& other) : ptr(other.ptr) {
		if (ptr) {
			ptr->addObserver(this);
		}
	}

	ObserverPtr(ObserverPtr&& other) noexcept : ptr(other.ptr) {
		other.ptr = nullptr;
		if (ptr) {
			ptr->addObserver(this);
		}
	}

	~ObserverPtr() {
		reset();
	}

	ObserverPtr& operator=(const ObserverPtr& other) {
		if (this != &other) {
			reset();
			ptr = other.ptr;
			if (ptr) {
				ptr->addObserver(this);
			}
		}
		return *this;
	}

	ObserverPtr& operator=(ObserverPtr&& other) noexcept {
		if (this != &other) {
			reset();
			ptr = other.ptr;
			other.ptr = nullptr;
			if (ptr) {
				ptr->addObserver(this);
			}
		}
		return *this;
	}

	void reset() {
		if (ptr) {
			ptr->removeObserver(this);
			ptr = nullptr;
		}
	}

	T* get() const { return ptr; }
	T* operator->() const { return ptr; }
	T& operator*() const { return *ptr; }
	explicit operator bool() const { return ptr != nullptr; }

private:
	void onObservableDestroyed() override {
		ptr = nullptr;
	}
};

// Your Object class with Observable mixed in
template<typename Derived, typename... Bases>
class Object : public Observable {
private:
	static bool checkBases(entt::type_info typeInfo) {
		if constexpr (sizeof...(Bases) == 0) {
			return typeInfo == entt::type_id<Observable>();
		} else {
			return ((typeInfo == entt::type_id<Bases>() ||
					Bases::isDerivedFromTypeInfo_static(typeInfo)) || ...);
		}
	}

public:
	entt::type_info getTypeInfo() const {
		return entt::type_id<Derived>();
	}

	uint32 getTypeId() const {
		return entt::type_hash<Derived>::value();
	}

	std::string_view getTypeName() const {
		return entt::type_name<Derived>::value();
	}

	bool isDerivedFromTypeInfo(entt::type_info typeInfo) const {
		return typeInfo == entt::type_id<Derived>() ||
			   typeInfo == entt::type_id<Observable>() ||
			   checkBases(typeInfo);
	}

	static bool isDerivedFromTypeInfo_static(entt::type_info typeInfo) {
		return typeInfo == entt::type_id<Derived>() ||
			   typeInfo == entt::type_id<Observable>() ||
			   checkBases(typeInfo);
	}

	template <typename T>
	bool isType() const {
		return getTypeInfo() == entt::type_id<T>();
	}

	template <typename T>
	bool isDerivedFrom() const {
		return isDerivedFromTypeInfo(entt::type_id<T>());
	}
};

// Alternative: Arena allocator for batch allocation
template<typename T>
class Arena {
	struct Block {
		static constexpr size_t BlockSize = 4096;
		alignas(T) char storage[BlockSize];
		size_t used = 0;
		std::unique_ptr<Block> next;
	};

	std::unique_ptr<Block> head = std::make_unique<Block>();
	Block* current = head.get();

public:
	template<typename... Args>
	T* create(Args&&... args) {
		size_t needed = sizeof(T);
		if (current->used + needed > Block::BlockSize) {
			current->next = std::make_unique<Block>();
			current = current->next.get();
		}

		T* ptr = new(current->storage + current->used) T(std::forward<Args>(args)...);
		current->used += needed;
		return ptr;
	}

	// Note: Arena doesn't support individual destruction
	// All objects destroyed when Arena is destroyed
	~Arena() {
		// Destructors called automatically as blocks are freed
	}
};

}

// Usage example:
/*
class View : public orb::Object<View> {
public:
	void render() { }
};

class SliderView : public orb::Object<SliderView, View> {
public:
	float value = 0.5f;
};

void example() {
	// Option 1: Unique ownership
	auto slider = std::make_unique<SliderView>();

	// Create observers (lightweight, no refcounting)
	orb::ObserverPtr<SliderView> observer1(slider.get());
	orb::ObserverPtr<SliderView> observer2 = observer1;

	// Observers automatically nulled when object destroyed
	slider.reset();
	assert(!observer1);  // Automatically cleared!

	// Option 2: Arena allocation (super fast, batch deallocation)
	orb::Arena<SliderView> arena;

	// Create many objects with no allocation overhead
	std::vector<SliderView*> sliders;
	for (int i = 0; i < 1000; ++i) {
		sliders.push_back(arena.create());
	}

	// All destroyed when arena goes out of scope
}
*/