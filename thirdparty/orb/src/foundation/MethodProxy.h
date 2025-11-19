#include <type_traits>
#include <functional>
#include <iostream>
#include <variant>
#include "readerwriterqueue.h"

namespace MethodProxy {
	class BasicAllocator {
	public:
		void* alloc(size_t size) {
			return ::malloc(size);
		}

		void free(void* ptr) {
			::free(ptr);
		}
	};

	struct VoidT {};
	using ErrorHandler = std::function<void(std::exception_ptr)>;

	/////////////////////////////////////////////////////////////////
	// Utilities for providing compile time information about methods
	// Taken from entt (https://github.com/skypjack/entt)

	template<typename, bool, bool>
	struct FunctionMeta;

	template<typename Ret, typename... Args, bool Const, bool Static>
	struct FunctionMeta<Ret(Args...), Const, Static> {
		using ReturnType = typename std::conditional_t<std::is_same<Ret, void>::value, VoidT, Ret>;
		using ParamTuple = typename std::tuple<Args...>;
	};

	template<typename Type, typename Ret, typename... Args, typename Class>
	constexpr FunctionMeta<std::conditional_t<std::is_same_v<Type, Class>, Ret(Args...), Ret(Class&, Args...)>, true, !std::is_same_v<Type, Class>>
		toFunctionMeta(Ret(Class::*)(Args...) const);


	template<typename Type, typename Ret, typename... Args, typename Class>
	constexpr FunctionMeta<std::conditional_t<std::is_same_v<Type, Class>, Ret(Args...), Ret(Class&, Args...)>, false, !std::is_same_v<Type, Class>>
		toFunctionMeta(Ret(Class::*)(Args...));


	template<typename Type, typename Ret, typename... Args>
	constexpr FunctionMeta<Ret(Args...), false, true> toFunctionMeta(Ret(*)(Args...));

	template<typename Type>
	constexpr void toFunctionMeta(...);


	template<typename Type, typename Candidate>
	using FunctionMetaT = decltype(toFunctionMeta<Type>(std::declval<Candidate>()));

	/////////////////////////////////////////////////////////////////
	// Templatized structs for storing calls

	template <typename T>
	struct StoredCallBase {
		using MethodFunction = std::function<void(T&)>;
		using ReturnFunction = std::function<void()>;

		MethodFunction caller;
		ReturnFunction retHandler;

		std::exception_ptr error;
		ErrorHandler errorCallback;
	};

	template <typename T, auto Func>
	struct StoredCall : public StoredCallBase<T> {
	public:
		using ParamTuple = typename FunctionMetaT<T, decltype(Func)>::ParamTuple;
		using ReturnT = typename FunctionMetaT<T, decltype(Func)>::ReturnType;
		using ReturnCallback = typename std::function<void(ReturnT&)>;

		ParamTuple params;
		ReturnT ret;
		ReturnCallback retCallback;
	};

	template <typename T, auto Func>
	class Promise {
	private:
		using StoredCall = StoredCall<T, Func>;

		StoredCall* _call;
		bool _resolved;

	public:
		Promise() : _call(nullptr), _resolved(false) {}
		Promise(const Promise& other) : _call(other._call), _resolved(other._resolved) {}
		Promise(StoredCall* call, bool resolved) : _call(call), _resolved(resolved) {}

		inline Promise then(std::function<void()>&& cb) {
			if (_resolved) {
				if (_call->error == nullptr) {
					cb();
				}
			} else {
				_call->retHandler = std::move(cb);
			}

			return Promise(_call, _resolved);
		}

		inline Promise then(std::function<void(typename StoredCall::ReturnT&)>&& cb) {
			if (_resolved) {
				if (_call->error == nullptr) {
					cb(_call->ret);
				}
			} else {
				_call->retCallback = std::move(cb);
				_call->retHandler = [call = _call]() { call->retCallback(call->ret); };
			}

			return Promise(_call, _resolved);
		}

		inline Promise error(ErrorHandler&& cb) {
			if (_resolved) {
				if (_call->error != nullptr) {
					cb(_call->error);
				}
			} else {
				_call->errorCallback = std::move(cb);
			}

			return Promise(_call, _resolved);
		}
	};


	template <typename T>
	class Sync {
	private:
		char* _storedCallBuffer = nullptr;
		size_t _storedCallSize = 0;

	public:
		Sync(size_t storedCallSize = 512) {
			if (storedCallSize > 0) {
				_storedCallBuffer = new char[storedCallSize];
				_storedCallSize = storedCallSize;
			}
		}
		
		Sync(Sync&& other) noexcept {
			_storedCallBuffer = other._storedCallBuffer;
			_storedCallSize = other._storedCallSize;
			other._storedCallBuffer = nullptr;
			other._storedCallSize = 0;
		}

		~Sync() {
			if (_storedCallBuffer) {
				delete[] _storedCallBuffer;
			}
		}

		template <auto Func, typename... Args>
		inline Promise<T, Func> call(T& target, Args&&... args) {
			using StoredCall = StoredCall<T, Func>;

			StoredCall* storedCall = allocStoredCall<Func>();

			try {
				if constexpr (std::is_same_v<typename StoredCall::ReturnT, VoidT>) {
					std::invoke(Func, target, std::forward<Args>(args)...);
				} else {
					storedCall->ret = std::invoke(Func, target, std::forward<Args>(args)...);
				}
			} catch (...) {
				storedCall->error = std::current_exception();
			}

			return Promise<T, Func>(storedCall, true);
		}

