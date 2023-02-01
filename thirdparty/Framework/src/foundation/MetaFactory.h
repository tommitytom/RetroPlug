#pragma once

//#include <entt/meta/container.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>
#include <entt/core/hashed_string.hpp>

using namespace entt::literals;

#include "TypeInfo.h"

namespace fw {
	template <typename T>
	class MetaFactory {
	private:
		entt::meta_factory<T, T> _factory;
		size_t _fieldIdx = 0;

	public:
		MetaFactory() : MetaFactory(entt::type_id<T>().name()) {}
		MetaFactory(std::string_view name) : _factory(entt::meta<T>().type(entt::hashed_string(name.data(), name.size()))) {}

		entt::meta_factory<T, T>& getFactory() {
			return _factory;
		}

		template <auto Field, typename ...Property>
		MetaFactory& addField(std::string_view name, Property... property) {
			_factory
				.template data<Field, entt::as_ref_t>(entt::hashed_string(name.data(), name.size()))
				.props(
					std::make_pair("Name"_hs, name),
					std::make_pair("Order"_hs, _fieldIdx++),
					forwardProps<Property>(property)...
				);

			return *this;
		}

		MetaFactory& conv() {
			_factory.conv<int32>();
			return *this;
		}

		template<auto Setter, auto Getter, typename ...Property>
		MetaFactory& addField(std::string_view name, Property... property) {
			_factory
				.data<Setter, Getter, entt::as_ref_t>(entt::hashed_string(name.data(), name.size()));
			/*.props(
				std::make_pair("Name"_hs, name),
				std::make_pair("Order"_hs, _fieldIdx++),
				forwardProps<Property>(property)...
			);
			*/
			return *this;
		}

	private:
		template <typename Property>
		auto forwardProps(Property prop) {
			return std::make_pair(entt::type_id<Property>().hash(), prop);
		}
	};
}
