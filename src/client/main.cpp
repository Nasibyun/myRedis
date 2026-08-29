///
/// myredis-cli — Interactive command-line client
///
/// Connects to myredis-server and provides a REPL (Read-Eval-Print Loop)
/// for sending commands interactively.
///
/// Usage:
///   ./myredis-cli                      # Connect to 127.0.0.1:6399
///   ./myredis-cli 192.168.1.10 7000    # Connect to custom host:port
///

#include "networking/client.h"
#include "platform/platform.h"

#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // ── Initialize platform (WSAStartup on Windows) ──
    if (!myredis::platform::init()) {
        std::cerr << "ERROR: Failed to initialize networking." << std::endl;
        return 1;
    }

    // Parse optional host and port arguments
    std::string host = "127.0.0.1";
    int port = 6399;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::atoi(argv[2]);

    // Connect to the server
    myredis::Client client(host, port);

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

    if (!client.connect()) {
        std::cerr << "Could not connect to myredis at "
                  << host << ":" << port << std::endl;
        std::cerr << "Is the server running?" << std::endl;
        myredis::platform::cleanup();
        return 1;
    }

    std::cout << "Connected to myredis at " << host << ":" << port << std::endl;
    std::cout << "Type commands (QUIT to exit)." << std::endl;
    std::cout << std::endl;

    // ── REPL (Read-Eval-Print Loop) ─────────────────────────
    std::string line;
    while (true) {
        // Print the prompt
        std::cout << "myredis> ";
        std::cout.flush();

        // Read a line of input from the user
        if (!std::getline(std::cin, line)) {
            // EOF — user pressed Ctrl+D (Linux) or Ctrl+Z (Windows)
            std::cout << std::endl;
            break;
        }

        // Skip empty lines
        if (line.empty()) continue;

        // Skip lines that are only whitespace
        bool all_space = std::all_of(line.begin(), line.end(),
                                     [](unsigned char c) { return std::isspace(c); });
        if (all_space) continue;

        // Send the command to the server and print the response
        std::string response = client.send_command(line);
        std::cout << response << std::endl;

        // Check if the user wants to quit
        std::string upper_line = line;
        std::transform(upper_line.begin(), upper_line.end(), upper_line.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        // Trim whitespace for comparison
        size_t start = upper_line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            upper_line = upper_line.substr(start);
        }
        size_t end = upper_line.find_last_not_of(" \t");
        if (end != std::string::npos) {
            upper_line = upper_line.substr(0, end + 1);
        }

        if (upper_line == "QUIT") {
            break;
        }

        // Check if we got disconnected
        if (!client.is_connected()) {
            std::cerr << "Disconnected from server." << std::endl;
            break;
        }
    }

    client.disconnect();
    myredis::platform::cleanup();
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
