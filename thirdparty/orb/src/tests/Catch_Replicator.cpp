#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>

#include "Replicator.h"

using namespace orb;
using namespace std::chrono_literals;

// Test components
struct Position {
	float x, y, z;

	bool operator==(const Position& other) const {
		return x == other.x && y == other.y && z == other.z;
	}
};

struct Velocity {
	float dx, dy, dz;

	bool operator==(const Velocity& other) const {
		return dx == other.dx && dy == other.dy && dz == other.dz;
	}
};

struct Health {
	int current;
	int max;

	bool operator==(const Health& other) const {
		return current == other.current && max == other.max;
	}
};

struct Tag {
	std::string name;

	bool operator==(const Tag& other) const {
		return name == other.name;
	}
};

// Helper function for single-threaded tests
void fullUpdate(entt::registry& registry) {
	Replicator::beginUpdate(registry);
	Replicator::getContext(registry).eventNode.update();
	Replicator::endUpdate(registry);
}

// Helper function for multi-threaded tests with timeout
void threadedUpdate(entt::registry& registry, std::atomic<bool>& running) {
	while (running.load()) {
		Replicator::beginUpdate(registry);
		Replicator::getContext(registry).eventNode.wait(100); // 100 microseconds
		Replicator::endUpdate(registry);
	}
}

