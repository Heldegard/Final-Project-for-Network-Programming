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

// Best LAN address of this machine. Skips virtual adapters (VMware, VirtualBox,
// WSL, Hyper-V, VPN, Bluetooth) and prefers the interface that owns the
// default route, so the answer is the address a peer can actually reach.
std::string local_ipv4();

// Address this machine would use to reach `peer`. More accurate than local_ipv4
// when the peer is known, because it asks the routing table instead of guessing.
std::string local_ipv4_for(const std::string& peer);

// Human readable dump of every adapter and why it was accepted or skipped.
std::string describe_adapters();

enum class ProbeResult { Ok, Refused, Timeout, Unreachable, Error };

// Single TCP connect attempt with a clear verdict. Unlike tcp_connect this
// distinguishes "host reached, nothing listening" (Refused) from "no answer at
// all" (Timeout), which is what makes it useful for diagnosing a LAN.
ProbeResult probe_tcp(const std::string& host, uint16_t port, int timeout_ms, int* elapsed_ms,
                      std::string* detail);

}  // namespace lk
