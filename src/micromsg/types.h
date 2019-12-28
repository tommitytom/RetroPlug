#pragma once

#include <functional>

namespace micromsg {
	struct Envelope;
	class VariantFunction;
	class Allocator;

	using RequestHandlerFunc = std::function<Envelope* (Envelope*, VariantFunction&, Allocator&)>;
	using ResponseHandlerFunc = std::function<void (Envelope*, VariantFunction&)>;

	template <typename T>
	using IsPushType = std::is_same<typename T::Return, PushVoidT>;

	template <typename T>
	using RequestSignature = std::conditional_t<IsPushType<T>::value,
		void(const typename T::Arg&),
		void(const typename T::Arg&, typename T::Return&)
	>;

	template <typename T>
	using ResponseSignature = void(const typename T::Return&);

	template <typename T>
	struct TypeId {
		static size_t get() {
			return reinterpret_cast<size_t>(&_dummy);
		}

	private:
		static char _dummy;
	};

	template <typename T>
	char TypeId<T>::_dummy;
}
