#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <random>

#include "EventNode.h"

using namespace fw;
using namespace std::chrono_literals;

// Test event types
struct SimpleEvent {
	int value;
};

struct LargeEvent {
	std::vector<uint8_t> data;

	LargeEvent() = default;
	LargeEvent(size_t size) : data(size, 0) {}
};

struct EmptyEvent {};

struct CounterEvent {
	std::atomic<int>* counter;
	int increment;
};

// Helper to ensure node processes events
void processEvents(EventNode& node, int iterations = 10, int delayMs = 1) {
	for (int i = 0; i < iterations; ++i) {
		node.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
	}
}

TEST_CASE("EventNode Basic Construction", "[eventnode]") {
	SECTION("Create single node") {
		EventNode node("test");
		REQUIRE(node.getId() == "test"_hs);
	}

	SECTION("Spawn child node") {
		EventNode parent("parent");
		EventNode child = parent.spawn("child");

		REQUIRE(parent.getId() == "parent"_hs);
		REQUIRE(child.getId() == "child"_hs);
	}

	SECTION("Move construction") {
		EventNode node1("node1");
		auto id = node1.getId();

		EventNode node2(std::move(node1));
		REQUIRE(node2.getId() == id);
		REQUIRE(node1.getId() == 0);
	}

	SECTION("Move assignment") {
		EventNode node1("node1");
		EventNode node2("node2");
		auto id = node1.getId();

		node2 = std::move(node1);
		REQUIRE(node2.getId() == id);
		REQUIRE(node1.getId() == 0);
	}
}

TEST_CASE("EventNode Subscription", "[eventnode]") {
	EventNode node("test");

	SECTION("Subscribe to event with data") {
		int received = 0;

		node.subscribe<SimpleEvent>([&received](const SimpleEvent& e) {
			received = e.value;
		});

		REQUIRE(node.hasSubscription<SimpleEvent>());
	}

	SECTION("Subscribe to empty event") {
		bool received = false;

		node.subscribe<EmptyEvent>([&received]() {
			received = true;
		});

		REQUIRE(node.hasSubscription<EmptyEvent>());
	}

	SECTION("Receive with move semantics") {
		std::vector<uint8_t> received;

		node.receive<LargeEvent>([&received](LargeEvent&& e) {
			received = std::move(e.data);
		});

		REQUIRE(node.hasSubscription<LargeEvent>());
	}

	SECTION("Unsubscribe") {
		node.subscribe<SimpleEvent>([](const SimpleEvent&) {});
		REQUIRE(node.hasSubscription<SimpleEvent>());

		node.unsubscribe<SimpleEvent>();
		REQUIRE_FALSE(node.hasSubscription<SimpleEvent>());
	}

	SECTION("Multiple subscriptions to different events") {
		node.subscribe<SimpleEvent>([](const SimpleEvent&) {});
		node.subscribe<EmptyEvent>([]() {});
		node.subscribe<LargeEvent>([](LargeEvent&&) {});

		REQUIRE(node.hasSubscription<SimpleEvent>());
		REQUIRE(node.hasSubscription<EmptyEvent>());
		REQUIRE(node.hasSubscription<LargeEvent>());
	}
}

TEST_CASE("EventNode Send and Receive", "[eventnode]") {
	EventNode sender("sender");
	EventNode receiver = sender.spawn("receiver");

	SECTION("Send event with data") {
		std::atomic<int> received{0};

		receiver.subscribe<SimpleEvent>([&received](const SimpleEvent& e) {
			received = e.value;
		});

		sender.send(receiver.getId(), SimpleEvent{42});
		processEvents(receiver);

		REQUIRE(received == 42);
	}

	SECTION("Send empty event") {
		std::atomic<bool> received{false};

		receiver.subscribe<EmptyEvent>([&received]() {
			received = true;
		});

		sender.send<EmptyEvent>(receiver.getId());
		processEvents(receiver);

		REQUIRE(received);
	}

	SECTION("Send with move semantics") {
		std::atomic<size_t> receivedSize{0};

		receiver.receive<LargeEvent>([&receivedSize](LargeEvent&& e) {
			receivedSize = e.data.size();
		});

		sender.send(receiver.getId(), LargeEvent{1024});
		processEvents(receiver);

		REQUIRE(receivedSize == 1024);
	}

	SECTION("TrySend to non-existent node") {
		bool result = sender.trySend("nonexistent"_hs, SimpleEvent{1});
		REQUIRE_FALSE(result);
	}

	SECTION("Multiple sends") {
		std::atomic<int> sum{0};

		receiver.subscribe<SimpleEvent>([&sum](const SimpleEvent& e) {
			sum += e.value;
		});

		for (int i = 1; i <= 10; ++i) {
			sender.send(receiver.getId(), SimpleEvent{i});
		}

		processEvents(receiver);
		REQUIRE(sum == 55); // 1+2+...+10
	}
}

