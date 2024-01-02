#pragma once

#include <cassert>
#include <span>
#include <vector>

#include <entt/core/any.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/meta/utility.hpp>
#include <entt/meta/template.hpp>
#include <entt/meta/type_traits.hpp>

#include <magic_enum.hpp>

#include <spdlog/spdlog.h>

#include "foundation/Types.h"
#include "foundation/MetaTypes.h"
#include "foundation/AssociativeContainer.h"
#include "foundation/SequenceContainer.h"

namespace fw {
	enum class EnTest {
		One,
		Two
	};

	class TypeInstance {
	private:
		entt::any _value;

	public:
		TypeInstance() = default;
		TypeInstance(const TypeInstance&) = delete;
		TypeInstance(TypeInstance&&) = default;

		TypeInstance& operator=(const TypeInstance&) = delete;
		TypeInstance& operator=(TypeInstance&&) = default;

		template<typename Type, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Type>, TypeInstance>>>
		TypeInstance(Type& value) noexcept : TypeInstance{} {
			if constexpr (std::is_same_v<std::decay_t<Type>, entt::any>) {
				_value = value.as_ref();
			} else {
				_value.emplace<Type&>(value);
			}
		}

		entt::any& getValue() {
			return _value;
		}

		explicit operator bool() const ENTT_NOEXCEPT {
			return static_cast<bool>(_value);
		}

		entt::any* operator->() {
			return &_value;
		}

		const entt::any* operator->() const {
			return &_value;
		}
	};

	static NameHash getNameHash(std::string_view name) {
		return NameHash{ entt::hashed_string(name.data(), name.size()).value() };
	}

	namespace internal {
		enum class TypeTraits : uint32 {
			None = 0,
			Const = 1 << 0,
			Static = 1 << 1,
			Integral = 1 << 2,
			FloatingPoint = 1 << 3,
			Array = 1 << 4,
			Enum = 1 << 5,
			Class = 1 << 6,
			Pointer = 1 << 7,
			PointerLike = 1 << 8,
			SequenceContainer = 1 << 9,
			AssociativeContainer = 1 << 10,
			_entt_enum_as_bitmask
		};

		template <typename T>
		constexpr TypeTraits getTypeTraits() {
			TypeTraits traits = TypeTraits::None;
			if constexpr (std::is_class_v<T>) { traits |= TypeTraits::Class; }
			if constexpr (std::is_enum_v<T>) { traits |= TypeTraits::Enum; }
			if constexpr (std::is_integral_v<T>) { traits |= TypeTraits::Integral; }
			if constexpr (std::is_floating_point_v<T>) { traits |= TypeTraits::FloatingPoint; }
			if constexpr (std::is_array_v<T>) { traits |= TypeTraits::Array; }
			if constexpr (std::is_pointer_v<T>) { traits |= TypeTraits::Pointer; }
			if constexpr (entt::is_complete_v<entt::meta_sequence_container_traits<T>>) { traits |= TypeTraits::SequenceContainer; }
			if constexpr (entt::is_complete_v<entt::meta_associative_container_traits<T>>) { traits |= TypeTraits::AssociativeContainer; }
			return traits;
		}

		template <typename ...Args>
		std::vector<TypeId> getTemplateArgs(entt::type_list<Args...>) {
			std::vector<TypeId> ret;
			(ret.push_back(getTypeId<Args>()), ...);
			return ret;
		}

		template <typename T, auto Data>
		entt::any makeGetter(TypeInstance instance) {
			if constexpr (std::is_member_pointer_v<decltype(Data)>) {
				assert(getTypeId<T>() == getTypeId(instance.getValue()));

				using DataType = std::remove_reference_t<typename entt::meta_function_helper_t<T, decltype(Data)>::return_type>;

				if constexpr (std::is_invocable_v<decltype(Data), T&>) {
					T* ref = entt::any_cast<T>(&instance.getValue());
					if (ref) {
						return entt::make_any<DataType&>(std::invoke(Data, *ref));
					}
				}
				
				if constexpr (std::is_invocable_v<decltype(Data), const T&>) {
					const T* ref = entt::any_cast<const T>(&instance.getValue());
					if (ref) {
						return entt::make_any<DataType&>(std::invoke(Data, *ref));
					}
				}

				assert(false);
				return entt::any();
			} else {
				return Data;
			}
		}

