#pragma once

#include <string>
#include <vector>
#include "IControl.h"
#include "plugs/RetroPlug.h"
#include "EmulatorView.h"

class RetroPlugRoot : public IControl {
private:
	RetroPlug* _plug;
	std::vector<EmulatorView*> _views;
	EmulatorView* _active = nullptr;
	size_t _activeIdx = 0;

public:
	RetroPlugRoot(IRECT b, RetroPlug* plug);
	~RetroPlugRoot() {}

	void OnInit() override;

	bool OnKey(const IKeyPress& key, bool down);

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
		_active->OnMouseDblClick(x, y, mod);
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod);

	void Draw(IGraphics& g) override {}

private:
	void AddView(EmulatorView* view);

	void CreatePlugInstance(EmulatorView* view, CreateInstanceType type);

	void SetActive(EmulatorView* view);
};
