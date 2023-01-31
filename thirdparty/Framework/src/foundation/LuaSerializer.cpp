#include "LuaSerializer.h"

#include "foundation/SolUtil.h"
#include "foundation/AssociativeContainer.h"

namespace fw {
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

	sol::object serializeItem(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack);

	sol::object serializeInteger(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);

		// Ordered by most (likely) common
		if (type.id == getTypeId<bool>()) { return sol::make_object(lua, entt::any_cast<bool>(source)); }
		if (type.id == getTypeId<int32>()) { return sol::make_object(lua, entt::any_cast<int32>(source)); }
		if (type.id == getTypeId<int32>()) { return sol::make_object(lua, entt::any_cast<int32>(source)); }
		if (type.id == getTypeId<uint32>()) { return sol::make_object(lua, entt::any_cast<uint32>(source)); }
		if (type.id == getTypeId<int64>()) { return sol::make_object(lua, entt::any_cast<int64>(source)); }
		if (type.id == getTypeId<uint64>()) { return sol::make_object(lua, entt::any_cast<uint64>(source)); }
		if (type.id == getTypeId<int16>()) { return sol::make_object(lua, entt::any_cast<int16>(source)); }
		if (type.id == getTypeId<uint16>()) { return sol::make_object(lua, entt::any_cast<uint16>(source)); }
		if (type.id == getTypeId<int8>()) { return sol::make_object(lua, entt::any_cast<int8>(source)); }
		if (type.id == getTypeId<uint8>()) { return sol::make_object(lua, entt::any_cast<uint8>(source)); }

		spdlog::error("Failed to serialize integer");
		//logSerializeTypeError(source.get_type(), type.name, fieldStack);

