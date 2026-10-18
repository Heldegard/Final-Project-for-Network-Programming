#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/crypto.h"
#include "common/protocol.h"
#include "common/types.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

void test_geometry() {
    lk::Rect screen{0, 0, 1920, 1080};
    check(lk::norm_x(0, screen) == 0, "normalize left edge -> 0");
    check(lk::norm_x(1919, screen) == 65535, "normalize right edge -> 65535");
    check(lk::denorm_x(lk::norm_x(960, screen), screen) == 960, "round trip middle x");
    check(lk::denorm_y(lk::norm_y(540, screen), screen) == 540, "round trip middle y");

    lk::Rect empty{0, 0, 0, 0};
    check(lk::norm_x(500, empty) == 0, "degenerate rect clamps to 0");
}

void test_frames() {
    lk::Frame frame;
    frame.type = lk::MsgType::InputBatch;
    frame.flags = 7;
    frame.seq = 1234;
    frame.payload = {9, 8, 7};

    std::vector<uint8_t> bytes;
    lk::encode_frame(frame, &bytes);
    check(bytes.size() == lk::kHeaderSize + 3, "encoded frame size");

    lk::FrameDecoder decoder;
    decoder.feed(bytes.data(), 5);
    lk::Frame decoded;
    check(!decoder.next(&decoded), "partial frame not emitted");

    decoder.feed(bytes.data() + 5, bytes.size() - 5);
    check(decoder.next(&decoded), "full frame emitted");
    check(decoded.type == lk::MsgType::InputBatch && decoded.seq == 1234 && decoded.flags == 7,
          "frame header round trip");
    check(decoded.payload == frame.payload, "frame payload round trip");

    std::vector<uint8_t> corrupt = bytes;
    corrupt[0] = 0xFF;
    lk::FrameDecoder bad;
    bad.feed(corrupt.data(), corrupt.size());
    lk::Frame ignored;
    bad.next(&ignored);
    check(bad.failed(), "bad magic rejected");
}

void test_input_batch() {
    std::vector<lk::InputEvent> events(3);
    events[0].kind = lk::InputKind::MouseMove;
    events[0].data = (123u << 16) | 456u;
    events[1].kind = lk::InputKind::KeyDown;
    events[1].data = 0x1E;  // 'A'
    events[2].kind = lk::InputKind::MouseWheel;
    events[2].data = static_cast<uint32_t>(-120);

    std::vector<uint8_t> payload = lk::msg::input_batch(999, events.data(), events.size());
    uint64_t ts = 0;
    std::vector<lk::InputEvent> parsed;
    check(lk::msg::parse_input_batch(payload, &ts, &parsed), "input batch parses");
    check(ts == 999 && parsed.size() == 3, "input batch header");
    check(parsed[0].kind == lk::InputKind::MouseMove && parsed[0].data == events[0].data,
          "mouse move round trip");
    check(parsed[2].data == static_cast<uint32_t>(-120), "wheel delta round trip");
}

void test_crypto() {
    check(lk::crypto_init(), "crypto_init");

    std::vector<uint8_t> digest = lk::sha256("abc", 3);
    check(digest.size() == 32 && digest[0] == 0xba && digest[1] == 0x78, "sha256 known answer");

    // RFC 4231 test case 1.
    std::vector<uint8_t> key(20, 0x0b);
    std::vector<uint8_t> mac = lk::hmac_sha256(key, "Hi There", 8);
    const std::vector<uint8_t> expected = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
    check(mac == expected, "hmac-sha256 RFC 4231 vector");

    uint8_t aes_key[32]{};
    uint8_t nonce[12]{};
    for (int i = 0; i < 32; ++i) {
        aes_key[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> plain(1000);
    for (size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<uint8_t>(i * 7);
    }
    std::vector<uint8_t> cipher;
    uint8_t tag[16]{};
    bool ok = lk::aes256_gcm_encrypt(aes_key, nonce, plain.data(), plain.size(), &cipher, tag);
    check(ok && cipher.size() == plain.size(), "aes-gcm encrypt");

    std::vector<uint8_t> roundtrip;
    check(lk::aes256_gcm_decrypt(aes_key, nonce, cipher.data(), cipher.size(), tag, &roundtrip),
          "aes-gcm decrypt");
    check(roundtrip == plain, "aes-gcm round trip");

    tag[3] ^= 0x01;
    std::vector<uint8_t> tampered;
    check(!lk::aes256_gcm_decrypt(aes_key, nonce, cipher.data(), cipher.size(), tag, &tampered),
          "aes-gcm rejects tampered tag");
}

void test_hex_and_config() {
    std::vector<uint8_t> raw = {0x00, 0x0f, 0xa5, 0xff};
    std::string text = lk::to_hex(raw);
    check(text == "000fa5ff", "hex encode");

    std::vector<uint8_t> decoded;
    check(lk::from_hex(text, &decoded) && decoded == raw, "hex decode");
    check(!lk::from_hex("xyz", &decoded), "hex rejects invalid input");

    lk::Config cfg;
    cfg.role = "agent";
    cfg.peer_ip = "192.168.1.9";
    cfg.port = 25000;
    cfg.psk_hex = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    cfg.edge = "left";
    cfg.clipboard = false;
    cfg.encrypt_clipboard = true;
    cfg.dropzone = false;

    std::wstring path = L"lankvm_test_config.ini";
    check(lk::config_save(path, cfg, nullptr), "config save");

    lk::Config loaded;
    check(lk::config_load(path, &loaded, nullptr), "config load");
    check(loaded.role == cfg.role && loaded.peer_ip == cfg.peer_ip && loaded.port == cfg.port,
          "config round trip: identity");
    check(loaded.edge == cfg.edge && loaded.clipboard == cfg.clipboard &&
              loaded.dropzone == cfg.dropzone && loaded.encrypt_clipboard == cfg.encrypt_clipboard,
          "config round trip: options");
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
}

}  // namespace

int main() {
    test_geometry();
    test_frames();
    test_input_batch();
    test_crypto();
    test_hex_and_config();
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures;
}
