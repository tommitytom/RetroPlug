#pragma once

#include "foundation/Types.h"
#include "foundation/MetaTypes.h"

namespace fw {
	class AnyRef {
	private:
		struct VTableBase {
			TypeId _type;
			uint32 _size;

			VTableBase(TypeId type, uint32 size) : _type(type), _size(size) {}
			virtual bool compare(const AnyRef& lhs, const AnyRef& rhs) const = 0;
			virtual void move(AnyRef& lhs, AnyRef&& rhs) const = 0;
			virtual void copy(AnyRef& lhs, const AnyRef& rhs) const = 0;
		};

		template <typename T>
		struct VTable : public VTableBase {
			VTable() : VTableBase(fw::getTypeId<T>(), sizeof(T)) {}

			bool compare(const AnyRef& lhs, const AnyRef& rhs) const override {
				assert(lhs.getType() == rhs.getType());
				return lhs.getValue<T>() == rhs.getValue<T>();
			}

			void move(AnyRef& lhs, AnyRef&& rhs) const override {
				assert(std::movable<T>);
				if constexpr (std::movable<T>) {
					lhs.setValue<T>(std::move(rhs.getValue<T>()));
					rhs.reset();
				}
			}

			void copy(AnyRef& lhs, const AnyRef& rhs) const override {
				assert(std::copyable<T>);
				if constexpr (std::copyable<T>) {
					lhs.setValue(rhs.getValue<T>());
				}
			}
		};

		void* _data = nullptr;
		VTableBase* _vtable = nullptr;

	public:
		AnyRef() {}

		template<typename T>
		explicit AnyRef(const T& value) {
			initializeVTable<T>();
			_data = static_cast<void*>(&value);
		}

		template<typename T>
		explicit AnyRef(T& value) {
			initializeVTable<T>();
			_data = static_cast<void*>(&value);
		}

		~AnyRef() {
			reset();
		}

		template <typename T>
		void setValue(T&& value) {
			getValue<T>() = std::move(value);
		}

		template <typename T>
		void setValue(const T& value) {
			getValue<T>() = value;
		}

		void setValue(AnyRef&& other) {
			if (other.isValid()) {
				other._vtable->move(*this, std::forward<AnyRef>(other));
			} else {
				reset();
			}
		}

		void setValue(const AnyRef& other) noexcept {
			if (other.isValid()) {
				other._vtable->copy(*this, other);
			} else {
				reset();
			}
		}

		bool operator==(const AnyRef& other) const noexcept {
			if (_vtable->_type != other._vtable->_type) {
				return false;
			}

			return _vtable->compare(*this, other);
		}

		void reset() {
			_data = nullptr;
			_vtable = nullptr;
		}

		template <typename T>
		T& getValue() {
			assert(isValid());
			assert(getType() == fw::getTypeId<T>());
			return *reinterpret_cast<T*>(_data);
		}

		template <typename T>
		const T& getValue() const {
			assert(isValid());
			assert(getType() == fw::getTypeId<T>());
			return *reinterpret_cast<const T*>(_data);
		}

		TypeId getType() const {
			return _vtable->_type;
		}

		uint32 getSize() const {
			return _vtable->_size;
		}

		const void* getData() const {
			return _data;
		}

		void* getData() {
			return _data;
		}

		bool isValid() const {
			return _vtable;
		}

	private:
		template <typename T>
		void initializeVTable() {
			static VTable<T> vtable; // make const?
			_vtable = &vtable;
		}
	};

	/*template <const uint32 BaseSize>
	class BaseAny {
	private:
		struct VTableBase {
			TypeId _type;
			uint32 _size;

			VTableBase(TypeId type, uint32 size) : _type(type), _size(size) {}
			virtual bool compare(const BaseAny& lhs, const BaseAny& rhs) const = 0;
			virtual void move(BaseAny& lhs, BaseAny&& rhs) const = 0;
			virtual void copy(BaseAny& lhs, const BaseAny& rhs) const = 0;

			bool isAllocated() const {
				return _size > BaseSize;
			}
		};

		template <typename T>
		struct VTable : public VTableBase {
			VTable() : VTableBase(fw::getTypeId<T>(), sizeof(T)) {}

			bool compare(const BaseAny& lhs, const BaseAny& rhs) const override {
				assert(lhs.getType() == rhs.getType());
				return lhs.getValue<T>() == rhs.getValue<T>();
			}

			void move(BaseAny& lhs, BaseAny&& rhs) const override {
				assert(std::movable<T>);
				if constexpr (std::movable<T>) {
					lhs = std::move(rhs.getValue<T>());
					rhs.reset();
				}
			}

			void copy(BaseAny& lhs, const BaseAny& rhs) const override {
				assert(std::copyable<T>);
				if constexpr (std::copyable<T>) {
					lhs = rhs.getValue<T>();
				}
			}
		};

		char _sbo[BaseSize]{};
		char* _data = nullptr;
		VTableBase* _vtable = nullptr;

	public:
		BaseAny() {}

		template<typename T>
		explicit BaseAny(T&& value) noexcept {
			*this = std::move(value);
		}

		template<typename T>
		explicit BaseAny(const T& value) noexcept {
			*this = value;
		}

		~BaseAny() {
			reset();
		}

		template <typename T> requires std::movable<T>
		BaseAny& operator=(T&& value) noexcept {
			initialize<T>();
			new (_data) T(std::move(value));
			return *this;
		}

		template <typename T> requires std::copyable<T>
		BaseAny& operator=(const T& value) noexcept {
			initialize<T>();
			new (_data) T(value);
			return *this;
		}

		BaseAny& operator=(BaseAny&& other) noexcept {
			if (other.isValid()) {
				other._vtable->move(*this, std::forward<BaseAny>(other));
			} else {
				reset();
			}

			return *this;
		}

		BaseAny& operator=(const BaseAny& other) noexcept {
			if (other.isValid()) {
				other._vtable->copy(*this, other);
			} else {
				reset();
			}

			return *this;
		}

		bool operator==(const BaseAny& other) const noexcept {
			if (_vtable->_type != other._vtable->_type) {
				return false;
			}

			return _vtable->compare(*this, other);
		}

		void reset() {
			if (_vtable && _vtable->isAllocated()) {
				delete[] _data;
			}

			_data = nullptr;
			_vtable = nullptr;
		}

		template <typename T>
		T& getValue() {
			assert(isValid());
			assert(getType() == fw::getTypeId<T>());
			return *reinterpret_cast<T*>(_data);
		}

		template <typename T>
		const T& getValue() const {
			assert(isValid());
			assert(getType() == fw::getTypeId<T>());
			return *reinterpret_cast<const T*>(_data);
		}

		TypeId getType() const {
			return _vtable->_type;
		}

		uint32 getSize() const {
			return _vtable->_size;
		}

		const char* getData() const {
			return _data;
		}

		char* getData() {
			return _data;
		}

		bool isValid() const {
			return _vtable;
		}

	private:
		template <typename T>
		void initialize() {
			reset();

			static VTable<T> vtable;
			_vtable = &vtable;

			if constexpr (sizeof(T) <= BaseSize) {
				_data = _sbo;
			} else {
				_data = new char[sizeof(T)];
			}
		}
	};

	using Any = BaseAny<16>;*/
}
