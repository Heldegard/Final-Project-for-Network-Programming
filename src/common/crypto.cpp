#include "common/crypto.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

namespace lk {
namespace {

bool ok(NTSTATUS st) { return BCRYPT_SUCCESS(st); }

struct AlgHandle {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~AlgHandle() {
        if (handle) {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }
};

}  // namespace

bool crypto_init() {
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!ok(st)) {
        return false;
    }
    st = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                           reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                           sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok(st);
}

std::vector<uint8_t> sha256(const void* data, size_t len) {
    std::vector<uint8_t> digest;
    AlgHandle alg;
    if (!ok(BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return digest;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!ok(BCryptCreateHash(alg.handle, &hash, nullptr, 0, nullptr, 0, 0))) {
        return digest;
    }
    digest.resize(32);
    if (len > 0) {
        BCryptHashData(hash, const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                       static_cast<ULONG>(len), 0);
    }
    NTSTATUS st = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    if (!ok(st)) {
        digest.clear();
    }
    return digest;
}

std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& key, const void* data, size_t len) {
    std::vector<uint8_t> digest;
    AlgHandle alg;
    if (!ok(BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return digest;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!ok(BCryptCreateHash(alg.handle, &hash, nullptr, 0,
                             const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0))) {
        return digest;
    }
    digest.resize(32);
    if (len > 0) {
        BCryptHashData(hash, const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                       static_cast<ULONG>(len), 0);
    }
    NTSTATUS st = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    if (!ok(st)) {
        digest.clear();
    }
    return digest;
}

bool aes256_gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* plain,
                        size_t len, std::vector<uint8_t>* cipher, uint8_t tag[16]) {
    AlgHandle alg;
    if (!ok(BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return false;
    }
    if (!ok(BCryptSetProperty(alg.handle, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        return false;
    }
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (!ok(BCryptGenerateSymmetricKey(alg.handle, &key_handle, nullptr, 0,
                                       const_cast<PUCHAR>(key), 32, 0))) {
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = 12;
    info.pbTag = tag;
    info.cbTag = 16;

    cipher->assign(len, 0);
    ULONG written = 0;
    NTSTATUS st = BCryptEncrypt(key_handle, const_cast<PUCHAR>(plain), static_cast<ULONG>(len),
                                &info, nullptr, 0, cipher->empty() ? nullptr : cipher->data(),
                                static_cast<ULONG>(len), &written, 0);
    BCryptDestroyKey(key_handle);
    if (!ok(st)) {
        cipher->clear();
        return false;
    }
    cipher->resize(written);
    return true;
}

bool aes256_gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* cipher,
                        size_t len, const uint8_t tag[16], std::vector<uint8_t>* plain) {
    AlgHandle alg;
    if (!ok(BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        return false;
    }
    if (!ok(BCryptSetProperty(alg.handle, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        return false;
    }
    BCRYPT_KEY_HANDLE key_handle = nullptr;
    if (!ok(BCryptGenerateSymmetricKey(alg.handle, &key_handle, nullptr, 0,
                                       const_cast<PUCHAR>(key), 32, 0))) {
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce);
    info.cbNonce = 12;
    info.pbTag = const_cast<PUCHAR>(tag);
    info.cbTag = 16;

    plain->assign(len, 0);
    ULONG written = 0;
    NTSTATUS st = BCryptDecrypt(key_handle, const_cast<PUCHAR>(cipher), static_cast<ULONG>(len),
                                &info, nullptr, 0, plain->empty() ? nullptr : plain->data(),
                                static_cast<ULONG>(len), &written, 0);
    BCryptDestroyKey(key_handle);
    if (!ok(st)) {
        plain->clear();
        return false;
    }
    plain->resize(written);
    return true;
}

void random_bytes(void* buf, size_t len) {
    BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), static_cast<ULONG>(len),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

std::string to_hex(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

std::string to_hex(const std::vector<uint8_t>& data) { return to_hex(data.data(), data.size()); }

bool from_hex(const std::string& text, std::vector<uint8_t>* out) {
    if (text.size() % 2 != 0) {
        return false;
    }
    out->clear();
    out->reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); i += 2) {
        int hi = nibble(text[i]);
        int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            out->clear();
            return false;
        }
        out->push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

}  // namespace lk