TEST_CASE("Basic Single-Threaded Replication", "[replicator][single-thread]") {

	SECTION("Owner-Subscriber Setup") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		REQUIRE(Replicator::subscribe(subReg, subNode, ownerNode.getId(), false));

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		auto& ownerCtx = Replicator::getContext(ownerReg);
		auto& subCtx = Replicator::getContext(subReg);

		REQUIRE(ownerCtx.owner == true);
		REQUIRE(ownerCtx.canMutate == true);
		REQUIRE(ownerCtx.state == Replicator::ReplicatorState::Ready);

		REQUIRE(subCtx.owner == false);
		REQUIRE(subCtx.canMutate == false);
		REQUIRE(subCtx.state == Replicator::ReplicatorState::Ready);

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Component Registration and Replication") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Health>(ownerReg);

		REQUIRE(Replicator::isReplicating<Position>(Replicator::getContext(ownerReg)));
		REQUIRE(Replicator::isReplicating<Health>(Replicator::getContext(ownerReg)));
		REQUIRE_FALSE(Replicator::isReplicating<Velocity>(Replicator::getContext(ownerReg)));

		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create entity with components
		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e, 1.0f, 2.0f, 3.0f);
		ownerReg.emplace<Health>(e, 75, 100);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(e));
		REQUIRE(subReg.all_of<Position, Health>(e));

		auto& pos = subReg.get<Position>(e);
		REQUIRE(pos == Position{1.0f, 2.0f, 3.0f});

		auto& health = subReg.get<Health>(e);
		REQUIRE(health == Health{75, 100});

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Entity Lifecycle") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Spawn multiple entities
		std::vector<entt::entity> entities;
		for (int i = 0; i < 5; ++i) {
			entt::entity e = Replicator::spawn(ownerReg);
			ownerReg.emplace<Position>(e, float(i), float(i * 2), float(i * 3));
			entities.push_back(e);
		}

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Verify all entities exist in subscriber
		for (auto e : entities) {
			REQUIRE(subReg.valid(e));
			REQUIRE(subReg.all_of<Position>(e));
		}

		// Destroy some entities
		Replicator::destroy(ownerReg, entities[1]);
		Replicator::destroy(ownerReg, entities[3]);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(entities[0]));
		REQUIRE_FALSE(subReg.valid(entities[1]));
		REQUIRE(subReg.valid(entities[2]));
		REQUIRE_FALSE(subReg.valid(entities[3]));
		REQUIRE(subReg.valid(entities[4]));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Component Updates") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e, 0.0f, 0.0f, 0.0f);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Test replace
		ownerReg.replace<Position>(e, 10.0f, 20.0f, 30.0f);
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.get<Position>(e) == Position{10.0f, 20.0f, 30.0f});

		// Test patch
		ownerReg.patch<Position>(e, [](Position& p) {
			p.x += 5.0f;
			p.y *= 2.0f;
			p.z = 0.0f;
		});
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.get<Position>(e) == Position{15.0f, 40.0f, 0.0f});

		// Test component removal
		ownerReg.remove<Position>(e);
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE_FALSE(subReg.all_of<Position>(e));
		REQUIRE(subReg.valid(e)); // Entity still exists

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Bidirectional Replication") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Health>(ownerReg);

		// Subscribe with mutation privileges
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), true);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Subscriber creates entity
		entt::entity subEntity = Replicator::spawn(subReg);
		subReg.emplace<Position>(subEntity, 5.0f, 5.0f, 5.0f);
		subReg.emplace<Health>(subEntity, 50, 50);

		fullUpdate(subReg);
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Verify entity exists in owner
		REQUIRE(ownerReg.valid(subEntity));
		REQUIRE(ownerReg.all_of<Position, Health>(subEntity));
		REQUIRE(ownerReg.get<Position>(subEntity) == Position{5.0f, 5.0f, 5.0f});
		REQUIRE(ownerReg.get<Health>(subEntity) == Health{50, 50});

		// Subscriber modifies component
		subReg.patch<Health>(subEntity, [](Health& h) {
			h.current = 25;
		});

		fullUpdate(subReg);
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(ownerReg.get<Health>(subEntity).current == 25);

		// Subscriber destroys entity
		Replicator::destroy(subReg, subEntity);

		fullUpdate(subReg);
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE_FALSE(ownerReg.valid(subEntity));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Multiple Subscribers") {
		EventNode ownerNode("owner");
		EventNode sub1Node = ownerNode.spawn("sub1");
		EventNode sub2Node = ownerNode.spawn("sub2");
		EventNode sub3Node = ownerNode.spawn("sub3");

		entt::registry ownerReg;
		entt::registry sub1Reg;
		entt::registry sub2Reg;
		entt::registry sub3Reg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);

		// Mix of mutable and read-only subscribers
		Replicator::subscribe(sub1Reg, sub1Node, ownerNode.getId(), true);
		Replicator::subscribe(sub2Reg, sub2Node, ownerNode.getId(), false);
		Replicator::subscribe(sub3Reg, sub3Node, ownerNode.getId(), true);

		auto updateAll = [&]() {
			fullUpdate(ownerReg);
			fullUpdate(sub1Reg);
			fullUpdate(sub2Reg);
			fullUpdate(sub3Reg);
		};

		updateAll();

		// Owner creates entity
		entt::entity e1 = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e1, 1.0f, 1.0f, 1.0f);

		updateAll();

		// Verify all subscribers have it
		REQUIRE(sub1Reg.valid(e1));
		REQUIRE(sub2Reg.valid(e1));
		REQUIRE(sub3Reg.valid(e1));

		// Sub1 creates entity
		entt::entity e2 = Replicator::spawn(sub1Reg);
		sub1Reg.emplace<Position>(e2, 2.0f, 2.0f, 2.0f);

		updateAll();

		// Verify propagation
		REQUIRE(ownerReg.valid(e2));
		REQUIRE(sub2Reg.valid(e2));
		REQUIRE(sub3Reg.valid(e2));

		// Sub3 modifies e1
		sub3Reg.replace<Position>(e1, 3.0f, 3.0f, 3.0f);

		updateAll();

		// Verify all have the update
		REQUIRE(ownerReg.get<Position>(e1) == Position{3.0f, 3.0f, 3.0f});
		REQUIRE(sub1Reg.get<Position>(e1) == Position{3.0f, 3.0f, 3.0f});
		REQUIRE(sub2Reg.get<Position>(e1) == Position{3.0f, 3.0f, 3.0f});

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(sub1Reg);
		Replicator::shutdown(sub2Reg);
		Replicator::shutdown(sub3Reg);
	}

	SECTION("Unsubscribe") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e, 1.0f, 2.0f, 3.0f);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(e));

		// Unsubscribe
		Replicator::unsubscribe(subReg, ownerNode.getId());

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create new entity after unsubscribe
		entt::entity e2 = Replicator::spawn(ownerReg);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Sub should not receive new entity
		REQUIRE_FALSE(subReg.valid(e2));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}
}

