#ifndef ENTT_CONTAINER_DENSE_MAP_HPP
#define ENTT_CONTAINER_DENSE_MAP_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include "../config/config.h"
#include "../core/compressed_pair.hpp"
#include "../core/iterator.hpp"
#include "../core/memory.hpp"
#include "../core/type_traits.hpp"
#include "fwd.hpp"

namespace entt {

/**
 * @cond TURN_OFF_DOXYGEN
 * Internal details not to be documented.
 */

namespace internal {

template<typename Key, typename Type>
struct dense_map_node final {
    using value_type = std::pair<Key, Type>;

    template<typename... Args>
    dense_map_node(const std::size_t pos, Args &&...args)
        : next{pos},
          element{std::forward<Args>(args)...} {}

    template<typename Allocator, typename... Args>
    dense_map_node(std::allocator_arg_t, const Allocator &allocator, const std::size_t pos, Args &&...args)
        : next{pos},
          element{entt::make_obj_using_allocator<value_type>(allocator, std::forward<Args>(args)...)} {}

    template<typename Allocator>
    dense_map_node(std::allocator_arg_t, const Allocator &allocator, const dense_map_node &other)
        : next{other.next},
          element{entt::make_obj_using_allocator<value_type>(allocator, other.element)} {}

    template<typename Allocator>
    dense_map_node(std::allocator_arg_t, const Allocator &allocator, dense_map_node &&other)
        : next{other.next},
          element{entt::make_obj_using_allocator<value_type>(allocator, std::move(other.element))} {}

    std::size_t next;
    value_type element;
};

template<typename It>
class dense_map_iterator final {
    template<typename>
    friend class dense_map_iterator;

    using first_type = decltype(std::as_const(std::declval<It>()->element.first));
    using second_type = decltype((std::declval<It>()->element.second));

public:
    using value_type = std::pair<first_type, second_type>;
    using pointer = input_iterator_pointer<value_type>;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    dense_map_iterator() ENTT_NOEXCEPT
        : it{} {}

    dense_map_iterator(const It iter) ENTT_NOEXCEPT
        : it{iter} {}

    template<typename Other, typename = std::enable_if_t<!std::is_same_v<It, Other> && std::is_constructible_v<It, Other>>>
    dense_map_iterator(const dense_map_iterator<Other> &other) ENTT_NOEXCEPT
        : it{other.it} {}

    dense_map_iterator &operator++() ENTT_NOEXCEPT {
        return ++it, *this;
    }

    dense_map_iterator operator++(int) ENTT_NOEXCEPT {
        dense_map_iterator orig = *this;
        return ++(*this), orig;
    }

    dense_map_iterator &operator--() ENTT_NOEXCEPT {
        return --it, *this;
    }

    dense_map_iterator operator--(int) ENTT_NOEXCEPT {
        dense_map_iterator orig = *this;
        return operator--(), orig;
    }

    dense_map_iterator &operator+=(const difference_type value) ENTT_NOEXCEPT {
        it += value;
        return *this;
    }

    dense_map_iterator operator+(const difference_type value) const ENTT_NOEXCEPT {
        dense_map_iterator copy = *this;
        return (copy += value);
    }

    dense_map_iterator &operator-=(const difference_type value) ENTT_NOEXCEPT {
        return (*this += -value);
    }

    dense_map_iterator operator-(const difference_type value) const ENTT_NOEXCEPT {
        return (*this + -value);
    }

    [[nodiscard]] reference operator[](const difference_type value) const ENTT_NOEXCEPT {
        return {it[value].element.first, it[value].element.second};
    }

    [[nodiscard]] pointer operator->() const ENTT_NOEXCEPT {
        return operator*();
    }

    [[nodiscard]] reference operator*() const ENTT_NOEXCEPT {
        return {it->element.first, it->element.second};
    }

    template<typename ILhs, typename IRhs>
    friend std::ptrdiff_t operator-(const dense_map_iterator<ILhs> &, const dense_map_iterator<IRhs> &) ENTT_NOEXCEPT;

