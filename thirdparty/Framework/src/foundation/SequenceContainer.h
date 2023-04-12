#pragma once

#include <vector>

#include <entt/meta/type_traits.hpp>
#include <entt/core/iterator.hpp>

#include "foundation/MetaTypes.h"

namespace fw {
	/*! @brief Proxy object for sequence containers. */
	class SequenceContainer {
		class meta_iterator final {
			friend class SequenceContainer;

			using deref_fn_type = entt::any(const entt::any&, const std::ptrdiff_t);

			template<typename It>
			static entt::any deref_fn(const entt::any& value, const std::ptrdiff_t pos) {
				return entt::any{ std::in_place_type<typename std::iterator_traits<It>::reference>, entt::any_cast<const It&>(value)[pos] };
			}

		public:
			using difference_type = std::ptrdiff_t;
			using value_type = entt::any;
			using pointer = entt::input_iterator_pointer<value_type>;
			using reference = value_type;
			using iterator_category = std::input_iterator_tag;

			meta_iterator() ENTT_NOEXCEPT
				: deref{},
				offset{},
				handle{} {}

			template<typename Type>
			explicit meta_iterator(Type& cont, const difference_type init) ENTT_NOEXCEPT
				: deref{ &deref_fn<decltype(cont.begin())> },
				offset{ init },
				handle{ cont.begin() } {}

			meta_iterator& operator++() ENTT_NOEXCEPT {
				return ++offset, * this;
			}

			meta_iterator operator++(int value) ENTT_NOEXCEPT {
				meta_iterator orig = *this;
				offset += ++value;
				return orig;
			}

			meta_iterator& operator--() ENTT_NOEXCEPT {
				return --offset, * this;
			}

			meta_iterator operator--(int value) ENTT_NOEXCEPT {
				meta_iterator orig = *this;
				offset -= ++value;
				return orig;
			}

			[[nodiscard]] reference operator*() const {
				return deref(handle, offset);
			}

			[[nodiscard]] pointer operator->() const {
				return operator*();
			}

			[[nodiscard]] explicit operator bool() const ENTT_NOEXCEPT {
				return static_cast<bool>(handle);
			}

			[[nodiscard]] bool operator==(const meta_iterator& other) const ENTT_NOEXCEPT {
				return offset == other.offset;
			}

			[[nodiscard]] bool operator!=(const meta_iterator& other) const ENTT_NOEXCEPT {
				return !(*this == other);
			}

		private:
			deref_fn_type* deref;
			difference_type offset;
			entt::any handle;
		};

	public:
		/*! @brief Unsigned integer type. */
		using size_type = std::size_t;
		/*! @brief Meta iterator type. */
		using iterator = meta_iterator;

		/*! @brief Default constructor. */
		SequenceContainer() ENTT_NOEXCEPT = default;

		/**
		 * @brief Construct a proxy object for sequence containers.
		 * @tparam Type Type of container to wrap.
		 * @param instance The container to wrap.
		 */
		template<typename Type>
		SequenceContainer(std::in_place_type_t<Type>, entt::any instance) ENTT_NOEXCEPT
			: value_type_node{ getTypeId<std::remove_cv_t<std::remove_reference_t<typename Type::value_type>>>() },
			size_fn{ &entt::meta_sequence_container_traits<Type>::size },
			resize_fn{ &entt::meta_sequence_container_traits<Type>::resize },
			iter_fn{ &entt::meta_sequence_container_traits<Type>::iter },
			insert_fn{ &entt::meta_sequence_container_traits<Type>::insert },
			erase_fn{ &entt::meta_sequence_container_traits<Type>::erase },
			storage{ std::move(instance) } {}

		[[nodiscard]] inline TypeId value_type() const ENTT_NOEXCEPT { return value_type_node; }
		[[nodiscard]] inline size_type size() const ENTT_NOEXCEPT { return size_fn(storage); }
		inline bool resize(const size_type sz) { return resize_fn(storage, sz); }
		inline bool clear() { return resize_fn(storage, 0u); }
		[[nodiscard]] inline iterator begin() { return iter_fn(storage, false); }
		[[nodiscard]] inline iterator end() { return iter_fn(storage, true); }
		inline iterator insert(iterator it, entt::any value) { return insert_fn(storage, it.offset, value); }
		inline iterator erase(iterator it) { return erase_fn(storage, it.offset); }
		[[nodiscard]] inline entt::any operator[](const size_type pos) {
			auto it = begin();
			it.operator++(static_cast<int>(pos) - 1);
			return *it;
		}
		[[nodiscard]] inline explicit operator bool() const ENTT_NOEXCEPT { return static_cast<bool>(storage); }

	private:
		TypeId value_type_node = INVALID_TYPE_ID;
		size_type(*size_fn)(const entt::any&) ENTT_NOEXCEPT = nullptr;
		bool (*resize_fn)(entt::any&, size_type) = nullptr;
		iterator(*iter_fn)(entt::any&, const bool) = nullptr;
		iterator(*insert_fn)(entt::any&, const std::ptrdiff_t, entt::any&) = nullptr;
		iterator(*erase_fn)(entt::any&, const std::ptrdiff_t) = nullptr;
		entt::any storage{};
	};

	template<typename Type>
	struct BasicSequenceContainerTraits {
		using iterator = SequenceContainer::iterator;
		using size_type = std::size_t;

		[[nodiscard]] static size_type size(const entt::any& container) ENTT_NOEXCEPT {
			return any_cast<const Type&>(container).size();
		}

		[[nodiscard]] static bool resize([[maybe_unused]] entt::any& container, [[maybe_unused]] size_type sz) {
			if constexpr (is_dynamic_sequence_container<Type>::value) {
				if (auto* const cont = entt::any_cast<Type>(&container); cont) {
					cont->resize(sz);
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] static iterator iter(entt::any& container, const bool as_end) {
			if (auto* const cont = entt::any_cast<Type>(&container); cont) {
				return iterator{ *cont, static_cast<typename iterator::difference_type>(as_end * cont->size()) };
			}

			const Type& as_const = entt::any_cast<const Type&>(container);
			return iterator{ as_const, static_cast<typename iterator::difference_type>(as_end * as_const.size()) };
		}

		[[nodiscard]] static iterator insert([[maybe_unused]] entt::any& container, [[maybe_unused]] const std::ptrdiff_t offset, [[maybe_unused]] entt::any& value) {
			if constexpr (is_dynamic_sequence_container<Type>::value) {
				if (auto* const cont = entt::any_cast<Type>(&container); cont) {
					const auto* element = entt::any_cast<std::remove_reference_t<typename Type::const_reference>>(&value);
					const auto curr = cont->insert(cont->begin() + offset, element ? *element : entt::any_cast<typename Type::value_type>(value));
					return iterator{ *cont, curr - cont->begin() };
				}
			}

			return {};
		}

		[[nodiscard]] static iterator erase([[maybe_unused]] entt::any& container, [[maybe_unused]] const std::ptrdiff_t offset) {
			if constexpr (is_dynamic_sequence_container<Type>::value) {
				if (auto* const cont = entt::any_cast<Type>(&container); cont) {
					const auto curr = cont->erase(cont->begin() + offset);
					return iterator{ *cont, curr - cont->begin() };
				}
			}

			return {};
		}
	};
}

/*namespace entt {
	template<typename Type, typename... Args>
	struct meta_sequence_container_traits<std::vector<Type, Args...>>
		: fw::BasicSequenceContainerTraits<std::vector<Type, Args...>> {};
}*/
