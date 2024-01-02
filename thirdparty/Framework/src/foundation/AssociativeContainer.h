#pragma once

#include <map>
#include <unordered_map>

#include <entt/meta/type_traits.hpp>
#include <entt/core/iterator.hpp>

#include "foundation/MetaTypes.h"

namespace fw {
	/*! @brief Proxy object for associative containers. */
	class AssociativeContainer {
	private:
		class meta_iterator {
			enum class operation : std::uint8_t {
				incr,
				deref
			};

			using vtable_type = void(const operation, const entt::any&, std::pair<entt::any, entt::any>*);

			template<bool KeyOnly, typename It>
			static void basic_vtable(const operation op, const entt::any& value, std::pair<entt::any, entt::any>* other) {
				switch (op) {
				case operation::incr:
					++entt::any_cast<It&>(const_cast<entt::any&>(value));
					break;
				case operation::deref:
					const auto& it = entt::any_cast<const It&>(value);
					if constexpr (KeyOnly) {
						other->first.emplace<decltype(*it)>(*it);
					} else {
						other->first.emplace<decltype((it->first))>(it->first);
						other->second.emplace<decltype((it->second))>(it->second);
					}
					break;
				}
			}

		public:
			using difference_type = std::ptrdiff_t;
			using value_type = std::pair<entt::any, entt::any>;
			using pointer = entt::input_iterator_pointer<value_type>;
			using reference = value_type;
			using iterator_category = std::input_iterator_tag;

			meta_iterator() ENTT_NOEXCEPT
				: vtable{},
				handle{} {}

			template<bool KeyOnly, typename It>
			meta_iterator(std::integral_constant<bool, KeyOnly>, It iter) ENTT_NOEXCEPT
				: vtable{ &basic_vtable<KeyOnly, It> },
				handle{ std::move(iter) } {}

			meta_iterator& operator++() ENTT_NOEXCEPT {
				vtable(operation::incr, handle, nullptr);
				return *this;
			}

			meta_iterator operator++(int) ENTT_NOEXCEPT {
				meta_iterator orig = *this;
				return ++(*this), orig;
			}

			[[nodiscard]] reference operator*() const {
				reference other;
				vtable(operation::deref, handle, &other);
				return other;
			}

			[[nodiscard]] pointer operator->() const {
				return operator*();
			}

			[[nodiscard]] explicit operator bool() const ENTT_NOEXCEPT {
				return static_cast<bool>(handle);
			}

			[[nodiscard]] bool operator==(const meta_iterator& other) const ENTT_NOEXCEPT {
				return handle == other.handle;
			}

			[[nodiscard]] bool operator!=(const meta_iterator& other) const ENTT_NOEXCEPT {
				return !(*this == other);
			}

		private:
			vtable_type* vtable;
			entt::any handle;
		};

	public:
		/*! @brief Unsigned integer type. */
		using size_type = std::size_t;
		/*! @brief Meta iterator type. */
		using iterator = meta_iterator;

		/*! @brief Default constructor. */
		AssociativeContainer() ENTT_NOEXCEPT = default;

		/**
		 * @brief Construct a proxy object for associative containers.
		 * @tparam Type Type of container to wrap.
		 * @param instance The container to wrap.
		 */
		template<typename Type>
		AssociativeContainer(std::in_place_type_t<Type>, entt::any instance) ENTT_NOEXCEPT
			: key_only_container{ entt::meta_associative_container_traits<Type>::key_only },
			key_type_node{ getTypeId<std::remove_cv_t<std::remove_reference_t<typename Type::key_type>>>()},
			mapped_type_node{ INVALID_TYPE_ID },
			value_type_node{ getTypeId<std::remove_cv_t<std::remove_reference_t<typename Type::value_type>>>() },
			size_fn{ &entt::meta_associative_container_traits<Type>::size },
			clear_fn{ &entt::meta_associative_container_traits<Type>::clear },
			iter_fn{ &entt::meta_associative_container_traits<Type>::iter },
			insert_fn{ &entt::meta_associative_container_traits<Type>::insert },
			erase_fn{ &entt::meta_associative_container_traits<Type>::erase },
			find_fn{ &entt::meta_associative_container_traits<Type>::find },
			storage{ std::move(instance) } 
		{
			if constexpr (!entt::meta_associative_container_traits<Type>::key_only) {
				mapped_type_node = getTypeId<std::remove_cv_t<std::remove_reference_t<typename Type::mapped_type>>>();
			}

			assert(!storage.owner());
		}