TEST_CASE("EventNode Broadcasting", "[eventnode]") {
	EventNode broadcaster("broadcaster");
	EventNode receiver1 = broadcaster.spawn("receiver1");
	EventNode receiver2 = broadcaster.spawn("receiver2");

	SECTION("Broadcast excludes sender by default") {
		std::atomic<int> broadcasterReceived{0};
		std::atomic<int> receiver1Count{0};
		std::atomic<int> receiver2Count{0};

		broadcaster.subscribe<SimpleEvent>([&broadcasterReceived](const SimpleEvent& e) {
			broadcasterReceived = e.value;
		});

		receiver1.subscribe<SimpleEvent>([&receiver1Count](const SimpleEvent& e) {
			receiver1Count = e.value;
		});

		receiver2.subscribe<SimpleEvent>([&receiver2Count](const SimpleEvent& e) {
			receiver2Count = e.value;
		});

		broadcaster.broadcast(SimpleEvent{100});

		processEvents(broadcaster);
		processEvents(receiver1);
		processEvents(receiver2);

		REQUIRE(broadcasterReceived == 0);
		REQUIRE(receiver1Count == 100);
		REQUIRE(receiver2Count == 100);
	}

	SECTION("Broadcast includes sender when requested") {
		std::atomic<int> count{0};

		broadcaster.subscribe<SimpleEvent>([&count](const SimpleEvent&) {
			count++;
		});

		broadcaster.broadcast(SimpleEvent{1}, true);
		processEvents(broadcaster);

		REQUIRE(count == 1);
	}

	SECTION("Broadcast empty event") {
		std::atomic<int> receivedCount{0};

		receiver1.subscribe<EmptyEvent>([&receivedCount]() {
			receivedCount++;
		});

		receiver2.subscribe<EmptyEvent>([&receivedCount]() {
			receivedCount++;
		});

		broadcaster.broadcast<EmptyEvent>();

		processEvents(receiver1);
		processEvents(receiver2);

		REQUIRE(receivedCount == 2);
	}

	SECTION("HasSubscribers check") {
		REQUIRE_FALSE(broadcaster.hasSubscribers<SimpleEvent>());

		receiver1.subscribe<SimpleEvent>([](const SimpleEvent&) {});
		processEvents(broadcaster); // Process subscription system event

		REQUIRE(broadcaster.hasSubscribers<SimpleEvent>());
	}
}

TEST_CASE("EventNode Lifecycle", "[eventnode]") {
	SECTION("Node destruction removes from topology") {
		EventNode root("root");
		auto child = std::make_unique<EventNode>(root.spawn("child"));
		auto childId = child->getId();

		std::atomic<bool> canSend{true};

		// Destroy child
		child.reset();

		// Give time for RemoveNodeEvent to propagate
		processEvents(root, 20);

		// Sending to destroyed node should fail
		canSend = root.trySend(childId, SimpleEvent{1});
		REQUIRE_FALSE(canSend);
	}

	SECTION("Subscription survives across multiple events") {
		EventNode node("test");
		std::atomic<int> count{0};

		node.subscribe<SimpleEvent>([&count](const SimpleEvent&) {
			count++;
		});

		node.send(node.getId(), SimpleEvent{1});
		node.send(node.getId(), SimpleEvent{2});
		node.send(node.getId(), SimpleEvent{3});

		processEvents(node);

		REQUIRE(count == 3);
	}
}

