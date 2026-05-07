#include <tuple>
#include "micromsg/readerwriterqueue.h"

template <typename T, typename... Methods>
class Proxy {
private:
	struct VoidT {};
	using ErrorHandler = std::function<void(std::exception_ptr)>;

	template <typename R, typename... Args>
	struct StoredCall {
		using MethodSignature = R(T::*)(Args...);
		using ParamTuple = typename std::tuple<Args...>;
		using ReturnT = typename std::conditional_t<std::is_same<R, void>::value, VoidT, R>;
		using ReturnVariant = typename std::variant<std::function<void(ReturnT&)>, std::function<void()>>;

		MethodSignature method;
		ParamTuple params;
		ReturnT ret;
		ReturnVariant retHandler;
		std::exception_ptr error;
		ErrorHandler errorHandler;
	};

	template<typename S>
	struct StoredCallFromMethod;

	template <typename R, typename... Args>
	struct StoredCallFromMethod<R(T::*)(Args...)> {
		using type = typename StoredCall<R, Args...>;
	};

	template <typename S>
	using StoredCallFromMethodT = typename StoredCallFromMethod<S>::type;

	template <typename R, typename... Args>
	class Promise {
	private:
		using StoredCall = typename StoredCall<R, Args...>;
		StoredCall* _call;

		Promise(StoredCall* call) : _call(call) {}

	public:
		Promise& then(std::function<void()>&& cb) {
			//assert(!_call->retHandler); // Already has a return handler set
			_call->retHandler = std::move(cb);
			return *this;
		}

		Promise& then(std::function<void(typename StoredCall::ReturnT&)>&& cb) {
			//assert(!_call->retHandler); // Already has a return handler set
			_call->retHandler = std::move(cb);
			return *this;
		}

		Promise& error(ErrorHandler&& cb) {
			assert(!_call->errorHandler); // Already has an error handler set
			_call->errorHandler = std::move(cb);
			return *this;
		}

		friend class Proxy;
	};

	using CallVariant = typename std::variant<StoredCallFromMethodT<Methods>*...>;
	using CallQueue = typename moodycamel::ReaderWriterQueue<CallVariant>;

	CallQueue _outgoing;
	CallQueue _incoming;

public:
	template <typename R, typename... Args>
	inline Promise<R, Args...> call(R(T::* fn)(Args...), Args&&... args) {
		std::cout << "Calling with " << sizeof(StoredCall<R, Args...>) << " bytes" << std::endl;
		auto call = new StoredCall<R, Args...>{
			.method = fn,
			.params = std::forward_as_tuple(std::move(args)...)
		};

		_outgoing.enqueue(call);

		return Promise<R, Args...>(call);
	}

	template <typename R, typename... Args>
	inline Promise<R, Args...> call(R(T::* fn)(Args...), const Args&... args) {
		std::cout << "Calling with " << sizeof(StoredCall<R, Args...>) << " bytes" << std::endl;
		auto call = new StoredCall<R, Args...>{
			.method = fn,
			.params = std::forward_as_tuple(args...)
		};

		_outgoing.enqueue(call);

		return Promise<R, Args...>(call);
	}

	void pull() {
		// Here we receive the data that was used for previous outgoing calls.
		// In some cases the data may also contain return values, which are
		// passed to the relevant callbacks.  Data is free'd back to the allocator
		// that it was acquired from.

		CallVariant value;
		while (_incoming.try_dequeue(value)) {
			std::visit([this](auto&& call) {
				if (!call->error) {
					std::visit([this, &call](auto&& retFunc) {
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

				delete call;
			}, value);
		}
	}

	class Listener {
	private:
		T* _obj;
		CallQueue* _incoming;
		CallQueue* _outgoing;

	public:
		Listener(CallQueue* incoming, CallQueue* outgoing, T* obj = nullptr) 
			: _incoming(incoming), _outgoing(outgoing), _obj(obj) {}

		void pull() {
			CallVariant value;
			while (_incoming->try_dequeue(value)) {
				std::visit([this](auto&& arg) {
					try {
						if constexpr (std::is_same_v<decltype(arg->ret), VoidT>) {
							std::apply(
								arg->method,
								std::tuple_cat(
									std::forward_as_tuple(*_obj),
									std::forward<decltype(arg->params)>(arg->params)
								)
							);
						} else {
							arg->ret = std::apply(
								arg->method,
								std::tuple_cat(
									std::forward_as_tuple(*_obj),
									std::forward<decltype(arg->params)>(arg->params)
								)
							);
						}
					} catch (...) {
						arg->error = std::current_exception();
					}

					_outgoing->enqueue(arg);
				}, std::forward<CallVariant>(value));
			}
		}

		void setObj(T& obj) {
			_obj = &obj;
		}

		T& getObj() {
			return *_obj;
		}

		const T& getObj() const {
			return *_obj;
		}
	};

	Listener getListener() {
		return Listener(&_outgoing, &_incoming);
	}

	Listener getListener(T& obj) {
		return Listener(&_outgoing, &_incoming, &obj);
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

using FooProxy = Proxy<
	Foo, 
	decltype(&Foo::bar), 
	decltype(&Foo::bar2),
	decltype(&Foo::baz),
	decltype(&Foo::shit), decltype(&Foo::pants)
>;

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