		void receiverPull(T& target) {}

		void transmitterPull() {}

	private:
		template <auto Func>
		StoredCall<T, Func>* allocStoredCall() {
			using StoredCall = StoredCall<T, Func>;

			if (sizeof(StoredCall) > _storedCallSize) {
				_storedCallSize = sizeof(StoredCall);
				_storedCallBuffer = new char[_storedCallSize];
				memset(_storedCallBuffer, 0, _storedCallSize);
			}

			return new (_storedCallBuffer) StoredCall();
		}
	};

	template <typename T, auto Func>
	constexpr auto createCallTuple(T& obj, StoredCall<T, Func>* call) {
		return std::tuple_cat(
			std::forward_as_tuple(obj),
			std::forward<typename StoredCall<T, Func>::ParamTuple>(call->params)
		);
	}

	template <typename T>
	struct Async {
	private:
		using CallQueue = typename moodycamel::ReaderWriterQueue<StoredCallBase<T>*>;

		CallQueue _rcvToTrx;
		CallQueue _trxToRcv;
		BasicAllocator* _allocator = new BasicAllocator();

	public:
		template <auto Func, typename... Args>
		inline Promise<T, Func> call(T& target, Args&&... args) {
			StoredCall<T, Func>* call = allocStoredCall<Func>({
				.params = std::forward_as_tuple(std::move(args)...)
			});

			call->caller = createCaller(call);
			_trxToRcv.enqueue(call);

			return Promise<T, Func>(call, false);
		}

		template <auto Func, typename... Args>
		inline Promise<T, Func> call(T& target, const Args&... args) {
			StoredCall<T, Func>* call = allocStoredCall<Func>({
				.params = std::forward_as_tuple(args...)
			});

			call->caller = createCaller(call);
			_trxToRcv.enqueue(call);

			return Promise<T, Func>(call, false);
		}

		void transmitterPull() {
			// Here we receive the data that was used for previous outgoing calls.
			// In some cases the data may also contain return values, which are
			// passed to the relevant callbacks.  Data is free'd back to the allocator
			// that it was acquired from.

			StoredCallBase<T>* call;
			while (_rcvToTrx.try_dequeue(call)) {
				if (!call->error) {
					if (call->retHandler) {
						call->retHandler();
					}
				} else if (call->errorCallback) {
					call->errorCallback(call->error);
				} else {
					std::rethrow_exception(call->error);
				}

				_allocator->free(call);
			}
		}

		void receiverPull(T& target) {
			StoredCallBase<T>* call;
			while (_trxToRcv.try_dequeue(call)) {
				call->caller(target);
				_rcvToTrx.enqueue(call);
			}
		}

	private:
		template <auto Func>
		StoredCall<T, Func>* allocStoredCall(StoredCall<T, Func>&& callData) {
			using StoredCall = StoredCall<T, Func>;
			void* data = _allocator->alloc(sizeof(StoredCall));
			return new (data) StoredCall(std::forward<StoredCall>(callData));
		}

		// Creates the function that is called by the receiver
		template <auto Func>
		std::function<void(T&)> createCaller(StoredCall<T, Func>* call) const {
			// Type erasure
			return [call](T& obj) {
				try {
					if constexpr (std::is_same_v<typename StoredCall<T, Func>::ReturnT, VoidT>) {
						std::apply(Func, createCallTuple<T, Func>(obj, call));
					} else {
						call->ret = std::apply(Func, createCallTuple<T, Func>(obj, call));
					}
				} catch (...) {
					call->error = std::current_exception();
				}
			};
		}
	};

	template <typename T, typename ...ProxyTypes>
	struct BasicProxy {
	private:
	public:
		// TODO: delete constructors

		class Transmitter {
		private:
			T* _target;
			std::variant<ProxyTypes...>* _caller;

		public:
			Transmitter(): _target(nullptr), _caller(nullptr) {}
			Transmitter(T* target, std::variant<ProxyTypes...>* caller) : _target(target), _caller(caller) {}
			~Transmitter() {}

			template <auto Func, typename... Args>
			inline Promise<T, Func> call(Args&&... args) {
				assert(_target && _caller);

				Promise<T, Func> res;

				std::visit([&](auto&& v) {
					res = v.template call<Func>(*_target, std::forward<Args>(args)...);
				}, *_caller);

				return res;
			}

			inline void pull() {
				assert(_target && _caller);

				std::visit([&](auto&& v) { 
					v.transmitterPull(); 
				}, *_caller);
			}
		};

		class Receiver {
		private:
			std::vector<std::variant<ProxyTypes...>> _transmitters;
			T* _target;

		public:
			Receiver(T* target) : _target(target) { _transmitters.reserve(10); }
			~Receiver() {}

			template <typename ProxyType>
			Transmitter createTransmitter(ProxyType proxyType = ProxyType()) {
				_transmitters.push_back(std::move(proxyType));
				return Transmitter(_target, &_transmitters.back());
			}

			inline void pull() {
				for (std::variant<ProxyTypes...>& var : _transmitters) {
					std::visit([&](auto&& v) { v.receiverPull(*_target); }, var);
				}
			}
		};
	};

	template <typename T, typename ...ProxyTypes>
	using Proxy = BasicProxy<T, Sync<T>, Async<T>, ProxyTypes...>;
}
