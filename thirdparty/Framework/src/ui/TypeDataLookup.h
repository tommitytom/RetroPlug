#pragma once

#include <unordered_map>
#include <entt/core/any.hpp>

namespace fw {
	// Maps a type to a piece of data of that type
	class TypeDataLookup {
	private:
		std::unordered_map<entt::id_type, entt::any> _items;

	public:
		template <typename T>
		T& emplace() {
			static_assert(std::is_default_constructible_v<T>);
			entt::id_type type = entt::type_id<T>().index();
			_items[type] = entt::make_any<T>();
			return entt::any_cast<T&>(_items[type]);

		}

		template <typename T>
		T& emplace(const T& item) {
			entt::id_type type = entt::type_id<T>().index();
			_items[type] = entt::make_any<T>(item);
			return entt::any_cast<T&>(_items[type]);
		}

		template <typename T>
		T& emplace(T&& item) {
			entt::id_type type = entt::type_id<T>().index();
			_items[type] = std::move(item);
			return entt::any_cast<T&>(_items[type]);
		}

		entt::any& emplace(entt::any&& item) {
			entt::id_type type = item.type().index();
			_items[type] = std::move(item);
			return _items[type];
		}

		template <typename T>
		const T* tryGet() const {
			entt::id_type type = entt::type_id<T>().index();
			auto found = _items.find(type);
			
			if (found != _items.end()) {
				return &entt::any_cast<T&>(found->second);
			}

			return nullptr;
		}

		template <typename T>
		T* tryGet() {
			entt::id_type type = entt::type_id<T>().index();
			auto found = _items.find(type);

			if (found != _items.end()) {
				return &entt::any_cast<T&>(found->second);
			}

			return nullptr;
		}

		const entt::any* tryGet(entt::id_type type) const {
			auto found = _items.find(type);

			if (found != _items.end()) {
				return &found->second;
			}

			return nullptr;
		}

		entt::any* tryGet(entt::id_type type) {
			auto found = _items.find(type);

			if (found != _items.end()) {
				return &found->second;
			}

			return nullptr;
		}

		template <typename T>
		const T& get() const {
			T* v = tryGet<T>();
			assert(v);
			return *v;
		}

		template <typename T>
		T& get() {
			T* v = tryGet<T>();
			assert(v);
			return *v;
		}

		template <typename T>
		T& getOrEmplace(T&& data) {
			entt::id_type type = entt::type_id<T>().index();

			auto found = _items.find(type);
			if (found != _items.end()) {
				return entt::any_cast<T&>(found->second);
			}

			return emplace<T>(std::forward<T>(data));
		}

		template <typename T>
		T& getOrEmplace() {
			static_assert(std::is_default_constructible_v<T>);
			return getOrEmplace(T());
		}

		template <typename T>
		bool has() const {
			entt::id_type type = entt::type_id<T>().index();
			return _items.find(type) != _items.end();
		}
	};
}