		template <typename T, auto Data>
		void makeSetter(TypeInstance instance, const entt::any& value) {
			using DataType = std::remove_reference_t<typename entt::meta_function_helper_t<T, decltype(Data)>::return_type>;
			std::invoke(Data, entt::any_cast<T&>(instance.getValue())) = entt::any_cast<const DataType>(value);
		}

		/*template <typename T>
		meta_sequence_container createSequence() {
			return meta_sequence_container{ std::in_place_type<T>, std::move(const_cast<entt::any&>(value)) };
		}*/

		template <typename T>
		AssociativeContainer createAssociative(entt::any instance) {
			assert(!instance.owner());
			return AssociativeContainer{ std::in_place_type<T>, instance.as_ref() };
		}

		template <typename T>
		SequenceContainer createSequence(entt::any instance) {
			assert(!instance.owner());
			return SequenceContainer{ std::in_place_type<T>, instance.as_ref() };
		}
	}

	template <typename T>
	std::string_view getTypeName() {
		std::string_view name = entt::type_id<T>().name();
		size_t offset = name.find_first_of(' ');

		if (offset != std::string::npos) {
			return name.substr(offset + 1);
		}

		return name;
	}

	struct Attribute {
		TypeId type = INVALID_TYPE_ID;
		entt::any value;
	};

	template <typename T>
	struct TypedAttribute : public Attribute {
		const T& getValue() const {
			assert(type == fw::getTypeId<T>());
			return entt::any_cast<const T&>(value);
		}
	};

	struct Field {
		TypeId type = INVALID_TYPE_ID;
		NameHash hash = INVALID_NAME_HASH;
		std::string_view name;
		std::span<const Attribute> attributes;

		void(*setter)(TypeInstance instance, const entt::any& value) = nullptr;
		entt::any(*getter)(TypeInstance instance) = nullptr;

		bool operator==(const Field& other) const {
			return type == other.type;
		}

		template <typename T>
		void set(TypeInstance instance, const T& value) const {
			assert(setter);
			assert(getTypeId<T>() == type);
			setter(std::forward<TypeInstance>(instance), value);
		}

		template <typename T>
		T& get(TypeInstance instance) const {
			assert(getter);
			assert(getTypeId<T>() == type);
			return entt::any_cast<T&>(getter(std::forward<TypeInstance>(instance)));
		}

		entt::any get(TypeInstance instance) const {
			assert(getter);
			return getter(std::forward<TypeInstance>(instance)).as_ref();
		}

		const Attribute* findAttribute(TypeId typeId) const {
			for (const Attribute& attrib : attributes) {
				if (attrib.type == typeId) {
					return &attrib;
				}
			}

			return nullptr;
		}

		template <typename T>
		const TypedAttribute<T>* findAttribute() const {
			return static_cast<const TypedAttribute<T>*>(findAttribute(fw::getTypeId<T>()));
		}

		const Attribute& getAttribute(TypeId typeId) const {
			const Attribute* attrib = findAttribute(typeId);
			assert(attrib);
			return *attrib;
		}

		template <typename T>
		const TypedAttribute<T>& getAttribute() const {
			const TypedAttribute<T>* attrib = findAttribute<T>();
			assert(attrib);
			return *attrib;
		}

		template <typename T>
		const T& getAttributeValue() const {
			return getAttribute<T>().getValue();
		}
	};

	struct Method {
		NameHash hash = INVALID_NAME_HASH;
		std::string_view name;
		std::span<const Attribute> attributes;
	};