TEST_CASE("Multi-Threaded Replication", "[replicator][multi-thread]") {

	SECTION("Basic Thread Safety") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Health>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		std::atomic<bool> running{true};

		// Subscriber thread
		std::thread subThread([&subReg, &running]() {
			threadedUpdate(subReg, running);
		});

		// Owner thread operations
		std::thread ownerThread([&ownerReg, &running]() {
			std::vector<entt::entity> entities;

			// Create entities
			for (int i = 0; i < 10; ++i) {
				entt::entity e = Replicator::spawn(ownerReg);
				ownerReg.emplace<Position>(e, float(i), 0.0f, 0.0f);
				ownerReg.emplace<Health>(e, 100, 100);
				entities.push_back(e);
			}

			threadedUpdate(ownerReg, running);
		});

		// Let threads run
		std::this_thread::sleep_for(50ms);

		// Stop threads
		running = false;
		subThread.join();
		ownerThread.join();

		// Verify final state
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		int entityCount = 0;
		subReg.each([&entityCount](auto entity) {
			entityCount++;
		});

		REQUIRE(entityCount == 10);

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Concurrent Bidirectional Updates") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Velocity>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), true);

		std::atomic<bool> running{true};
		std::atomic<int> ownerCreated{0};
		std::atomic<int> subCreated{0};

		// Owner thread
		std::thread ownerThread([&]() {
			while (running) {
				// Create entities periodically
				entt::entity e = Replicator::spawn(ownerReg);
				ownerReg.emplace<Position>(e, 1.0f, 1.0f, 1.0f);
				ownerCreated++;

				Replicator::beginUpdate(ownerReg);
				Replicator::getContext(ownerReg).eventNode.wait(1000);
				Replicator::endUpdate(ownerReg);

				std::this_thread::sleep_for(5ms);
			}
		});

		// Subscriber thread
		std::thread subThread([&]() {
			while (running) {
				// Create entities from subscriber
				entt::entity e = Replicator::spawn(subReg);
				subReg.emplace<Velocity>(e, 2.0f, 2.0f, 2.0f);
				subCreated++;

				Replicator::beginUpdate(subReg);
				Replicator::getContext(subReg).eventNode.wait(1000);
				Replicator::endUpdate(subReg);

				std::this_thread::sleep_for(7ms);
			}
		});

		// Run for a period
		std::this_thread::sleep_for(100ms);
		running = false;

		ownerThread.join();
		subThread.join();

		// Final sync
		for (int i = 0; i < 10; ++i) {
			fullUpdate(ownerReg);
			fullUpdate(subReg);
		}

		// Count entities in both registries
		int ownerCount = 0;
		int subCount = 0;

		ownerReg.each([&ownerCount](auto entity) {
			ownerCount++;
		});

		subReg.each([&subCount](auto entity) {
			subCount++;
		});

		// Both should have same count
		REQUIRE(ownerCount == subCount);
		REQUIRE(ownerCount == (ownerCreated + subCreated));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Stress Test - Many Entities") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Velocity>(ownerReg);
		Replicator::replicate<Health>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		const int ENTITY_COUNT = 1000;
		std::atomic<bool> running{true};

		// Subscriber thread
		std::thread subThread([&]() {
			threadedUpdate(subReg, running);
		});

		// Create many entities
		std::vector<entt::entity> entities;
		for (int i = 0; i < ENTITY_COUNT; ++i) {
			entt::entity e = Replicator::spawn(ownerReg);
			ownerReg.emplace<Position>(e, float(i), float(i), float(i));
			if (i % 2 == 0) {
				ownerReg.emplace<Velocity>(e, 1.0f, 0.0f, 0.0f);
			}
			if (i % 3 == 0) {
				ownerReg.emplace<Health>(e, 100, 100);
			}
			entities.push_back(e);
		}

		// Owner thread for updates
		std::thread ownerThread([&]() {
			threadedUpdate(ownerReg, running);
		});

		// Let replication happen
		std::this_thread::sleep_for(100ms);

		running = false;
		ownerThread.join();
		subThread.join();

		// Final sync
		for (int i = 0; i < 10; ++i) {
			fullUpdate(ownerReg);
			fullUpdate(subReg);
		}

		// Verify all entities replicated
		for (auto e : entities) {
			REQUIRE(subReg.valid(e));
			REQUIRE(subReg.all_of<Position>(e));

			auto& ownerPos = ownerReg.get<Position>(e);
			auto& subPos = subReg.get<Position>(e);
			REQUIRE(ownerPos == subPos);
		}

		// Verify component counts
		auto ownerPosView = ownerReg.view<Position>();
		auto subPosView = subReg.view<Position>();
		REQUIRE(ownerPosView.size() == subPosView.size());

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Multiple Thread Subscribers") {
		EventNode ownerNode("owner");
		EventNode sub1Node = ownerNode.spawn("sub1");
		EventNode sub2Node = ownerNode.spawn("sub2");
		EventNode sub3Node = ownerNode.spawn("sub3");

		entt::registry ownerReg;
		entt::registry sub1Reg, sub2Reg, sub3Reg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);

		Replicator::subscribe(sub1Reg, sub1Node, ownerNode.getId(), false);
		Replicator::subscribe(sub2Reg, sub2Node, ownerNode.getId(), false);
		Replicator::subscribe(sub3Reg, sub3Node, ownerNode.getId(), false);

		std::atomic<bool> running{true};

		// Start subscriber threads
		std::thread sub1Thread([&]() { threadedUpdate(sub1Reg, running); });
		std::thread sub2Thread([&]() { threadedUpdate(sub2Reg, running); });
		std::thread sub3Thread([&]() { threadedUpdate(sub3Reg, running); });

		// Owner creates entities
		std::vector<entt::entity> entities;
		for (int i = 0; i < 50; ++i) {
			entt::entity e = Replicator::spawn(ownerReg);
			ownerReg.emplace<Position>(e, float(i), 0.0f, 0.0f);
			entities.push_back(e);

			fullUpdate(ownerReg);
			std::this_thread::sleep_for(2ms);
		}

		// Let all updates propagate
		std::this_thread::sleep_for(50ms);

		running = false;
		sub1Thread.join();
		sub2Thread.join();
		sub3Thread.join();

		// Final sync
		for (int i = 0; i < 10; ++i) {
			fullUpdate(ownerReg);
			fullUpdate(sub1Reg);
			fullUpdate(sub2Reg);
			fullUpdate(sub3Reg);
		}

		// All subscribers should have all entities
		for (auto e : entities) {
			REQUIRE(sub1Reg.valid(e));
			REQUIRE(sub2Reg.valid(e));
			REQUIRE(sub3Reg.valid(e));
		}

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(sub1Reg);
		Replicator::shutdown(sub2Reg);
		Replicator::shutdown(sub3Reg);
	}
}

