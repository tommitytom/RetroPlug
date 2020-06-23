#pragma once

#include <memory>
#include "platform.h"

// TODO: Look at replacing this with std::variant/std::function/std::visit

namespace micromsg {
	class FunctionWrapperBase {
	public:
		virtual ~FunctionWrapperBase() {}
		virtual void clear() = 0;
	};

	template <class T>
	class FunctionWrapper : public FunctionWrapperBase {
	public:
		std::function<T> func;
		FunctionWrapper() {}
		FunctionWrapper(std::function<T> f) : func(f) {}
		
		void clear() override { func = nullptr; }
	};

	class VariantFunction {
	private:
		FunctionWrapperBase* _func = nullptr;

	public:
		VariantFunction() {}
		VariantFunction(VariantFunction& other) {
			_func = other._func;
			other._func = nullptr;
		}

		template <typename T>
		VariantFunction(FunctionWrapper<T>* func) {
			set(func);
		}

		~VariantFunction() {}

		bool isValid() const { return _func != nullptr; }

		template <typename T>
		void set(FunctionWrapper<T>* func) {
			_func = func;
		}

		FunctionWrapperBase* getRaw() {
			return _func;
		}

		FunctionWrapperBase* take() {
			FunctionWrapperBase* t = _func;
			_func = nullptr;
			return t;
		}

		template <class T>
		FunctionWrapper<T>* get() const {
			assert(_func != nullptr);
#ifdef RTTI_ENABLED
			//assert(dynamic_cast<FunctionWrapper<T>*>(_func));
#endif
			return static_cast<FunctionWrapper<T>*>(_func);
		}

		VariantFunction& operator=(VariantFunction& other) {
			_func = other._func;
			other._func = nullptr;
			return *this;
		}
	};
}

