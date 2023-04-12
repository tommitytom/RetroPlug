#pragma once

#include <refl.hpp>
#include <spdlog/spdlog.h>
#include <entt/core/type_info.hpp>
#include "foundation/Types.h"

namespace fw {
	/**
 * Used with trait::map_t to provide storage type for the member.
 */
	template <typename Member>
	struct MakeRefStorage {
		using underlying_type = decltype(Member{}(std::declval<const typename Member::declaring_type&>()));
		using type = const refl::trait::remove_qualifiers_t<underlying_type>*;
	};

	/**
	 * A proxy which stores properties of the target type as const*.
	 */
	template <typename T>
	class RefProxy : public refl::runtime::proxy<RefProxy<T>, T> {
	public:
		// Fields and property getters.
		static constexpr auto members = filter(refl::member_list<T>{}, [](auto member) { return is_readable(member) && has_writer(member); });
		
		using member_list = std::remove_cv_t<decltype(members)>;
		using member_storage_list = refl::trait::map_t<MakeRefStorage, member_list>;

		refl::trait::as_tuple_t<member_storage_list> _data;
		
	public:
		RefProxy() {
			for_each(member_list{}, [&](auto member) {
				constexpr size_t idx = refl::trait::index_of_v<decltype(member), member_list>;
				refl::util::get<idx>(_data) = nullptr;
			});
		}
		
		RefProxy(const T& source) { *this = source; }
		
		RefProxy& operator=(const T& source) {
			for_each(member_list{}, [&](auto member) {
				constexpr size_t idx = refl::trait::index_of_v<decltype(member), member_list>;
				refl::util::get<idx>(_data) = &member(source);
			});

			return *this;
		}

		// Trap getter calls.
		template <typename Member, typename Self, typename... Args>
		static decltype(auto) invoke_impl(Self&& self) {
			static_assert(is_readable(Member{}));
			return self.template get<Member>();
		}

		// Trap setter calls.
		template <typename Member, typename Self, typename Value>
		static void invoke_impl(Self&& self, Value&& value) {
			static_assert(is_writable(Member{}));
			using getter_t = decltype(get_writer(Member{}));
			self.template get<getter_t>() = std::forward<Value>(value);
		}

		template <typename Member>
		void set(const typename Member::value_type* ptr) {
			constexpr size_t idx = refl::trait::index_of_v<Member, member_list>;
			static_assert(idx != -1);
			refl::util::get<idx>(_data) = ptr;
		}

		template <typename Member>
		auto& get() {
			constexpr size_t idx = refl::trait::index_of_v<Member, member_list>;
			static_assert(idx != -1);
			return *refl::util::get<idx>(_data);
		}

		template <typename Member>
		const auto& get() const {
			constexpr size_t idx = refl::trait::index_of_v<Member, member_list>;
			static_assert(idx != -1);
			return *refl::util::get<idx>(_data);
		}
	};
}