		return sol::make_object(lua, sol::nil);
	}

	sol::object serializeFloat(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);

		if (type.id == getTypeId<f32>()) { return sol::make_object(lua, entt::any_cast<f32>(source)); }
		if (type.id == getTypeId<f64>()) { return sol::make_object(lua, entt::any_cast<f64>(source)); }

		spdlog::error("Failed to serialize float");
		//logSerializeTypeError(source.get_type(), type.name, fieldStack);

		return sol::make_object(lua, sol::nil);
	}

	sol::object serializeString(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		return sol::make_object(lua, entt::any_cast<const std::string&>(source));
	}

	sol::object serializeAny(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const entt::any& anyValue = entt::any_cast<const entt::any&>(source);
		const fw::TypeInfo* valueType = registry.findTypeInfo(anyValue);

		if (valueType) {
			return lua.create_table_with(
				"type", valueType->name,
				"data", serializeItem(registry, lua, anyValue, fieldStack)
			);
		}

		spdlog::error("Failed to serialize any");
		//logSerializeTypeError(source.get_type(), type.name, fieldStack);

		return sol::make_object(lua, sol::nil);
	}

	sol::object serializeEnum(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);

		if (type.fields.size()) {
			for (const fw::Field& field : type.fields) {
				if (field.get({}) == source) {
					return sol::make_object(lua, field.name);
				}
			}
		} else {
			const void* data = source.data();
			uint32 value = *((uint32*)data);
			return sol::make_object(lua, value);
		}

		spdlog::error("Lua serialization: Failed serialize enum value of type {}", type.name);

		// spdlog::error("Lua deserialization: Failed to assign enum value '{}' to field '{}'", value, fieldStack.getFieldPath());
		// logDeserializeTypeError(source.get_type(), type.name, fieldStack);

		return sol::make_object(lua, sol::nil);
	}

	sol::object serializeClass(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);
		sol::table target = lua.create_table(0, (int)type.fields.size());

		for (const fw::Field& field : type.fields) {
			fieldStack.push(field.name);

			entt::any fieldData = field.get(source);
			target[field.name] = serializeItem(registry, lua, fieldData, fieldStack);

			fieldStack.pop();
		}

		return target;
	}

	sol::object serializeSequence(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);
		assert(type.createSequenceContainer);

		SequenceContainer container = type.createSequenceContainer(source.as_ref());
		assert(registry.findTypeInfo(container.value_type()));

		sol::table target = lua.create_table((int)container.size(), 0);

		for (auto it = container.begin(); it != container.end(); ++it) {
			assert(!it->owner());
			target.add(serializeItem(registry, lua, it->as_ref(), fieldStack));
		}

		return target;
	}

	sol::object serializeAssociative(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(source);
		assert(type.createAssociativeContainer);

		AssociativeContainer container = type.createAssociativeContainer(source.as_ref());
		assert(registry.findTypeInfo(container.key_type()));
		assert(container.key_only() || registry.findTypeInfo(container.mapped_type()));

		sol::table target = lua.create_table(0, (int)container.size());

		for (auto it = container.begin(); it != container.end(); ++it) {
			assert(!it->first.owner());
			assert(!it->second.owner());
			target[serializeItem(registry, lua, it->first, fieldStack)] = serializeItem(registry, lua, it->second, fieldStack);
		}

		return target;
	}

	sol::object serializeItem(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source, FieldStack& fieldStack) {
		const fw::TypeInfo* type = registry.findTypeInfo(source);

		if (type) {
			if (type->isFloat()) {
				return serializeFloat(registry, lua, source, fieldStack);
			} else if (type->isIntegral()) {
				return serializeInteger(registry, lua, source, fieldStack);
			} else  if (type->isEnum()) {
				return serializeEnum(registry, lua, source, fieldStack);
			} else if (type->isAssociativeContainer()) {
				return serializeAssociative(registry, lua, source, fieldStack);
			} else if (type->isSequenceContainer()) {
				return serializeSequence(registry, lua, source, fieldStack);
			} else if (type->isClass()) {
				if (type->id == getTypeId<std::string>()) {
					return serializeString(registry, lua, source, fieldStack);
				} else if (type->id == getTypeId<entt::any>()) {
					return serializeAny(registry, lua, source, fieldStack);
				} else {
					return serializeClass(registry, lua, source, fieldStack);
				}
			} else {
				spdlog::error("Lua serialization: Field '{}' type '{}' is not supported", fieldStack.getFieldPath(), type->name);
			}
		} else {
			spdlog::error("Lua serialization: Field '{}' type '{}' has not been added to the type registry", fieldStack.getFieldPath(), source.type().name());
		}

		return sol::make_object(lua, sol::nil);
	}

	sol::object LuaSerializer::serializeToObject(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source) {
		FieldStack fieldStack;
		return serializeItem(registry, lua, source, fieldStack);
	}

	std::string LuaSerializer::serializeToString(const fw::TypeRegistry& registry, sol::state& lua, const entt::any& source) {
		FieldStack fieldStack;
		sol::object obj = serializeItem(registry, lua, source, fieldStack);

		std::string str;
		SolUtil::serializeTable(lua, obj.as<sol::table>(), str);

		return str;
	}

	std::string LuaSerializer::serializeToString(const fw::TypeRegistry& registry, const entt::any& obj) {
		sol::state s;
		SolUtil::prepareState(s);
		return LuaSerializer::serializeToString(registry, s, obj);
	}

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

	bool deserializeAny(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::table) {
			sol::table sourceTable = source.as<sol::table>();
			std::string typeName = sourceTable["type"];

			const fw::TypeInfo* typeInfo = registry.findTypeInfo(std::string_view(typeName));

			if (typeInfo) {
				entt::any& inner = entt::any_cast<entt::any&>(target);
				inner = std::move(typeInfo->construct());
				return deserializeItem(registry, sourceTable["data"], inner, fieldStack);
			} else {
				spdlog::error("Failed to find type {}", typeName);
			}
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
		} else if (source.get_type() == sol::type::number) {
			uint32* data = (uint32*)target.data();
			*data = source.as<uint32>();

			return true;
		} else {
			logDeserializeTypeError(source.get_type(), type.name, fieldStack);
		}

		return false;
	}

	bool deserializeSequence(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::table) {
			const sol::table sourceTable = source.as<sol::table>();

			SequenceContainer container = type.createSequenceContainer(target.as_ref());
			const TypeInfo& valueType = registry.getTypeInfo(container.value_type());

			bool valid = true;

			size_t idx = container.size();
			container.resize(container.size() + sourceTable.size());

			for (size_t i = 0; i < sourceTable.size(); ++i) {
				const sol::object obj = sourceTable[i + 1];
				entt::any itemTarget = container[i + idx].as_ref();

				if (!deserializeItem(registry, obj, itemTarget, fieldStack)) {
					valid = false;
				}
			}

			return valid;
		} else {
			spdlog::error("FAIL");
		}

		return false;
	}

	bool deserializeAssociative(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const TypeInfo& type = registry.getTypeInfo(target);

		if (source.get_type() == sol::type::table) {
			const sol::table sourceTable = source.as<sol::table>();

			AssociativeContainer container = type.createAssociativeContainer(target.as_ref());
			const TypeInfo& keyType = registry.getTypeInfo(container.key_type());
			const TypeInfo& valueType = registry.getTypeInfo(container.mapped_type());

			bool valid = true;

			for (const std::pair<sol::object, sol::object>& kv : sourceTable) {
				entt::any key = keyType.construct();
				entt::any value = valueType.construct();

				if (!deserializeItem(registry, kv.first, key, fieldStack)) {
					valid = false;
					continue;
				}

				if (!deserializeItem(registry, kv.second, value, fieldStack)) {
					valid = false;
					continue;
				}

				container.insert(std::move(key), std::move(value));
			}

			return valid;
		} else {
			spdlog::error("FAIL");
		}		

		return false;
	}

	bool deserializeItem(const fw::TypeRegistry& registry, const sol::object& source, entt::any& target, FieldStack& fieldStack) {
		const fw::TypeInfo* type = registry.findTypeInfo(target);
		
		if (type) {
			if (type->isSequenceContainer()) {
				return deserializeSequence(registry, source, target, fieldStack);
			} else if (type->isAssociativeContainer()) {
				return deserializeAssociative(registry, source, target, fieldStack);
			} else if (type->isClass()) {
				if (type->id == fw::getTypeId<std::string>()) {
					return deserializeString(registry, source, target, fieldStack);
				} else if (type->id == fw::getTypeId<entt::any>()) {
					return deserializeAny(registry, source, target, fieldStack);
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

	bool LuaSerializer::deserialize(const fw::TypeRegistry& registry, const sol::object& source, fw::TypeInstance target) {
		FieldStack fieldStack;
		return deserializeItem(registry, source, target.getValue(), fieldStack);
	}

	bool LuaSerializer::deserializeFromString(const fw::TypeRegistry& registry, const std::string& source, TypeInstance target) {
		sol::state lua;
		SolUtil::prepareState(lua);

		sol::table table;
		if (SolUtil::deserializeTable(lua, source, table)) {
			FieldStack fieldStack;
			return LuaSerializer::deserialize(registry, table, std::move(target));
		} else {
			spdlog::error("Failed to deserialize table");
		}

		return false;
	}
}

