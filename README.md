# MyRedis

A Redis-inspired in-memory database built from scratch in C++17.

This is a systems programming learning project — the goal is to deeply understand networking, concurrency, persistence, and database internals by building them from the ground up.

## Features (v0.1.0)

- **In-memory key-value store** using hash tables
- **TCP server** with thread-per-client concurrency
- **Custom wire protocol** with length-prefixed framing
- **Interactive CLI** client
- **Commands**: `SET`, `GET`, `DEL`, `EXISTS`, `KEYS`, `DBSIZE`, `FLUSHDB`, `PING`, `QUIT`

## Build

Requires: Linux/WSL, g++ (C++17), CMake 3.10+

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Run

### Start the server
```bash
./myredis-server
```

### Connect with the CLI
```bash
./myredis-cli
```

### Example session
```
myredis> PING
PONG
myredis> SET name Nasib
OK
myredis> GET name
Nasib
myredis> SET age 21
OK
myredis> KEYS
1) age
2) name
myredis> DEL age
(integer) 1
myredis> EXISTS name
(integer) 1
myredis> DBSIZE
(integer) 1
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
Client (myredis-cli)
   │
   │ TCP + Length-Prefixed Protocol
   ▼
Server (myredis-server)
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
├── tests/                     # Unit tests
├── CMakeLists.txt
└── README.md
```

## License

MIT
