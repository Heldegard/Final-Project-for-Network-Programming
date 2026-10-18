// Loopback integration test: real TCP sockets, real handshake, real frames.
// No UI, no input injection - safe to run unattended.

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/clipboard.h"
#include "common/clipxfer.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/stream.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

lk::Config make_config() {
    lk::Config cfg;
    cfg.port = 24999;
    cfg.psk_hex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    cfg.encrypt_clipboard = true;
    cfg.clipboard = true;
    return cfg;
}

// Socket plus frame sending, mirroring what the real peers do.
struct Peer : lk::FrameSink {
    lk::Socket sock;
    std::atomic<uint32_t> seq{1};

    bool send(lk::MsgType type, const std::vector<uint8_t>& payload) override {
        lk::Frame frame;
        frame.type = type;
        frame.seq = seq.fetch_add(1);
        frame.payload = payload;
        std::vector<uint8_t> bytes;
        lk::encode_frame(frame, &bytes);
        return lk::send_all(sock, bytes.data(), bytes.size());
    }
};

void agent_side(Peer* peer, const lk::Config& cfg, std::atomic<bool>* ok) {
    lk::FrameStream stream(peer->sock);
    lk::Frame frame;
    if (!stream.read(&frame) || frame.type != lk::MsgType::Hello) {
        *ok = false;
        return;
    }
    uint16_t proto = 0;
    uint32_t caps = 0;
    std::string name;
    lk::msg::parse_hello(frame.payload, &proto, &caps, &name);

    uint8_t nonce[16]{};
    lk::random_bytes(nonce, sizeof(nonce));
    peer->send(lk::MsgType::AuthChallenge, std::vector<uint8_t>(nonce, nonce + sizeof(nonce)));
    if (!stream.read(&frame) || frame.type != lk::MsgType::AuthResponse) {
        *ok = false;
        return;
    }
    std::vector<uint8_t> expected = lk::hmac_sha256(lk::psk_key(cfg), nonce, sizeof(nonce));
    bool key_ok = frame.payload.size() == expected.size() &&
                  lk::constant_time_equal(frame.payload.data(), expected.data(), expected.size());
    peer->send(lk::MsgType::AuthResult, lk::msg::auth_result(key_ok, key_ok ? "ok" : "bad key"));
    if (!key_ok) {
        *ok = false;
        return;
    }

    lk::ScreenInfo info;
    info.rc = {0, 0, 1920, 1080};
    info.dpi = 96;
    info.name = "loopback-agent";
    peer->send(lk::MsgType::ScreenInfo, lk::msg::screen_info(info));

    if (!stream.read(&frame) || frame.type != lk::MsgType::InputBatch) {
        *ok = false;
        return;
    }
    uint64_t ts = 0;
    std::vector<lk::InputEvent> events;
    lk::msg::parse_input_batch(frame.payload, &ts, &events);
    bool batch_ok = events.size() == 3 && events[0].kind == lk::InputKind::MouseMove &&
                    events[1].kind == lk::InputKind::KeyDown &&
                    events[2].kind == lk::InputKind::MouseWheel;
    peer->send(lk::MsgType::Echo, lk::msg::echo(ts));

    lk::ClipboardReceiver receiver;
    bool clipboard_ok = false;
    while (stream.read(&frame)) {
        if (frame.type == lk::MsgType::ClipboardMeta) {
            receiver.on_meta(frame.payload, nullptr);
        } else if (frame.type == lk::MsgType::ClipboardChunk) {
            receiver.on_chunk(frame.payload, nullptr);
            if (receiver.complete()) {
                lk::ClipBlob blob;
                clipboard_ok = receiver.finish(cfg, &blob, nullptr) && blob.format == 1 &&
                               std::string(blob.data.begin(), blob.data.end()) == "hello clipboard";
                break;
            }
        }
    }
    *ok = batch_ok && clipboard_ok;
}

class CaptureSink : public lk::FrameSink {
public:
    bool send(lk::MsgType type, const std::vector<uint8_t>& payload) override {
        frames.emplace_back(type, payload);
        return true;
    }
    std::vector<std::pair<lk::MsgType, std::vector<uint8_t>>> frames;
};

