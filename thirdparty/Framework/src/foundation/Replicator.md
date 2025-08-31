# Replicator Library Documentation

## Overview

The Replicator library provides thread-safe synchronization of [entt](https://github.com/skypjack/entt) ECS registries across multiple threads. Built on top of the EventNode message-passing system, it enables efficient replication of entities and components with automatic conflict resolution and error recovery.

## Key Features

- **Thread-Safe Registry Replication**: Synchronize ECS data across thread boundaries
- **Owner-Subscriber Architecture**: One authoritative owner with multiple subscribers
- **Bidirectional Synchronization**: Optional mutation capabilities for subscribers
- **Automatic Conflict Resolution**: Error detection and state recovery mechanisms
- **Type-Safe Component Replication**: Compile-time registration of replicable components
- **Efficient Delta Updates**: Only changes are transmitted, not full state

## Architecture

### Core Concepts

#### Owner Registry
The authoritative source of truth for all entity and component data. Only one owner registry should exist per replication group.

#### Subscriber Registry
A replica that receives updates from the owner. Subscribers can optionally have mutation privileges to push changes back to the owner.

#### Replication Context
Each registry maintains a `ReplicatorContext` that tracks:
- Connection state and error conditions
- List of replication targets
- Component replicator functions
- Whether the registry is an owner or subscriber

#### Replication States
- **Unsubscribed**: Not connected to any replication network
- **Ready**: Normal operating state, processing updates
- **Error**: Inconsistency detected, requesting full state refresh
- **RequestingState**: Waiting for state response after error

## API Reference

### Setup Functions

#### `setupOwner`
```cpp
void setupOwner(entt::registry& registry, fw::EventNode& eventNode)
```
Configures a registry as the authoritative owner. The owner accepts subscriptions and broadcasts changes to all subscribers.

#### `subscribe`
```cpp
bool subscribe(entt::registry& registry, fw::EventNode& eventNode,
               fw::EventNode::NodeId targetNodeId, bool canMutate)
```
Subscribes a registry to an owner registry.
- **canMutate**: If true, allows this subscriber to make changes that replicate back to the owner
- **Returns**: true if subscription request was sent successfully

#### `unsubscribe`
```cpp
bool unsubscribe(entt::registry& registry, fw::EventNode::NodeId ownerNodeId)
```
Disconnects a subscriber from the replication network.

### Component Registration

#### `replicate<Component>`
```cpp
template <typename Component>
void replicate(entt::registry& registry)
```
Registers a component type for replication. Must be called on the owner before any instances of this component are created.

#### `dereplicate<Component>`
```cpp
template <typename Component>
void dereplicate(entt::registry& registry)
```
Stops replicating a component type and cleans up associated handlers.

#### `isReplicating<Component>`
```cpp
template <typename Component>
bool isReplicating(const ReplicatorContext& ctx)
```
Checks if a component type is currently being replicated.

### Entity Management

#### `spawn`
```cpp
entt::entity spawn(entt::registry& registry)
```
Creates a new entity that will be replicated across all connected registries.

**Important**: Only entities created with `spawn()` are replicated. Entities created with `registry.create()` remain local.

#### `destroy`
```cpp
entt::entity destroy(entt::registry& registry, entt::entity entity)
```
Destroys an entity and replicates the destruction to all connected registries.

**Important**: Only use this for replicated entities. Using `registry.destroy()` directly will cause inconsistencies.

### Update Loop

#### `beginUpdate`
```cpp
void beginUpdate(entt::registry& registry)
```
Prepares the registry to receive replication events. Must be called before processing EventNode messages.

#### `endUpdate`
```cpp
void endUpdate(entt::registry& registry)
```
Finalizes the update cycle. Must be called after processing EventNode messages.

### Utility Functions

#### `getContext`
```cpp
ReplicatorContext& getContext(entt::registry& registry)
```
Retrieves the replication context for a registry.

#### `shutdown`
```cpp
void shutdown(entt::registry& registry)
```
Cleanly shuts down replication for a registry, unsubscribing from all events and cleaning up resources.

## Usage Examples

### Basic Owner-Subscriber Setup

```cpp
#include "Replicator.h"

struct Position {
    float x, y, z;
};

struct Health {
    int current;
    int max;
};

void fullUpdate(entt::registry& registry) {
    Replicator::beginUpdate(registry);
    Replicator::getContext(registry).eventNode.update();
    Replicator::endUpdate(registry);
}

int main() {
    // Create event nodes for thread communication
    fw::EventNode masterNode("master");
    fw::EventNode workerNode = masterNode.spawn("worker");

    // Create registries
    entt::registry masterRegistry;
    entt::registry workerRegistry;

    // Setup owner on master thread
    Replicator::setupOwner(masterRegistry, masterNode);

    // Register components for replication
    Replicator::replicate<Position>(masterRegistry);
    Replicator::replicate<Health>(masterRegistry);

    // Subscribe worker (read-only)
    Replicator::subscribe(workerRegistry, workerNode,
                         masterNode.getId(), false);

    // Process initial subscription
    fullUpdate(masterRegistry);
    fullUpdate(workerRegistry);

    // Create replicated entity
    entt::entity player = Replicator::spawn(masterRegistry);
    masterRegistry.emplace<Position>(player, 0.0f, 0.0f, 0.0f);
    masterRegistry.emplace<Health>(player, 100, 100);

    // Sync to worker
    fullUpdate(masterRegistry);
    fullUpdate(workerRegistry);

    // Worker now has the entity and components
    assert(workerRegistry.valid(player));
    assert(workerRegistry.all_of<Position, Health>(player));

    // Cleanup
    Replicator::shutdown(masterRegistry);
    Replicator::shutdown(workerRegistry);
}
```

### Bidirectional Replication

```cpp
void setupBidirectionalReplication() {
    fw::EventNode serverNode("server");
    fw::EventNode clientNode = serverNode.spawn("client");

    entt::registry serverRegistry;
    entt::registry clientRegistry;

    // Setup server as owner
    Replicator::setupOwner(serverRegistry, serverNode);
    Replicator::replicate<Position>(serverRegistry);

    // Subscribe client with mutation privileges
    Replicator::subscribe(clientRegistry, clientNode,
                         serverNode.getId(), true);  // canMutate = true

    fullUpdate(serverRegistry);
    fullUpdate(clientRegistry);

    // Client can now create entities that replicate to server
    entt::entity clientEntity = Replicator::spawn(clientRegistry);
    clientRegistry.emplace<Position>(clientEntity, 10.0f, 20.0f, 30.0f);

    // Sync changes back to server
    fullUpdate(serverRegistry);
    fullUpdate(clientRegistry);

    // Server now has the client-created entity
    assert(serverRegistry.valid(clientEntity));
    auto& pos = serverRegistry.get<Position>(clientEntity);
    assert(pos.x == 10.0f && pos.y == 20.0f && pos.z == 30.0f);
}
```

### Multi-Subscriber Broadcasting

```cpp
void setupMultipleSubscribers() {
    // Create network topology
    fw::EventNode hostNode("host");
    fw::EventNode playerNode1 = hostNode.spawn("player1");
    fw::EventNode playerNode2 = hostNode.spawn("player2");
    fw::EventNode spectatorNode = hostNode.spawn("spectator");

    // Create registries
    entt::registry hostRegistry;
    entt::registry player1Registry;
    entt::registry player2Registry;
    entt::registry spectatorRegistry;

    // Setup host as owner
    Replicator::setupOwner(hostRegistry, hostNode);
    Replicator::replicate<Position>(hostRegistry);
    Replicator::replicate<Health>(hostRegistry);

    // Players can mutate
    Replicator::subscribe(player1Registry, playerNode1,
                         hostNode.getId(), true);
    Replicator::subscribe(player2Registry, playerNode2,
                         hostNode.getId(), true);

    // Spectator is read-only
    Replicator::subscribe(spectatorRegistry, spectatorNode,
                         hostNode.getId(), false);

    // Sync all
    auto updateAll = [&]() {
        fullUpdate(hostRegistry);
        fullUpdate(player1Registry);
        fullUpdate(player2Registry);
        fullUpdate(spectatorRegistry);
    };

    updateAll();

    // Player 1 creates their character
    entt::entity p1 = Replicator::spawn(player1Registry);
    player1Registry.emplace<Position>(p1, 0.0f, 0.0f, 0.0f);
    player1Registry.emplace<Health>(p1, 100, 100);

    updateAll();

    // All registries now have player 1's entity
    assert(hostRegistry.valid(p1));
    assert(player2Registry.valid(p1));
    assert(spectatorRegistry.valid(p1));
}
```

### Component Update Patterns

```cpp
void demonstrateComponentUpdates() {
    // ... setup code ...

    entt::entity entity = Replicator::spawn(ownerRegistry);
    ownerRegistry.emplace<Position>(entity, 0.0f, 0.0f, 0.0f);

    fullUpdate(ownerRegistry);
    fullUpdate(subscriberRegistry);

    // Method 1: Replace entire component
    ownerRegistry.replace<Position>(entity, 10.0f, 20.0f, 30.0f);

    // Method 2: Patch component in-place
    ownerRegistry.patch<Position>(entity, [](Position& pos) {
        pos.x += 5.0f;
        pos.y += 10.0f;
    });

    // Method 3: Get and modify
    auto& pos = ownerRegistry.get<Position>(entity);
    pos.z = 100.0f;
    ownerRegistry.patch<Position>(entity);  // Trigger update event

    fullUpdate(ownerRegistry);
    fullUpdate(subscriberRegistry);

    // Verify changes propagated
    auto& subPos = subscriberRegistry.get<Position>(entity);
    assert(subPos.x == 15.0f && subPos.y == 30.0f && subPos.z == 100.0f);
}
```

### Error Recovery

```cpp
void demonstrateErrorRecovery() {
    // ... setup code ...

    // Simulate an inconsistency: create local entity in subscriber
    entt::entity localEntity = subscriberRegistry.create();

    // Owner tries to create entity with same ID
    // This would normally cause an inconsistency

    // The replicator will:
    // 1. Detect the error in the subscriber
    // 2. Set state to Error
    // 3. Request full state from owner
    // 4. Rebuild subscriber registry from scratch

    fullUpdate(subscriberRegistry);  // Detects error, requests state
    fullUpdate(ownerRegistry);       // Sends full state
    fullUpdate(subscriberRegistry);  // Receives and applies full state

    // Subscriber is now consistent with owner
    auto& ctx = Replicator::getContext(subscriberRegistry);
    assert(ctx.state == Replicator::ReplicatorState::Ready);
}
```

## Best Practices

### Thread Safety
1. **One EventNode per thread**: Each thread should own its EventNode
2. **Regular updates**: Call `fullUpdate()` consistently to process messages
3. **Synchronous initialization**: Setup replication before starting worker threads

### Performance Optimization
1. **Batch operations**: Group entity/component operations before calling update
2. **Component granularity**: Use smaller, focused components for better delta efficiency
3. **Update frequency**: Balance between latency and CPU usage

### Entity Management
1. **Always use Replicator functions**: Use `spawn()` and `destroy()` for replicated entities
2. **Local entities**: Entities created with `registry.create()` remain thread-local
3. **Component registration**: Call `replicate<T>()` before creating any instances

### Error Handling
1. **Automatic recovery**: The system automatically recovers from inconsistencies
2. **State monitoring**: Check `ReplicatorContext::state` for health monitoring
3. **Clean shutdown**: Always call `shutdown()` before destroying registries

## Common Pitfalls

### Forgetting to Register Components
```cpp
// WRONG: Component not registered
entt::entity e = Replicator::spawn(registry);
registry.emplace<Health>(e, 100, 100);  // Won't replicate!

// CORRECT: Register first
Replicator::replicate<Health>(registry);
entt::entity e = Replicator::spawn(registry);
registry.emplace<Health>(e, 100, 100);  // Will replicate
```

### Using Wrong Entity Creation Method
```cpp
// WRONG: Entity won't replicate
entt::entity e = registry.create();

// CORRECT: Use Replicator::spawn
entt::entity e = Replicator::spawn(registry);
```

### Missing Update Wrapper
```cpp
// WRONG: No begin/end wrapper
registry.ctx().at<Replicator::ReplicatorContext>().eventNode.update();

// CORRECT: Properly wrapped
Replicator::beginUpdate(registry);
registry.ctx().at<Replicator::ReplicatorContext>().eventNode.update();
Replicator::endUpdate(registry);
```

### Modifying Read-Only Subscriber
```cpp
// Setup subscriber as read-only (canMutate = false)
Replicator::subscribe(registry, node, ownerId, false);

// WRONG: Trying to create entity in read-only subscriber
entt::entity e = Replicator::spawn(registry);  // Won't replicate!

// CORRECT: Only modify if canMutate = true
```

## Technical Details

### Message Types
The library uses several internal event types for synchronization:
- `CreateEntityEvent`: Replicates entity creation
- `DestroyEntityEvent`: Replicates entity destruction
- `EmplaceComponentEvent`: Adds component to entity
- `UpdateComponentEvent`: Updates existing component
- `DestroyComponentEvent`: Removes component from entity
- `StateRequestEvent`: Requests full state after error
- `StateResponseEvent`: Sends complete registry state
- `RegistrySubscribeEvent`: Subscriber registration
- `RegistryUnsubscribeEvent`: Subscriber removal

### Conflict Resolution
When inconsistencies are detected:
1. Subscriber enters Error state
2. Ignores incoming updates
3. Requests full state from owner
4. Clears local registry
5. Rebuilds from received state
6. Returns to Ready state

### Memory Considerations
- Components are copied/moved through the event system
- Large components benefit from move semantics
- Each registry maintains its own component storage
- Event queues may buffer multiple updates

## Integration with Existing Systems

The Replicator library is designed to work alongside existing entt usage:
- Non-replicated entities can coexist with replicated ones
- Local components can be added to replicated entities (won't sync)
- Multiple replication groups can exist in the same application
- Compatible with entt's observer and group features for local processing

## Limitations

1. **Entity ID Consistency**: Assumes entity IDs don't collide across threads
2. **Component Serializability**: Components must be copyable/movable
3. **Network Topology**: Requires star topology (one owner, multiple subscribers)
4. **Ordering**: Updates from different sources may arrive out of order
5. **No Persistence**: State is memory-only, no built-in save/load
