#pragma once 

#include <memory>
#include <variant>

#include "foundation/Types.h"
#include "MessageBus.h"
#include "NodeProcessor.h"
#include "NodeBase.h"

namespace rp {
	template <typename Processor>
	class Node : public NodeBase {
	public:
		using Message = typename Processor::MessageType;
		using MessageWrapper = typename MessageBus<Message>::MessageWrapper;

	private:
		MessageBus<Message> _messageBus;

	public:
		std::shared_ptr<Processor> createProcessor() {
			std::shared_ptr<Processor> processor = std::make_shared<Processor>();
			processor->setMessageBus(&_messageBus);

			for (auto& input : processor->getInputs()) {
				addInputCopy(input.name, input.defaultValue);
			}

			for (auto& output : processor->getOutputs()) {
				addOutputCopy(output.name, output.data);
			}

			setAlwaysActive(processor->isAlwaysActive());

			processor->initialize();
			onProcessorInitialized(*processor);

			return processor;
		}

	protected:
		virtual void onProcessorInitialized(Processor& processor) {}

		void sendMessage(Message&& message) {
			_messageBus.nodeToProcessor.emplace(MessageWrapper {
				.message = std::move(message) 
			});
		}

		template <typename Func>
		void processMessages(Func&& f) {
			MessageWrapper message;

			while (_messageBus.processorToNode.try_dequeue(message)) {
				if (!message.garbage) {
					std::visit(f, message.message);
				}
			}
		}
	};

	using NodePtr = std::shared_ptr<NodeBase>;
}
