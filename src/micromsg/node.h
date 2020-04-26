#pragma once

#include <functional>
#include <sstream>

#include "readerwriterqueue.h"
#include "allocator/allocator.h"
#include "request.h"
#include "envelope.h"

#include <type_traits>
#include <functional>

#include "magic_enum.hpp"
#include "platform.h"
#include "lookups.h"

#include "variantfunctionfactory.h"
#include "error.h"

namespace micromsg {
	const int RESPONSE_ID = 0;

	using RequestQueue = moodycamel::ReaderWriterQueue<Envelope*>;

	struct Responder {
		int callTypeId = -1;
		VariantFunction func;
	};

	template <typename NodeType>
	class Node {
	private:
		NodeType _type;
		RequestQueue* _sources[(int)NodeType::COUNT] = { nullptr };
		Node* _targets[(int)NodeType::COUNT] = { nullptr };
		Allocator* _alloc = nullptr;
		std::atomic<bool> _active = false;

		std::vector<VariantFunction> _callbacks;
		HandlerLookups* _handlers;

		std::vector<Responder> _responderLookup;
		std::vector<size_t> _responderStack;

		VariantFunctionFactory _funcFactory;

		template <typename> friend class NodeManager;

	public:
		void pull() {
			assert(isValid());
			assert(_active);

			for (int i = 0; i < (int)NodeType::COUNT; ++i) {
				RequestQueue* source = _sources[i];
				if (source) {
					Envelope* envelope;
					while (source->try_dequeue(envelope)) {
						assert(envelope->callTypeId < (int)_handlers->requests.size());

						if (envelope->callTypeId != RESPONSE_ID) {
							VariantFunction& v = _callbacks[envelope->callTypeId];
							if (v.isValid()) {
								RequestHandlerFunc& c = _handlers->requests[envelope->callTypeId];
								if (c != nullptr) {
									Envelope* outEnv = c(envelope, v, *_alloc);
									if (outEnv) {
										send((NodeType)i, outEnv);
									}
								} else {
									callTypeError(_type, envelope->callTypeId, _handlers, "handler");
									assert(false);
								}
							} else {
								callTypeError(_type, envelope->callTypeId, _handlers, "callback");
								assert(false);
							}
						} else {
							Responder& responder = _responderLookup[envelope->callId];
							assert(responder.callTypeId != -1);

							if (responder.callTypeId != -1) {
								ResponseHandlerFunc& c = _handlers->responses[responder.callTypeId];
								if (c != nullptr) {
									c(envelope, responder.func);
								} else {
									callTypeError(_type, envelope->callTypeId, _handlers, "handler");
									assert(false);
								}
								
								_responderStack.push_back(envelope->callId);
								responder.callTypeId = -1;
								_funcFactory.free(responder.func);
							}
						}

						_alloc->free(envelope);
					}
				}
			}
		}

		template <typename T>
		void on(std::function<typename RequestSignature<T>> func) {
			assert(!_active);

			size_t typeId = _handlers->typeIds[TypeId<T>::get()];
			mm_assert_m(typeId != 0, "Call type not found.  Did you remember to register your call?");

			std::stringstream ss;
			ss << magic_enum::enum_name(_type) << " added handler for " << typeId;
			std::cout << ss.str() << std::endl;

			_callbacks[typeId] = _funcFactory.alloc(func);
		}

		template <typename RequestT, std::enable_if_t<IsPushType<RequestT>::value, int> = 0>
		void broadcast(typename RequestT::Arg& message) {
			assert(_active);
			assert(isValid());
			for (int i = 0; i < (int)NodeType::COUNT; ++i) {
				if (_targets[i]) {
					push<RequestT>((NodeType)i, message);
				}
			}
		}

		template <typename RequestT, std::enable_if_t<IsPushType<RequestT>::value, int> = 0>
		bool canPush() {
			return _alloc->canAlloc<TypedEnvelope<RequestT::Arg>>();
		}

		template <typename RequestT, std::enable_if_t<IsPushType<RequestT>::value, int> = 0>
		bool push(NodeType target, typename RequestT::Arg& message) {
			if (!_active || !isValid()) {
				//assert(_active);
				//assert(isValid());
				return false;
			}

			TypedEnvelope<RequestT::Arg>* envelope = _alloc->alloc<TypedEnvelope<RequestT::Arg>>();
			if (!envelope) {
				//assert(envelope);
				return false;
			}
			
			envelope->sourceNodeId = (int)_type;
			envelope->callTypeId = (int)_handlers->typeIds[TypeId<RequestT>::get()];
			envelope->message = message; // TODO: Move semantics?

			if (envelope->callTypeId == 0) {
				mm_assert_m(envelope->callTypeId != 0, "Call type not found.  Did you remember to register your call?");
				return false;
			}

			return send(target, envelope);
		}

		template <typename RequestT, std::enable_if_t<!IsPushType<RequestT>::value, int> = 0>
		bool request(NodeType target, const typename RequestT::Arg& message, std::function<void(const typename RequestT::Return&)> cb) {
			assert(_active);
			assert(isValid());
			mm_assert_m(!_responderStack.empty(), "Maximum call count reached");

			if (!_active || !isValid() || _responderStack.empty()) {
				return false;
			}

			TypedEnvelope<RequestT::Arg>* envelope = _alloc->alloc<TypedEnvelope<RequestT::Arg>>();
			assert(envelope);
			if (envelope) {
				envelope->sourceNodeId = (int)_type;
				envelope->callTypeId = (int)_handlers->typeIds[TypeId<RequestT>::get()];
				envelope->message = message; // TODO: Move semantics?
				envelope->callId = _responderStack.back();
				_responderStack.pop_back();

				mm_assert_m(envelope->callTypeId != 0, "Call type not found.  Did you remember to register your call?");

				if (envelope->callTypeId != 0) {
					if (send(target, envelope)) {
						Responder& r = _responderLookup[envelope->callId];
						r.callTypeId = envelope->callTypeId;
						r.func = _funcFactory.alloc(cb);
						return true;
					}
				}
			}

			return false;
		}

		void waitUntilActive() {
			while (_active == false) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		bool isActive() {
			return _active;
		}

		bool isValid() const {
			return _alloc != nullptr;
		}

		size_t remainingRequests() const {
			return _responderStack.size();
		}

		Allocator* getAllocator() const {
			return _alloc;
		}

	private:
		void setup(NodeType type, Allocator* alloc, HandlerLookups* handlers) {
			_type = type;
			_alloc = alloc;
			_handlers = handlers;
			_callbacks.resize(handlers->requests.size());
			_funcFactory.init(handlers->responseCount);
			_responderLookup.resize(handlers->responseCount);

			_responderStack.reserve(handlers->responseCount);
			for (int i = (int)handlers->responseCount - 1; i > 0; --i) {
				_responderStack.push_back(i);
			}
		}

		void setTarget(NodeType nodeType, Node<NodeType>* target) {
			assert(!_active);
			_targets[(int)nodeType] = target;
		}

		void setSource(NodeType nodeType, RequestQueue* source) {
			assert(!_active);
			_sources[(int)nodeType] = source;
		}

		void setActive() {
			assert(!_active);
			_active = true;
		}

		bool send(NodeType target, Envelope* message) {
			assert((int)target < (int)NodeType::COUNT);
			Node* targetNode = _targets[(int)target];
			assert(targetNode);
			RequestQueue* targetQueue = targetNode->_sources[(int)_type];
			assert(targetQueue);
			return targetQueue->try_enqueue(message);
		}
	};
}
