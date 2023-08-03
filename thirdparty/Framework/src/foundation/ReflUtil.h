#pragma once

#include <refl.hpp>

namespace fw::ReflUtil {
	template <typename T, typename MemberDescriptor>
	constexpr bool isInvokable(MemberDescriptor member) noexcept {
		return is_function(member) && std::is_invocable_v<MemberDescriptor, T>;
	}

	template <typename T, typename MemberDescriptor>
	constexpr bool isMutableRef(MemberDescriptor member) noexcept {
		if constexpr (is_field(member)) {
			return true;
		}

		if constexpr (is_function(member) && !is_property(member)) {
			return false;
		}

		if constexpr (isInvokable<T>(member)) {
			using MemberType = typename MemberDescriptor::template return_type<typename MemberDescriptor::declaring_type&>;
			if constexpr (std::is_reference_v<MemberType> && !std::is_const_v<std::remove_reference_t<MemberType>>) {
				return true;
			}
		}

		return false;
	}

	template <typename T, typename MemberDescriptor>
	constexpr bool isWritable(MemberDescriptor member) noexcept {
		if constexpr (ReflUtil::isMutableRef<T>(member)) {
			return true;
		}

		if constexpr (is_property(member) && !is_writable(member)) {
			return has_writer(member);
		}

		return false;
	}	
}