TEST_CASE("Error Recovery", "[replicator][error]") {

	SECTION("Inconsistent State Recovery") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create consistent state first
		entt::entity e1 = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e1, 1.0f, 1.0f, 1.0f);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(e1));

		// Manually create inconsistency - create entity directly in subscriber
		entt::entity badEntity = subReg.create();

		// Now when owner tries to create an entity with same ID, subscriber should detect error
		// This would happen if we manually set the entity ID, but for testing we'll trigger
		// error by trying to add component to non-existent entity

		auto& ctx = Replicator::getContext(subReg);

		// Force error state
		Replicator::toggleError(subReg);
		REQUIRE(ctx.state == Replicator::ReplicatorState::Error);

		// Error recovery should request state
		fullUpdate(subReg); // Requests state
		fullUpdate(ownerReg); // Sends state
		fullUpdate(subReg); // Receives state

		// Should be back to ready
		REQUIRE(ctx.state == Replicator::ReplicatorState::Ready);

		// Verify subscriber has correct state
		REQUIRE(subReg.valid(e1));
		REQUIRE_FALSE(subReg.valid(badEntity)); // Bad entity should be gone

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Component Dereplicate") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Health>(ownerReg);

		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e, 1.0f, 2.0f, 3.0f);
		ownerReg.emplace<Health>(e, 100, 100);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.all_of<Position, Health>(e));

		// Stop replicating Health
		Replicator::dereplicate<Health>(ownerReg);
		REQUIRE_FALSE(Replicator::isReplicating<Health>(Replicator::getContext(ownerReg)));

		// New entities shouldn't replicate Health
		entt::entity e2 = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e2, 5.0f, 5.0f, 5.0f);
		ownerReg.emplace<Health>(e2, 50, 50);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.all_of<Position>(e2));
		REQUIRE_FALSE(subReg.all_of<Health>(e2)); // Health not replicated

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}
}