TEST_CASE("EventNode Thread Safety", "[eventnode][threading]") {
	SECTION("Concurrent sends from multiple threads") {
		EventNode receiver("receiver");
		std::atomic<int> sum{0};

		receiver.subscribe<SimpleEvent>([&sum](const SimpleEvent& e) {
			sum += e.value;
		});

		// Create sender nodes in different threads
		std::vector<std::thread> threads;
		const int threadCount = 4;
		const int sendsPerThread = 100;

		for (int i = 0; i < threadCount; ++i) {
			threads.emplace_back([&receiver, i, sendsPerThread]() {
				EventNode sender = receiver.spawn("sender" + std::to_string(i));

				for (int j = 0; j < sendsPerThread; ++j) {
					sender.send(receiver.getId(), SimpleEvent{1});
				}

				// Keep sender alive briefly
				std::this_thread::sleep_for(10ms);
			});
		}

		// Process events while senders are active
		auto startTime = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - startTime < 100ms) {
			receiver.update();
			std::this_thread::sleep_for(1ms);
		}

		for (auto& t : threads) {
			t.join();
		}

		// Final processing
		processEvents(receiver, 20);

		REQUIRE(sum == threadCount * sendsPerThread);
	}

	SECTION("Producer-consumer pattern") {
		EventNode producer("producer");
		EventNode consumer = producer.spawn("consumer");

		std::atomic<bool> done{false};
		std::atomic<int> produced{0};
		std::atomic<int> consumed{0};

		consumer.subscribe<SimpleEvent>([&consumed](const SimpleEvent& e) {
			consumed += e.value;
		});

		std::thread producerThread([&producer, &consumer, &done, &produced]() {
			for (int i = 1; i <= 50; ++i) {
				producer.send(consumer.getId(), SimpleEvent{i});
				produced += i;
				std::this_thread::sleep_for(1ms);
			}
			done = true;
		});

		std::thread consumerThread([&consumer, &done]() {
			while (!done || consumer.getState().nodes.size() > 1) {
				consumer.wait(1000); // 1ms timeout
			}
			// Final drain
			for (int i = 0; i < 10; ++i) {
				consumer.update();
				std::this_thread::sleep_for(1ms);
			}
		});

		producerThread.join();
		consumerThread.join();

		REQUIRE(consumed == produced);
		REQUIRE(consumed == 1275); // Sum of 1..50
	}
}

TEST_CASE("EventNode Edge Cases", "[eventnode][edge]") {
	SECTION("Unsubscribe during event processing") {
		EventNode node("test");
		std::atomic<int> count{0};
		EventNode::EventType eventType;

		eventType = node.subscribe<SimpleEvent>([&node, &count, &eventType](const SimpleEvent&) {
			count++;
			if (count == 5) {
				node.unsubscribe(eventType);
			}
		});

		// Send 10 events
		for (int i = 0; i < 10; ++i) {
			node.send(node.getId(), SimpleEvent{i});
		}

		processEvents(node);

		// Should only process 5 events before unsubscribing
		REQUIRE(count == 5);
	}

	SECTION("Spawn during event processing") {
		EventNode root("root");
		std::atomic<int> spawnCount{0};

		root.subscribe<EmptyEvent>([&root, &spawnCount]() {
			if (spawnCount < 3) {
				auto child = root.spawn("child" + std::to_string(spawnCount));
				spawnCount++;
			}
		});

		root.send<EmptyEvent>(root.getId());
		processEvents(root);

		REQUIRE(spawnCount == 1);
		REQUIRE(root.getState().nodes.size() == 2); // root + 1 child
	}

	SECTION("Send to self") {
		EventNode node("test");
		std::atomic<int> received{0};

		node.subscribe<SimpleEvent>([&received](const SimpleEvent& e) {
			received = e.value;
		});

		node.send(node.getId(), SimpleEvent{42});
		processEvents(node);

		REQUIRE(received == 42);
	}

	SECTION("Rapid subscribe/unsubscribe") {
		EventNode node("test");

		for (int i = 0; i < 100; ++i) {
			node.subscribe<SimpleEvent>([](const SimpleEvent&) {});
			REQUIRE(node.hasSubscription<SimpleEvent>());

			node.unsubscribe<SimpleEvent>();
			REQUIRE_FALSE(node.hasSubscription<SimpleEvent>());
		}
	}
}

