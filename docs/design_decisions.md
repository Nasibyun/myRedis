# MyRedis — Design Decisions Log

This document records important architectural and design decisions made during development.

---

## Decision 1: Use `std::unordered_map` for the data store

**Date**: Milestone 2

**Context**: We need a key-value store with fast lookups.

**Options considered**:
1. `std::map` — O(log n) operations, sorted keys, red-black tree
2. `std::unordered_map` — O(1) average operations, hash table
3. Custom hash table — full control, more educational

**Decision**: `std::unordered_map`

**Reason**: O(1) average-case GET/SET is exactly what a database needs. The STL implementation is well-tested. We can replace it with a custom hash table later if we want to learn hash table internals.

**Tradeoff**: Higher memory overhead than a sorted structure. Hash collisions degrade worst-case to O(n). No key ordering.

---

## Decision 2: 4-byte length-prefixed protocol

**Date**: Milestone 4

**Context**: TCP is a byte stream — it doesn't preserve message boundaries. We need a framing protocol.

**Options considered**:
1. Newline-delimited (`\n` terminates each message) — simple but breaks if values contain newlines
2. Length-prefixed (4-byte header + payload) — robust, handles any payload content
3. Full RESP protocol — compatible with real Redis clients, more complex

**Decision**: Length-prefixed

**Reason**: Simplest protocol that correctly handles the TCP framing problem for arbitrary payloads. Easy to implement and debug. We can upgrade to RESP later.

**Tradeoff**: Not compatible with real Redis clients. 4-byte header limits messages to ~4GB (more than enough).

---

## Decision 3: One thread per client

**Date**: Milestone 5

**Context**: We need to handle multiple concurrent clients.

**Options considered**:
1. Single-threaded with blocking I/O — can only serve one client at a time
2. Thread-per-client — each connection gets a dedicated thread
3. Thread pool — fixed number of threads, queue connections
4. Event-driven (epoll/select) — single thread handles all connections

**Decision**: Thread-per-client

**Reason**: Simplest correct concurrency model. Easy to understand and debug. Good enough for learning and moderate workloads.

**Tradeoff**: Each thread uses ~8MB stack memory. Doesn't scale beyond ~1000 clients. Thread creation has overhead. We may upgrade to a thread pool or event loop in a later milestone.

---

## Decision 4: `std::shared_mutex` for database locking

**Date**: Milestone 5

**Context**: Multiple threads access the database concurrently. We need synchronization.

**Options considered**:
1. `std::mutex` — simple, but blocks all readers while one thread writes
2. `std::shared_mutex` — allows concurrent readers, exclusive writers
3. No locking (single-threaded) — fastest but incorrect with multiple clients

**Decision**: `std::shared_mutex`

**Reason**: Most database workloads are read-heavy. Allowing concurrent reads significantly improves throughput. The API is straightforward: `shared_lock` for reads, `unique_lock` for writes.

**Tradeoff**: Slightly more complex than a plain mutex. Minor overhead for the shared/exclusive distinction. Could become a bottleneck under very high write loads.

---

## Decision 5: Port 6399

**Date**: Milestone 5

**Decision**: Use port 6399 instead of Redis's default 6379.

**Reason**: Avoids conflict if real Redis is installed on the same machine.
