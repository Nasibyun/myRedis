# MyRedis

A Redis-inspired in-memory database built from scratch in C++17.

This is a systems programming learning project — the goal is to deeply understand networking, concurrency, persistence, and database internals by building them from the ground up.

## Features (v0.4.0)

- **In-memory key-value store** using hash tables
- **Multiple data types**: strings and lists
- **TTL / Key expiry** with lazy + active expiry strategies
- **Publish/Subscribe messaging** with channels and subscription mode
- **TCP server** with thread-per-client concurrency
- **Custom wire protocol** with length-prefixed framing
- **RDB-style persistence** with binary format and CRC32 checksums
- **Interactive CLI** client
- **Cross-platform** — Windows (Winsock2) and Linux (POSIX sockets)
- **String commands**: `SET`, `GET`, `DEL`, `EXISTS`, `KEYS`, `DBSIZE`, `FLUSHDB`, `PING`, `QUIT`
- **List commands**: `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN`, `LINDEX`
- **TTL commands**: `EXPIRE`, `TTL`, `PERSIST`
- **Pub/Sub commands**: `SUBSCRIBE`, `UNSUBSCRIBE`, `PUBLISH`
- **Persistence commands**: `SAVE`

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
./test_server
```

### Connect with the CLI
```bash
./test_client
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
myredis> KEYS
1) session
2) name
3) tasks
myredis> SAVE
OK
myredis> QUIT
OK
```

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Architecture

```
Client (myredis_cli)
   │
   │ TCP + Length-Prefixed Protocol
   ▼
Server (myredis_server)
   ├── Platform Layer  →  cross-platform socket abstraction (POSIX/Winsock2)
   ├── Protocol Layer  →  frame complete messages from TCP byte stream
   ├── Parser          →  tokenize into Command { name, args[] }
   ├── Command Handler →  route to database operations
   └── Database        →  thread-safe std::unordered_map
```

## Project Structure

```
myredis/
├── src/
│   ├── server/main.cpp        # Server entry point
│   ├── client/main.cpp        # CLI entry point
│   ├── database/database.cpp  # In-memory store
│   ├── parser/parser.cpp      # Command tokenizer
│   ├── protocol/protocol.cpp  # Wire protocol
│   ├── commands/command_handler.cpp
│   └── networking/
│       ├── server.cpp         # TCP server
│       └── client.cpp         # TCP client
├── include/                   # Header files (mirrors src/)
│   └── platform/platform.h    # Cross-platform socket abstraction
├── tests/                     # Unit tests
├── CMakeLists.txt
└── README.md
```

## License

MIT
