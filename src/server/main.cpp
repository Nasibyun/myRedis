///
/// myredis-server — Entry point
///
/// Starts the in-memory database and TCP server.
/// Accepts connections on port 6399 (or a custom port via command line).
///
/// Usage:
///   ./myredis-server          # Listen on port 6399
///   ./myredis-server 7000     # Listen on port 7000
///

#include "networking/server.h"
#include "database/database.h"
#include "platform/platform.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>

// Global server pointer for signal handler access
static myredis::Server* g_server = nullptr;

void signal_handler(int sig) {
    std::cout << "\n[myredis] Received signal " << sig
              << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    // ── Initialize platform (WSAStartup on Windows, SIGPIPE on Linux) ──
    if (!myredis::platform::init()) {
        std::cerr << "ERROR: Failed to initialize networking." << std::endl;
        return 1;
    }

    // ── Startup banner ──────────────────────────────────────
    std::cout << std::endl;
    std::cout << "  ╔══════════════════════════════════════╗" << std::endl;
    std::cout << "  ║           M y R e d i s              ║" << std::endl;
    std::cout << "  ║     In-Memory Database Server        ║" << std::endl;
    std::cout << "  ╚══════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "  Version:  0.1.0" << std::endl;
    std::cout << "  PID:      " << platform_getpid() << std::endl;

    // Parse optional port argument
    int port = 6399;  // Default port (6399 to avoid conflict with real Redis on 6379)
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "  ERROR: Invalid port: " << argv[1] << std::endl;
            return 1;
        }
    }
    std::cout << "  Port:     " << port << std::endl;
    std::cout << std::endl;

    // Set up signal handlers for graceful shutdown (Ctrl+C)
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    // Create the shared database
    myredis::Database db;

    // Create and start the server
    myredis::Server server("0.0.0.0", port, db);
    g_server = &server;

    server.start();  // Blocks until shutdown

    // ── Cleanup ──
    myredis::platform::cleanup();
    std::cout << "[myredis] Server stopped." << std::endl;
    return 0;
}
