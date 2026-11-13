// End-to-end file transfer test: real Agent + real Master over 127.0.0.1.
// Verifies that the bytes actually land on disk, not just that a file appears.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "agent/agent.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/net.h"
#include "master/master.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main() {
    if (!lk::net_init() || !lk::crypto_init()) {
        std::printf("init failed\n");
        return 1;
    }

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "lankvm_file_transfer_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const std::string psk = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

    lk::Config agent_cfg;
    agent_cfg.role = "agent";
    agent_cfg.port = 24980;
    agent_cfg.psk_hex = psk;
    agent_cfg.clipboard = false;
    agent_cfg.dropzone = false;
    agent_cfg.heartbeat_ms = 300;
    agent_cfg.download_dir = (root / "downloads").string();

    lk::Agent agent(agent_cfg);
    std::string err;
    if (!agent.start(&err)) {
        std::printf("agent start failed: %s\n", err.c_str());
        return 1;
    }

    lk::Config master_cfg = agent_cfg;
    master_cfg.role = "master";
    master_cfg.peer_ip = "127.0.0.1";
    master_cfg.auto_connect = true;
    master_cfg.download_dir.clear();

    lk::Master master(master_cfg);
    if (!master.start(&err)) {
        std::printf("master start failed: %s\n", err.c_str());
        agent.stop();
        return 1;
    }
    master.connect_now();

    for (int i = 0; i < 60 && !master.connected(); ++i) {
        Sleep(100);
    }
    check(master.connected(), "master connected to agent");

    // Source file with a recognisable pattern. LANKVM_TEST_MB lets the same
    // test push a video-sized payload without keeping it in memory.
    size_t total = 300 * 1024;
    if (const char* mb = std::getenv("LANKVM_TEST_MB")) {
        long value = std::atol(mb);
        if (value > 0) {
            total = static_cast<size_t>(value) * 1024 * 1024;
        }
    }
    const std::filesystem::path source = root / "payload.bin";
    {
        std::ofstream out(source, std::ios::binary | std::ios::trunc);
        std::vector<char> chunk(1024 * 1024);
        size_t written = 0;
        while (written < total) {
            size_t n = std::min(chunk.size(), total - written);
            for (size_t i = 0; i < n; ++i) {
                chunk[i] = static_cast<char>(((written + i) * 31 + 7) & 0xFF);
            }
            out.write(chunk.data(), static_cast<std::streamsize>(n));
            written += n;
        }
    }

    const ULONGLONG start_ms = GetTickCount64();
    master.send_files({source.string()});

    const std::filesystem::path received = root / "downloads" / "payload.bin";
    const int wait_ticks = static_cast<int>(std::min<size_t>(6000, 100 + total / 1024));
    for (int i = 0; i < wait_ticks; ++i) {
        if (std::filesystem::exists(received) &&
            std::filesystem::file_size(received, ec) == total && !ec) {
            break;
        }
        ec.clear();
        Sleep(100);
    }
    const double seconds = (GetTickCount64() - start_ms) / 1000.0;

    check(std::filesystem::exists(received), "file appears in download directory");

    const auto actual_size = std::filesystem::file_size(received, ec);
    std::printf("expected %zu bytes, received %llu bytes, %.2f s\n", total,
                static_cast<unsigned long long>(ec ? 0 : actual_size), seconds);
    check(!ec && actual_size == total, "received size matches");
    if (!ec && actual_size == total && seconds > 0.001) {
        std::printf("throughput: %.1f MB/s\n", (total / (1024.0 * 1024.0)) / seconds);
    }

    // Byte-for-byte comparison, streamed so a 1 GB file does not blow up memory.
    bool same = true;
    {
        std::ifstream a(source, std::ios::binary);
        std::ifstream b(received, std::ios::binary);
        std::vector<char> ba(1 << 20);
        std::vector<char> bb(1 << 20);
        while (same && a && b) {
            a.read(ba.data(), static_cast<std::streamsize>(ba.size()));
            b.read(bb.data(), static_cast<std::streamsize>(bb.size()));
            const std::streamsize na = a.gcount();
            const std::streamsize nb = b.gcount();
            if (na != nb || std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0) {
                same = false;
            }
        }
        if (!a.eof() || !b.eof()) {
            same = false;
        }
    }
    check(same, "received content matches byte for byte");

    master.stop();
    agent.stop();
    std::filesystem::remove_all(root, ec);
    lk::net_shutdown();

    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures;
}
