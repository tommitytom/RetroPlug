#pragma once

#include "variant_function.h"
#include "fixedstack.h"
#include <stack>
#include <functional>

namespace micromsg {
	class VariantFunctionFactory {
	private:
		FunctionWrapper<void()>* _data = nullptr;
		std::vector<FunctionWrapperBase*> _scratch;

	public:
		void init(size_t size) {
			_data = new FunctionWrapper<void()>[size];

			_scratch.reserve(size);
			for (size_t i = 0; i < size; ++i) {
				_scratch.push_back(static_cast<FunctionWrapperBase*>(_data + i));
			}
		}

		template <typename T>
		VariantFunction alloc(std::function<T> f) {
			static_assert(sizeof(FunctionWrapper<T>) == sizeof(FunctionWrapper<void()>), "Function wrapper size inconsistent");
			assert(!_scratch.empty());
			FunctionWrapper<T>* d = reinterpret_cast<FunctionWrapper<T>*>(_scratch.back());
			_scratch.pop_back();
			d->func = f;
			return VariantFunction(d);
		}

		void free(VariantFunction f) {
			auto p = f.take();
			p->clear();
			_scratch.push_back(p);
		}
	};
}