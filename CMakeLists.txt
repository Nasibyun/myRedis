cmake_minimum_required(VERSION 3.10)
project(myredis VERSION 0.1.0 LANGUAGES CXX)

# ─── C++ Standard ───────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build Type ─────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ─── Compiler Warnings ─────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ─── Core Library ───────────────────────────────────────────────
# All shared components compiled into a static library.
# Both the server and client link against this.
add_library(myredis_core STATIC
    src/database/database.cpp
    src/parser/parser.cpp
    src/protocol/protocol.cpp
    src/commands/command_handler.cpp
    src/networking/server.cpp
    src/networking/client.cpp
)

target_include_directories(myredis_core PUBLIC include)

# ─── Platform-specific linking ──────────────────────────────────
if(WIN32)
    # Winsock2 for socket API on Windows
    target_link_libraries(myredis_core PUBLIC ws2_32)
else()
    # pthreads for std::thread and std::mutex support on Linux/macOS
    find_package(Threads REQUIRED)
    target_link_libraries(myredis_core PUBLIC Threads::Threads)
endif()

# ─── Executables ────────────────────────────────────────────────
add_executable(myredis_server src/server/main.cpp)
target_link_libraries(myredis_server PRIVATE myredis_core)

add_executable(myredis_cli src/client/main.cpp)
target_link_libraries(myredis_cli PRIVATE myredis_core)

# ─── Tests ──────────────────────────────────────────────────────
enable_testing()

add_executable(test_database tests/test_database.cpp)
target_link_libraries(test_database PRIVATE myredis_core)
add_test(NAME DatabaseTest COMMAND test_database)

add_executable(parser_test tests/test_parser.cpp)
target_link_libraries(parser_test PRIVATE myredis_core)
add_test(NAME ParserTest COMMAND parser_test)

add_executable(protocol_test tests/test_protocol.cpp)
target_link_libraries(protocol_test PRIVATE myredis_core)
add_test(NAME ProtocolTest COMMAND protocol_test)
