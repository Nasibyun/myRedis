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

---

## Decision 6: Dual expiry strategy (lazy + active)

**Date**: Milestone 7

**Context**: Keys can have a TTL. When a key expires, it needs to be deleted. But how and when?

**Options considered**:
1. Lazy only — check TTL on every read, delete if expired. Simple but "forgotten" keys leak memory forever.
2. Active only — a background thread scans all keys periodically. Expensive for large datasets.
3. Lazy + Active — check on read AND periodically scan in the background (same as Redis).

**Decision**: Lazy + Active

**Reason**: Lazy expiry is free (piggybacks on existing reads) and gives O(1) correctness. Active expiry (100ms cycle, 20-key sample) prevents memory leaks from keys that are set-and-forgotten. Together they give the best of both worlds, exactly like Redis does it.

**Tradeoff**: `get()`, `exists()`, `keys()`, `dbsize()` can no longer be `const` methods because lazy expiry mutates state. This is acceptable — correctness over const-correctness.

---

## Decision 7: Dual-map architecture for data types

**Date**: Milestone 9

**Context**: We need to support list operations (LPUSH, RPUSH, LPOP, etc.) alongside existing string operations (SET, GET). A key can hold either a string or a list — never both. Running a string command on a list key (or vice versa) must return a `WRONGTYPE` error.

**Options considered**:
1. `std::variant<std::string, std::deque<std::string>>` as the value type in a single map — clean but requires refactoring every existing method signature
2. Separate `store_` and `list_store_` maps (dual-map) — no changes to existing string method internals
3. Inheritance / polymorphic value objects — over-engineered for two types

**Decision**: Dual-map architecture

**Reason**: Adding a parallel `list_store_` map alongside the existing `store_` avoids touching any of the battle-tested string methods. Type checking is a simple helper (`key_type_unlocked()`) that checks which map a key lives in. The invariant "a key exists in at most one map" is enforced by the methods themselves (e.g., `set()` erases from `list_store_` before inserting into `store_`).

**Tradeoff**: Methods that operate on "all keys" (`del`, `exists`, `keys`, `dbsize`, `flushdb`, `expire`, `ttl`, `persist`, expiry loop) must now check/operate on both maps. This is a small constant-factor overhead. If we add more data types later (sets, hashes, sorted sets), we may want to refactor to a single map with variant values.

---

## Decision 8: Pub/Sub Manager Architecture & Concurrency

**Date**: Milestone 10

**Context**: Pub/Sub allows clients to subscribe to channels and receive messages published to those channels in real-time. This requires tracking which client sockets are subscribed to which channels and broadcasting messages across client threads.

**Options considered**:
1. Global pub/sub state inside Database class — couples messaging with key-value data storage
2. Dedicated `PubSubManager` class owned by `Server` with its own mutex — clean separation of concerns
3. Third-party event bus / message broker library — adds external dependencies

**Decision**: Dedicated `PubSubManager` class with bidirectional mapping and its own mutex

**Reason**: Keeping `PubSubManager` separate from `Database` maintains the single-responsibility principle. It uses bidirectional mapping (`channel_to_clients_` and `client_to_channels_`) for O(1) channel lookups during `PUBLISH` and instant cleanup during client disconnect. A dedicated `std::mutex` ensures thread safety without contending with database reads and writes.

**Tradeoff**: Pushing messages directly to subscriber sockets from the publishing thread requires robust handling of dead or disconnected sockets, handled via automatic cleanup on socket write failures.

---

## Decision 9: RESP2 Wire Protocol

**Date**: Milestone 12

**Context**: Our custom 4-byte length-prefixed protocol works correctly but is incompatible with standard Redis clients (`redis-cli`, Python `redis`, Node `ioredis`). To be useful as a Redis replacement, we need to speak the standard wire protocol.

**Options considered**:
1. Keep custom protocol — works, but isolates us from the Redis ecosystem
2. RESP2 (Redis Serialization Protocol v2) — the standard since Redis 1.2, widely supported
3. RESP3 — newer protocol with richer types, but less universally supported

**Decision**: RESP2

**Reason**: RESP2 is simple, well-documented, and universally supported by every Redis client library. It uses five data types (Simple String, Error, Integer, Bulk String, Array) identified by a single leading byte. Our server accepts both RESP array format (from client libraries) and inline commands (from telnet/our CLI) — same dual-mode as Redis itself.

**Tradeoff**: RESP2 uses `\r\n` delimiters which makes it slightly less efficient than our binary length-prefix for large payloads. The smart response serializer (`Resp::serialize_response()`) must infer RESP types from CommandHandler string responses — this is brittle but avoids refactoring every handler to return structured types.

---

## Decision 10: Single-Threaded Event Loop (select)

**Date**: Milestone 11

**Context**: Our thread-per-client model (Decision 3) works but doesn't scale — each thread uses ~8MB stack memory and requires `shared_mutex` locking on every database operation.

**Options considered**:
1. Keep thread-per-client — simple, already working
2. Thread pool — bounded threads, but still needs locking
3. Single-threaded event loop with `select()` — no threads, no locks
4. `epoll`/`kqueue` — highest performance, but platform-specific

**Decision**: Single-threaded `select()` event loop

**Reason**: A single-threaded event loop eliminates ALL locking overhead. Since Redis itself is single-threaded, this matches the real architecture. `select()` is available on both POSIX and Winsock2, making it cross-platform. Per-client state is tracked in a `std::unordered_map<socket_t, ClientState>` with read buffers for accumulating partial messages.

**Tradeoff**: `select()` has a hard limit on the number of file descriptors (`FD_SETSIZE` = 64 on Windows, 1024 on Linux). Not suitable for 10k+ connections. Can upgrade to `epoll` (Linux) or `WSAPoll` (Windows) later if needed.

---

## Decision 11: Optimistic Locking with Value Snapshots (WATCH)

**Date**: Milestone 14

**Context**: Redis transactions (`MULTI`/`EXEC`) execute commands atomically, but `WATCH` provides optimistic locking — if a watched key changes between `WATCH` and `EXEC`, the transaction is aborted.

**Options considered**:
1. Version counters per key — increment on every write, compare at EXEC time
2. Value snapshots — store the actual value at WATCH time, compare at EXEC time
3. Dirty flags — mark keys as dirty when any client modifies them

**Decision**: Value snapshots stored in `WatchedKeySnapshot` structs

**Reason**: In a single-threaded event loop, transactions are naturally atomic (no other client can interleave). The main purpose of `WATCH` is detecting changes between `WATCH` and `EXEC` across separate command cycles. Value snapshots (string value + list size) give precise change detection without needing global version counters. The snapshot approach correctly handles both string and list key types.

**Tradeoff**: Snapshot comparison is O(value_size) for strings. For very large values, version counters would be O(1). Acceptable for our use case since most Redis values are small.
