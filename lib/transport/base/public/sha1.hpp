#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace transport {

inline std::array<uint8_t, 20> sha1(const std::string& data) {
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  // 填充：末尾加 0x80，补 0x00 至长度 ≡ 56 (mod 64)，再追加原始比特长度（大端 64 bit）
  uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;
  std::vector<uint8_t> msg(data.begin(), data.end());
  // 开始填充
  msg.push_back(0x80);
  while (msg.size() % 64 != 56)
    msg.push_back(0x00);
  // 防止不同长度的消息经过填充后撞成相同的输入序列
  for (int i = 7; i >= 0; --i)
    msg.push_back((bitLen >> (i * 8)) & 0xFF);

  auto rotl = [](uint32_t x, int n) -> uint32_t { return (x << n) | (x >> (32 - n)); };

  // 逐 64 字节 chunk 压缩
  for (size_t i = 0; i < msg.size(); i += 64) {
    std::array<uint32_t, 80> W{};

    // W[0..15]：从 chunk 字节按大端拼成 32 bit 字
    for (int j = 0; j < 16; ++j) {
      W[j] = (uint32_t(msg[i + j * 4]) << 24) | (uint32_t(msg[i + j * 4 + 1]) << 16) |
             (uint32_t(msg[i + j * 4 + 2]) << 8) | uint32_t(msg[i + j * 4 + 3]);
    }

    // W[16..79]：由前面四个字混合派生
    for (int j = 16; j < 80; ++j)
      W[j] = rotl(W[j - 3] ^ W[j - 8] ^ W[j - 14] ^ W[j - 16], 1);

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

    // 80 轮压缩，每 20 轮换一套 f 和 K
    for (int j = 0; j < 80; ++j) {
      uint32_t f, k;
      if (j < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (j < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (j < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t t = rotl(a, 5) + f + e + k + W[j];
      e = d;
      d = c;
      c = rotl(b, 30);
      b = a;
      a = t;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  // 拼输出：h0..h4 按大端序逐字节写入 20 字节数组
  std::array<uint8_t, 20> digest{};
  for (int i = 0; i < 4; ++i) {
    digest[i] = (h0 >> (24 - i * 8)) & 0xFF;
    digest[i + 4] = (h1 >> (24 - i * 8)) & 0xFF;
    digest[i + 8] = (h2 >> (24 - i * 8)) & 0xFF;
    digest[i + 12] = (h3 >> (24 - i * 8)) & 0xFF;
    digest[i + 16] = (h4 >> (24 - i * 8)) & 0xFF;
  }
  return digest;
}

}  // namespace transport