		[[nodiscard]] inline bool key_only() const ENTT_NOEXCEPT { return key_only_container; }
		[[nodiscard]] inline TypeId key_type() const ENTT_NOEXCEPT { return key_type_node; }
		[[nodiscard]] inline TypeId mapped_type() const ENTT_NOEXCEPT { return mapped_type_node; }
		[[nodiscard]] inline TypeId value_type() const ENTT_NOEXCEPT { return value_type_node; }
		[[nodiscard]] inline size_type size() const ENTT_NOEXCEPT { return size_fn(storage); }
		inline bool clear() { return clear_fn(storage); }
		[[nodiscard]] inline iterator begin() { return iter_fn(storage, false); }
		[[nodiscard]] inline iterator end() { return iter_fn(storage, true); }
		inline bool insert(entt::any key, entt::any value) { return insert_fn(storage, key, value); }
		inline bool erase(entt::any key) { return erase_fn(storage, key); }
		[[nodiscard]] inline iterator find(entt::any key) { return find_fn(storage, key); }
		[[nodiscard]] inline explicit operator bool() const ENTT_NOEXCEPT { return static_cast<bool>(storage); }

	private:
		bool key_only_container{};
		TypeId key_type_node = INVALID_TYPE_ID;
		TypeId mapped_type_node = INVALID_TYPE_ID;
		TypeId value_type_node = INVALID_TYPE_ID;
		size_type(*size_fn)(const entt::any&) ENTT_NOEXCEPT = nullptr;
		bool (*clear_fn)(entt::any&) = nullptr;
		iterator(*iter_fn)(entt::any&, const bool) = nullptr;
		bool (*insert_fn)(entt::any&, entt::any&, entt::any&) = nullptr;
		bool (*erase_fn)(entt::any&, entt::any&) = nullptr;
		iterator(*find_fn)(entt::any&, entt::any&) = nullptr;
		entt::any storage{};
	};

	template<typename Type>
	struct BasicAssociativeContainerTraits {
		using iterator = fw::AssociativeContainer::iterator;
		using size_type = std::size_t;

		static constexpr auto key_only = is_key_only_meta_associative_container<Type>::value;

		[[nodiscard]] static size_type size(const entt::any& container) ENTT_NOEXCEPT {
			return entt::any_cast<const Type&>(container).size();
		}

		[[nodiscard]] static bool clear(entt::any& container) {
			if (auto* const cont = entt::any_cast<Type>(&container); cont) {
				cont->clear();
				return true;
			}

			return false;
		}

		[[nodiscard]] static iterator iter(entt::any& container, const bool as_end) {
			if (auto* const cont = entt::any_cast<Type>(&container); cont) {
				return iterator{ std::integral_constant<bool, key_only>{}, as_end ? cont->end() : cont->begin() };
			}

			const auto& as_const = entt::any_cast<const Type&>(container);
			return iterator{ std::integral_constant<bool, key_only>{}, as_end ? as_const.end() : as_const.begin() };
		}

		[[nodiscard]] static bool insert(entt::any& container, entt::any& key, [[maybe_unused]] entt::any& value) {
			auto* const cont = entt::any_cast<Type>(&container);

			if constexpr (is_key_only_meta_associative_container<Type>::value) {
				return cont && cont->insert(entt::any_cast<const typename Type::key_type&>(key)).second;
			} else {
				return cont&& cont->emplace(entt::any_cast<const typename Type::key_type&>(key), entt::any_cast<const typename Type::mapped_type&>(value)).second;
			}
		}

		[[nodiscard]] static bool erase(entt::any& container, entt::any& key) {
			auto* const cont = entt::any_cast<Type>(&container);
			return cont && cont->erase(entt::any_cast<const typename Type::key_type&>(key));
		}

		[[nodiscard]] static iterator find(entt::any& container, entt::any& key) {
			if (auto* const cont = entt::any_cast<Type>(&container); cont) {
				return iterator{ std::integral_constant<bool, key_only>{}, cont->find(entt::any_cast<const typename Type::key_type&>(key)) };
			}

			return iterator{ std::integral_constant<bool, key_only>{}, entt::any_cast<const Type&>(container).find(entt::any_cast<const typename Type::key_type&>(key)) };
		}
	};
}

namespace entt {
	template<typename Key, typename Value, typename... Args>
	struct meta_associative_container_traits<std::unordered_map<Key, Value, Args...>>
		: fw::BasicAssociativeContainerTraits<std::unordered_map<Key, Value, Args...>> {};

	template<typename Key, typename Value, typename... Args>
	struct meta_associative_container_traits<std::map<Key, Value, Args...>>
		: fw::BasicAssociativeContainerTraits<std::map<Key, Value, Args...>> {};
}