TEST_CASE("EventNode Performance", "[eventnode][performance]") {
	SECTION("High throughput messaging") {
		EventNode sender("sender");
		EventNode receiver = sender.spawn("receiver");

		std::atomic<int> received{0};
		const int messageCount = 10000;

		receiver.subscribe<SimpleEvent>([&received](const SimpleEvent&) {
			received++;
		});

		auto start = std::chrono::high_resolution_clock::now();

		// Send all messages
		for (int i = 0; i < messageCount; ++i) {
			sender.send(receiver.getId(), SimpleEvent{i});
		}

		// Process all messages
		while (received < messageCount) {
			receiver.update();
		}

		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

		REQUIRE(received == messageCount);

		// Performance assertion (adjust based on your requirements)
		// Should process 10k messages in under 100ms
		REQUIRE(duration.count() < 100000);
	}

	SECTION("Large event handling") {
		EventNode sender("sender");
		EventNode receiver = sender.spawn("receiver");

		const size_t eventSize = 1024 * 1024; // 1MB
		std::atomic<bool> received{false};
		std::atomic<size_t> receivedSize{0};

		receiver.receive<LargeEvent>([&received, &receivedSize](LargeEvent&& e) {
			receivedSize = e.data.size();
			received = true;
		});

		sender.send(receiver.getId(), LargeEvent{eventSize});

		while (!received) {
			receiver.update();
		}

		REQUIRE(received);
		REQUIRE(receivedSize == eventSize);
	}
}

TEST_CASE("EventNode System Events", "[eventnode][system]") {
	SECTION("Subscription propagation") {
		EventNode node1("node1");
		EventNode node2 = node1.spawn("node2");
		EventNode node3 = node1.spawn("node3");

		// Let spawns propagate
		processEvents(node1);
		processEvents(node2);
		processEvents(node3);

		// Subscribe node2 to an event
		node2.subscribe<SimpleEvent>([](const SimpleEvent&) {});

		// Let subscription propagate
		processEvents(node1, 20);
		processEvents(node3, 20);

		// Both node1 and node3 should see that someone subscribes to SimpleEvent
		REQUIRE(node1.hasSubscribers<SimpleEvent>());
		REQUIRE(node3.hasSubscribers<SimpleEvent>());
	}

	SECTION("Node removal propagation") {
		EventNode root("root");
		auto node1 = std::make_unique<EventNode>(root.spawn("node1"));
		auto node2 = std::make_unique<EventNode>(root.spawn("node2"));

		// Let spawns propagate
		processEvents(root);
		processEvents(*node1);
		processEvents(*node2);

		auto node1Id = node1->getId();

		// Destroy node1
		node1.reset();

		// Let removal propagate
		processEvents(root, 30);
		processEvents(*node2, 30);

		// Sending to removed node should fail
		REQUIRE_FALSE(root.trySend(node1Id, SimpleEvent{1}));
		REQUIRE_FALSE(node2->trySend(node1Id, SimpleEvent{1}));
	}
}

TEST_CASE("EventNode Wait Function", "[eventnode][wait]") {
	SECTION("Wait with timeout") {
		EventNode node("test");
		std::atomic<bool> received{false};

		node.subscribe<EmptyEvent>([&received]() {
			received = true;
		});

		std::thread sender([&node]() {
			std::this_thread::sleep_for(50ms);
			node.send<EmptyEvent>(node.getId());
		});

		auto start = std::chrono::high_resolution_clock::now();

		// Wait for up to 100ms
		while (!received &&
		       std::chrono::high_resolution_clock::now() - start < 100ms) {
			node.wait(10000); // 10ms timeout
		}

		sender.join();

		REQUIRE(received);
	}

	SECTION("Wait timeout expires") {
		EventNode node("test");

		auto start = std::chrono::high_resolution_clock::now();
		node.wait(5000); // 5ms timeout
		auto end = std::chrono::high_resolution_clock::now();

		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// Should timeout after ~5ms (allow some variance)
		REQUIRE(duration.count() >= 4);
		REQUIRE(duration.count() <= 10);
	}
}