TEST_CASE("Edge Cases", "[replicator][edge]") {

	SECTION("Empty Component") {
		struct EmptyTag {};

		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<EmptyTag>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<EmptyTag>(e);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(e));
		REQUIRE(subReg.all_of<EmptyTag>(e));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Large Component") {
		struct LargeData {
			std::array<float, 10000> values;

			LargeData() {
				values.fill(42.0f);
			}

			bool operator==(const LargeData& other) const {
				return values == other.values;
			}
		};

		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<LargeData>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<LargeData>(e);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		REQUIRE(subReg.valid(e));
		REQUIRE(subReg.all_of<LargeData>(e));
		REQUIRE(subReg.get<LargeData>(e) == ownerReg.get<LargeData>(e));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Rapid Updates") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		entt::entity e = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(e, 0.0f, 0.0f, 0.0f);

		// Rapid fire updates without processing
		for (int i = 0; i < 100; ++i) {
			ownerReg.patch<Position>(e, [i](Position& p) {
				p.x = float(i);
			});
		}

		// Process all at once
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Should have final value
		REQUIRE(subReg.get<Position>(e).x == 99.0f);

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Mixed Local and Replicated Entities") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create replicated entity
		entt::entity replicated = Replicator::spawn(ownerReg);
		ownerReg.emplace<Position>(replicated, 1.0f, 1.0f, 1.0f);

		// Create local entity in owner
		entt::entity localOwner = ownerReg.create();
		ownerReg.emplace<Position>(localOwner, 2.0f, 2.0f, 2.0f);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create local entity in subscriber
		entt::entity localSub = subReg.create();
		subReg.emplace<Position>(localSub, 3.0f, 3.0f, 3.0f);

		// Verify only replicated entity is shared
		REQUIRE(subReg.valid(replicated));
		REQUIRE_FALSE(subReg.valid(localOwner));
		REQUIRE_FALSE(ownerReg.valid(localSub));

		// Local entities remain local
		REQUIRE(ownerReg.valid(localOwner));
		REQUIRE(subReg.valid(localSub));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("Component on Non-Replicated Entity") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Create local entity and add replicated component type
		entt::entity local = ownerReg.create();
		ownerReg.emplace<Position>(local, 1.0f, 1.0f, 1.0f);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		// Component should not replicate for non-replicated entity
		REQUIRE_FALSE(subReg.valid(local));

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}
}

TEST_CASE("Performance", "[replicator][performance]") {

	SECTION("Batch Entity Creation") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		fullUpdate(ownerReg);
		fullUpdate(subReg);

		const int BATCH_SIZE = 10000;

		auto start = std::chrono::high_resolution_clock::now();

		// Create many entities at once
		std::vector<entt::entity> entities;
		for (int i = 0; i < BATCH_SIZE; ++i) {
			entt::entity e = Replicator::spawn(ownerReg);
			ownerReg.emplace<Position>(e, float(i), 0.0f, 0.0f);
			entities.push_back(e);
		}

		// Single update for all
		fullUpdate(ownerReg);
		fullUpdate(subReg);

		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// Verify all replicated
		for (auto e : entities) {
			REQUIRE(subReg.valid(e));
		}

		// Performance expectation: should handle 10k entities in reasonable time
		REQUIRE(duration.count() < 5000); // Less than 5 seconds

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}

	SECTION("High Frequency Updates") {
		EventNode ownerNode("owner");
		EventNode subNode = ownerNode.spawn("subscriber");

		entt::registry ownerReg;
		entt::registry subReg;

		Replicator::setupOwner(ownerReg, ownerNode);
		Replicator::replicate<Position>(ownerReg);
		Replicator::replicate<Velocity>(ownerReg);
		Replicator::subscribe(subReg, subNode, ownerNode.getId(), false);

		std::atomic<bool> running{true};
		std::atomic<int> updateCount{0};

		// Create entities
		std::vector<entt::entity> entities;
		for (int i = 0; i < 100; ++i) {
			entt::entity e = Replicator::spawn(ownerReg);
			ownerReg.emplace<Position>(e, 0.0f, 0.0f, 0.0f);
			ownerReg.emplace<Velocity>(e, 1.0f, 0.0f, 0.0f);
			entities.push_back(e);
		}

		// Subscriber thread
		std::thread subThread([&]() {
			while (running) {
				Replicator::beginUpdate(subReg);
				Replicator::getContext(subReg).eventNode.wait(10);
				Replicator::endUpdate(subReg);
			}
		});

		// High frequency updates for 1 second
		auto start = std::chrono::high_resolution_clock::now();
		auto deadline = start + 1s;

		std::thread ownerThread([&]() {
			while (std::chrono::high_resolution_clock::now() < deadline) {
				// Update all entities
				for (auto e : entities) {
					ownerReg.patch<Position>(e, [](Position& p) {
						p.x += 0.1f;
					});
				}

				Replicator::beginUpdate(ownerReg);
				Replicator::getContext(ownerReg).eventNode.wait(10);
				Replicator::endUpdate(ownerReg);

				updateCount++;
			}
			running = false;
		});

		ownerThread.join();
		subThread.join();

		// Final sync
		for (int i = 0; i < 10; ++i) {
			fullUpdate(ownerReg);
			fullUpdate(subReg);
		}

		// Verify final state is consistent
		for (auto e : entities) {
			auto& ownerPos = ownerReg.get<Position>(e);
			auto& subPos = subReg.get<Position>(e);
			REQUIRE(std::abs(ownerPos.x - subPos.x) < 0.001f);
		}

		// Should have processed many updates
		REQUIRE(updateCount > 100);

		Replicator::shutdown(ownerReg);
		Replicator::shutdown(subReg);
	}
}
