#pragma once

#include <entt/core/any.hpp>
#include "foundation/RefProxy.h"

namespace fw {
	template <typename T>
	struct NodeEvent {};

	class NodeStateBase;
	using NodeStatePtr = std::shared_ptr<NodeStateBase>;

	template <typename T>
	struct FullNodeState {
		T state;
		T::Input input;
		T::Output output;
	};
	
	class NodeStateBase {
	public:
		virtual entt::any getFullState() const = 0;
	};	
	
	template <typename T>
	class NodeState : public NodeStateBase {
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

		entt::any getFullState() const override {			
			typename T::Input input;

			for_each(refl::reflect<typename T::Input>().members, [&](auto member) {
				//auto v = member(_input);
				//member(input) = *member(_input);
			});
			
			return FullNodeState<T>{ 
				.state = _state, 
				.input = input, 
				.output = _output
			};
		}
		
		/*void clone(std::byte* target, size_t targetSize) const override {
			new (target) NodeState<T>(*this);
		}*/

		T& get() { return _state; }

		const T& get() const { return _state; }

		//fw::RefProxy<T::Input>& input() { return _input; }

		fw::RefProxy<typename T::Input>& input() { return _input; }

		const fw::RefProxy<typename T::Input>& input() const { return _input; }

		typename T::Output& output() { return _output; }
		
		const typename T::Output& output() const { return _output; }

	private:
		template <typename R>
		R refProxyToType(const fw::RefProxy<R>& other) {
			R ret;

			for_each(refl::reflect<R>().members, [&](auto member) {
				member(ret) = *member(other);
			});

			return ret;
		}
	};

	template <typename T> const typename T::Input NodeState<T>::_defaultInput;

	

	template <typename T>
	FullNodeState<T> createFullNodeState(const NodeState<T>& other) {
		auto conv = refProxyToType(other.input());
		return FullNodeState<T>{ other.get(), conv, other.output() };
	}

	
}
