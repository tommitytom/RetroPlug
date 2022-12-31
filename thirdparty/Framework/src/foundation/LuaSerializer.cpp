#include "LuaSerializer.h"

namespace fw {
	std::string_view toString(sol::type type) {
		switch (type) {
		case sol::type::none: return "none";
		case sol::type::lua_nil: return "nil";
		case sol::type::string: return "string";
		case sol::type::number: return "number";
		case sol::type::thread: return "thread";
		case sol::type::boolean: return "boolean";
		case sol::type::function: return "function";
		case sol::type::userdata: return "userdata";
		case sol::type::lightuserdata: return "lightuserdata";
		case sol::type::table: return "table";
		case sol::type::poly: return "poly";
		}

		return "unknown";
	}

	bool LuaSerializer::serialize(const fw::TypeRegistry& registry, const entt::any& obj, sol::table& target) {
		return false;
	}

	struct FieldStack {
		std::array<std::string_view, 5> names;
		size_t count = 0;

		void push(std::string_view name) {
			if (count < names.size()) {
				names[count] = name;
			}

			count++;
		}

		void pop() {
			assert(count > 0);
			count--;
		}

		std::string getFieldPath() const {
			if (count) {
				std::string fieldName;
				for (size_t i = 0; i < std::min(count, names.size()); ++i) {
					if (i > 0) {
						fieldName += ".";
					}

					fieldName += names[i];
				}

				if (count > names.size()) {
					fieldName += fmt::format(" + {}", count - names.size());
				}

				return fieldName;
			}
			
			return "<root>";
		}
	};

	bool deserializeItem(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack);

	void logDeserializeTypeError(sol::type sourceType, std::string_view targetName, const FieldStack& fieldStack) {
		spdlog::error("Lua deserialization: Tried to deserialize field '{}' of type '{}' in to a '{}'", fieldStack.getFieldPath(), toString(sourceType), targetName);
	}

	bool deserializeClass(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::table) {
			bool valid = true;
			sol::table table = source.as<sol::table>();

			for (const fw::Field& field : type.fields) {
				fieldStack.push(field.name);

				std::optional<sol::object> sourceValue = table[field.name];

				if (sourceValue.has_value() && sourceValue.value().get_type() != sol::type::lua_nil) {
					entt::any targetValue = field.get(target);
					assert(!targetValue.owner());			

					if (!deserializeItem(registry, sourceValue.value(), targetValue, fieldStack)) {
						valid = false;
					}
				} else {
					spdlog::error("Lua deserialization: Field '{}' missing in lua table", fieldStack.getFieldPath());
					valid = false;
				}

				fieldStack.pop();
			}

			return valid;
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeInteger(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::number || source.get_type() == sol::type::boolean) {
			TypeId targetType = getTypeId(target);

			// Ordered by most (likely) common
			if (targetType == getTypeId<bool>()) { return target.assign(source.as<bool>()); }
			if (targetType == getTypeId<int32>()) { return target.assign(source.as<int32>()); }
			if (targetType == getTypeId<int32>()) { return target.assign(source.as<int32>()); }
			if (targetType == getTypeId<uint32>()) { return target.assign(source.as<uint32>()); }
			if (targetType == getTypeId<int64>()) { return target.assign(source.as<int64>()); }
			if (targetType == getTypeId<uint64>()) { return target.assign(source.as<uint64>()); }
			if (targetType == getTypeId<int16>()) { return target.assign(source.as<int16>()); }
			if (targetType == getTypeId<uint16>()) { return target.assign(source.as<uint16>()); }
			if (targetType == getTypeId<int8>()) { return target.assign(source.as<int8>()); }
			if (targetType == getTypeId<uint8>()) { return target.assign(source.as<uint8>()); }

			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeFloat(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::number) {
			TypeId targetType = getTypeId(target);

			if (targetType == getTypeId<f32>()) { return target.assign(source.as<f32>()); }
			if (targetType == getTypeId<f64>()) { return target.assign(source.as<f64>()); }

			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeString(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::string) {
			return target.assign(source.as<std::string>());
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeEnum(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::string) {
			std::string value = source.as<std::string>();
			
			for (const Field& field : type.fields) {
				if (field.name == value) {
					if (target.assign(field.get({}))) {
						return true;
					}
				}
			}

			spdlog::error("Lua deserialization: Failed to assign enum value '{}' to field '{}'", value, fieldStack.getFieldPath());
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeItem(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		assert(!target.owner());
		const fw::TypeInfo* type = registry.findTypeInfo(target);
		
		if (type) {
			if (type->isClass()) {
				if (type->id == getTypeId<std::string>()) {
					return deserializeString(registry, source, target, fieldStack);
				} else {
					return deserializeClass(registry, source, target, fieldStack);
				}
			} else if (type->isIntegral()) {
				return deserializeInteger(registry, source, target, fieldStack);
			} else if (type->isFloat()) {
				return deserializeFloat(registry, source, target, fieldStack);
			} else if (type->isEnum()) {
				return deserializeEnum(registry, source, target, fieldStack);
			} else {
				spdlog::error("Lua deserialization: Field '{}' type '{}' is not supported", fieldStack.getFieldPath(), type->name);
			}
		} else {
			spdlog::error("Lua deserialization: Field '{}' type '{}' has not been added to the type registry", fieldStack.getFieldPath(), target.type().name());
		}

		return false;
	}

	bool LuaSerializer::deserialize(const fw::TypeRegistry& registry, const sol::object& source, TypeInstance target) {
		FieldStack fieldStack;
		return deserializeItem(registry, source, target.getValue(), fieldStack);
	}
}
