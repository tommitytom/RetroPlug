#pragma once

#include "foundation/RefProxy.h"

namespace fw {
	template <typename T>
	struct NodeEvent {};
	
	template <typename T>
	class NodeState {
	private:
		static const T::Input _defaultInput;
		
		T _state;
		fw::RefProxy<typename T::Input> _input;
		T::Output _output;

	public:
		using Input = fw::RefProxy<typename T::Input>;
		uint32 frameCount = 0;

		NodeState(): _input(_defaultInput) {}
		~NodeState() = default;

		T& get() { return _state; }

		const T& get() const { return _state; }

		//fw::RefProxy<T::Input>& input() { return _input; }

		fw::RefProxy<typename T::Input>& input() { return _input; }

		const fw::RefProxy<typename T::Input>& input() const { return _input; }

		typename T::Output& output() { return _output; }
		
		const typename T::Output& output() const { return _output; }
	};

	template <typename T> const typename T::Input NodeState<T>::_defaultInput;
}
