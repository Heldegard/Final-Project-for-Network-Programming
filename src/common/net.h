#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace lk {

bool net_init();
void net_shutdown();

class Socket {
public:
    Socket() = default;
    explicit Socket(SOCKET s) : s_(s) {}
    Socket(Socket&& other) noexcept : s_(other.s_) { other.s_ = INVALID_SOCKET; }
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket();

    bool valid() const { return s_ != INVALID_SOCKET; }
    SOCKET get() const { return s_; }
    uint16_t local_port() const;
    void close();
    void shutdown_both();
    void set_nodelay(bool on);
    void set_keepalive(bool on);

private:
    SOCKET s_ = INVALID_SOCKET;
};

Socket tcp_connect(const std::string& host, uint16_t port, int timeout_ms, std::string* err);
Socket tcp_listen(uint16_t port, std::string* err);
Socket tcp_accept(const Socket& listener, std::string* err);

bool send_all(const Socket& sock, const void* data, size_t len);
bool recv_all(const Socket& sock, void* data, size_t len);

std::string resolve_ipv4(const std::string& host, std::string* err);
std::string local_ipv4();

}  // namespace lk
