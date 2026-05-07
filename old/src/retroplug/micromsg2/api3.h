#pragma once

#include <tuple>
#include <functional>
#include <variant>
#include <iostream>
#include <string>
#include "micromsg/readerwriterqueue.h"

class BasicAllocator {
public:
	void* alloc(size_t size) {
		return ::malloc(size);
	}

	void free(void* ptr) {
		::free(ptr);
	}
};

template <typename T, typename AllocatorT = BasicAllocator>
class Proxy {
private:
	struct VoidT {};
	using ErrorHandler = std::function<void(std::exception_ptr)>;

	struct StoredCallBase {
		using MethodFunction = std::function<void(T&)>;
		MethodFunction method;
	};

	template <typename R, typename... Args>
	struct StoredCall : public StoredCallBase {
		using MethodSignature = R(T::*)(Args...);
		using ParamTuple = typename std::tuple<Args...>;
		using ReturnT = typename std::conditional_t<std::is_same<R, void>::value, VoidT, R>;
		using ReturnCallback = typename std::variant<std::function<void(ReturnT&)>, std::function<void()>>;

		ParamTuple params;
		ReturnT ret;
		ReturnCallback retHandler;
		std::exception_ptr error;
		ErrorHandler errorHandler;
	};

	using CallQueue = typename moodycamel::ReaderWriterQueue<StoredCallBase*>;

public:
	class Listener {
	private:
		T* _obj;
		CallQueue* _incoming;
		CallQueue* _outgoing;

		Listener(CallQueue* incoming, CallQueue* outgoing, T* obj = nullptr)
			: _incoming(incoming), _outgoing(outgoing), _obj(obj) {}

	public:
		void pull() {
			StoredCallBase* call;
			while (_incoming->try_dequeue(call)) {
				call->method(*_obj);
			}
		}

		void setObj(T& obj) noexcept {
			_obj = &obj;
		}

		T& getObj() noexcept {
			return *_obj;
		}

		const T& getObj() const noexcept {
			return *_obj;
		}

		friend class Proxy;
	};

	template <typename R, typename... Args>
	class Promise {
	private:
		using StoredCall = typename StoredCall<R, Args...>;
		StoredCall* _call;

		Promise(StoredCall* call) : _call(call) {}

	public:
		Promise& then(std::function<void()>&& cb) noexcept {
			_call->retHandler = std::move(cb);
			return *this;
		}

		Promise& then(std::function<void(typename StoredCall::ReturnT&)>&& cb) noexcept {
			_call->retHandler = std::move(cb);
			return *this;
		}

		Promise& error(ErrorHandler&& cb) noexcept {
			_call->errorHandler = std::move(cb);
			return *this;
		}

		friend class Proxy;
	};

private:
	CallQueue _outgoing;
	CallQueue _incoming;
	AllocatorT* _allocator = nullptr;

public:
	Proxy(): _allocator(new AllocatorT()) {}
	Proxy(AllocatorT* allocator): _allocator(allocator) {}

	template <typename R, typename... Args>
	inline Promise<R, Args...> call(R(T::* fn)(Args...), Args&&... args) {
		auto* call = allocStoredCall({ fn, std::forward_as_tuple(std::move(args)...) });
		call->method = createMethod(call);
		_outgoing.enqueue(call);
		return Promise<R, Args...>(call);
	}

	template <typename R, typename... Args>
	inline Promise<R, Args...> call(R(T::* fn)(Args...), const Args&... args) {
		auto* call = allocStoredCall({ fn, std::forward_as_tuple(args...) });
		call->method = createMethod(call);
		_outgoing.enqueue(call);
		return Promise<R, Args...>(call);
	}

