#include "SolUtil.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "foundation/generated/CompiledScripts.h"

using namespace fw;

void SolUtil::addIncludePath(sol::state& s, std::string_view path) {
	s["package"]["path"] = fmt::format("{};{}/?.lua", s["package"]["path"].get<std::string>(), path);
}

void SolUtil::prepareState(sol::state& s) {
	s.open_libraries(sol::lib::base, sol::lib::package, sol::lib::table, sol::lib::string, sol::lib::math);
	//SolUtil::addIncludePath(s, "../../src/scripts/utils");
	s.add_package_loader(CompiledScripts::utils::loader);
}

bool SolUtil::serializeTable(sol::state& s, const sol::table& source, std::string& target) {
	try {
		sol::protected_function_result serpent = s.script("return require('serpent').block");

		if (!serpent.valid()) {
			sol::error err = serpent;
			spdlog::error(err.what());
			return false;
		}

		sol::protected_function f = serpent.get<sol::protected_function>();
		sol::protected_function_result res = f(source, s.create_table_with("indent", '\t', "comment", false));

		if (!res.valid()) {
			sol::error err = res;
			spdlog::error(err.what());
			return false;
		}

		target = res.get<std::string>();
		return true;
	} catch (...) {
		spdlog::error("Failed to serialize table");
		return false;
	}
}

bool SolUtil::deserializeTable(sol::state& s, std::string_view data, sol::table& target) {
	try {
		sol::protected_function_result result = s.script("return require('serpent').load");

		if (!result.valid()) {
			sol::error err = result;
			spdlog::error(err.what());
			return false;
		}

		sol::protected_function f = result.get<sol::protected_function>();
		sol::protected_function_result res = f(data, s.create_table_with("safe", true));

		if (!res.valid()) {
			sol::error err = res;
			spdlog::error(err.what());
			return false;
		}

		auto out = res.get<std::tuple<bool, sol::table>>();

		if (!std::get<0>(out)) {
			spdlog::error("Failed to deserialize table");
			return true;
		}

		target = std::get<1>(out);
		return true;
	} catch (const std::exception& ex) {
		spdlog::error("Failed to deserialize table: {}", ex.what());
		return false;
	}
}
