#include "common/clipxfer.h"

#include <algorithm>
#include <cstring>

#include "common/crypto.h"
#include "common/log.h"

namespace lk {
namespace {

constexpr uint32_t kChunkSize = 32 * 1024;
constexpr uint32_t kMaxClipboard = 16u * 1024u * 1024u;

}  // namespace

std::vector<uint8_t> psk_key(const Config& cfg) {
    std::vector<uint8_t> key;
    if (!from_hex(cfg.psk_hex, &key) || key.size() != 32) {
        key.assign(32, 0);
    }
    return key;
}

bool send_clipboard(FrameSink& sink, const Config& cfg, const ClipBlob& blob, std::string* err) {
    if (blob.data.size() > kMaxClipboard) {
        if (err) {
            *err = "clipboard payload too large";
        }
        return false;
    }
    std::vector<uint8_t> hash = sha256(blob.data);

    msg::ClipboardMeta meta{};
    meta.format = blob.format;
    meta.plain_size = static_cast<uint32_t>(blob.data.size());
    std::memcpy(meta.hash, hash.data(), 32);

    std::vector<uint8_t> wire = blob.data;
    if (cfg.encrypt_clipboard) {
        std::vector<uint8_t> key = psk_key(cfg);
        random_bytes(meta.nonce, sizeof(meta.nonce));
        std::vector<uint8_t> cipher;
        if (!aes256_gcm_encrypt(key.data(), meta.nonce, blob.data.data(), blob.data.size(), &cipher,
                                meta.tag)) {
            if (err) {
                *err = "clipboard encryption failed";
            }
            return false;
        }
        wire = std::move(cipher);
        meta.encrypted = true;
    }
    meta.total = static_cast<uint32_t>(wire.size());

    if (!sink.send(MsgType::ClipboardMeta, msg::clipboard_meta(meta))) {
        if (err) {
            *err = "failed to send clipboard meta";
        }
        return false;
    }
    for (uint32_t offset = 0; offset < wire.size(); offset += kChunkSize) {
        uint32_t n = std::min<uint32_t>(kChunkSize, static_cast<uint32_t>(wire.size()) - offset);
        if (!sink.send(MsgType::ClipboardChunk, msg::clipboard_chunk(offset, wire.data() + offset, n))) {
            if (err) {
                *err = "failed to send clipboard chunk";
            }
            return false;
        }
    }
    LK_INFO("clipboard sent: format=%u bytes=%u encrypted=%d", meta.format, meta.total,
            meta.encrypted ? 1 : 0);
    return true;
}

void ClipboardReceiver::reset() {
    meta_ = msg::ClipboardMeta{};
    buffer_.clear();
    received_ = 0;
    active_ = false;
}

bool ClipboardReceiver::on_meta(const std::vector<uint8_t>& payload, std::string* err) {
    msg::ClipboardMeta meta{};
    if (!msg::parse_clipboard_meta(payload, &meta)) {
        if (err) {
            *err = "bad clipboard meta";
        }
        return false;
    }
    if (meta.total > kMaxClipboard) {
        if (err) {
            *err = "clipboard payload too large";
        }
        return false;
    }
    meta_ = meta;
    buffer_.assign(meta.total, 0);
    received_ = 0;
    active_ = true;
    return true;
}

bool ClipboardReceiver::on_chunk(const std::vector<uint8_t>& payload, std::string* err) {
    uint32_t offset = 0;
    std::vector<uint8_t> data;
    if (!msg::parse_clipboard_chunk(payload, &offset, &data)) {
        if (err) {
            *err = "bad clipboard chunk";
        }
        return false;
    }
    if (!active_ || offset != received_ || offset + data.size() > buffer_.size()) {
        if (err) {
            *err = "clipboard chunk out of order";
        }
        return false;
    }
    std::memcpy(buffer_.data() + offset, data.data(), data.size());
    received_ += static_cast<uint32_t>(data.size());
    return true;
}

bool ClipboardReceiver::finish(const Config& cfg, ClipBlob* out, std::string* err) {
    if (!complete()) {
        if (err) {
            *err = "clipboard transfer incomplete";
        }
        return false;
    }
    std::vector<uint8_t> plain;
    if (meta_.encrypted) {
        std::vector<uint8_t> key = psk_key(cfg);
        if (!aes256_gcm_decrypt(key.data(), meta_.nonce, buffer_.data(), buffer_.size(), meta_.tag,
                                &plain)) {
            reset();
            if (err) {
                *err = "clipboard decryption failed";
            }
            return false;
        }
    } else {
        plain = buffer_;
    }
    if (plain.size() != meta_.plain_size) {
        reset();
        if (err) {
            *err = "clipboard size mismatch";
        }
        return false;
    }
    std::vector<uint8_t> hash = sha256(plain);
    if (!constant_time_equal(hash.data(), meta_.hash, 32)) {
        reset();
        if (err) {
            *err = "clipboard hash mismatch";
        }
        return false;
    }
    out->format = meta_.format;
    out->data = std::move(plain);
    reset();
    return true;
}

}  // namespace lk
