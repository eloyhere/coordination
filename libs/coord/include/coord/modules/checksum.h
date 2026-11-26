#pragma once

// Check sums used by the package manager: SHA-256 for package integrity and
// CRC-32 for gzip containers. Implemented locally so the toolchain has no
// external crypto dependency on any platform.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace coord::checksum {

[[nodiscard]] inline std::string toHex(const std::uint8_t* data,
                                       std::size_t size) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    result[i * 2] = digits[(data[i] >> 4) & 0x0F];
    result[i * 2 + 1] = digits[data[i] & 0x0F];
  }
  return result;
}

[[nodiscard]] inline std::string toHex(std::string_view bytes) {
  return toHex(reinterpret_cast<const std::uint8_t*>(bytes.data()),
               bytes.size());
}

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
              0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    bufferSize_ = 0;
    length_ = 0;
  }

  void update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    length_ += size;
    std::size_t offset = 0;
    if (bufferSize_ != 0) {
      const std::size_t needed = 64 - bufferSize_;
      const std::size_t taken = size < needed ? size : needed;
      for (std::size_t i = 0; i < taken; ++i) {
        buffer_[bufferSize_ + i] = bytes[i];
      }
      bufferSize_ += taken;
      offset += taken;
      if (bufferSize_ == 64) {
        transform(buffer_.data());
        bufferSize_ = 0;
      }
    }
    while (offset + 64 <= size) {
      transform(bytes + offset);
      offset += 64;
    }
    for (; offset < size; ++offset) {
      buffer_[bufferSize_++] = bytes[offset];
    }
  }

  void update(std::string_view text) {
    update(text.data(), text.size());
  }

  [[nodiscard]] std::array<std::uint8_t, 32> digest() {
    const std::uint64_t bitLength = length_ * 8;
    const std::uint8_t padding = 0x80;
    update(&padding, 1);
    const std::uint8_t zero = 0;
    while (bufferSize_ != 56) {
      update(&zero, 1);
    }
    std::uint8_t lengthBytes[8] = {};
    for (int i = 0; i < 8; ++i) {
      lengthBytes[i] =
          static_cast<std::uint8_t>((bitLength >> (56 - i * 8)) & 0xFF);
    }
    update(lengthBytes, sizeof(lengthBytes));
    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < 8; ++i) {
      result[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
      result[i * 4 + 1] =
          static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
      result[i * 4 + 2] =
          static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
      result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
    }
    reset();
    return result;
  }

 private:
  static constexpr std::array<std::uint32_t, 64> kConstants = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
      0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
      0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
      0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
      0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
      0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
      0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
      0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
      0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
      0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  static std::uint32_t rotateRight(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
  }

  void transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
      schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                    (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                    (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                    static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotateRight(schedule[i - 15], 7) ^
                               rotateRight(schedule[i - 15], 18) ^
                               (schedule[i - 15] >> 3);
      const std::uint32_t s1 = rotateRight(schedule[i - 2], 17) ^
                               rotateRight(schedule[i - 2], 19) ^
                               (schedule[i - 2] >> 10);
      schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^
                                 rotateRight(e, 25);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 =
          h + sum1 + choose + kConstants[i] + schedule[i];
      const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^
                                 rotateRight(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t length_ = 0;
  std::size_t bufferSize_ = 0;
};

[[nodiscard]] inline std::string sha256Hex(const void* data, std::size_t size) {
  Sha256 hasher;
  hasher.update(data, size);
  const std::array<std::uint8_t, 32> digest = hasher.digest();
  return toHex(digest.data(), digest.size());
}

[[nodiscard]] inline std::string sha256Hex(std::string_view text) {
  return sha256Hex(text.data(), text.size());
}

[[nodiscard]] inline std::uint32_t crc32(const void* data, std::size_t size) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> values{};
    for (std::uint32_t index = 0; index < 256; ++index) {
      std::uint32_t value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1U) != 0 ? (value >> 1) ^ 0xEDB88320U : value >> 1;
      }
      values[index] = value;
    }
    return values;
  }();
  std::uint32_t value = 0xFFFFFFFFU;
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    value = table[(value ^ bytes[i]) & 0xFF] ^ (value >> 8);
  }
  return value ^ 0xFFFFFFFFU;
}

[[nodiscard]] inline std::uint32_t crc32(std::string_view bytes) {
  return crc32(bytes.data(), bytes.size());
}

}  // namespace coord::checksum
