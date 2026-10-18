#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/clipboard.h"
#include "common/config.h"
#include "common/protocol.h"

namespace lk {

// Anything that can send a protocol frame: the master's connection and the
// agent's connection both implement this.
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual bool send(MsgType type, const std::vector<uint8_t>& payload) = 0;
};

std::vector<uint8_t> psk_key(const Config& cfg);

// Encrypts (when enabled) and streams a clipboard blob to the peer.
bool send_clipboard(FrameSink& sink, const Config& cfg, const ClipBlob& blob, std::string* err);

class ClipboardReceiver {
public:
    void reset();
    bool on_meta(const std::vector<uint8_t>& payload, std::string* err);
    bool on_chunk(const std::vector<uint8_t>& payload, std::string* err);
    bool complete() const { return active_ && received_ >= meta_.total; }
    bool finish(const Config& cfg, ClipBlob* out, std::string* err);

private:
    msg::ClipboardMeta meta_{};
    std::vector<uint8_t> buffer_;
    uint32_t received_ = 0;
    bool active_ = false;
};

}  // namespace lk
