# MyRedis

**A Redis-inspired in-memory database engineered from scratch in C++17.**

MyRedis is a systems programming project that explores the internal architecture of an in-memory database through networking, protocol design, data structures, concurrency, persistence, and transaction management.

It features a non-blocking TCP server powered by `select()`, RESP2 protocol support, multiple data types, key expiration, Pub/Sub messaging, optimistic transaction locking, and binary snapshots with integrity verification.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/Build-CMake-064F8C.svg)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey.svg)](#-cross-platform-support)
[![Protocol](https://img.shields.io/badge/Protocol-RESP2-orange.svg)](https://redis.io/docs/latest/develop/reference/protocol-spec/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [High-Level Design](#-high-level-design)
- [Request Lifecycle](#-request-lifecycle)
- [Core Components](#-core-components)
- [Data Model and Storage](#-data-model-and-storage)
- [Protocol Design](#-protocol-design)
- [Persistence and Recovery](#-persistence-and-recovery)
- [TTL and Expiration](#-ttl-and-expiration)
- [Pub/Sub Messaging](#-pubsub-messaging)
- [Transactions](#-transactions)
- [Command Reference](#-command-reference)
- [Getting Started](#-getting-started)
- [Usage Examples](#-usage-examples)
- [Configuration](#-configuration)
- [Testing](#-testing)
- [Project Structure](#-project-structure)
- [Design Decisions and Trade-offs](#-design-decisions-and-trade-offs)
- [Current Limitations](#-current-limitations)
- [Future Improvements](#-future-improvements)
- [License](#-license)

## 🚀 Overview

MyRedis implements the core ideas behind a Redis-style in-memory data store while keeping the implementation understandable and modular.

Instead of relying on an existing database engine, the project implements its own storage operations, TCP server, request parsing, protocol serialization, expiration management, persistence format, and command execution pipeline.

The current implementation includes:

- A hash-table-based in-memory database supporting strings and lists.
- A non-blocking TCP server using a `select()`-based event loop.
- RESP2 parsing and response serialization.
- A custom command execution layer.
- Lazy and active key expiration.
- Publish/Subscribe messaging.
- Transactions with `MULTI`, `EXEC`, `DISCARD`, and `WATCH`.
- Binary snapshots with CRC32 integrity checks.
- Configuration files and optional password authentication.
- Cross-platform socket abstractions for Windows and POSIX systems.

The goal is to understand how database systems work internally, including the trade-offs involved in choosing data structures, networking models, persistence mechanisms, and execution semantics.

## ✨ Features

| Feature | Implementation |
|---|---|
| In-memory storage | `std::unordered_map` |
| Supported data types | Strings and lists |
| List storage | `std::deque` |
| Networking | Non-blocking TCP sockets |
| I/O multiplexing | `select()` event loop |
| Wire protocol | RESP2 and inline commands |
| Key expiration | Lazy and active expiration |
| Persistence | Custom RDB-style binary snapshots |
| Data integrity | CRC32 checksums |
| Messaging | Channel-based Pub/Sub |
| Transactions | Command queuing and optimistic locking |
| Authentication | Optional password-based `AUTH` |
| Configuration | Configuration file and `CONFIG` commands |
| Server introspection | `INFO` |
| Build system | CMake |
| Testing | CTest-based test suite |
| Portability | Windows Winsock2 and POSIX sockets |

## 🏗️ High-Level Design

MyRedis follows a layered architecture that separates network communication, protocol handling, command execution, storage, messaging, and persistence.

### System Architecture

```text
                 ┌──────────────────────────┐
                 │       Client Layer       │
                 │                          │
                 │  Built-in CLI            │
                 │  redis-cli / RESP2       │
                 │  Telnet / Inline Commands│
                 └────────────┬─────────────┘
                              │
                         TCP Connection
                              │
                              ▼
                 ┌──────────────────────────┐
                 │       TCP Server         │
                 │                          │
                 │  Non-blocking Sockets    │
                 │  select() Event Loop     │
                 │  Per-client Read Buffers │
                 │  Authentication          │
                 └────────────┬─────────────┘
                              │
                              ▼
                 ┌──────────────────────────┐
                 │      Protocol Layer      │
                 │                          │
                 │  RESP2 Parser            │
                 │  Inline Command Parser   │
                 │  Response Serializer     │
                 └────────────┬─────────────┘
                              │
                              ▼
                 ┌──────────────────────────┐
                 │     Command Handler      │
                 │                          │
                 │  Validation & Dispatch   │
                 │  Transaction Management  │
                 │  Command Execution       │
                 └─────┬──────────────┬─────┘
                       │              │
             ┌─────────▼─────┐  ┌─────▼────────────┐
             │   Database    │  │  Pub/Sub Manager │
             │               │  │                  │
             │ String Store  │  │ Channel Mapping  │
             │ List Store    │  │ Subscribe/Publish│
             │ TTL Metadata  │  │ Message Delivery │
             └───────┬───────┘  └──────────────────┘
                     │
                     ▼
             ┌───────────────────┐
             │ Persistence Layer │
             │                   │
             │ Binary Snapshots  │
             │ CRC32 Validation  │
             │ Save / Recovery  │
             └───────────────────┘

      Supporting Components:
      ├── Platform Abstraction
      ├── Configuration Manager
      └── Background Expiration Thread
```

### Architecture Principles

- **Separation of concerns:** Networking, parsing, command execution, storage, and persistence have distinct responsibilities.
- **Protocol independence:** The protocol layer translates incoming requests and outgoing responses without embedding database logic into the networking code.
- **Centralized command dispatch:** Commands are routed through a dedicated handler.
- **Explicit storage semantics:** Strings and lists are stored separately, with type checks preventing invalid cross-type operations.
- **Recoverable state:** Database contents can be saved to a binary snapshot and restored when the server starts.
- **Platform abstraction:** Socket operations are isolated behind a common interface for Windows and POSIX systems.

## 🔄 Request Lifecycle

A typical client request follows this pipeline:

```text
Client sends command
        │
        ▼
TCP server receives bytes
        │
        ▼
Bytes appended to client buffer
        │
        ▼
RESP2 / inline request parsing
        │
        ▼
Command and arguments extracted
        │
        ▼
Authentication and command checks
        │
        ▼
Command handler dispatches request
        │
        ├── Database operation
        ├── Pub/Sub operation
        ├── Transaction operation
        ├── Configuration operation
        └── Persistence operation
        │
        ▼
Internal result converted to RESP2
        │
        ▼
Serialized response sent over TCP
        │
        ▼
Client receives response
```

TCP is a byte stream, so one read is not guaranteed to contain exactly one request. MyRedis maintains a separate read buffer for each client and processes complete requests as they become available.

This allows the server to handle partial requests and multiple commands arriving over a connection.

## 🧩 Core Components

### 1. Networking Server

**Files:** `src/networking/server.cpp`, `include/networking/server.h`

The server manages TCP connections and uses non-blocking sockets with `select()` to monitor the listening socket and connected clients.

Responsibilities:

- Accept incoming client connections.
- Maintain per-client connection state and read buffers.
- Detect readable sockets.
- Process complete requests.
- Manage client authentication state.
- Remove disconnected clients and clean up subscriptions.
- Shut down the server gracefully.

The event loop handles client I/O in a single thread, avoiding the overhead of creating a dedicated thread for every connection.

### 2. Protocol Layer

**Files:** `src/resp/resp.cpp`, `include/resp/resp.h`

The RESP module parses incoming RESP2 arrays and inline commands and serializes command results into RESP-compatible responses.

It handles protocol elements such as simple strings, errors, integers, bulk strings, arrays, and null values.

### 3. Command Handler

**Files:** `src/commands/command_handler.cpp`, `include/commands/command_handler.h`

The command handler is responsible for dispatching commands, validating arguments, and coordinating operations across the database, Pub/Sub manager, configuration system, and persistence layer.

It also manages transaction state and command queuing.

### 4. Database Engine

**Files:** `src/database/database.cpp`, `include/database/database.h`

The database stores keys and values in memory.

- String values are stored using `std::unordered_map`.
- List values are stored using `std::deque` containers associated with keys.
- Expiration metadata tracks key lifetimes.
- Type checks prevent string commands from being applied to list keys and vice versa.

Hash-table lookups provide average-case \(O(1)\) access for core key-value operations.

### 5. Pub/Sub Manager

**Files:** `src/pubsub/pubsub.cpp`, `include/pubsub/pubsub.h`

The Pub/Sub subsystem manages channel subscriptions and message delivery independently of the database storage engine.

It maintains mappings between channels and subscribed clients, enabling publication to the relevant subscribers and cleanup when clients disconnect.

### 6. Persistence Engine

**Files:** `src/persistence/persistence.cpp`, `include/persistence/persistence.h`

The persistence module serializes database state into a binary snapshot.

Its responsibilities include:

- Writing string and list entries.
- Preserving remaining TTL information.
- Computing and validating CRC32 checksums.
- Saving database snapshots.
- Loading persisted state during startup.
- Rejecting invalid or corrupted snapshot data.

### 7. Configuration and Platform Layers

**Files:** `src/config/`, `include/config/`, `include/platform/platform.h`

The configuration component handles server settings, including the port, bind address, password, and other supported configuration values.

The platform abstraction provides common socket operations across Windows Winsock2 and POSIX environments.

## 🗄️ Data Model and Storage

MyRedis currently supports two value types: strings and lists.

### Strings

The string store uses a hash table:

```cpp
std::unordered_map<std::string, std::string>
```

This provides average-case \(O(1)\) lookup, insertion, and deletion.

### Lists

Lists use deque-based storage:

```cpp
std::unordered_map<std::string, std::deque<std::string>>
```

The deque supports efficient insertion and removal at both ends, making it suitable for `LPUSH`, `RPUSH`, `LPOP`, and `RPOP`.

### Type Safety

A key can hold a string or a list, but not both simultaneously.

Operations that are incompatible with the existing value type return a `WRONGTYPE` error.

### Complexity

| Operation | Expected complexity |
|---|---|
| `GET`, `SET`, `DEL` | Average \(O(1)\) per key |
| `EXISTS` | Average \(O(1)\) per key |
| `LPUSH`, `RPUSH` | \(O(k)\) for \(k\) inserted elements |
| `LPOP`, `RPOP` | Amortized \(O(1)\) |
| `LLEN` | \(O(1)\) |
| `LINDEX` | \(O(n)\) in the general case |
| `LRANGE` | \(O(n+k)\), where \(n\) is list length and \(k\) is the returned range |
| `KEYS` | \(O(N)\) to scan the keyspace |
| `DBSIZE` | \(O(N)\) when counting keys and handling expiration |
| `FLUSHDB` | \(O(N)\) in the number of stored entries |

Actual costs depend on expiration checks, allocation, serialization, and the size of the requested result.

## 🌐 Protocol Design

MyRedis implements RESP2, the Redis Serialization Protocol version 2, for communication with Redis-compatible clients.

It also accepts inline commands, which are convenient for interactive terminal use.

### Supported RESP2 Data Types

| Type | Prefix | Example |
|---|---|---|
| Simple string | `+` | `+OK\r\n` |
| Error | `-` | `-ERR unknown command\r\n` |
| Integer | `:` | `:42\r\n` |
| Bulk string | `$` | `$5\r\nhello\r\n` |
| Array | `*` | `*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n` |
| Null bulk string | `$-1` | `$-1\r\n` |

### Example Request

A client can send a RESP2 array representing:

```text
SET name Nasib
```

Encoded as:

```text
*3\r\n
$3\r\nSET\r\n
$4\r\nname\r\n
$5\r\nNasib\r\n
```

The server parses the request, executes the command, and returns a serialized response.

### Client Compatibility

The RESP2 implementation enables interoperability with clients that use the supported RESP2 commands and semantics.

MyRedis implements a subset of Redis functionality; it is not intended to be a drop-in replacement for every Redis command or protocol feature.

## 💾 Persistence and Recovery

MyRedis supports RDB-style binary snapshots through its persistence module.

The snapshot format uses a project-specific magic header (`MYREDIS01`) and CRC32 validation to detect corruption.

### Save Flow

```text
In-memory database
        │
        ▼
Collect snapshot data
        │
        ▼
Serialize strings, lists and TTLs
        │
        ▼
Compute CRC32 checksum
        │
        ▼
Write snapshot to disk
        │
        ▼
Persisted dump.mrdb
```

### Recovery Flow

```text
Server startup
        │
        ▼
Check for dump.mrdb
        │
        ▼
Validate snapshot structure
        │
        ▼
Verify CRC32 checksum
        │
        ▼
Deserialize stored entries
        │
        ▼
Restore database state
```

The persistence implementation includes atomic snapshot-writing behavior to reduce the risk of leaving a partially written snapshot at the target path.

### Important Details

- Snapshot saving is available through the `SAVE` command.
- The server attempts to load `dump.mrdb` during startup.
- String and list entries can be persisted.
- Remaining TTL information is included in snapshots.
- CRC32 detects accidental corruption; it is not a cryptographic security mechanism.

Snapshots are not the same as write-ahead logging. Writes made after the latest successful snapshot may not survive a process failure.

## ⏱️ TTL and Expiration

MyRedis implements key expiration using two complementary strategies.

### Lazy Expiration

Expiration is checked when commands access keys.

```text
Client requests a key
        │
        ▼
Check expiration metadata
        │
        ├── Key valid → Return value
        │
        └── Key expired → Remove key
```

This avoids a full scan of the database on every request.

### Active Expiration

A background expiration thread periodically scans for expired keys and removes them, including keys that are never accessed again.

Together, these strategies balance access-time correctness with background cleanup.

### Supported Commands

- `EXPIRE key seconds`
- `TTL key`
- `PERSIST key`
- `SET key value EX seconds`

TTL information is also preserved by the snapshot implementation.

## 📡 Pub/Sub Messaging

The Pub/Sub subsystem enables message delivery through named channels.

Publishers send messages to channels, while subscribers receive messages from channels to which they are subscribed.

```text
Publisher
    │
    │ PUBLISH news "Hello"
    ▼
PubSubManager
    │
    ├───────────────┐
    ▼               ▼
Subscriber A     Subscriber B
    │               │
    ▼               ▼
  Message         Message
```

The dedicated `PubSubManager` maintains channel-to-client and client-to-channel mappings.

Separating Pub/Sub from the database avoids coupling transient messaging state with persistent key-value data.

Supported commands:

- `SUBSCRIBE`
- `UNSUBSCRIBE`
- `PUBLISH`

## 🔁 Transactions

MyRedis supports transaction-oriented command queuing and optimistic locking.

### Transaction Lifecycle

```text
MULTI
  │
  ▼
Queue commands
  │
  ▼
EXEC
  │
  ├── Watch condition unchanged
  │       │
  │       ▼
  │   Execute queued commands
  │
  └── Watched key modified
          │
          ▼
      Abort transaction
```

### Commands

| Command | Purpose |
|---|---|
| `MULTI` | Start transaction command queuing |
| `EXEC` | Execute queued commands |
| `DISCARD` | Discard the queued transaction |
| `WATCH` | Watch keys for conflicting modifications |

`WATCH` provides optimistic concurrency control: a transaction can be aborted if a watched key changes before execution.

**Scope:** MyRedis implements Redis-inspired transaction behavior, not every aspect of Redis transaction semantics. Do not assume full Redis isolation or rollback guarantees for arbitrary runtime command errors.

## 📚 Command Reference

### String and Key Operations

| Command | Description |
|---|---|
| `SET key value` | Store a string |
| `GET key` | Retrieve a string |
| `DEL key [key ...]` | Delete keys |
| `EXISTS key [key ...]` | Check key existence |
| `KEYS pattern` | Find matching keys |
| `DBSIZE` | Return the number of keys |
| `FLUSHDB` | Remove all stored keys |
| `PING` | Test server responsiveness |
| `QUIT` | Close the client connection |

### List Operations

| Command | Description |
|---|---|
| `LPUSH key value [value ...]` | Insert values at the left end |
| `RPUSH key value [value ...]` | Insert values at the right end |
| `LPOP key` | Remove the leftmost value |
| `RPOP key` | Remove the rightmost value |
| `LRANGE key start stop` | Retrieve a list range |
| `LLEN key` | Get list length |
| `LINDEX key index` | Retrieve a value by index |

### Expiration Operations

| Command | Description |
|---|---|
| `EXPIRE key seconds` | Set key expiration |
| `TTL key` | Retrieve remaining lifetime |
| `PERSIST key` | Remove key expiration |

### Pub/Sub Operations

| Command | Description |
|---|---|
| `SUBSCRIBE channel [channel ...]` | Subscribe to channels |
| `UNSUBSCRIBE [channel ...]` | Unsubscribe from channels |
| `PUBLISH channel message` | Publish a message |

### Transaction Operations

| Command | Description |
|---|---|
| `MULTI` | Begin queuing commands |
| `EXEC` | Execute queued commands |
| `DISCARD` | Cancel a queued transaction |
| `WATCH key [key ...]` | Watch keys for changes |

### Server and Administration

| Command | Description |
|---|---|
| `AUTH password` | Authenticate a client |
| `CONFIG GET parameter` | Read supported configuration values |
| `CONFIG SET parameter value` | Update supported configuration values |
| `INFO` | Retrieve server information |
| `SAVE` | Write a persistence snapshot |

The implementation supports a Redis-inspired command subset. Refer to the command handler for the exact argument validation and semantics of each operation.

## 🛠️ Getting Started

### Prerequisites

- A C++17-compatible compiler (GCC or Clang; MinGW-w64 on Windows).
- CMake 3.10 or later.
- Standard platform networking libraries.

No external database engine is required.

### 1. Clone the Repository

```bash
git clone https://github.com/Nasibyun/myRedis.git
cd myRedis
```

### 2. Build

**Linux**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**Windows (MSYS2 MinGW-w64)**

Ensure that your MinGW-w64 compiler and CMake are available in `PATH`.

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The build produces the server and client executables named `test_server` and `test_client` in the build directory.

### 3. Start the Server

**Linux**

```bash
./build/test_server
```

**Windows**

```powershell
.\build\test_server.exe
```

The default listening port is `6399`.

To use a different port:

```bash
./build/test_server 7000
```

To load a configuration file:

```bash
./build/test_server --config myredis.conf
```

### 4. Connect Using the Built-in Client

Open another terminal.

**Linux**

```bash
./build/test_client
```

**Windows**

```powershell
.\build\test_client.exe
```

You can also connect using a compatible Redis CLI:

```bash
redis-cli -p 6399
```

Redis CLI compatibility depends on the commands and RESP2 semantics supported by MyRedis.

## 💻 Usage Examples

### Basic Key-Value Operations

```text
SET name Nasib
OK

GET name
Nasib

EXISTS name
(integer) 1

DEL name
(integer) 1

GET name
(nil)
```

### Lists

```text
RPUSH tasks "write code" "run tests" "deploy"
(integer) 3

LRANGE tasks 0 -1
1) "write code"
2) "run tests"
3) "deploy"

LPOP tasks
"write code"

LLEN tasks
(integer) 2
```

### Key Expiration

```text
SET session abc123 EX 60
OK

TTL session
(integer) 59

PERSIST session
(integer) 1
```

The TTL value decreases with time, so the exact integer returned will vary.

### Transactions

```text
MULTI
OK

SET counter 1
QUEUED

SET status active
QUEUED

EXEC
```

The response to `EXEC` contains the results of the queued commands.

### Pub/Sub

Use separate client connections.

Subscriber:

```text
SUBSCRIBE news
```

Publisher:

```text
PUBLISH news "Hello from MyRedis"
```

The subscriber receives the published message through the subscribed channel.

### Persistence

```text
SET language C++
SAVE
OK
```

After a successful snapshot, restarting the server allows it to attempt to restore the persisted database from `dump.mrdb`.

## ⚙️ Configuration

Create a file named `myredis.conf` in your working directory:

```conf
# Networking
port 6399
bind 0.0.0.0

# Authentication (set a password to enable AUTH)
requirepass mysecretpassword

# Persistence configuration value
save 0

# Logging configuration
loglevel notice

# Memory configuration value
maxmemory 0
```

Start the server with:

```bash
./build/test_server --config myredis.conf
```

When authentication is enabled, clients must authenticate before executing commands that require authentication.

**Security note:** Binding to `0.0.0.0` exposes the server on all available IPv4 interfaces. Use a trusted network and an appropriate firewall; do not expose this learning project directly to the public internet.

The configuration module stores and exposes several settings, but not every Redis configuration option has the behavior of its production Redis counterpart. The `save` and `maxmemory` values should not be interpreted as a complete automatic snapshot policy or memory-eviction implementation.

## 🧪 Testing

MyRedis includes a CTest-based test suite covering the main modules.

Run the tests after building:

```bash
ctest --test-dir build --output-on-failure
```

Test coverage is organized into the following areas:

| Test file | Area |
|---|---|
| `test_database.cpp` | Storage, lists, expiration and key operations |
| `test_parser.cpp` | Command tokenization |
| `test_protocol.cpp` | Legacy length-prefixed protocol |
| `test_resp.cpp` | RESP parsing and serialization |
| `test_persistence.cpp` | Snapshot saving, loading and integrity checks |
| `test_pubsub.cpp` | Channel subscriptions and message delivery |
| `test_transactions.cpp` | Transaction behavior and watched keys |
| `test_config.cpp` | Configuration parsing |

The legacy length-prefixed protocol remains in the codebase for protocol-level testing; RESP2 is the current client/server wire protocol.

## 📁 Project Structure

```text
myRedis/
├── CMakeLists.txt
├── LICENSE
├── README.md
│
├── docs/
│   └── design_decisions.md
│
├── include/
│   ├── commands/
│   │   └── command_handler.h
│   ├── config/
│   │   └── config.h
│   ├── database/
│   │   └── database.h
│   ├── networking/
│   │   ├── client.h
│   │   └── server.h
│   ├── parser/
│   │   └── parser.h
│   ├── persistence/
│   │   └── persistence.h
│   ├── platform/
│   │   └── platform.h
│   ├── protocol/
│   │   └── protocol.h
│   ├── pubsub/
│   │   └── pubsub.h
│   └── resp/
│       └── resp.h
│
├── src/
│   ├── client/
│   │   └── main.cpp
│   ├── commands/
│   │   └── command_handler.cpp
│   ├── config/
│   │   └── config.cpp
│   ├── database/
│   │   └── database.cpp
│   ├── networking/
│   │   ├── client.cpp
│   │   └── server.cpp
│   ├── parser/
│   │   └── parser.cpp
│   ├── persistence/
│   │   └── persistence.cpp
│   ├── protocol/
│   │   └── protocol.cpp
│   ├── pubsub/
│   │   └── pubsub.cpp
│   ├── resp/
│   │   └── resp.cpp
│   └── server/
│       └── main.cpp
│
└── tests/
    ├── test_config.cpp
    ├── test_database.cpp
    ├── test_parser.cpp
    ├── test_persistence.cpp
    ├── test_protocol.cpp
    ├── test_pubsub.cpp
    ├── test_resp.cpp
    └── test_transactions.cpp
```

## 🧠 Design Decisions and Trade-offs

### 1. `std::unordered_map` for Storage

**Decision:** Use a hash table for key lookup.

**Reason:** Average-case constant-time access makes it a practical choice for an in-memory key-value store.

**Trade-off:** Keys are unordered, hash collisions can degrade performance, and hash tables incur memory overhead.

### 2. `select()` Instead of Thread-per-client

**Decision:** Use a non-blocking, single-threaded event loop for client networking.

**Reason:** One event loop can monitor multiple connections without allocating one thread per client.

**Trade-off:** `select()` has descriptor-set limits and is less suitable for very large connection counts than scalable facilities such as `epoll` or `kqueue`.

### 3. Separate String and List Stores

**Decision:** Keep strings and lists in separate maps.

**Reason:** This supports multiple data types while preserving a relatively straightforward storage implementation.

**Trade-off:** Operations that affect the entire keyspace must account for both stores. A tagged value representation could simplify the addition of more types later.

### 4. Lazy and Active Expiration

**Decision:** Combine access-time expiration checks with periodic background cleanup.

**Reason:** Lazy expiration handles keys when accessed, while active expiration cleans up keys that remain untouched.

**Trade-off:** Background scanning consumes CPU, and expiration timing depends on the cleanup cycle.

### 5. RESP2 Compatibility

**Decision:** Implement RESP2 rather than retaining only a custom length-prefixed protocol.

**Reason:** RESP2 enables integration with existing Redis clients and makes the project more useful for interoperability experiments.

**Trade-off:** Supporting RESP2 requires careful handling of parsing, serialization, partial network reads, and command semantics.

### 6. CRC32-Protected Snapshots

**Decision:** Use a binary snapshot format with CRC32 integrity checks.

**Reason:** Binary serialization provides a compact representation, and checksums help detect accidental corruption.

**Trade-off:** Snapshots do not provide write-ahead logging or guarantee recovery of writes made after the last snapshot.

## ⚠️ Current Limitations

MyRedis is a learning-oriented implementation and should not be considered production-equivalent to Redis.

Current limitations include:

- The command set covers only a subset of Redis functionality.
- The `select()` event loop has platform-dependent descriptor limits and is not designed for very large connection counts.
- The server does not implement Redis's full command semantics or every RESP feature.
- Persistence uses explicit snapshots rather than a complete write-ahead log.
- Transaction behavior does not guarantee full Redis-compatible isolation and rollback semantics.
- Configuration settings do not imply complete implementations of every Redis feature, such as memory eviction or automatic snapshot policies.
- Security, observability, fault recovery, and production hardening remain areas for further development.

## 🛣️ Future Improvements

Potential next steps for the project include:

- [ ] Replace `select()` with `epoll`/`kqueue` or a portable scalable event loop.
- [ ] Add integration and stress tests for concurrent client connections.
- [ ] Improve transaction semantics and conflict detection.
- [ ] Add additional data types such as sets and hashes.
- [ ] Introduce a more comprehensive persistence and recovery strategy.
- [ ] Add performance benchmarks for throughput, latency, and memory usage.
- [ ] Improve error handling, resource limits, and operational observability.
- [ ] Expand command and protocol compatibility.

## 📄 License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

**MyRedis** is an ongoing exploration of database internals, built to learn by implementing the systems behind a Redis-inspired in-memory database.