	struct TypeInfo {
		TypeId id = INVALID_TYPE_ID;
		TypeId templateType = INVALID_TYPE_ID;
		std::vector<TypeId> templateArgs;
		NameHash hash = INVALID_NAME_HASH;
		std::string_view name;
		std::span<const Field> fields;
		std::span<const Attribute> attributes;
		std::span<const Method> methods;
		entt::any(*construct)() = nullptr;
		SequenceContainer(*createSequenceContainer)(entt::any) = nullptr;
		AssociativeContainer(*createAssociativeContainer)(entt::any) = nullptr;
		internal::TypeTraits traits = internal::TypeTraits::None;
		size_t size = 0;

		template <typename T>
		bool isType() const {
			return getTypeId<T>() == id;
		}

		bool isTemplateSpecialization() const {
			return templateType != INVALID_TYPE_ID;
		}

		bool isClass() const {
			return !!(traits & internal::TypeTraits::Class);
		}

		bool isSequenceContainer() const {
			return !!(traits & internal::TypeTraits::SequenceContainer);
		}

		bool isAssociativeContainer() const {
			return !!(traits & internal::TypeTraits::AssociativeContainer);
		}

		bool isEnum() const {
			return !!(traits & internal::TypeTraits::Enum);
		}

		bool isIntegral() const {
			return !!(traits & internal::TypeTraits::Integral);
		}

		bool isFloat() const {
			return !!(traits & internal::TypeTraits::FloatingPoint);
		}

		bool isArray() const {
			return !!(traits & internal::TypeTraits::Array);
		}

		const Attribute* findAttribute(TypeId typeId) const {
			for (const Attribute& attribute : attributes) {
				if (attribute.type == typeId) {
					return &attribute;
				}
			}

			return nullptr;
		}

		template <typename T>
		const TypedAttribute<T>* findAttribute() const {
			return static_cast<const TypedAttribute<T>*>(findAttribute(fw::getTypeId<T>()));
		}

		const Attribute& getAttribute(TypeId typeId) const {
			const Attribute* attribute = findAttribute(typeId);
			assert(attribute);
			return *attribute;
		}

		template <typename T>
		const TypedAttribute<T>& getAttribute() const {
			const TypedAttribute<T>* attribute = findAttribute<T>();
			assert(attribute);
			return *attribute;
		}

		template <typename T>
		const T& getAttributeValue() const {
			return getAttribute<T>().getValue();
		}

		bool hasAttribute(TypeId typeId) const {
			return findAttribute(typeId) != nullptr;
		}

		template <typename T>
		bool hasAttribute() const {
			return hasAttribute(getTypeId<T>());
		}

		const Field* findField(NameHash nameHash) const {
			for (const Field& field : fields) {
				if (field.hash == nameHash) {
					return &field;
				}
			}

			return nullptr;
		}

		template <typename T>
		void setField(TypeInstance instance, std::string_view name, const T& value) const {
			setField(std::forward<TypeInstance>(instance), getNameHash(name), value);
		}

		template <typename T>
		void setField(TypeInstance instance, NameHash nameHash, const T& value) const {
			const Field* found = findField(nameHash);
			assert(found);
			found->set(std::forward<TypeInstance>(instance), value);
		}
	};

	struct TypeRegistryState {
		std::vector<Field> fields;
		std::vector<Attribute> attributes;
		std::vector<Method> methods;
		std::vector<TypeInfo> types;

		const TypeInfo* findTypeInfo(TypeId typeId) const {
			if ((size_t)typeId < types.size()) {
				const TypeInfo& typeInfo = types[(size_t)typeId];

				if (typeInfo.id != INVALID_TYPE_ID) {
					return &typeInfo;
				}
			}

			return nullptr;
		}

		const TypeInfo* findTypeInfo(NameHash nameHash) const {
			for (const TypeInfo& type : types) {
				if (type.hash == nameHash) {
					return &type;
				}
			}

			return nullptr;
		}

		template <typename T>
		const TypeInfo* findTypeInfo() const {
			return findTypeInfo(getTypeId<T>());
		}
	};

