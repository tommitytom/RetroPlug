#include <tuple>

#define Method(func) decltype(func)

class Foo {
public:
	void bar(int v, float v2) {
		std::cout << "BAR! " << v << " " << v2 << std::endl;
	}

	int baz() { return 1337; }
};

using FuncT = decltype(&Foo::bar);

// https://stackoverflow.com/questions/28509273/get-types-of-c-function-parameters

#include <iostream>

template<typename T, typename F>
void calltest3(T&& t, F&& f)
{
	f(std::forward<T>(t));
}

struct StoredCallBase {
};

template <typename T, typename R, typename... Args>
struct StoredCall {
	R(T::*method)(Args...);
	std::tuple<Args...> data;
};

template <typename T, typename R, typename... Args>
struct StoredCallSplit {
	//using type = StoredCall
};

template <typename T, typename Method1, typename Method2>
class ProxyBase {
private:
	//std::vector<std::variant<StoredCall>> _queue;
	std::variant<StoredCall<T, void, int, float>, StoredCall<T, int>> _data;

public:
	T* _obj = nullptr;

	template <typename R, typename... Args>
	std::tuple<Args...> call(R(T::*fn)(Args...), Args&&... args) {
		_data = StoredCall<T, R, Args...> { fn, std::tuple<Args...>(args...) };
		return std::tuple<Args...>(args...);
	}

	void callDeferred() {
		std::visit([this](auto&& arg) {
			std::apply(arg.method, std::tuple_cat(std::make_tuple(*_obj), arg.data));
		}, _data);
	}
};

template <typename T>
class ListenerBase {
private:
	T* _obj = nullptr;

public:
	ListenerBase(T& obj): _obj(&obj) {}

	template <typename R, typename... Args>
	void call(R(T::*fn)(Args...), std::tuple<Args...> params) {
		std::apply(fn, std::tuple_cat(std::make_tuple(*_obj), params));
	}
};

template <typename T, typename... Methods>
struct ClassWrap {
	
};

using FooMethods = ClassWrap<Foo, Method(&Foo::bar), Method(&Foo::baz)>;



static void func() {
	Foo f;

	//std::variant<FooMethods::type> varTest;
	
	ProxyBase<Foo, Method(&Foo::bar), Method(&Foo::baz)> emitter;
	emitter._obj = &f;

	auto params = emitter.call(&Foo::bar, 10, 3.14f);
	emitter.callDeferred();
	
	//ListenerBase<Foo> listener(f);
	//listener.call(&Foo::bar, params);

	//std::apply(&Foo::bar, std::tuple_cat(std::make_tuple(f), params));

	//auto f2 = std::bind(&Foo::bar, f);
	//std::apply(f2, t);
	
	//calltest3(f2, 20);
	
	//auto args2 = calltest2<Foo, &Foo::bar>(&f);

	//some_function(args_t<decltype(testFunc)>{});
}
