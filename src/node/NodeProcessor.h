#pragma once 

#include <memory>

#include "platform/Types.h"
#include "MessageBus.h"
#include "Ports.h"
#include "NodeBase.h"

namespace rp {
	class NodeProcessorBase : public NodeBase {};

	template <typename Message = std::variant<std::monostate>>
	class NodeProcessor : public NodeProcessorBase {
	public:
		using MessageType = Message;
		using NodeMessageBus = MessageBus<Message>;

	private:
		NodeMessageBus* _messageBus = nullptr;
		
		NodeLifecycleState _lifecycle = NodeLifecycleState::Created;

	public:
		NodeProcessor() {}

		void setMessageBus(NodeMessageBus* messageBus) {
			_messageBus = messageBus;
		}

	protected:
		template <typename Func>
		void processMessages(Func&& f) {
			typename NodeMessageBus::MessageWrapper message;

			while (_messageBus->nodeToProcessor.try_dequeue(message)) {
				if (!message.garbage) {
					std::visit(f, message.message);

					message.garbage = true;
					_messageBus->processorToNode.try_emplace(std::move(message));
				}
			}
		}
	};

	using NodeProcessorPtr = std::shared_ptr<NodeProcessorBase>;
}