    template<typename ILhs, typename IRhs>
    friend bool operator==(const dense_map_iterator<ILhs> &, const dense_map_iterator<IRhs> &) ENTT_NOEXCEPT;

    template<typename ILhs, typename IRhs>
    friend bool operator<(const dense_map_iterator<ILhs> &, const dense_map_iterator<IRhs> &) ENTT_NOEXCEPT;

private:
    It it;
};

template<typename ILhs, typename IRhs>
[[nodiscard]] std::ptrdiff_t operator-(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return lhs.it - rhs.it;
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator==(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return lhs.it == rhs.it;
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator!=(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return !(lhs == rhs);
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator<(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return lhs.it < rhs.it;
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator>(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return rhs < lhs;
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator<=(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return !(lhs > rhs);
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator>=(const dense_map_iterator<ILhs> &lhs, const dense_map_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return !(lhs < rhs);
}

template<typename It>
class dense_map_local_iterator final {
    template<typename>
    friend class dense_map_local_iterator;

    using first_type = decltype(std::as_const(std::declval<It>()->element.first));
    using second_type = decltype((std::declval<It>()->element.second));

public:
    using value_type = std::pair<first_type, second_type>;
    using pointer = input_iterator_pointer<value_type>;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    dense_map_local_iterator() ENTT_NOEXCEPT
        : it{},
          offset{} {}

    dense_map_local_iterator(It iter, const std::size_t pos) ENTT_NOEXCEPT
        : it{iter},
          offset{pos} {}

    template<typename Other, typename = std::enable_if_t<!std::is_same_v<It, Other> && std::is_constructible_v<It, Other>>>
    dense_map_local_iterator(const dense_map_local_iterator<Other> &other) ENTT_NOEXCEPT
        : it{other.it},
          offset{other.offset} {}

    dense_map_local_iterator &operator++() ENTT_NOEXCEPT {
        return offset = it[offset].next, *this;
    }

    dense_map_local_iterator operator++(int) ENTT_NOEXCEPT {
        dense_map_local_iterator orig = *this;
        return ++(*this), orig;
    }

    [[nodiscard]] pointer operator->() const ENTT_NOEXCEPT {
        return operator*();
    }

    [[nodiscard]] reference operator*() const ENTT_NOEXCEPT {
        return {it[offset].element.first, it[offset].element.second};
    }

    [[nodiscard]] std::size_t index() const ENTT_NOEXCEPT {
        return offset;
    }

private:
    It it;
    std::size_t offset;
};

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator==(const dense_map_local_iterator<ILhs> &lhs, const dense_map_local_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return lhs.index() == rhs.index();
}

template<typename ILhs, typename IRhs>
[[nodiscard]] bool operator!=(const dense_map_local_iterator<ILhs> &lhs, const dense_map_local_iterator<IRhs> &rhs) ENTT_NOEXCEPT {
    return !(lhs == rhs);
}

} // namespace internal

/**
 * Internal details not to be documented.
 * @endcond
 */

/**
 * @brief Associative container for key-value pairs with unique keys.
 *
 * Internally, elements are organized into buckets. Which bucket an element is
 * placed into depends entirely on the hash of its key. Keys with the same hash
 * code appear in the same bucket.
 *
 * @tparam Key Key type of the associative container.
 * @tparam Type Mapped type of the associative container.
 * @tparam Hash Type of function to use to hash the keys.
 * @tparam KeyEqual Type of function to use to compare the keys for equality.
 * @tparam Allocator Type of allocator used to manage memory and elements.
 */
template<typename Key, typename Type, typename Hash, typename KeyEqual, typename Allocator>
class dense_map {
    static constexpr float default_threshold = 0.875f;
    static constexpr std::size_t minimum_capacity = 8u;

    using node_type = internal::dense_map_node<Key, Type>;
    using alloc_traits = typename std::allocator_traits<Allocator>;
    static_assert(std::is_same_v<typename alloc_traits::value_type, std::pair<const Key, Type>>, "Invalid value type");
    using sparse_container_type = std::vector<std::size_t, typename alloc_traits::template rebind_alloc<std::size_t>>;
    using packed_container_type = std::vector<node_type, typename alloc_traits::template rebind_alloc<node_type>>;

    template<typename Other>
    [[nodiscard]] std::size_t key_to_bucket(const Other &key) const ENTT_NOEXCEPT {
        return fast_mod(sparse.second()(key), bucket_count());
    }

    template<typename Other>
    [[nodiscard]] auto constrained_find(const Other &key, std::size_t bucket) {
        for(auto it = begin(bucket), last = end(bucket); it != last; ++it) {
            if(packed.second()(it->first, key)) {
                return begin() + static_cast<typename iterator::difference_type>(it.index());
            }
        }

        return end();
    }

    template<typename Other>
    [[nodiscard]] auto constrained_find(const Other &key, std::size_t bucket) const {
        for(auto it = cbegin(bucket), last = cend(bucket); it != last; ++it) {
            if(packed.second()(it->first, key)) {
                return cbegin() + static_cast<typename iterator::difference_type>(it.index());
            }
        }

        return cend();
    }

    template<typename Other, typename... Args>
    [[nodiscard]] auto insert_or_do_nothing(Other &&key, Args &&...args) {
        const auto index = key_to_bucket(key);

        if(auto it = constrained_find(key, index); it != end()) {
            return std::make_pair(it, false);
        }

        packed.first().emplace_back(sparse.first()[index], std::piecewise_construct, std::forward_as_tuple(std::forward<Other>(key)), std::forward_as_tuple(std::forward<Args>(args)...));
        sparse.first()[index] = packed.first().size() - 1u;
        rehash_if_required();

        return std::make_pair(--end(), true);
    }

    template<typename Other, typename Arg>
    [[nodiscard]] auto insert_or_overwrite(Other &&key, Arg &&value) {
        const auto index = key_to_bucket(key);

        if(auto it = constrained_find(key, index); it != end()) {
            it->second = std::forward<Arg>(value);
            return std::make_pair(it, false);
        }

        packed.first().emplace_back(sparse.first()[index], std::forward<Other>(key), std::forward<Arg>(value));
        sparse.first()[index] = packed.first().size() - 1u;
        rehash_if_required();

        return std::make_pair(--end(), true);
    }

    void move_and_pop(const std::size_t pos) {
        if(const auto last = size() - 1u; pos != last) {
            packed.first()[pos] = std::move(packed.first().back());
            size_type *curr = sparse.first().data() + key_to_bucket(packed.first().back().element.first);
            for(; *curr != last; curr = &packed.first()[*curr].next) {}
            *curr = pos;
        }

        packed.first().pop_back();
    }

    void rehash_if_required() {
        if(size() > (bucket_count() * max_load_factor())) {
            rehash(bucket_count() * 2u);
        }
    }

public:
    /*! @brief Key type of the container. */
    using key_type = Key;
    /*! @brief Mapped type of the container. */
    using mapped_type = Type;
    /*! @brief Key-value type of the container. */
    using value_type = std::pair<const Key, Type>;
    /*! @brief Unsigned integer type. */
    using size_type = std::size_t;
    /*! @brief Type of function to use to hash the keys. */
    using hasher = Hash;
    /*! @brief Type of function to use to compare the keys for equality. */
    using key_equal = KeyEqual;
    /*! @brief Allocator type. */
    using allocator_type = Allocator;
    /*! @brief Input iterator type. */
    using iterator = internal::dense_map_iterator<typename packed_container_type::iterator>;
    /*! @brief Constant input iterator type. */
    using const_iterator = internal::dense_map_iterator<typename packed_container_type::const_iterator>;
    /*! @brief Input iterator type. */
    using local_iterator = internal::dense_map_local_iterator<typename packed_container_type::iterator>;
    /*! @brief Constant input iterator type. */
    using const_local_iterator = internal::dense_map_local_iterator<typename packed_container_type::const_iterator>;

    /*! @brief Default constructor. */
    dense_map()
        : dense_map(minimum_capacity) {}

    /**
     * @brief Constructs an empty container with a given allocator.
     * @param allocator The allocator to use.
     */
    explicit dense_map(const allocator_type &allocator)
        : dense_map{minimum_capacity, hasher{}, key_equal{}, allocator} {}

    /**
     * @brief Constructs an empty container with a given allocator and user
     * supplied minimal number of buckets.
     * @param bucket_count Minimal number of buckets.
     * @param allocator The allocator to use.
     */
    dense_map(const size_type bucket_count, const allocator_type &allocator)
        : dense_map{bucket_count, hasher{}, key_equal{}, allocator} {}

    /**
     * @brief Constructs an empty container with a given allocator, hash
     * function and user supplied minimal number of buckets.
     * @param bucket_count Minimal number of buckets.
     * @param hash Hash function to use.
     * @param allocator The allocator to use.
     */
    dense_map(const size_type bucket_count, const hasher &hash, const allocator_type &allocator)
        : dense_map{bucket_count, hash, key_equal{}, allocator} {}

    /**
     * @brief Constructs an empty container with a given allocator, hash
     * function, compare function and user supplied minimal number of buckets.
     * @param bucket_count Minimal number of buckets.
     * @param hash Hash function to use.
     * @param equal Compare function to use.
     * @param allocator The allocator to use.
     */
    explicit dense_map(const size_type bucket_count, const hasher &hash = hasher{}, const key_equal &equal = key_equal{}, const allocator_type &allocator = allocator_type{})
        : sparse{allocator, hash},
          packed{allocator, equal},
          threshold{default_threshold} {
        rehash(bucket_count);
    }

    /*! @brief Default copy constructor. */
    dense_map(const dense_map &) = default;

    /**
     * @brief Allocator-extended copy constructor.
     * @param other The instance to copy from.
     * @param allocator The allocator to use.
     */
    dense_map(const dense_map &other, const allocator_type &allocator)
        : sparse{std::piecewise_construct, std::forward_as_tuple(other.sparse.first(), allocator), std::forward_as_tuple(other.sparse.second())},
          packed{std::piecewise_construct, std::forward_as_tuple(other.packed.first(), allocator), std::forward_as_tuple(other.packed.second())},
          threshold{other.threshold} {}

    /*! @brief Default move constructor. */
    dense_map(dense_map &&) = default;

    /**
     * @brief Allocator-extended move constructor.
     * @param other The instance to move from.
     * @param allocator The allocator to use.
     */
    dense_map(dense_map &&other, const allocator_type &allocator)
        : sparse{std::piecewise_construct, std::forward_as_tuple(std::move(other.sparse.first()), allocator), std::forward_as_tuple(std::move(other.sparse.second()))},
          packed{std::piecewise_construct, std::forward_as_tuple(std::move(other.packed.first()), allocator), std::forward_as_tuple(std::move(other.packed.second()))},
          threshold{other.threshold} {}

    /**
     * @brief Default copy assignment operator.
     * @return This container.
     */
    dense_map &operator=(const dense_map &) = default;

    /**
     * @brief Default move assignment operator.
     * @return This container.
     */
    dense_map &operator=(dense_map &&) = default;

    /**
     * @brief Returns the associated allocator.
     * @return The associated allocator.
     */
    [[nodiscard]] constexpr allocator_type get_allocator() const ENTT_NOEXCEPT {
        return sparse.first().get_allocator();
    }

    /**
     * @brief Returns an iterator to the beginning.
     *
     * The returned iterator points to the first instance of the internal array.
     * If the array is empty, the returned iterator will be equal to `end()`.
     *
     * @return An iterator to the first instance of the internal array.
     */
    [[nodiscard]] const_iterator cbegin() const ENTT_NOEXCEPT {
        return packed.first().begin();
    }

    /*! @copydoc cbegin */
    [[nodiscard]] const_iterator begin() const ENTT_NOEXCEPT {
        return cbegin();
    }

    /*! @copydoc begin */
    [[nodiscard]] iterator begin() ENTT_NOEXCEPT {
        return packed.first().begin();
    }

    /**
     * @brief Returns an iterator to the end.
     *
     * The returned iterator points to the element following the last instance
     * of the internal array. Attempting to dereference the returned iterator
     * results in undefined behavior.
     *
     * @return An iterator to the element following the last instance of the
     * internal array.
     */
    [[nodiscard]] const_iterator cend() const ENTT_NOEXCEPT {
        return packed.first().end();
    }

    /*! @copydoc cend */
    [[nodiscard]] const_iterator end() const ENTT_NOEXCEPT {
        return cend();
    }

    /*! @copydoc end */
    [[nodiscard]] iterator end() ENTT_NOEXCEPT {
        return packed.first().end();
    }

    /**
     * @brief Checks whether a container is empty.
     * @return True if the container is empty, false otherwise.
     */
    [[nodiscard]] bool empty() const ENTT_NOEXCEPT {
        return packed.first().empty();
    }

    /**
     * @brief Returns the number of elements in a container.
     * @return Number of elements in a container.
     */
    [[nodiscard]] size_type size() const ENTT_NOEXCEPT {
        return packed.first().size();
    }

    /*! @brief Clears the container. */
    void clear() ENTT_NOEXCEPT {
        sparse.first().clear();
        packed.first().clear();
        rehash(0u);
    }

    /**
     * @brief Inserts an element into the container, if the key does not exist.
     * @param value A key-value pair eventually convertible to the value type.
     * @return A pair consisting of an iterator to the inserted element (or to
     * the element that prevented the insertion) and a bool denoting whether the
     * insertion took place.
     */
    std::pair<iterator, bool> insert(const value_type &value) {
        return insert_or_do_nothing(value.first, value.second);
    }

    /*! @copydoc insert */
    std::pair<iterator, bool> insert(value_type &&value) {
        return insert_or_do_nothing(std::move(value.first), std::move(value.second));
    }

    /**
     * @copydoc insert
     * @tparam Arg Type of the key-value pair to insert into the container.
     */
    template<typename Arg>
    std::enable_if_t<std::is_constructible_v<value_type, Arg &&>, std::pair<iterator, bool>>
    insert(Arg &&value) {
        return insert_or_do_nothing(std::forward<Arg>(value).first, std::forward<Arg>(value).second);
    }

    /**
     * @brief Inserts elements into the container, if their keys do not exist.
     * @tparam It Type of input iterator.
     * @param first An iterator to the first element of the range of elements.
     * @param last An iterator past the last element of the range of elements.
     */
    template<typename It>
    void insert(It first, It last) {
        for(; first != last; ++first) {
            insert(*first);
        }
    }

    /**
     * @brief Inserts an element into the container or assigns to the current
     * element if the key already exists.
     * @tparam Arg Type of the value to insert or assign.
     * @param key A key used both to look up and to insert if not found.
     * @param value A value to insert or assign.
     * @return A pair consisting of an iterator to the element and a bool
     * denoting whether the insertion took place.
     */
    template<typename Arg>
    std::pair<iterator, bool> insert_or_assign(const key_type &key, Arg &&value) {
        return insert_or_overwrite(key, std::forward<Arg>(value));
    }

    /*! @copydoc insert_or_assign */
    template<typename Arg>
    std::pair<iterator, bool> insert_or_assign(key_type &&key, Arg &&value) {
        return insert_or_overwrite(std::move(key), std::forward<Arg>(value));
    }

    /**
     * @brief Constructs an element in-place, if the key does not exist.
     *
     * The element is also constructed when the container already has the key,
     * in which case the newly constructed object is destroyed immediately.
     *
     * @tparam Args Types of arguments to forward to the constructor of the
     * element.
     * @param args Arguments to forward to the constructor of the element.
     * @return A pair consisting of an iterator to the inserted element (or to
     * the element that prevented the insertion) and a bool denoting whether the
     * insertion took place.
     */
    template<typename... Args>
    std::pair<iterator, bool> emplace([[maybe_unused]] Args &&...args) {
        if constexpr(sizeof...(Args) == 0u) {
            return insert_or_do_nothing(key_type{});
        } else if constexpr(sizeof...(Args) == 1u) {
            return insert_or_do_nothing(std::forward<Args>(args).first..., std::forward<Args>(args).second...);
        } else if constexpr(sizeof...(Args) == 2u) {
            return insert_or_do_nothing(std::forward<Args>(args)...);
        } else {
            auto &node = packed.first().emplace_back(packed.first().size(), std::forward<Args>(args)...);
            const auto index = key_to_bucket(node.element.first);

            if(auto it = constrained_find(node.element.first, index); it != end()) {
                packed.first().pop_back();
                return std::make_pair(it, false);
            }

            std::swap(node.next, sparse.first()[index]);
            rehash_if_required();

            return std::make_pair(--end(), true);
        }
    }

    /**
     * @brief Inserts in-place if the key does not exist, does nothing if the
     * key exists.
     * @tparam Args Types of arguments to forward to the constructor of the
     * element.
     * @param key A key used both to look up and to insert if not found.
     * @param args Arguments to forward to the constructor of the element.
     * @return A pair consisting of an iterator to the inserted element (or to
     * the element that prevented the insertion) and a bool denoting whether the
     * insertion took place.
     */
    template<typename... Args>
    std::pair<iterator, bool> try_emplace(const key_type &key, Args &&...args) {
        return insert_or_do_nothing(key, std::forward<Args>(args)...);
    }

    /*! @copydoc try_emplace */
    template<typename... Args>
    std::pair<iterator, bool> try_emplace(key_type &&key, Args &&...args) {
        return insert_or_do_nothing(std::move(key), std::forward<Args>(args)...);
    }

    /**
     * @brief Removes an element from a given position.
     * @param pos An iterator to the element to remove.
     * @return An iterator following the removed element.
     */
    iterator erase(const_iterator pos) {
        const auto diff = pos - cbegin();
        erase(pos->first);
        return begin() + diff;
    }

    /**
     * @brief Removes the given elements from a container.
     * @param first An iterator to the first element of the range of elements.
     * @param last An iterator past the last element of the range of elements.
     * @return An iterator following the last removed element.
     */
    iterator erase(const_iterator first, const_iterator last) {
        const auto dist = first - cbegin();

        for(auto from = last - cbegin(); from != dist; --from) {
            erase(packed.first()[from - 1u].element.first);
        }

        return (begin() + dist);
    }

    /**
     * @brief Removes the element associated with a given key.
     * @param key A key value of an element to remove.
     * @return Number of elements removed (either 0 or 1).
     */
    size_type erase(const key_type &key) {
        for(size_type *curr = sparse.first().data() + key_to_bucket(key); *curr != (std::numeric_limits<size_type>::max)(); curr = &packed.first()[*curr].next) {
            if(packed.second()(packed.first()[*curr].element.first, key)) {
                const auto index = *curr;
                *curr = packed.first()[*curr].next;
                move_and_pop(index);
                return 1u;
            }
        }

        return 0u;
    }

    /**
     * @brief Exchanges the contents with those of a given container.
     * @param other Container to exchange the content with.
     */
    void swap(dense_map &other) {
        using std::swap;
        swap(sparse, other.sparse);
        swap(packed, other.packed);
        swap(threshold, other.threshold);
    }

    /**
     * @brief Accesses a given element with bounds checking.
     * @param key A key of an element to find.
     * @return A reference to the mapped value of the requested element.
     */
    [[nodiscard]] mapped_type &at(const key_type &key) {
        auto it = find(key);
        ENTT_ASSERT(it != end(), "Invalid key");
        return it->second;
    }

    /*! @copydoc at */
    [[nodiscard]] const mapped_type &at(const key_type &key) const {
        auto it = find(key);
        ENTT_ASSERT(it != cend(), "Invalid key");
        return it->second;
    }

    /**
     * @brief Accesses or inserts a given element.
     * @param key A key of an element to find or insert.
     * @return A reference to the mapped value of the requested element.
     */
    [[nodiscard]] mapped_type &operator[](const key_type &key) {
        return insert_or_do_nothing(key).first->second;
    }

    /**
     * @brief Accesses or inserts a given element.
     * @param key A key of an element to find or insert.
     * @return A reference to the mapped value of the requested element.
     */
    [[nodiscard]] mapped_type &operator[](key_type &&key) {
        return insert_or_do_nothing(std::move(key)).first->second;
    }

    /**
     * @brief Finds an element with a given key.
     * @param key Key value of an element to search for.
     * @return An iterator to an element with the given key. If no such element
     * is found, a past-the-end iterator is returned.
     */
    [[nodiscard]] iterator find(const key_type &key) {
        return constrained_find(key, key_to_bucket(key));
    }

    /*! @copydoc find */
    [[nodiscard]] const_iterator find(const key_type &key) const {
        return constrained_find(key, key_to_bucket(key));
    }

    /**
     * @brief Finds an element with a key that compares _equivalent_ to a given
     * value.
     * @tparam Other Type of the key value of an element to search for.
     * @param key Key value of an element to search for.
     * @return An iterator to an element with the given key. If no such element
     * is found, a past-the-end iterator is returned.
     */
    template<typename Other>
    [[nodiscard]] std::enable_if_t<is_transparent_v<hasher> && is_transparent_v<key_equal>, std::conditional_t<false, Other, iterator>>
    find(const Other &key) {
        return constrained_find(key, key_to_bucket(key));
    }

    /*! @copydoc find */
    template<typename Other>
    [[nodiscard]] std::enable_if_t<is_transparent_v<hasher> && is_transparent_v<key_equal>, std::conditional_t<false, Other, const_iterator>>
    find(const Other &key) const {
        return constrained_find(key, key_to_bucket(key));
    }

    /**
     * @brief Checks if the container contains an element with a given key.
     * @param key Key value of an element to search for.
     * @return True if there is such an element, false otherwise.
     */
    [[nodiscard]] bool contains(const key_type &key) const {
        return (find(key) != cend());
    }

    /**
     * @brief Checks if the container contains an element with a key that
     * compares _equivalent_ to a given value.
     * @tparam Other Type of the key value of an element to search for.
     * @param key Key value of an element to search for.
     * @return True if there is such an element, false otherwise.
     */
    template<typename Other>
    [[nodiscard]] std::enable_if_t<is_transparent_v<hasher> && is_transparent_v<key_equal>, std::conditional_t<false, Other, bool>>
    contains(const Other &key) const {
        return (find(key) != cend());
    }

    /**
     * @brief Returns an iterator to the beginning of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the beginning of the given bucket.
     */
    [[nodiscard]] const_local_iterator cbegin(const size_type index) const {
        return {packed.first().begin(), sparse.first()[index]};
    }

    /**
     * @brief Returns an iterator to the beginning of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the beginning of the given bucket.
     */
    [[nodiscard]] const_local_iterator begin(const size_type index) const {
        return cbegin(index);
    }

    /**
     * @brief Returns an iterator to the beginning of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the beginning of the given bucket.
     */
    [[nodiscard]] local_iterator begin(const size_type index) {
        return {packed.first().begin(), sparse.first()[index]};
    }

    /**
     * @brief Returns an iterator to the end of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the end of the given bucket.
     */
    [[nodiscard]] const_local_iterator cend([[maybe_unused]] const size_type index) const {
        return {packed.first().begin(), (std::numeric_limits<size_type>::max)()};
    }

    /**
     * @brief Returns an iterator to the end of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the end of the given bucket.
     */
    [[nodiscard]] const_local_iterator end([[maybe_unused]] const size_type index) const {
        return cend(index);
    }

    /**
     * @brief Returns an iterator to the end of a given bucket.
     * @param index An index of a bucket to access.
     * @return An iterator to the end of the given bucket.
     */
    [[nodiscard]] local_iterator end([[maybe_unused]] const size_type index) {
        return {packed.first().begin(), (std::numeric_limits<size_type>::max)()};
    }

    /**
     * @brief Returns the number of buckets.
     * @return The number of buckets.
     */
    [[nodiscard]] size_type bucket_count() const {
        return sparse.first().size();
    }

    /**
     * @brief Returns the maximum number of buckets.
     * @return The maximum number of buckets.
     */
    [[nodiscard]] size_type max_bucket_count() const {
        return sparse.first().max_size();
    }

    /**
     * @brief Returns the number of elements in a given bucket.
     * @param index The index of the bucket to examine.
     * @return The number of elements in the given bucket.
     */
    [[nodiscard]] size_type bucket_size(const size_type index) const {
        return static_cast<size_type>(std::distance(begin(index), end(index)));
    }

    /**
     * @brief Returns the bucket for a given key.
     * @param key The value of the key to examine.
     * @return The bucket for the given key.
     */
    [[nodiscard]] size_type bucket(const key_type &key) const {
        return key_to_bucket(key);
    }

    /**
     * @brief Returns the average number of elements per bucket.
     * @return The average number of elements per bucket.
     */
    [[nodiscard]] float load_factor() const {
        return size() / static_cast<float>(bucket_count());
    }

    /**
     * @brief Returns the maximum average number of elements per bucket.
     * @return The maximum average number of elements per bucket.
     */
    [[nodiscard]] float max_load_factor() const {
        return threshold;
    }

    /**
     * @brief Sets the desired maximum average number of elements per bucket.
     * @param value A desired maximum average number of elements per bucket.
     */
    void max_load_factor(const float value) {
        ENTT_ASSERT(value > 0.f, "Invalid load factor");
        threshold = value;
        rehash(0u);
    }

    /**
     * @brief Reserves at least the specified number of buckets and regenerates
     * the hash table.
     * @param count New number of buckets.
     */
    void rehash(const size_type count) {
        auto value = (std::max)(count, minimum_capacity);
        value = (std::max)(value, static_cast<size_type>(size() / max_load_factor()));

        if(const auto sz = next_power_of_two(value); sz != bucket_count()) {
            sparse.first().resize(sz);
            std::fill(sparse.first().begin(), sparse.first().end(), (std::numeric_limits<size_type>::max)());

            for(size_type pos{}, last = size(); pos < last; ++pos) {
                const auto index = key_to_bucket(packed.first()[pos].element.first);
                packed.first()[pos].next = std::exchange(sparse.first()[index], pos);
            }
        }
    }

    /**
     * @brief Reserves space for at least the specified number of elements and
     * regenerates the hash table.
     * @param count New number of elements.
     */
    void reserve(const size_type count) {
        packed.first().reserve(count);
        rehash(static_cast<size_type>(std::ceil(count / max_load_factor())));
    }

    /**
     * @brief Returns the function used to hash the keys.
     * @return The function used to hash the keys.
     */
    [[nodiscard]] hasher hash_function() const {
        return sparse.second();
    }

    /**
     * @brief Returns the function used to compare keys for equality.
     * @return The function used to compare keys for equality.
     */
    [[nodiscard]] key_equal key_eq() const {
        return packed.second();
    }

private:
    compressed_pair<sparse_container_type, hasher> sparse;
    compressed_pair<packed_container_type, key_equal> packed;
    float threshold;
};

} // namespace entt

/**
 * @cond TURN_OFF_DOXYGEN
 * Internal details not to be documented.
 */

namespace std {

template<typename Key, typename Value, typename Allocator>
struct uses_allocator<entt::internal::dense_map_node<Key, Value>, Allocator>
    : std::true_type {};

} // namespace std

/**
 * Internal details not to be documented.
 * @endcond
 */

#endif
