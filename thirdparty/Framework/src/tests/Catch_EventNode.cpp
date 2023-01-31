#include <catch/catch.hpp>

#include "foundation/Event.h"

using namespace fw;

TEST_CASE("Event Node", "[EventNode]") {
	EventNode root("Root");

	SECTION("Remove node") {
		{
			EventNode other = root.spawn("Other");
			root.update();
			other.update();

			REQUIRE(root.getState().nodes.size() == 2);
		}

		root.update();

		REQUIRE(root.getState().nodes.size() == 1);		

		{
			EventNode other = root.spawn("Other");
			root.update();
			other.update();

			REQUIRE(root.getState().nodes.size() == 2);
		}

		root.update();

		REQUIRE(root.getState().nodes.size() == 1);
	}
}
