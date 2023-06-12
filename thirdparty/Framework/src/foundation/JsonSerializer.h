#pragma once

#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <variant>
#include <vector>

#include <magic_enum.hpp>
#include <refl.hpp>
#include <simdjson/simdjson.h>
#include <spdlog/spdlog.h>

#include "foundation/Types.h"

template< class T, template<class...> class Primary >
struct is_specialization_of : std::false_type {};

template< template<class...> class Primary, class... Args >
struct is_specialization_of< Primary<Args...>, Primary> : std::true_type {};

namespace fw::JsonSerializer {
	template <typename T>
	static constexpr simdjson::error_code deserialize(simdjson::ondemand::value source, T& value);

	namespace internal {
		template<class T>
		struct IsArray : std::is_array<T> {};
		template<class T, std::size_t N>
		struct IsArray<std::array<T, N>> :std::true_type {};
		// optional:
		template<class T>
		struct IsArray<T const> : IsArray<T> {};
		template<class T>
		struct IsArray<T volatile> : IsArray<T> {};
		template<class T>
		struct IsArray<T volatile const> : IsArray<T> {};

		template <typename SourceType, typename TargetType>
		static constexpr simdjson::error_code errorCheckNumeric(simdjson::simdjson_result<SourceType>&& res, TargetType& value) {
			if (res.error() == simdjson::error_code::SUCCESS) [[likely]] {
				value = static_cast<TargetType>(res.value());
			} else {
				spdlog::error("Failed to deserialize number: {}", simdjson::error_message(res.error()));
			}

			return res.error();
		}
		
		template <typename SourceType, typename TargetType>
		static constexpr simdjson::error_code deserializeNumeric(simdjson::ondemand::value source, TargetType& value) {
			return errorCheckNumeric(source.get<SourceType>(), value);
		}

		template <typename SourceType, typename TargetType>
		static constexpr simdjson::error_code deserializeEnumAs(simdjson::ondemand::value source, TargetType& value) {
			using MagicEnumInputType = std::conditional_t<std::is_arithmetic_v<SourceType>, int, SourceType>;

			simdjson::simdjson_result<SourceType> result = source.get<SourceType>();

			if (result.error() == simdjson::error_code::SUCCESS) {
				std::optional<TargetType> enumValue = magic_enum::enum_cast<TargetType>(static_cast<MagicEnumInputType>(result.value()));

				if (enumValue.has_value()) [[likely]] {
					value = enumValue.value();
					return simdjson::error_code::SUCCESS;
				}

				spdlog::error("Failed to deserialize enum: Provided value '{}' does not match any enum values", result.value());
				return simdjson::error_code::INDEX_OUT_OF_BOUNDS;
			}

			return result.error();
		}

		template <typename TargetType>
		static constexpr simdjson::error_code deserializeEnum(simdjson::ondemand::value source, TargetType& value) {
			simdjson::error_code error;

			if constexpr (std::is_unsigned_v<TargetType>) {
				error = deserializeEnumAs<uint64>(source, value);
			} else {
				error = deserializeEnumAs<int64>(source, value);
			}

			// Enums may also be provided as a string
			if (error == simdjson::error_code::INCORRECT_TYPE) {
				error = deserializeEnumAs<std::string_view>(source, value);
			}

			if (error != simdjson::error_code::SUCCESS) {
				spdlog::error("Failed to deserialize enum: {}", simdjson::error_message(error));
			}

			return error;
		}

		static simdjson::error_code deserializeString(simdjson::ondemand::value source, std::string& value) {
			simdjson::simdjson_result<std::string_view> res = source.get_string();

			if (res.error() == simdjson::error_code::SUCCESS) [[likely]] {
				value = std::string(res.value());
			} else {
				spdlog::error("Failed to deserialize string: {}", simdjson::error_message(res.error()));
			}

			return res.error();
		}
		
		template <typename TargetType>
		static simdjson::error_code deserializeObject(simdjson::ondemand::value source, TargetType& target) {
			simdjson::simdjson_result<simdjson::ondemand::object> obj = source.get_object();

			if (obj.error() == simdjson::error_code::SUCCESS) [[likely]] {
				for_each(refl::reflect(target).members, [&](auto member) {
					using MemberType = std::decay_t<decltype(member(target))>;

					if constexpr (is_writable(member)/* && refl::descriptor::has_attribute<serializable>(member)*/) {
						simdjson::simdjson_result<simdjson::ondemand::value> field = obj[get_display_name(member)];
						
						if (field.error() == simdjson::error_code::SUCCESS) [[likely]] {
							deserialize(field.value(), member(target));
						} else {
							spdlog::error("Failed to deserialize object: {}", simdjson::error_message(field.error()));
						}
					}
				});
			} else {
				spdlog::info("Failed to deserialize object: {}", simdjson::error_message(obj.error()));
			}

			return obj.error();
		}

		template <typename TargetType>
		static simdjson::error_code deserializeKey(std::string_view source, TargetType& value) {
			if constexpr (std::is_same_v<TargetType, std::string>) {
				value = std::string(source);
				return simdjson::error_code::SUCCESS;
			}
			
			if constexpr (std::is_arithmetic_v<TargetType>) {
				auto [ptr, ec] { std::from_chars(source.data(), source.data() + source.size(), value) };
				
				if (ec == std::errc()) [[likely]] {
					return simdjson::error_code::SUCCESS;
				}
				
				return simdjson::error_code::NUMBER_ERROR;
			}

			return simdjson::error_code::INCORRECT_TYPE;
		}

