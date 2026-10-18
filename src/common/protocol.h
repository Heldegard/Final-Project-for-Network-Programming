#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

namespace lk {

constexpr uint16_t kMagic = 0x4C4B;  // "KL"
constexpr uint8_t kProtoVersion = 1;
constexpr size_t kHeaderSize = 14;
constexpr uint32_t kMaxPayload = 4u * 1024u * 1024u;

enum class MsgType : uint8_t {
    Hello = 1,
    AuthChallenge = 2,
    AuthResponse = 3,
    AuthResult = 4,
    ScreenInfo = 5,
    InputBatch = 6,
    ClipboardMeta = 7,
    ClipboardChunk = 8,
    FileBegin = 9,
    FileChunk = 10,
    FileEnd = 11,
    FileAck = 12,
    Heartbeat = 13,
    HeartbeatAck = 14,
    Echo = 15,
    Bye = 16,
};

// Capability bits exchanged in Hello.
constexpr uint32_t kCapClipboard = 1u << 0;
constexpr uint32_t kCapFiles = 1u << 1;
constexpr uint32_t kCapEncryption = 1u << 2;

enum class InputKind : uint8_t {
    MouseMove = 1,    // data = (normalized x << 16) | normalized y
    MouseButton = 2,  // flags = button id, data = 1 down / 0 up
    MouseWheel = 3,   // data = signed wheel delta
    KeyDown = 4,      // data = scan code | (extended ? 0x10000 : 0)
    KeyUp = 5,
};

enum class MouseButton : uint8_t { Left = 1, Right = 2, Middle = 3, X1 = 4, X2 = 5 };

struct InputEvent {
    InputKind kind = InputKind::MouseMove;
    uint8_t flags = 0;
    int16_t dx = 0;
    int16_t dy = 0;
    uint32_t data = 0;
};

struct Frame {
    MsgType type = MsgType::Heartbeat;
    uint16_t flags = 0;
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
};

class Writer {
public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void raw(const void* p, size_t n);
    void str(const std::string& s);
    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    bool raw(void* out, size_t n);
    std::string str(size_t max_len = 4096);
    bool ok() const { return ok_; }
    size_t remaining() const { return n_ - off_; }

private:
    const uint8_t* p_ = nullptr;
    size_t n_ = 0;
    size_t off_ = 0;
    bool ok_ = true;
};

void encode_frame(const Frame& frame, std::vector<uint8_t>* out);

class FrameDecoder {
public:
    void feed(const uint8_t* data, size_t len);
    bool next(Frame* out);
    bool failed() const { return failed_; }
    void reset();

private:
    std::vector<uint8_t> buf_;
    size_t off_ = 0;
    bool failed_ = false;
};

namespace msg {

std::vector<uint8_t> hello(uint32_t caps, const std::string& name);
bool parse_hello(const std::vector<uint8_t>& p, uint16_t* proto, uint32_t* caps, std::string* name);

std::vector<uint8_t> screen_info(const ScreenInfo& info);
bool parse_screen_info(const std::vector<uint8_t>& p, ScreenInfo* info);

std::vector<uint8_t> input_batch(uint64_t ts, const InputEvent* events, size_t count);
bool parse_input_batch(const std::vector<uint8_t>& p, uint64_t* ts, std::vector<InputEvent>* out);

struct ClipboardMeta {
    uint8_t format = 0;   // 1 = UTF-8 text, 2 = DIB image
    uint32_t total = 0;   // bytes on the wire (ciphertext length when encrypted)
    uint32_t plain_size = 0;
    uint8_t hash[32]{};
    bool encrypted = false;
    uint8_t nonce[12]{};
    uint8_t tag[16]{};
};

std::vector<uint8_t> clipboard_meta(const ClipboardMeta& meta);
bool parse_clipboard_meta(const std::vector<uint8_t>& p, ClipboardMeta* meta);

std::vector<uint8_t> clipboard_chunk(uint32_t offset, const uint8_t* data, size_t len);
bool parse_clipboard_chunk(const std::vector<uint8_t>& p, uint32_t* offset, std::vector<uint8_t>* data);

std::vector<uint8_t> file_begin(uint32_t id, uint64_t size, const std::string& name);
bool parse_file_begin(const std::vector<uint8_t>& p, uint32_t* id, uint64_t* size, std::string* name);

std::vector<uint8_t> file_chunk(uint32_t id, uint64_t offset, const uint8_t* data, size_t len);
bool parse_file_chunk(const std::vector<uint8_t>& p, uint32_t* id, uint64_t* offset,
                      std::vector<uint8_t>* data);

std::vector<uint8_t> file_end(uint32_t id);
bool parse_file_end(const std::vector<uint8_t>& p, uint32_t* id);

std::vector<uint8_t> file_ack(uint32_t id, uint8_t status, const std::string& message);
bool parse_file_ack(const std::vector<uint8_t>& p, uint32_t* id, uint8_t* status, std::string* message);

std::vector<uint8_t> heartbeat(uint64_t ts);
bool parse_heartbeat(const std::vector<uint8_t>& p, uint64_t* ts);

std::vector<uint8_t> echo(uint64_t ts);

std::vector<uint8_t> auth_result(bool ok, const std::string& reason);
bool parse_auth_result(const std::vector<uint8_t>& p, bool* ok, std::string* reason);

std::vector<uint8_t> bye(uint8_t reason);

}  // namespace msg

}  // namespace lk
