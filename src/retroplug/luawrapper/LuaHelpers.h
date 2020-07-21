#pragma once

#include <string>
#include <vector>
#include <sol/sol.hpp>
#include "util/DataBuffer.h"

const std::string_view LUA_COMPONENT_PREFIX = "components.";

bool validateResult(const sol::protected_function_result& result, const std::string& prefix, const std::string& name = "");

static bool runFile(sol::state& state, const std::string& path) {
	sol::protected_function_result res = state.do_file(path);
	return validateResult(res, "Failed to load", path);
}

static bool runScript(sol::state& state, const std::string& script, const char* error = nullptr) {
	sol::protected_function_result res = state.do_string(script);
	if (error != nullptr) {
		return validateResult(res, error);
	} else {
		return validateResult(res, "Failed to run script", script);
	}
}

static bool requireComponent(sol::state& state, const std::string& path) {
	return runScript(state, "_loadComponent(\"" + path + "\")", "Failed to load component");
}

template <typename ...Args>
static bool callFunc(sol::state& state, const char* name, Args&&... args) {
	sol::protected_function f = state[name];
	sol::protected_function_result result = f(args...); // Use std::forward?
	return validateResult(result, "Failed to call", name);
}

template <typename ReturnType, typename ...Args>
static bool callFuncRet(sol::state& state, const char* name, ReturnType& ret, Args&&... args) {
	sol::protected_function f = state[name];
	sol::protected_function_result result = f(args...);
	if (validateResult(result, "Failed to call", name)) {
		ret = result.get<ReturnType>();
		return true;
	}

	return false;
}

template <typename ...Args>
static bool callFunc(sol::table& table, const char* name, Args&&... args) {
	sol::protected_function f = table[name];
	sol::protected_function_result result = f(table, args...); // Use std::forward?
	return validateResult(result, "Failed to call", name);
}

template <typename ReturnType, typename ...Args>
static bool callFuncRet(sol::table& table, const char* name, ReturnType& ret, Args&&... args) {
	sol::protected_function f = table[name];
	sol::protected_function_result result = f(table, args...);
	if (validateResult(result, "Failed to call", name)) {
		ret = result.get<ReturnType>();
		return true;
	}

	return false;
}

template <typename ReturnType>
static bool runScriptRet(sol::state& state, const std::string& script, ReturnType& ret, const char* error = nullptr) {
	sol::protected_function_result res = state->do_string(script);
	bool valid;
	if (error != nullptr) {
		valid = validateResult(res, error);
	} else {
		valid = validateResult(res, "Failed to run script", script);
	}

	if (valid) {
		auto t = res.get_type();
		ret = res.get<ReturnType>();
	}

	return valid;
}

void loadComponentsFromFile(sol::state& state, const std::string path);

void loadComponentsFromBinary(sol::state& state, const std::vector<std::string_view>& names);

void registerCommon(sol::state& s);
