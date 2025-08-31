# EventNode System Documentation

## Overview

The EventNode system is a thread-safe, lock-free message passing library for C++ that enables communication between threads using concurrent queues. Each thread owns one or more EventNodes that can send and receive typed messages.

## Key Features

- **Lock-free Design**: Uses concurrent queues for message passing without locks
- **Type-safe Events**: Leverages C++ templates for compile-time type safety
- **Eventual Consistency**: Each node maintains its own view of the network topology
- **Zero-copy Message Passing**: Supports move semantics for efficient message transfer
- **Flexible Subscriptions**: Subscribe to events with lambdas or function objects

## Architecture

### Core Components

1. **EventNode**: The main communication endpoint owned by a single thread
2. **Event Queue**: Lock-free concurrent queue for incoming messages
3. **Network Topology**: Each node maintains its own view of all nodes and subscriptions
4. **System Events**: Internal events that synchronize topology changes across nodes

### Design Principles

- **Single Thread Ownership**: Each EventNode should only be accessed from one thread
- **Eventual Consistency**: Topology changes propagate asynchronously via system events
- **No Shared State**: Each node has its own copy of the network state

## API Reference

### Construction and Lifecycle

```cpp
// Create a root node
fw::EventNode audioNode("audio");

// Spawn a child node (shares topology)
fw::EventNode uiNode = audioNode.spawn("ui");

// Nodes are automatically cleaned up on destruction
```

### Event Subscription

```cpp
// Subscribe to events with data
audioNode.subscribe<PlayEvent>([](const PlayEvent& event) {
    // Handle play event
});

// Subscribe to empty events (signals)
audioNode.subscribe<StopEvent>([]() {
    // Handle stop signal
});

// Receive events with move semantics
audioNode.receive<BufferData>([](BufferData&& data) {
    // Take ownership of data
});

// Unsubscribe from specific event
audioNode.unsubscribe<PlayEvent>();

// Unsubscribe from all events
audioNode.unsubscribeAll();
```

### Sending Messages

```cpp
// Send to specific node
uiNode.send(audioNodeId, PlayEvent{1.0f});

// Try to send (returns false if node doesn't exist)
if (!uiNode.trySend(audioNodeId, LargeData{...})) {
    // Handle send failure
}

// Send empty event (signal)
uiNode.send<StopEvent>(audioNodeId);

// Broadcast to all subscribers
uiNode.broadcast(PlayEvent{0.5f});

// Broadcast including sender
uiNode.broadcast(ConfigUpdate{...}, true);

// Check if anyone is listening
if (uiNode.hasSubscribers<PlayEvent>()) {
    uiNode.broadcast(PlayEvent{...});
}
```

### Processing Messages

```cpp
// Process pending messages (non-blocking)
audioNode.update();

// Wait for messages with timeout (microseconds)
audioNode.wait(1000); // Wait up to 1ms
```

### Utility Methods

```cpp
// Get node ID
auto nodeId = audioNode.getId();

// Check if subscribed to an event
if (audioNode.hasSubscription<PlayEvent>()) {
    // ...
}

// Unsubscribe from all events at once
audioNode.unsubscribeAll();

// Access network topology (read-only)
const auto& state = audioNode.getState();
```

## Usage Examples

### Basic Producer-Consumer

```cpp
// Define events
struct Task {
    int id;
    std::string data;
};

struct Result {
    int taskId;
    bool success;
};

// Producer thread
fw::EventNode producer("producer");
producer.subscribe<Result>([](const Result& r) {
    std::cout << "Task " << r.taskId << " completed: " << r.success << std::endl;
});

// Consumer thread
fw::EventNode consumer = producer.spawn("consumer");
consumer.subscribe<Task>([&consumer](Task&& task) {
    // Process task
    bool success = processTask(task);

    // Send result back
    consumer.send("producer"_hs, Result{task.id, success});
});

// Producer sends work
producer.send("consumer"_hs, Task{1, "process this"});

// Both threads must call update() regularly
while (running) {
    producer.update();
    // ... producer work ...
}
```

### Multi-Node Broadcasting