	template <typename T>
	class TypeFactory {
	private:
		TypeInfo* _typeInfo = nullptr;
		TypeRegistryState* _state = nullptr;

		size_t _fieldOffset = 0;
		size_t _attributeOffset = 0;
		size_t _methodOffset = 0;

	public:
		TypeFactory(TypeInfo& typeInfo, TypeRegistryState& state) : _typeInfo(&typeInfo), _state(&state) {
			_fieldOffset = state.fields.size();
			_attributeOffset = state.attributes.size();
			_methodOffset = state.methods.size();
		}

		TypeFactory(TypeFactory&& other) noexcept {
			*this = std::move(other);
		}

		~TypeFactory() {
			if (_typeInfo) {
				_typeInfo->fields = std::span(_state->fields.begin() + _fieldOffset, _state->fields.end());
				_typeInfo->attributes = std::span(_state->attributes.begin() + _attributeOffset, _state->attributes.end());
				_typeInfo->methods = std::span(_state->methods.begin() + _methodOffset, _state->methods.end());
			}
		}

		TypeFactory& operator=(TypeFactory&& other) noexcept {
			_typeInfo = other._typeInfo;
			_state = other._state;
			_fieldOffset = other._fieldOffset;
			_attributeOffset = other._attributeOffset;
			_methodOffset = other._methodOffset;

			other._typeInfo = nullptr;
			other._state = nullptr;
			other._fieldOffset = 0;
			other._attributeOffset = 0;
			other._methodOffset = 0;

			return *this;
		}

		template <auto Data, typename ...AttributeType>
		TypeFactory<T> addField(std::string_view name, AttributeType&&... attribute) {
			assert(_state->fields.size() < _state->fields.capacity());

			size_t attributeOffset = _state->attributes.size();

			([&] {
				assert(_state->attributes.size() < _state->attributes.capacity());
				assert(_state->findTypeInfo<AttributeType>()); // Attribute types must be registered

				_state->attributes.push_back(Attribute{
					.type = getTypeId<AttributeType>(),
					.value = attribute
				});
			} (), ...);

			if constexpr (std::is_member_object_pointer_v<decltype(Data)>) {
				using DataType = std::remove_reference_t<typename entt::meta_function_helper_t<T, decltype(Data)>::return_type>;
				assert(_state->findTypeInfo<DataType>()); // Types referenced in fields must be registered

				_state->fields.push_back(Field{
					.type = getTypeId<DataType>(),
					.hash = getNameHash(name),
					.name = name,
					.attributes = std::span(_state->attributes.begin() + attributeOffset, _state->attributes.end()),
					.setter = &internal::makeSetter<T, Data>,
					.getter = &internal::makeGetter<T, Data>,
				});
			} else {
				using DataType = std::remove_reference_t<std::remove_pointer_t<decltype(Data)>>;
				assert(_state->findTypeInfo<DataType>());

				_state->fields.push_back(Field{
					.type = getTypeId<DataType>(),
					.hash = getNameHash(name),
					.name = name,
					.attributes = std::span(_state->attributes.begin() + attributeOffset, _state->attributes.end()),
					.setter = nullptr,
					.getter = &internal::makeGetter<T, Data>,
				});
			}

			return std::move(*this);
		}

		//template<typename = std::enable_if_t<std::is_enum_v<T>>>
		void addEnumFields() {
			magic_enum::enum_for_each<T>([&](auto val) {
				constexpr T valType = val;
				std::string_view name = magic_enum::enum_name(valType);

				_state->fields.push_back(Field{
					.type = getTypeId<T>(),
					.hash = getNameHash(name),
					.name = name,
					.getter = &internal::makeGetter<T, valType>
				});
			});
		}

		const TypeInfo& getType() const {
			assert(_typeInfo);
			return *_typeInfo;
		}
	};

	class TypeFactoryBase {
	private:
		TypeId _typeId;

	public:
		template <typename T>
		TypeFactoryBase(TypeFactory<T>&& factory) {
			_typeId = factory.getType().id;
		}
	};

