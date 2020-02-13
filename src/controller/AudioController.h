#pragma once

#include "model/AudioLuaContext.h"
#include "model/ProcessingContext.h"

class AudioController {
private:
	AudioLuaContext _lua;
	ProcessingContext _processingContext;
	Node* _node;

public:
	AudioController(): _node(nullptr), _lua(&_processingContext) {}
	~AudioController() {}

	void setNode(Node* node);
	
	ProcessingContext* getProcessingContext() { return &_processingContext; }

	AudioLuaContext* getLuaContext() { return &_lua; }
};