		template <typename TargetType>
		static simdjson::error_code deserializeAssociative(simdjson::ondemand::value source, TargetType& value) {
			static_assert(std::is_same_v<typename TargetType::key_type, std::string> || std::is_arithmetic_v<typename TargetType::key_type>);
			
			simdjson::simdjson_result<simdjson::ondemand::object> obj = source.get_object();

			if (obj.error() == simdjson::error_code::SUCCESS) [[likely]] {
				simdjson::error_code error;

				for (auto element : obj.value()) {
					typename TargetType::key_type key;
					typename TargetType::mapped_type val;

					simdjson::simdjson_result<std::string_view> keyResult = element.unescaped_key();
					if (keyResult.error() != simdjson::error_code::SUCCESS) [[unlikely]] {
						spdlog::error("Failed to deserialize map key: {}", simdjson::error_message(keyResult.error()));
						break;
					}

					error = internal::deserializeKey(keyResult.value(), key);
					if (error != simdjson::error_code::SUCCESS) [[unlikely]] {
						spdlog::error("Failed to deserialize map key: {}", simdjson::error_message(error));
						break;
					}

					simdjson::simdjson_result<simdjson::ondemand::value> valueResult = element.value();
					if (valueResult.error() != simdjson::error_code::SUCCESS) [[unlikely]] {
						spdlog::error("Failed to deserialize map value: {}", simdjson::error_message(valueResult.error()));
						break;
					}

					error = deserialize(element.value(), val);

					if (error == simdjson::error_code::SUCCESS) [[likely]] {
						value.insert(std::make_pair(std::move(key), std::move(val)));
					} else {
						// Error already logged
						break;
					}
				}
			} else {
				spdlog::error("Failed to deserialize map: {}", simdjson::error_message(obj.error()));
			}

			return obj.error();
		}

		template <typename TargetType>
		static simdjson::error_code deserializeArray(simdjson::ondemand::value source, TargetType& value) {
			simdjson::simdjson_result<simdjson::ondemand::array> arr = source.get_array();

			if (arr.error() == simdjson::error_code::SUCCESS) [[likely]] {
				size_t idx = 0;

				for (auto element : arr) {
					if (idx < value.size()) [[likely]] {
						deserialize(element.value(), value[idx++]);
					} else {
						spdlog::error("Failed to deserialize array: Too many elements");
						break;
					}
				}
			} else {
				spdlog::error("Failed to deserialize array: {}", simdjson::error_message(arr.error()));
			}

			return arr.error();
		}

		template <typename TargetType>
		static simdjson::error_code deserializeSequence(simdjson::ondemand::value source, TargetType& value) {
			simdjson::simdjson_result<simdjson::ondemand::array> arr = source.get_array();

			if (arr.error() == simdjson::error_code::SUCCESS) [[likely]] {
				for (auto element : arr.value()) {
					typename TargetType::value_type val;

					simdjson::error_code error = deserialize(element.value(), val);

					if (error == simdjson::error_code::SUCCESS) [[likely]] {
						value.insert(value.end(), std::move(val));
					}
				}
			} else {
				spdlog::error("Failed to deserialize array: {}", simdjson::error_message(arr.error()));
			}

			return arr.error();
		}
	}

	template <typename TargetType>
	static constexpr simdjson::error_code deserialize(simdjson::ondemand::value source, TargetType& value) {
		static_assert(!std::is_same_v<TargetType, std::string_view>, "Unable to deserialize in to an std::string_view");

		if constexpr (std::is_arithmetic_v<TargetType>) {
			if constexpr (std::is_floating_point_v<TargetType>) {
				return internal::deserializeNumeric<f64, TargetType>(source, value);
			} else if constexpr (std::is_unsigned_v<TargetType>) {
				return internal::deserializeNumeric<uint64, TargetType>(source, value);
			} else {
				return internal::deserializeNumeric<int64, TargetType>(source, value);
			}
		} else if constexpr (std::is_enum_v<TargetType>) {
			return internal::deserializeEnum(source, value);
		} else if constexpr (std::is_same_v<TargetType, std::string>) {
			return internal::deserializeString(source, value);
		} else if constexpr (internal::IsArray<TargetType>::value) {
			return internal::deserializeArray(source, value);
		} else if constexpr (is_specialization_of<TargetType, std::vector>::value || is_specialization_of<TargetType, std::set>::value || is_specialization_of<TargetType, std::unordered_set>::value) {
			return internal::deserializeSequence(source, value);
		} else if constexpr (is_specialization_of<TargetType, std::map>::value || is_specialization_of<TargetType, std::unordered_map>::value) {
			return internal::deserializeAssociative(source, value);
		} else if constexpr (is_specialization_of<TargetType, std::variant>::value) {
			//return internal::deserializeVariant(source, value);
		} else if constexpr (std::is_class_v<TargetType>) {
			return internal::deserializeObject(source, value);
		}
		
		return simdjson::error_code::INCORRECT_TYPE;
	}

	/*template <typename TargetType>
	static constexpr simdjson::error_code deserializeFromString(std::string_view source, TargetType& value) {
		simdjson::ondemand::parser parser;
		simdjson::ondemand::document doc = parser.iterate(source);
		return deserialize(doc.get_value(), value);
	}*/

	template <typename TargetType>
	static constexpr simdjson::error_code deserializeFromString(simdjson::padded_string_view source, TargetType& value) {
		simdjson::ondemand::parser parser;
		simdjson::ondemand::document doc = parser.iterate(source);
		return deserialize(doc.get_value(), value);
	}

	template <typename TargetType>
	static constexpr simdjson::error_code deserializeFromFile(std::string_view path, TargetType& value) {
		auto source = simdjson::padded_string::load(path);
		return deserializeFromString(source, value);
	}
}
