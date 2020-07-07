#include <tuple>
#include "micromsg/readerwriterqueue.h"

template <typename T, typename... Methods>
class Proxy {
private:
	struct VoidT {};

	template<typename S>
	struct StoredCall;

	template <typename R, typename... Args>
	struct StoredCall<R(T::*)(Args...)> {
		using ReturnT = typename std::conditional_t<std::is_same<R, void>::value, VoidT, R>;

		size_t callId;
		R(T::* method)(Args...);
		std::tuple<Args...> params;
		//std::function<void(ReturnT&)> retHandler;
		ReturnT ret;
	};

	using CallVariant = typename std::variant<StoredCall<Methods>*...>;
	using CallQueue = typename moodycamel::ReaderWriterQueue<CallVariant>;

	CallQueue _outgoing;
	CallQueue _incoming;
	size_t _callId = 0;

public:
	template <typename R, typename... Args>
	void call(R(T::*fn)(Args...), Args&&... args) {
		using StoredCallT = StoredCall<R(T::*)(Args...)>;

		StoredCallT* stored = (StoredCallT*)malloc(sizeof(StoredCallT));
		*stored = StoredCallT{ _callId++, fn, std::forward_as_tuple(args...) };

		_outgoing.enqueue(stored);
	}

	template <typename R, typename... Args>
	void callRet(R(T::*fn)(Args...), std::function<void(int&)> cb, Args&&... args) {
		using StoredCallT = StoredCall<R(T::*)(Args...)>;

		StoredCallT* stored = (StoredCallT*)malloc(sizeof(StoredCallT));
		*stored = StoredCallT{ _callId++, fn, std::forward_as_tuple(args...), cb };

		_outgoing.enqueue(stored);
	}

	template <typename R, typename... Args>
	R callSync(R(T::* fn)(Args...), Args&&... args) {
		using StoredCallT = StoredCall<R(T::*)(Args...)>;

		StoredCallT* stored = (StoredCallT*)malloc(sizeof(StoredCallT));
		*stored = StoredCallT{ fn, std::forward_as_tuple(args...) };

		_outgoing.enqueue(stored);
	}

	void pull() {
		CallVariant value;
		while (_incoming.try_dequeue(value)) {
			std::visit([this](auto&& arg) {
				constexpr bool HasReturn = std::is_same_v<decltype(arg->ret), VoidT>;
				if constexpr (!HasReturn) {
					// Handle return type
				}

				free(arg);
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
					if constexpr (std::is_same_v<decltype(arg->ret), VoidT>) {
						std::apply(arg->method, std::tuple_cat(std::forward_as_tuple(*_obj), arg->params));
					} else {
						arg->ret = std::apply(arg->method, std::tuple_cat(std::forward_as_tuple(*_obj), arg->params));
					}

					_outgoing->enqueue(arg);
				}, value);
			}
		}

		void setObj(T& obj) {
			_obj = &obj;
		}

		typename T& getObj() {
			return *_obj;
		}

		typename const T& getObj() const {
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

public:
	void bar(int v, float v2) {
		std::cout << "BAR! " << v << " " << v2 << std::endl;
		_text = std::to_string(v);
	}

	int baz() {
		std::cout << _text << std::endl;
		return 1337;
	}
};

using FooProxy = Proxy<
	Foo, 
	decltype(&Foo::bar), 
	decltype(&Foo::baz)
>;

static void func() {
	Foo f;
	
	FooProxy emitter;
	FooProxy::Listener listener = emitter.getListener(f);

	emitter.call(&Foo::bar, 10, 3.14f);
	emitter.call(&Foo::bar, 1337, 3.14f);
	emitter.call(&Foo::baz);

	/*emitter.callRet(&Foo::baz, [](int& v) {

	});*/

	listener.pull();
	emitter.pull();

	std::cout << std::endl;
}
