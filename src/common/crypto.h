#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lk {

bool crypto_init();

std::vector<uint8_t> sha256(const void* data, size_t len);
std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& key, const void* data, size_t len);

bool aes256_gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* plain,
                        size_t len, std::vector<uint8_t>* cipher, uint8_t tag[16]);
bool aes256_gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* cipher,
                        size_t len, const uint8_t tag[16], std::vector<uint8_t>* plain);

void random_bytes(void* buf, size_t len);

std::string to_hex(const uint8_t* data, size_t len);
std::string to_hex(const std::vector<uint8_t>& data);
bool from_hex(const std::string& text, std::vector<uint8_t>* out);

bool constant_time_equal(const uint8_t* a, const uint8_t* b, size_t len);

}  // namespace lk
