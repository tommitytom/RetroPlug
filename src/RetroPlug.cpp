#include "RetroPlug.h"

#include <sol/sol.hpp>

#include <iostream>

RetroPlug::RetroPlug() { 
	_lua = new sol::state();
	_lua->open_libraries(sol::lib::base, sol::lib::package);

	std::string packagePath = (*_lua)["package"]["path"];
	(*_lua)["package"]["path"] = (packagePath + ";../src/scripts/?.lua").c_str();

	sol::load_result rootFile = _lua->load_file("../src/scripts/plug.lua");
	if (!rootFile.valid()) {
		sol::error err = rootFile;
		std::string what = err.what();
		std::cout << "call failed, sol::error::what() is " << what << std::endl;
		return;
	}

	sol::protected_function_result rootRes = rootFile();
	if (!rootRes.valid()) {
		sol::error err = rootRes;
		std::string what = err.what();
		std::cout << "call failed, sol::error::what() is " << what << std::endl;
		return;
	}

	_lua->new_usertype<SameBoyPlug>("SameBoyPlug", 
		"setButtonState", &SameBoyPlug::setButtonStateT
	);

	_lua->new_usertype<iplug::igraphics::IKeyPress>("IKeyPress", 
		"vk", &iplug::igraphics::IKeyPress::VK,
		"shift", &iplug::igraphics::IKeyPress::S,
		"ctrl", &iplug::igraphics::IKeyPress::C,
		"alt", &iplug::igraphics::IKeyPress::A
	);

	std::cout << std::filesystem::current_path().string() << std::endl;
}

RetroPlug::~RetroPlug() {
	delete _lua;
}

void RetroPlug::setActive(SameBoyPlugPtr active) { 
	_active = active; 
	_lua->set("active", active);
}

void RetroPlug::onKey(const iplug::igraphics::IKeyPress& key, bool down) {
	(*_lua)["onKey"](key, down);
}

void RetroPlug::clear() {
	_projectPath.clear();
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		_plugs[i] = nullptr;
	}
}

SameBoyPlugPtr RetroPlug::addInstance(EmulatorType emulatorType) {
	SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
	plug->setSampleRate(_sampleRate);

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (!_plugs[i]) {
			_plugs[i] = plug;
			break;
		}
	}

	return plug;
}

void RetroPlug::removeInstance(size_t idx) {
	for (size_t i = idx; i < MAX_INSTANCES - 1; i++) {
		_plugs[i] = _plugs[i + 1];
	}

	_plugs[MAX_INSTANCES - 1] = nullptr;
}

size_t RetroPlug::instanceCount() const {
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (!_plugs[i]) {
			return i;
		}
	}

	return 4;
}

void RetroPlug::getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore) {
	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		if (_plugs[i] && _plugs[i] != ignore && _plugs[i]->active() && _plugs[i]->gameLink()) {
			targets.push_back(_plugs[i]);
		}
	}
}

void RetroPlug::updateLinkTargets() {
	size_t count = instanceCount();
	std::vector<SameBoyPlugPtr> targets;
	for (size_t i = 0; i < count; i++) {
		auto target = _plugs[i];
		if (target->active() && target->gameLink()) {
			targets.clear();
			getLinkTargets(targets, target);
			target->setLinkTargets(targets);
		}
	}
}

void RetroPlug::setSampleRate(double sampleRate) {
	_sampleRate = sampleRate;

	for (size_t i = 0; i < MAX_INSTANCES; i++) {
		SameBoyPlugPtr plugPtr = _plugs[i];
		if (plugPtr) {
			plugPtr->setSampleRate(sampleRate);
		}
	}
}