void test_clipboard_roundtrip(const lk::Config& cfg) {
    CaptureSink sink;
    lk::ClipBlob blob;
    blob.format = 1;
    const std::string text = "hello clipboard";
    blob.data.assign(text.begin(), text.end());

    check(lk::send_clipboard(sink, cfg, blob, nullptr), "send_clipboard produces frames");
    check(sink.frames.size() >= 2, "clipboard meta + at least one chunk");

    lk::ClipboardReceiver receiver;
    for (const auto& entry : sink.frames) {
        if (entry.first == lk::MsgType::ClipboardMeta) {
            receiver.on_meta(entry.second, nullptr);
        } else if (entry.first == lk::MsgType::ClipboardChunk) {
            receiver.on_chunk(entry.second, nullptr);
        }
    }
    lk::ClipBlob received;
    check(receiver.finish(cfg, &received, nullptr), "clipboard receiver completes");
    check(received.format == 1 && std::string(received.data.begin(), received.data.end()) == text,
          "clipboard content matches");
}

}  // namespace

int main() {
    if (!lk::net_init() || !lk::crypto_init()) {
        std::printf("initialisation failed\n");
        return 1;
    }

    lk::Config cfg = make_config();

    std::string err;
    lk::Socket listener = lk::tcp_listen(0, &err);
    if (!listener.valid()) {
        std::printf("listen failed: %s\n", err.c_str());
        return 1;
    }
    uint16_t port = listener.local_port();
    std::printf("loopback listener on 127.0.0.1:%u\n", port);

    std::atomic<bool> agent_ok{false};
    std::thread agent([&] {
        lk::Socket accepted = lk::tcp_accept(listener, &err);
        if (!accepted.valid()) {
            agent_ok = false;
            return;
        }
        Peer peer;
        peer.sock = std::move(accepted);
        agent_side(&peer, cfg, &agent_ok);
    });

    Peer client;
    client.sock = lk::tcp_connect("127.0.0.1", port, 3000, &err);
    check(client.sock.valid(), "tcp connect");
    if (!client.sock.valid()) {
        agent.join();
        return 1;
    }
    client.sock.set_nodelay(true);

    lk::FrameStream stream(client.sock);
    lk::Frame frame;

    check(client.send(lk::MsgType::Hello, lk::msg::hello(7, "loopback-master")), "send hello");
    check(stream.read(&frame) && frame.type == lk::MsgType::AuthChallenge, "receive challenge");
    std::vector<uint8_t> response =
        lk::hmac_sha256(lk::psk_key(cfg), frame.payload.data(), frame.payload.size());
    check(client.send(lk::MsgType::AuthResponse, response), "send auth response");

    check(stream.read(&frame) && frame.type == lk::MsgType::AuthResult, "receive auth result");
    bool auth_ok = false;
    std::string reason;
    lk::msg::parse_auth_result(frame.payload, &auth_ok, &reason);
    check(auth_ok, "authentication accepted");

    check(stream.read(&frame) && frame.type == lk::MsgType::ScreenInfo, "receive screen info");
    lk::ScreenInfo info;
    lk::msg::parse_screen_info(frame.payload, &info);
    check(info.rc.w == 1920 && info.rc.h == 1080 && info.name == "loopback-agent",
          "screen info matches");

    std::vector<lk::InputEvent> events(3);
    events[0].kind = lk::InputKind::MouseMove;
    events[0].data = (100u << 16) | 200u;
    events[1].kind = lk::InputKind::KeyDown;
    events[1].data = 0x1E;
    events[2].kind = lk::InputKind::MouseWheel;
    events[2].data = static_cast<uint32_t>(-120);
    check(client.send(lk::MsgType::InputBatch,
                      lk::msg::input_batch(4242, events.data(), events.size())),
          "send input batch");

    check(stream.read(&frame) && frame.type == lk::MsgType::Echo, "receive echo");
    uint64_t echoed = 0;
    lk::msg::parse_heartbeat(frame.payload, &echoed);
    check(echoed == 4242, "echo timestamp matches");

    lk::ClipBlob blob;
    blob.format = 1;
    const std::string text = "hello clipboard";
    blob.data.assign(text.begin(), text.end());
    check(lk::send_clipboard(client, cfg, blob, nullptr), "stream clipboard to agent");

    agent.join();
    check(agent_ok.load(), "agent side observed batch and clipboard");

    test_clipboard_roundtrip(cfg);

    client.sock.close();
    listener.close();
    lk::net_shutdown();

    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures;
}
