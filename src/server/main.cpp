///
/// myredis-server — Entry point
///
/// Starts the in-memory database and TCP server.
/// Accepts connections on port 6399 (or a custom port via command line).
///
/// Usage:
///   ./myredis-server                    # Listen on port 6399
///   ./myredis-server 7000               # Listen on port 7000
///   ./myredis-server --config myredis.conf  # Load config file
///

#include "networking/server.h"
#include "database/database.h"
#include "persistence/persistence.h"
#include "config/config.h"
#include "platform/platform.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <cstring>

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

    // ── Load configuration ───────────────────────────────────
    myredis::Config config;

    // Check for --config flag
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            if (config.load_file(argv[i + 1])) {
                std::cout << "  Config:   loaded " << argv[i + 1] << std::endl;
            } else {
                std::cerr << "  Config:   failed to load " << argv[i + 1] << std::endl;
            }
            ++i;  // Skip the filename
        }
    }

    // ── Startup banner ──────────────────────────────────────
    std::cout << std::endl;
    std::cout << "  ╔══════════════════════════════════════╗" << std::endl;
    std::cout << "  ║           M y R e d i s              ║" << std::endl;
    std::cout << "  ║     In-Memory Database Server        ║" << std::endl;
    std::cout << "  ╚══════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "  Version:  1.0.0" << std::endl;
    std::cout << "  PID:      " << platform_getpid() << std::endl;

    // Parse optional port argument (overrides config)
    int port = config.port();
    std::string bind_addr = config.bind_addr();

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            ++i;  // Skip config filename
            continue;
        }
        // Treat bare number as port
        int p = std::atoi(argv[i]);
        if (p > 0 && p <= 65535) {
            port = p;
        }
    }

    std::cout << "  Port:     " << port << std::endl;
    std::cout << "  Mode:     event loop (single-threaded)" << std::endl;
    if (!config.password().empty()) {
        std::cout << "  Auth:     password required" << std::endl;
    }
    std::cout << std::endl;

    // Set up signal handlers for graceful shutdown (Ctrl+C)
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    // Create the shared database
    myredis::Database db;

    // ── Load persisted data (if any) ──────────────────────
    if (myredis::Persistence::load("dump.mrdb", db)) {
        std::cout << "  Persistence: loaded dump.mrdb" << std::endl;
    } else {
        std::cout << "  Persistence: no dump file found, starting empty" << std::endl;
    }
    std::cout << std::endl;

    // Create and start the server
    myredis::Server server(bind_addr, port, db);
    g_server = &server;

    // Configure authentication
    if (!config.password().empty()) {
        server.set_password(config.password());
    }

    // Start the background expiry thread (cleans up expired keys)
    db.start_expiry_thread();

    server.start();  // Blocks until shutdown

    // ── Cleanup ──
    db.stop_expiry_thread();
    myredis::platform::cleanup();
    std::cout << "[myredis] Server stopped." << std::endl;
    return 0;
}
