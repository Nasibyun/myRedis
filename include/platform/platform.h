#pragma once
///
/// platform.h — Cross-platform socket abstraction
///
/// Normalizes the differences between POSIX sockets (Linux/macOS)
/// and Winsock2 (Windows) behind a single API.
///
/// On Windows:
///   - Sockets are SOCKET (unsigned), not int
///   - close() → closesocket()
///   - read()/write() → recv()/send()
///   - Must call WSAStartup() before using sockets
///   - No SIGPIPE signal
///
/// On Linux:
///   - Sockets are int
///   - Standard POSIX API
///

#ifdef _WIN32
    // ── Windows ──────────────────────────────────────────────
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>   // inet_pton(), sockaddr_in, etc.
    #include <windows.h>

    // Windows uses SOCKET (which is an unsigned pointer-sized int).
    // INVALID_SOCKET is the error sentinel (not -1).
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;

    #include <process.h>    // _getpid()
    #define platform_getpid _getpid
#else
    // ── POSIX (Linux, macOS) ────────────────────────────────
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>      // strerror()
    #include <signal.h>

    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VAL = -1;

    #define platform_getpid getpid
#endif

#include <string>
#include <cstdint>

namespace myredis::platform {

/// Initialize the platform's socket subsystem.
/// On Windows, this calls WSAStartup(). On POSIX, it ignores SIGPIPE.
/// Must be called once before any socket operations.
/// Returns true on success.
inline bool init() {
#ifdef _WIN32
    WSADATA wsa_data;
    // WSAStartup() initializes the Winsock DLL.
    // MAKEWORD(2, 2) requests version 2.2 (the latest).
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    // Ignore SIGPIPE so that writing to a disconnected socket
    // returns an error instead of killing the process.
    signal(SIGPIPE, SIG_IGN);
    return true;
#endif
}

/// Clean up the platform's socket subsystem.
/// On Windows, this calls WSACleanup(). On POSIX, it's a no-op.
inline void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

/// Close a socket.
/// Windows uses closesocket(), POSIX uses close().
inline int close_socket(socket_t sock) {
#ifdef _WIN32
    return closesocket(sock);
#else
    return close(sock);
#endif
}

/// Read from a socket.
/// Windows uses recv(), POSIX uses read().
/// Returns the number of bytes read, or -1 on error (0 on connection closed).
inline ssize_t socket_read(socket_t sock, char* buf, size_t len) {
#ifdef _WIN32
    // recv() on Windows returns int, and takes int for length
    return recv(sock, buf, static_cast<int>(len), 0);
#else
    return read(sock, buf, len);
#endif
}

/// Write to a socket.
/// Windows uses send(), POSIX uses write().
/// Returns the number of bytes written, or -1 on error.
inline ssize_t socket_write(socket_t sock, const char* buf, size_t len) {
#ifdef _WIN32
    return send(sock, buf, static_cast<int>(len), 0);
#else
    return write(sock, buf, len);
#endif
}

/// Check if a socket is valid.
inline bool is_valid(socket_t sock) {
#ifdef _WIN32
    return sock != INVALID_SOCKET;
#else
    return sock >= 0;
#endif
}

/// Get the last socket error as a human-readable string.
inline std::string last_error() {
#ifdef _WIN32
    int err = WSAGetLastError();
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    return std::string(strerror(errno));
#endif
}

} // namespace myredis::platform