	class TypeRegistry {
	private:
		TypeRegistryState _state;

	public:
		TypeRegistry(size_t maxFields = 256, size_t maxAttributes = 256, size_t maxMethods = 256) {
			_state.fields.reserve(maxFields);
			_state.attributes.reserve(maxAttributes);
			_state.methods.reserve(maxMethods);
		}

		~TypeRegistry() = default;

		template <typename T>
		TypeFactory<T> addType(std::string_view name = "") {
			assert(!findTypeInfo<T>());

			if (name.empty()) {
				name = getTypeName<T>();
			}

			NameHash nameHash = getNameHash(name);

			return std::move(addType<T>(nameHash, name));
		}

		template <typename T>
		TypeFactory<T> addType(NameHash nameHash, std::string_view name = "") {
			entt::id_type typeId = entt::type_id<T>().index();

			if (name.empty()) {
				name = getTypeName<T>();
			}

			_state.types.resize(std::max(_state.types.size(), (size_t)typeId + 1));

			TypeInfo typeInfo = TypeInfo{
				.id = getTypeId<T>(),
				.hash = nameHash,
				.name = name,
				.construct = entt::make_any<T>,
				.traits = internal::getTypeTraits<T>(),
				.size = sizeof(T)
			};

			if constexpr (entt::is_complete_v<entt::meta_template_traits<T>>) {
				typeInfo.templateType = getTypeId<typename entt::meta_template_traits<T>::class_type>();
				typeInfo.templateArgs = internal::getTemplateArgs(typename entt::meta_template_traits<T>::args_type{});
			}

			if constexpr (entt::is_complete_v<entt::meta_sequence_container_traits<T>>) {
				typeInfo.createSequenceContainer = &internal::createSequence<T>;
			}

			if constexpr (entt::is_complete_v<entt::meta_associative_container_traits<T>>) {
				typeInfo.createAssociativeContainer = &internal::createAssociative<T>;
			}

			_state.types[typeId] = std::move(typeInfo);

			return std::move(TypeFactory<T>(_state.types[typeId], _state));
		}

		template<typename T>//, typename = std::enable_if_t<std::is_enum_v<T>>>
		void addEnum() {
			addType<T>().addEnumFields();
		}

		void addCommonTypes() {
			addType<bool>("bool");
			addType<int8>("int8");
			addType<uint8>("uint8");
			addType<int16>("int16");
			addType<uint16>("uint16");
			addType<int32>("int32");
			addType<uint32>("uint32");
			addType<int64>("int64");
			addType<uint64>("uint64");
			addType<f32>("f32");
			addType<f64>("f64");
			addType<std::string>("std::string");
		}

		const TypeInfo* findTypeInfo(NameHash nameHash) const {
			return _state.findTypeInfo(nameHash);
		}

		const TypeInfo* findTypeInfo(std::string_view name) const {
			return _state.findTypeInfo(getNameHash(name));
		}

		const TypeInfo* findTypeInfo(TypeId typeId) const {
			return _state.findTypeInfo(typeId);
		}

		const TypeInfo* findTypeInfo(const entt::any& value) const {
			return _state.findTypeInfo(getTypeId(value));
		}

		template <typename T>
		const TypeInfo* findTypeInfo() const {
			return _state.findTypeInfo<T>();
		}

		const TypeInfo& getTypeInfo(NameHash nameHash) const {
			const TypeInfo* typeInfo = findTypeInfo(nameHash);
			assert(typeInfo);
			return *typeInfo;
		}

		const TypeInfo& getTypeInfo(TypeId typeId) const {
			assert((size_t)typeId < _state.types.size());
			assert(_state.types[(size_t)typeId].id != INVALID_TYPE_ID);
			return _state.types[(size_t)typeId];
		}

		const TypeInfo& getTypeInfo(const entt::any& value) const {
			return getTypeInfo(getTypeId(value));
		}

		template <typename T>
		const TypeInfo& getTypeInfo() const {
			return getTypeInfo(getTypeId<T>());
		}
	};
}
