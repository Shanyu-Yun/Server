#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace transport {

inline std::string base64Encode(const uint8_t* data, size_t len) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve((len + 2) / 3 * 4);

  // 每 3 字节一组，输出 4 个 base64 字符
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = uint32_t(data[i]) << 16;
    if (i + 1 < len) b |= uint32_t(data[i + 1]) << 8;
    if (i + 2 < len) b |= uint32_t(data[i + 2]);

    out += kTable[(b >> 18) & 0x3F];
    out += kTable[(b >> 12) & 0x3F];
    out += (i + 1 < len) ? kTable[(b >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? kTable[b & 0x3F]        : '=';
  }
  return out;
}

// 方便直接传 sha1() 返回的 array
template <size_t N>
inline std::string base64Encode(const std::array<uint8_t, N>& data) {
  return base64Encode(data.data(), N);
}

}  // namespace transport
