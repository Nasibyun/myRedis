# MyRedis

A Redis-inspired in-memory database built from scratch in C++17.

This is a systems programming learning project — the goal is to deeply understand networking, concurrency, persistence, and database internals by building them from the ground up.

## Features (v1.0.0)

- **In-memory key-value store** using hash tables
- **Multiple data types**: strings and lists
- **TTL / Key expiry** with lazy + active expiry strategies
- **Publish/Subscribe messaging** with channels and subscription mode
- **Transactions** with `MULTI`, `EXEC`, `DISCARD`, and optimistic locking (`WATCH`)
- **RESP2 wire protocol** — compatible with `redis-cli` and all standard Redis client libraries
- **Single-threaded event loop** using `select()` for I/O multiplexing
- **Configuration file** support (`myredis.conf`) with runtime `CONFIG GET/SET`
- **Server authentication** via `AUTH` command
- **Server information** via `INFO` command (uptime, clients, keyspace)
- **RDB-style persistence** with binary format and CRC32 checksums
- **Interactive CLI** client (also supports inline commands via telnet/netcat)
- **Cross-platform** — Windows (Winsock2) and Linux (POSIX sockets)

### Commands

| Category | Commands |
|----------|----------|
| **Strings** | `SET`, `GET`, `DEL`, `EXISTS`, `KEYS`, `DBSIZE`, `FLUSHDB`, `PING`, `QUIT` |
| **Lists** | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN`, `LINDEX` |
| **TTL** | `EXPIRE`, `TTL`, `PERSIST` |
| **Pub/Sub** | `SUBSCRIBE`, `UNSUBSCRIBE`, `PUBLISH` |
| **Transactions** | `MULTI`, `EXEC`, `DISCARD`, `WATCH` |
| **Server** | `AUTH`, `CONFIG GET/SET`, `INFO`, `SAVE` |

## Build

### Linux/macOS
Requires: g++ or clang++ (C++17), CMake 3.10+
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Windows
Requires: MSYS2 MinGW-w64 (GCC 13+), CMake 3.10+
```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
mkdir build; cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j4
```

## Run

### Start the server
```bash
./test_server                           # Default port 6399
./test_server 7000                      # Custom port
./test_server --config myredis.conf     # With config file
```

### Connect with redis-cli (standard Redis client)
Since v1.0.0, the server speaks standard RESP2 protocol:
```bash
redis-cli -p 6399
```

### Connect with the built-in CLI
```bash
./test_client
```

### Connect with telnet (inline commands)
```bash
telnet 127.0.0.1 6399
```

### Example session
```
myredis> PING
PONG
myredis> SET name Nasib
OK
myredis> GET name
Nasib
myredis> SET session abc123 EX 60
OK
myredis> TTL session
(integer) 59
myredis> RPUSH tasks "write code" "run tests" "deploy"
(integer) 3
myredis> LRANGE tasks 0 -1
1) "write code"
2) "run tests"
3) "deploy"
myredis> LPOP tasks
"write code"
myredis> LLEN tasks
(integer) 2
myredis> MULTI
OK
myredis> SET counter 1
QUEUED
myredis> SET status active
QUEUED
myredis> EXEC
1) OK
2) OK
myredis> INFO
# Server
myredis_version:1.0.0
...
myredis> SAVE
OK
myredis> QUIT
OK
```

### Configuration file (myredis.conf)
```
# MyRedis configuration

# Network
port 6399
bind 0.0.0.0

# Authentication (uncomment to enable)
# requirepass mysecretpassword

# Persistence
# save 300

# Logging
loglevel notice
```

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Architecture

```
Client (myredis_cli / redis-cli / telnet)
   │
   │ TCP + RESP2 Protocol (or inline commands)
   ▼
Server (myredis_server)
   ├── Event Loop       →  select()-based I/O multiplexing (single-threaded)
   ├── Platform Layer    →  cross-platform socket abstraction (POSIX/Winsock2)
   ├── RESP Module       →  RESP2 parser (array + inline) and serializer
   ├── Parser            →  tokenize inline commands into Command { name, args[] }
   ├── Command Handler   →  route to database operations + transaction queuing
   ├── Database          →  std::unordered_map (strings) + std::deque (lists)
   ├── PubSubManager     →  channel subscriptions + RESP-formatted message broadcasting
   ├── Persistence       →  RDB-style binary snapshots with CRC32
   └── Config            →  myredis.conf parser + runtime CONFIG support
```

## Project Structure

```
myredis/
├── src/
│   ├── server/main.cpp        # Server entry point
│   ├── client/main.cpp        # CLI entry point
│   ├── database/database.cpp  # In-memory store
│   ├── parser/parser.cpp      # Command tokenizer
│   ├── protocol/protocol.cpp  # Legacy wire protocol (4-byte length prefix)
│   ├── resp/resp.cpp          # RESP2 protocol parser & serializer
│   ├── commands/command_handler.cpp  # Command routing + transactions
│   ├── networking/
│   │   ├── server.cpp         # Event loop server (select)
│   │   └── client.cpp         # TCP client (RESP-aware)
│   ├── pubsub/pubsub.cpp     # Pub/Sub manager
│   ├── persistence/persistence.cpp  # RDB persistence
│   └── config/config.cpp     # Config file parser
├── include/                   # Header files (mirrors src/)
│   └── platform/platform.h   # Cross-platform socket abstraction
├── tests/                     # Unit tests
│   ├── test_database.cpp      # 38 database tests
│   ├── test_parser.cpp        # Parser tests
│   ├── test_protocol.cpp      # Legacy protocol tests
│   ├── test_resp.cpp          # 66 RESP protocol tests
│   ├── test_persistence.cpp   # 11 persistence tests
│   ├── test_pubsub.cpp        # 18 pub/sub tests
│   ├── test_transactions.cpp  # 14 transaction tests
│   └── test_config.cpp        # 7 config tests
├── docs/
│   └── design_decisions.md    # Architecture decision log (11 decisions)
├── CMakeLists.txt
└── README.md
```

## RESP Protocol

MyRedis v1.0.0 implements the [Redis Serialization Protocol (RESP2)](https://redis.io/docs/reference/protocol-spec/), making it compatible with all standard Redis clients.

### Data Types
| Type | Prefix | Example |
|------|--------|---------|
| Simple String | `+` | `+OK\r\n` |
| Error | `-` | `-ERR unknown command\r\n` |
| Integer | `:` | `:1000\r\n` |
| Bulk String | `$` | `$5\r\nhello\r\n` |
| Array | `*` | `*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n` |
| Null | `$-1` | `$-1\r\n` |

### Client → Server
Commands can be sent as RESP arrays or inline text:
```
# RESP array format (used by redis-cli and client libraries)
*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n

# Inline format (used by telnet, our CLI)
SET key value\r\n
```

## License

MIT