```cpp
// Audio system with multiple components
fw::EventNode masterNode("master");
fw::EventNode synthNode = masterNode.spawn("synth");
fw::EventNode effectsNode = masterNode.spawn("effects");
fw::EventNode outputNode = masterNode.spawn("output");

// All audio processors subscribe to timing events
struct ClockTick { uint64_t sample; };

synthNode.subscribe<ClockTick>([](const ClockTick& tick) {
    // Generate audio
});

effectsNode.subscribe<ClockTick>([](const ClockTick& tick) {
    // Process effects
});

// Master broadcasts timing to all
masterNode.broadcast(ClockTick{currentSample});
```

### Request-Response Pattern

```cpp
struct Request {
    int id;
    std::string query;
};

struct Response {
    int requestId;
    std::string result;
};

// Server node
serverNode.subscribe<Request>([&serverNode](const Request& req) {
    auto result = processQuery(req.query);
    serverNode.send(req.senderId, Response{req.id, result});
});

// Client sends request with its ID
Request req{nextId++, "SELECT * FROM users"};
clientNode.send(serverId, req);
```

### Dynamic Reconfiguration

```cpp
// Audio processor that can switch modes
enum class ProcessorMode { Effects, Analysis, Bypass };

struct ModeChange { ProcessorMode newMode; };

fw::EventNode processor("processor");

void configureMode(ProcessorMode mode) {
    // Clear all existing subscriptions
    processor.unsubscribeAll();

    // Set up new subscriptions based on mode
    switch (mode) {
        case ProcessorMode::Effects:
            processor.subscribe<AudioBuffer>([](AudioBuffer&& buf) {
                applyEffects(buf);
            });
            processor.subscribe<EffectParams>([](const EffectParams& params) {
                updateEffects(params);
            });
            break;

        case ProcessorMode::Analysis:
            processor.subscribe<AudioBuffer>([](AudioBuffer&& buf) {
                analyzeAudio(buf);
            });
            processor.subscribe<AnalysisRequest>([](const AnalysisRequest& req) {
                performAnalysis(req);
            });
            break;

        case ProcessorMode::Bypass:
            // No subscriptions in bypass mode
            break;
    }
}
```

## Best Practices

### Thread Management

1. **One Node Per Thread**: Each thread should own its EventNodes
2. **Regular Updates**: Call `update()` or `wait()` regularly to process messages
3. **Spawn Before Thread Start**: Create all nodes before starting worker threads

### Event Design

1. **Keep Events Small**: Large events should use move semantics
2. **Immutable Events**: Treat events as immutable once sent
3. **Clear Event Names**: Use descriptive struct names for events

### Performance

1. **Batch Processing**: The system processes up to 128 events per `update()`
2. **Move Semantics**: Use `receive<T>` and move semantics for large data
3. **Check Subscribers**: Use `hasSubscribers()` before expensive event creation

### Error Handling

1. **Node Existence**: Use `trySend()` when node existence is uncertain
2. **Destruction Order**: Be aware that nodes can disappear at any time
3. **Event Delivery**: Messages may be lost if a node is destroyed
4. **Clean Shutdown**: Consider using `unsubscribeAll()` before destroying nodes to cleanly disconnect from the network

## Limitations

1. **No Persistence**: Messages are lost if not processed before shutdown
2. **No Ordering Guarantees**: Messages from different senders may arrive out of order
3. **No Backpressure**: Fast senders can overwhelm slow receivers
4. **Name Uniqueness**: Node names must be unique (not enforced)

## Thread Safety

- **EventNode**: NOT thread-safe - use only from owning thread
- **Message Passing**: Thread-safe via lock-free queues
- **Topology Changes**: Eventually consistent across all nodes

## Memory Management

- Uses `std::shared_ptr` for queue lifetime management
- Events are copied/moved into queues
- Nodes can be safely destroyed at any time
- Circular references are avoided via weak_ptr usage

## Troubleshooting

### Messages Not Received

1. Ensure `update()` is called regularly
2. Verify subscription before sending
3. Check node IDs are correct
4. Confirm both nodes are alive

### Assertion Failures

1. Node already exists: Ensure unique names
2. Not subscribed: Check subscription before unsubscribe
3. Node not found: Use `trySend()` for safety
4. Complex subscription state: Use `unsubscribeAll()` to reset all subscriptions

### Performance Issues

1. Increase update frequency
2. Use move semantics for large events
3. Consider batching small events
4. Profile queue contention
