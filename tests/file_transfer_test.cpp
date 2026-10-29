// End-to-end file transfer test: real Agent + real Master over 127.0.0.1.
// Verifies that the bytes actually land on disk, not just that a file appears.

#include <windows.h>

#include <cstdio>
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

    // Source file with a recognisable pattern.
    const std::filesystem::path source = root / "payload.bin";
    std::vector<char> expected(300 * 1024);
    for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    }
    {
        std::ofstream out(source, std::ios::binary | std::ios::trunc);
        out.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    }

    master.send_files({source.string()});

    const std::filesystem::path received = root / "downloads" / "payload.bin";
    for (int i = 0; i < 100 && !std::filesystem::exists(received); ++i) {
        Sleep(100);
    }
    check(std::filesystem::exists(received), "file appears in download directory");

    // Give the receiver a moment to flush the last chunks.
    for (int i = 0; i < 50; ++i) {
        if (std::filesystem::exists(received) &&
            std::filesystem::file_size(received, ec) == expected.size()) {
            break;
        }
        Sleep(100);
    }

    const auto actual_size = std::filesystem::file_size(received, ec);
    std::printf("expected %zu bytes, received %llu bytes\n", expected.size(),
                static_cast<unsigned long long>(ec ? 0 : actual_size));
    check(!ec && actual_size == expected.size(), "received size matches");

    std::vector<char> actual;
    if (!ec) {
        std::ifstream in(received, std::ios::binary);
        actual.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    check(actual == expected, "received content matches byte for byte");

    master.stop();
    agent.stop();
    std::filesystem::remove_all(root, ec);
    lk::net_shutdown();

    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures;
}
