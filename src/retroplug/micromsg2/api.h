#include <tuple>

enum AudioCallTypes {
	LoadRom,
	TransmitVideo,
	SwapLuaContext
};

namespace async {
	class Hub {
	public:
		template <typename T>
		T* addNode(int id) { return nullptr; }
	};

	class PushBase {};
	class ReqBase {};

	template <const int CallType, typename ArgType>
	class Push : public PushBase {
	public:
		static const int CallType = CallType;
		using DelegateType = std::function<void(const ArgType&)>;
	};

	class InvalidT {};

	template <typename int CallType, typename ArgType, typename ReturnType>
	class Req : public ReqBase {
	public:
		static const int CallType = CallType;
		using DelegateType = std::function<void(const ArgType&, ReturnType&)>;
	};

	template <const int CallType, typename...> struct CallFind;
	template <const int CallType> struct CallFind<CallType> { using type = InvalidT; };

	template <const int CallType, typename Head, typename ...Tail>
	struct CallFind<CallType, Head, Tail...> {
		using type = typename std::conditional<
			Head::CallType == CallType,
			Head,
			typename CallFind<CallType, Tail...>::type
		>::type;
	};

	template <typename... Args>
	class Node {
	public:
		template <const int CallType>
		struct Call {
			using type = typename CallFind<CallType, Args...>::type;
			static_assert(!std::is_same<type, InvalidT>::value, "Invalid call type");
		};

		template <const int CallType>
		using CallT = typename Call<CallType>::type;

		template <const int CallType>
		void on(typename CallT<CallType>::DelegateType fn) {
			
		}
	};

	template <typename... Args>
	class ClassNode {
	};

}

struct LoadRomDesc {};
struct AudioLuaContextPtr {};

using Hub = async::Hub;
using AudioNode = async::Node<
	async::Push<LoadRom, LoadRomDesc>,
	async::Req<SwapLuaContext, AudioLuaContextPtr, AudioLuaContextPtr>
>;

enum FooMethods {
	Bar,
	Baz
};

class Foo {
public:
	void bar(int v) {}

	int baz() { return 1337; }
};

using FuncT = decltype(&Foo::bar);

// https://stackoverflow.com/questions/28509273/get-types-of-c-function-parameters

template<class...> struct types { using type = types; };

template<class Sig> struct args;

template<class R, class...Args>
struct args<R(Args...)> : types<Args...> {};

template<class Sig> using args_t = typename args<Sig>::type;

template <class...Params>
void some_function(types<Params...> vals) {
	
}

#include <iostream>

static void testFunc(int v, float t) {
	std::cout << "ff" << std::endl;
}

template <class...Params>
constexpr static std::tuple<Params...> call(Params... p) {
	return std::tuple<Params...>(p...);
}

template <typename R, typename... T>
constexpr std::tuple<T...> function_args(R(*)(T...)) {
	return std::tuple<T...>();
}

template<typename T, typename F>
void calltest3(T&& t, F&& f)
{
	f(std::forward<T>(t));
}

enum class NodeTypes {
	Audio,
	Ui
};


static void func() {
	async::Hub hub;
	AudioNode* node = hub.addNode<AudioNode>((int)NodeTypes::Audio);

	//call(Foo::bar, 20);

	auto t = call(20, 40.f);
	auto args = function_args(testFunc);

	std::apply(testFunc, t);


	//Foo f;
	//auto f2 = std::bind(&Foo::bar, f);
	
	//calltest3(f2, 20);
	
	//auto args2 = calltest2<Foo, &Foo::bar>(&f);

	/*some_function(args_t<decltype(testFunc)>{});

	using LoadRomT = AudioNode::CallT<LoadRom>;
	//using TransmitVideoT = AudioNode::Call<TransmitVideo>;
	using SwapLuaContextT = AudioNode::CallT<SwapLuaContext>;

	node->on<LoadRom>([](const LoadRomDesc&) {
		
	});*/
}
