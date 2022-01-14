#pragma once

#include "util/readerwriterqueue.h"

namespace rp {
	template <typename Message>
	struct MessageBus {
		struct MessageWrapper {
			Message message;
			bool garbage = false;
		};

		moodycamel::ReaderWriterQueue<MessageWrapper> nodeToProcessor;
		moodycamel::ReaderWriterQueue<MessageWrapper> processorToNode;
	};
}
