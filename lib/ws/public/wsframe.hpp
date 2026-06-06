#pragma once

#include <cstdint>
#include <string>

namespace ws {

enum class Opcode : uint8_t {
  kContinuation = 0x0,  // 分片消息的续帧，payload 追加到上一帧
  kText = 0x1,          // 文本帧，payload 是 UTF-8 字符串
  kBinary = 0x2,        // 二进制帧，payload 是任意字节序列（音频、图像等）
  kClose = 0x8,         // 关闭帧，双方协商关闭连接，可携带 2 字节状态码
  kPing = 0x9,          // 心跳探测，收到后必须立刻回一个 pong（payload 原样带回）
  kPong = 0xA,          // 心跳响应，确认对端存活
};

struct WsFrame {
  Opcode opcode;
  bool fin;
  std::string payload;  // 已还原掩码
};

}  // namespace ws
