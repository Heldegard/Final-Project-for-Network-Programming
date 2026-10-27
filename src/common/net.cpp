#include "common/net.h"

#include <iphlpapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <vector>

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

bool is_virtual_adapter(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    static const wchar_t* kMarkers[] = {L"vmware",   L"virtualbox", L"hyper-v",  L"vethernet",
                                        L"wsl",      L"loopback",   L"bluetooth", L"tap-",
                                        L"tunnel",   L"vpn",        L"tailscale", L"zerotier",
                                        L"docker",   L"npcap",      L"virtual"};
    for (const wchar_t* marker : kMarkers) {
        if (lower.find(marker) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr,
                                  0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
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
    ULONG buffer_size = 16 * 1024;
    std::vector<uint8_t> buffer(buffer_size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_INCLUDE_GATEWAYS;
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buffer_size);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    }
    if (result != ERROR_SUCCESS) {
        return "127.0.0.1";
    }

    auto looks_virtual = [](const std::wstring& text) { return is_virtual_adapter(text); };

    std::string fallback;
    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_TUNNEL) {
            continue;
        }
        if (adapter->Description && looks_virtual(adapter->Description)) {
            continue;
        }
        if (adapter->FriendlyName && looks_virtual(adapter->FriendlyName)) {
            continue;
        }
        const bool has_gateway = adapter->FirstGatewayAddress != nullptr;

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr ||
                unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
            char text[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text)) == nullptr) {
                continue;
            }
            std::string ip(text);
            if (ip.rfind("169.254.", 0) == 0 || ip == "0.0.0.0") {
                continue;
            }
            if (has_gateway) {
                return ip;
            }
            if (fallback.empty()) {
                fallback = ip;
            }
        }
    }
    return fallback.empty() ? "127.0.0.1" : fallback;
}

std::string local_ipv4_for(const std::string& peer) {
    std::string target = peer;
    in_addr parsed{};
    if (inet_pton(AF_INET, peer.c_str(), &parsed) != 1) {
        std::string err;
        target = resolve_ipv4(peer, &err);
        if (target.empty() || inet_pton(AF_INET, target.c_str(), &parsed) != 1) {
            return local_ipv4();
        }
    }

    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) {
        return local_ipv4();
    }
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9);
    remote.sin_addr = parsed;

    std::string result = local_ipv4();
    if (connect(probe, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        int length = sizeof(local);
        if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
            char text[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)) != nullptr) {
                result = text;
            }
        }
    }
    closesocket(probe);
    return result;
}

std::string describe_adapters() {
    ULONG buffer_size = 16 * 1024;
    std::vector<uint8_t> buffer(buffer_size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_INCLUDE_GATEWAYS;
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(buffer_size);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &buffer_size);
    }
    if (result != ERROR_SUCCESS) {
        return "cannot enumerate network adapters\n";
    }

    std::string out;
    int index = 0;
    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        ++index;
        const std::string name = narrow(adapter->FriendlyName ? adapter->FriendlyName : L"");
        const std::string desc = narrow(adapter->Description ? adapter->Description : L"");
        const bool up = adapter->OperStatus == IfOperStatusUp;
        const bool loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        const bool tunnel = adapter->IfType == IF_TYPE_TUNNEL;
        const bool virtual_adapter = is_virtual_adapter(adapter->FriendlyName
                                                            ? adapter->FriendlyName
                                                            : L"") ||
                                     is_virtual_adapter(adapter->Description ? adapter->Description
                                                                             : L"");
        const bool has_gateway = adapter->FirstGatewayAddress != nullptr;

        char header[512]{};
        std::snprintf(header, sizeof(header), "[%d] %s\n    desc  : %s\n    state : %s    gateway: %s\n",
                      index, name.c_str(), desc.c_str(), up ? "up" : "down",
                      has_gateway ? "yes" : "no");
        out += header;

        int ipv4_count = 0;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr ||
                unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            auto* sin = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
            char text[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text)) != nullptr) {
                ++ipv4_count;
                out += "    ipv4  : ";
                out += text;
                out += "\n";
            }
        }
        if (ipv4_count == 0) {
            out += "    ipv4  : (none)\n";
        }

        const char* verdict = "usable";
        if (!up) {
            verdict = "skip (adapter is down)";
        } else if (loopback) {
            verdict = "skip (loopback)";
        } else if (tunnel) {
            verdict = "skip (tunnel)";
        } else if (virtual_adapter) {
            verdict = "skip (virtual adapter: VMware/WSL/Hyper-V/VPN)";
        } else if (ipv4_count == 0) {
            verdict = "skip (no IPv4 address)";
        }
        out += "    -> ";
        out += verdict;
        out += "\n\n";
    }

    out += "best local address: ";
    out += local_ipv4();
    out += "\n";
    return out;
}

}  // namespace lk
