#include "AudioController.h"

void AudioController::setNode(Node* node) {
	_node = node;
	node->getAllocator()->reserveChunks(160 * 144 * 4, 16); // Video buffers

	_processingContext.setNode(node);

	node->on<calls::SwapLuaContext>([&](const AudioLuaContextPtr& ctx, AudioLuaContextPtr& other) {
		std::string componentData;
		if (_lua) {
			componentData = _lua->serializeSystems();
		}

		other = _lua;
		ctx->init(&_processingContext, _timeInfo, _sampleRate);
		//ctx->setSampleRate(_sampleRate);

		if (!componentData.empty()) {
			ctx->deserializeSystems(componentData);
		}

	_lua = ctx;
	});

	node->on<calls::SwapSystem>([&](const SystemSwapDesc& d, SystemSwapDesc& other) {
		assert(d.idx != -1);
		_lua->addSystem(d.idx, d.instance, *d.componentState);

		other.instance = _processingContext.swapSystem(d.idx, d.instance);
		other.componentState = d.componentState;
	});

	node->on<calls::DuplicateSystem>([&](const SystemDuplicateDesc& d, SameBoyPlugPtr& other) {
		other = _processingContext.duplicateSystem(d.sourceIdx, d.targetIdx, d.instance);
		_lua->duplicateSystem(d.sourceIdx, d.targetIdx, d.instance);
	});

	node->on<calls::ResetSystem>([&](const ResetSystemDesc& d) {
		_processingContext.resetSystem(d.idx, d.model);
	});

	node->on<calls::TakeSystem>([&](const SystemIndex& idx, SameBoyPlugPtr& other) {
		_lua->removeSystem(idx);
		other = _processingContext.removeSystem(idx);
	});

	node->on<calls::SetActive>([&](const SystemIndex& idx) {
		_lua->setActive(idx);
	});

	node->on<calls::UpdateProjectSettings>([&](const Project::Settings& settings) {
		_processingContext.setSettings(settings);
	});

	node->on<calls::EnableRendering>([&](const bool& enabled) {
		_processingContext.setRenderingEnabled(enabled);
	});

	node->on<calls::UpdateSystemSettings>([&](const SystemSettings& settings) {
		_processingContext.setSystemSettings(settings.idx, settings.settings);
	});

	node->on<calls::FetchState>([&](const FetchStateRequest& req, FetchStateResponse& state) {
		fetchState(req, state);
	});

	node->on<calls::SetRom>([&](const SetDataRequest& req, DataBufferPtr& ret) {
		SameBoyPlugPtr inst = _processingContext.getSystem(req.idx);
		if (inst) {
			inst->setRomData(req.buffer.get());
			if (req.reset) {
				inst->reset(inst->getSettings().model, true);
			}
		}

		ret = req.buffer;
	});

	node->on<calls::SetSram>([&](const SetDataRequest& req, DataBufferPtr& ret) {
		SameBoyPlugPtr inst = _processingContext.getSystem(req.idx);
		if (inst) {
			inst->loadSram(req.buffer->data(), req.buffer->size(), req.reset);
		}

		ret = req.buffer;
	});

	node->on<calls::SetState>([&](const SetDataRequest& req, DataBufferPtr& ret) {
		SameBoyPlugPtr inst = _processingContext.getSystem(req.idx);
		if (inst) {
			inst->loadState(req.buffer->data(), req.buffer->size());
		}

		ret = req.buffer;
	});

	node->on<calls::PressButtons>([&](const ButtonPressState& presses) {
		SameBoyPlugPtr& instance = _processingContext.getSystem(presses.idx);
		if (instance) {
			instance->pressButtons(presses.buttons.presses.data(), presses.buttons.pressCount);
		}
	});

	node->on<calls::ContextMenuResult>([&](const int& id) {
		_lua->onMenuResult(id);
	});
}

void AudioController::setAudioSettings(const AudioSettings& settings) {
	_processingContext.setAudioSettings(settings);
	_sampleRate = settings.sampleRate;

	if (_lua) {
		_lua->setSampleRate(settings.sampleRate);
	}
}

void AudioController::fetchState(const FetchStateRequest& req, FetchStateResponse& state) {
	for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
		if ((size_t)req.systems[i] & (size_t)ResourceType::Components) {
			if (_processingContext.getSystem(i)) {
				state.components[i] = _lua->serializeSystem(i);
			}
		}
	}

	_processingContext.fetchState(req, state);
}

bool AudioController::getSram(SystemIndex idx, DataBuffer<char>* target) {
	std::scoped_lock l(_lock);
	SameBoyPlugPtr instance = _processingContext.getSystem(idx);
	if (instance) {
		return instance->saveSram(target->data(), target->size());
	} else {
		std::cout << "Failed to fetch SRAM from instance " << idx << ": instance does not exist" << std::endl;
	}

	return false;
}

void AudioController::onMenu(SystemIndex idx, std::vector<Menu*>& menus) {
	auto ctx = _lua;
	if (ctx) {
		// TODO: This mutex is temporary until I find a good way of sending context menus
		// across threads!
		_lock.lock();
		ctx->onMenu(idx, menus);
		_lock.unlock();
	}
}

void AudioController::process(float** outputs, size_t frameCount) {
	auto ctx = _lua;
	// TODO: This mutex is temporary until I find a good way of sending context menus
	// across threads!
	_lock.lock();
	if (ctx && ctx->isValid()) {
		ctx->update(frameCount);
	}

	_processingContext.process(outputs, (size_t)frameCount);

	/*for (SystemIndex i = 0; i < MAX_SYSTEMS; ++i) {
		SameBoyPlugPtr& system = _processingContext.getSystem(i);

		if (system && _node->canPush<calls::SramChanged>()) {
			// TODO: It appears that doing the hash check is actually slower than just saving the SRAM out and sending it
			// to the UI thread!  Should probably just send the buffer every time and do the hash check in the UI thread
			if (system->sramHasChanged()) {
				DataBufferPtr sramData = std::make_shared<DataBuffer<char>>(system->sramSize());

				system->saveSram(sramData->data(), sramData->size());
				_node->push<calls::SramChanged>(NodeTypes::Ui, SetDataRequest {
					.idx = i,
					.buffer = sramData
				});
			}
		}
	}*/

	_lock.unlock();
}
