#pragma once

#include "envelope.h"
#include "allocator/allocator.h"
#include "variant_function.h"
#include <iostream>
#include <assert.h>

namespace micromsg {
	template <typename T>
	static inline Envelope* requestHandler(Envelope* env, VariantFunction& handler, Allocator& alloc) {
		TypedEnvelope<typename T::Arg>* w = static_cast<TypedEnvelope<typename T::Arg>*>(env);
		auto* f = handler.get<RequestSignature<T>>();
		assert(f);

		if constexpr (IsPushType<T>::value) {
			f->func(w->message);
			return nullptr;
		} else {
			TypedEnvelope<typename T::Return>* outEnv = alloc.alloc<TypedEnvelope<typename T::Return>>();
			outEnv->callTypeId = 0;
			outEnv->callId = env->callId;
			f->func(w->message, outEnv->message);
			return outEnv;
		}
	}

	template <typename T>
	static inline void responseHandler(Envelope* env, VariantFunction& handler) {
		TypedEnvelope<typename T::Return>* w = static_cast<TypedEnvelope<typename T::Return>*>(env);
		auto* f = handler.get<ResponseSignature<T>>();
		assert(f);
		f->func(w->message);
	}

	static Envelope* requestError(Envelope* env, VariantFunction& handler, Allocator& alloc) {
		std::cout << "Caller not defined" << std::endl;
		assert(false);
		return nullptr;
	}

	static void responseError(Envelope* env, VariantFunction& handler) {
		std::cout << "Caller not defined" << std::endl;
		assert(false);
	}
}