	void pull() {
		// Here we receive the data that was used for previous outgoing calls.
		// In some cases the data may also contain return values, which are
		// passed to the relevant callbacks.  Data is free'd back to the allocator
		// that it was acquired from.

		StoredCallBase* call;
		while (_incoming.try_dequeue(call)) {
			if (!call->error) {
				std::visit([call](auto&& retFunc) {
					if (retFunc) {
						//using StoredCall = decltype(arg);
						//using ReturnT = typename StoredCall::ReturnT;
						using RetFuncT = std::decay_t<decltype(retFunc)>;
						if constexpr (std::is_same_v<RetFuncT, std::function<void()>>) {
							retFunc();
						} else {//if constexpr (std::is_same_v<RetFuncT, std::function<void(ReturnT&)>>) {
							retFunc(call->ret);
						}
					}
				}, call->retHandler);
			} else if (call->errorHandler) {
				call->errorHandler(call->error);
			} else {
				std::rethrow_exception(call->error);
			}

			_allocator->free(call);
		}
	}

	inline Listener getListener() noexcept {
		return Listener(&_outgoing, &_incoming);
	}

	inline Listener getListener(T& obj) noexcept {
		return Listener(&_outgoing, &_incoming, &obj);
	}

private:
	template <typename R, typename... Args>
	StoredCall<R, Args...>* allocStoredCall(StoredCall<R, Args...>&& arg) {
		using StoredCall = typename StoredCall<R, Args...>;
		void* data = _allocator->alloc(sizeof(StoredCall));
		return new (call) StoredCall(std::forward<StoredCall>(arg))
	}

	template <typename R, typename... Args>
	std::function<void(T&)> createMethod(StoredCall<R, Args...>* call) const {
		return [call](T& obj) {
			try {
				if constexpr (std::is_same_v<R, void>) {
					std::apply(call->method, createCallTuple(obj, call));
				} else {
					call->ret = std::apply(call->method, createCallTuple(obj, call));
				}
			} catch (...) {
				call->error = std::current_exception();
			}
		};
	}

	template <typename R, typename... Args>
	constexpr std::tuple<T&, Args&&...> createCallTuple(T& obj, StoredCall<R, Args...>* call) const {
		return std::tuple_cat(
			std::forward_as_tuple(obj),
			std::forward<StoredCall<R, Args...>::ParamTuple>(call->params)
		);
	}
};

class Foo {
private:
	std::string _text;
	float _v = 0;
	std::unique_ptr<int> _ptr;

public:
	void bar(int v, float v2) {
		std::cout << "BAR! " << v << " " << v2 << std::endl;
		_text = std::to_string(v);
		_v = v2;
	}

	int bar2(int v, float v2) {
		std::cout << "BAR! " << v << " " << v2 << std::endl;
		_text = std::to_string(v);
		_v = v2;
		return 0;
	}

	float baz(int v) {
		throw std::runtime_error("shits fucked");
		std::cout << _text << std::endl;
		return _v;
	}

	void pants(std::unique_ptr<int> ptr, int v) {
		_ptr = std::move(ptr);
		std::cout << "move the ptr!" << std::endl;
	}

	std::string shit(size_t v1, size_t v2, size_t v3, size_t v4, size_t v5, size_t v6, size_t v7, size_t v8) {
		return "Hello world!";
	}
};

using FooProxy = Proxy<Foo>;

//https://riptutorial.com/cplusplus/example/24746/storing-function-arguments-in-std--tuple

static void func() {
	Foo f;
	
	FooProxy emitter;
	FooProxy::Listener listener = emitter.getListener(f);

	size_t v = 0;
	int vint = 0;

	//emitter.call(&Foo::bar, 10, 3.14f).then([]() {
		//return emitter.call(&Foo::bar, 10, 3.14f);
	//});

	std::unique_ptr<int> vp = std::make_unique<int>(9001);

	//f.pants(std::move(vp));

	emitter.call(&Foo::pants, std::forward<std::unique_ptr<int>>(vp), 20);
	//emitter.call(&Foo::pants, std::move(vp), vint);

	emitter.call(&Foo::bar, vint, 3.14f);

	emitter.call(&Foo::bar2, 1337, 3.14f);
	emitter.call(&Foo::baz, vint).then([](float&) {

	}).error([](std::exception_ptr e) {
		try {
			std::rethrow_exception(e);
		} catch (const std::exception& ee) {
			std::cerr << ee.what() << std::endl;
		}
	});
	
	listener.pull();
	emitter.pull();

	std::cout << std::endl;
}
