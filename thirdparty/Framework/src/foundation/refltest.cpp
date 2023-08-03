#include <refl.hpp>
#include <spdlog/spdlog.h>
#include "foundation/ReflUtil.h"

#include "ui/View.h"

struct Foo {
	std::string str;
	
	std::string& getTextFull() { return str; }
	const std::string& getTextFull() const { return str; }
	void setTextFull(const std::string& value) { str = value; }

	const std::string& getTextConst() const { return str; }
	void setTextConst(const std::string& value) { str = value; }

	const std::string& getTextReadOnly() const { return str; }

	std::string& getTextRefOnly() { return str; }

	void testFunc() {}
};

REFL_AUTO(
	type(Foo),
	func(getTextFull, property("textFull")), func(setTextFull, property("textFull")),
	func(getTextConst, property("textConst")), func(setTextConst, property("textConst")),
	func(getTextReadOnly, property("textReadOnly")),
	func(getTextRefOnly, property("textRefOnly")),
	field(str),
	func(testFunc)
)

/*
[2023-07-01 16:22:55.109] [info] textFull: Property,Function,Readable,Reader,Writer,Ref,
[2023-07-01 16:22:55.110] [info] textFull: Property,Function,Writable,Reader,Writer,
[2023-07-01 16:22:55.110] [info] textConst: Property,Function,Readable,Reader,Writer,
[2023-07-01 16:22:55.110] [info] textConst: Property,Function,Writable,Reader,Writer,
[2023-07-01 16:22:55.110] [info] textReadOnly: Property,Function,Readable,Reader,
[2023-07-01 16:22:55.110] [info] textRefOnly: Property,Function,Ref,
[2023-07-01 16:22:55.110] [info] str: Readable,Writable,Reader,Writer,
[2023-07-01 16:22:55.111] [info] testFunc: Function,
[2023-07-01 16:22:55.111] [info] textFull
[2023-07-01 16:22:55.111] [info] textConst
[2023-07-01 16:22:55.111] [info] textRefOnly
*/

template <typename T, typename MemberDescriptor>
std::string getMemberFlags(MemberDescriptor member) noexcept {
	std::string flags;
	if (is_property(member)) { flags += "Property,"; }
	if (is_function(member)) { flags += "Function,"; }
	if (is_readable(member)) { flags += "Readable,"; }
	if (is_writable(member)) { flags += "Writable,"; }

	if constexpr (is_readable(member) || is_property(member)) {
		if (has_reader(member)) { flags += "Reader,"; }
	}

	if constexpr (is_writable(member) || is_property(member)) {
		if (has_writer(member)) { flags += "Writer,"; }
	}

	if constexpr (is_function(member)) {
		if constexpr (std::is_invocable_v<MemberDescriptor, T>) {
			flags += "Invokable,";
		}
	}
	
	if constexpr ((fw::ReflUtil::isMutableRef<T>(member))) {
		flags += "Ref,";
	}

	return flags;
}

int reflTest() {
	constexpr auto type = refl::reflect<Foo>();
	constexpr auto members = get_members(type);

	for_each(members, [&](auto member) {
		spdlog::info("{}: {}", get_display_name(member), getMemberFlags<Foo>(member));
	});

	static constexpr auto filtered = filter(refl::member_list<Foo>{}, [&](auto member) {
		return fw::ReflUtil::isWritable<Foo>(member);
	});

	spdlog::info("-----");

	for_each(filtered, [&](auto member) {
		spdlog::info("{}: {}", get_display_name(member), getMemberFlags<Foo>(member));
	});

	return 0;
}
