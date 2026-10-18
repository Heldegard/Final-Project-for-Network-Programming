#include "common/net.h"

#include <cstdio>
#include <cstring>

#include "common/log.h"

namespace lk {
namespace {

bool set_nonblocking(SOCKET s, bool on) {
    u_long mode = on ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

std::string wsa_error_text(int code) {
    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "winsock error %d", code);
    return buf;
}

}  // namespace

bool net_init() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void net_shutdown() { WSACleanup(); }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        s_ = other.s_;
        other.s_ = INVALID_SOCKET;
    }
    return *this;
}

Socket::~Socket() { close(); }

void Socket::close() {
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

uint16_t Socket::local_port() const {
    if (s_ == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (getsockname(s_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

void Socket::shutdown_both() {
    if (s_ != INVALID_SOCKET) {
        ::shutdown(s_, SD_BOTH);
    }
}

void Socket::set_nodelay(bool on) {
    if (s_ == INVALID_SOCKET) {
        return;
    }
    BOOL value = on ? TRUE : FALSE;
    setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&value), sizeof(value));
}

void Socket::set_keepalive(bool on) {
    if (s_ == INVALID_SOCKET) {
        return;
    }
    BOOL value = on ? TRUE : FALSE;
    setsockopt(s_, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&value), sizeof(value));
}

Socket tcp_connect(const std::string& host, uint16_t port, int timeout_ms, std::string* err) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_text[16]{};
    std::snprintf(port_text, sizeof(port_text), "%u", port);

    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), port_text, &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (err) {
            *err = "cannot resolve " + host;
        }
        return {};
    }

    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(result);
        if (err) {
            *err = wsa_error_text(WSAGetLastError());
        }
        return {};
    }

    set_nonblocking(s, true);
    int connect_rc = connect(s, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);
    if (connect_rc == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
            closesocket(s);
            if (err) {
                *err = wsa_error_text(code);
            }
            return {};
        }
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(s, &write_set);
        fd_set error_set;
        FD_ZERO(&error_set);
        FD_SET(s, &error_set);
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int selected = select(0, nullptr, &write_set, &error_set, &tv);
        if (selected <= 0) {
            closesocket(s);
            if (err) {
                *err = selected == 0 ? "connect timed out" : wsa_error_text(WSAGetLastError());
            }
            return {};
        }
        int so_error = 0;
        int len = sizeof(so_error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
            closesocket(s);
            if (err) {
                *err = wsa_error_text(so_error);
            }
            return {};
        }
    }
    set_nonblocking(s, false);
    return Socket(s);
}

Socket tcp_listen(uint16_t port, std::string* err) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (err) {
            *err = wsa_error_text(WSAGetLastError());
        }
        return {};
    }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (err) {
            *err = "bind failed: " + wsa_error_text(WSAGetLastError());
        }
        closesocket(s);
        return {};
    }
    if (listen(s, 4) != 0) {
        if (err) {
            *err = "listen failed: " + wsa_error_text(WSAGetLastError());
        }
        closesocket(s);
        return {};
    }
    return Socket(s);
}

Socket tcp_accept(const Socket& listener, std::string* err) {
    sockaddr_in peer{};
    int len = sizeof(peer);
    SOCKET s = accept(listener.get(), reinterpret_cast<sockaddr*>(&peer), &len);
    if (s == INVALID_SOCKET) {
        if (err) {
            *err = wsa_error_text(WSAGetLastError());
        }
        return {};
    }
    return Socket(s);
}

bool send_all(const Socket& sock, const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        int chunk = static_cast<int>(len - sent > 1 << 20 ? 1 << 20 : len - sent);
        int rc = send(sock.get(), p + sent, chunk, 0);
        if (rc <= 0) {
            return false;
        }
        sent += static_cast<size_t>(rc);
    }
    return true;
}

bool recv_all(const Socket& sock, void* data, size_t len) {
    char* p = static_cast<char*>(data);
    size_t got = 0;
    while (got < len) {
        int rc = recv(sock.get(), p + got, static_cast<int>(len - got), 0);
        if (rc <= 0) {
            return false;
        }
        got += static_cast<size_t>(rc);
    }
    return true;
}

std::string resolve_ipv4(const std::string& host, std::string* err) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        if (err) {
            *err = "cannot resolve " + host;
        }
        return {};
    }
    char text[INET_ADDRSTRLEN]{};
    auto* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text));
    freeaddrinfo(result);
    return text;
}

std::string local_ipv4() {
    char name[256]{};
    if (gethostname(name, sizeof(name)) != 0) {
        return "127.0.0.1";
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &result) != 0 || result == nullptr) {
        return "127.0.0.1";
    }
    char text[INET_ADDRSTRLEN]{};
    auto* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text));
    freeaddrinfo(result);
    return text;
}

}  // namespace lk
